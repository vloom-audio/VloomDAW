// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/ChannelRackAudio.hpp"

#include "core/Decibels.hpp"

#include <vloom/engine/sample/SampleLoader.hpp>

#include <exception>

namespace vloomdaw {

std::shared_ptr<const vloom::SampleBuffer> ChannelRackAudio::load(
    const std::string& path, std::size_t owner, std::vector<LoadError>* errors)
{
    if (path.empty() || rate_ <= 0.0 || failed_.contains(path))
        return nullptr;
    if (const auto found = samples_.find(path); found != samples_.end())
        return found->second;
    std::string error;
    std::shared_ptr<const vloom::SampleBuffer> sample;
    try {
        const std::string source = resolver_ ? resolver_(path, rate_, &error) : path;
        if (!source.empty())
            sample = vloom::loadSample(source, rate_, &error);
    } catch (const std::exception&) {
        // Out of memory or a file system error: report, keep running.
        error = "not enough memory to load it";
    }
    if (!sample) {
        failed_.emplace(path, error);
        if (errors != nullptr)
            errors->push_back({owner, path, error});
        return nullptr;
    }
    samples_.emplace(path, sample);
    return sample;
}

bool ChannelRackAudio::hasSample(const std::string& path, double sampleRate) const
{
    return sampleRate == rate_ && samples_.contains(path);
}

void ChannelRackAudio::store(
    const std::string& path, std::shared_ptr<const vloom::SampleBuffer> sample, double sampleRate)
{
    if (sample == nullptr || !(sampleRate > 0.0))
        return;
    if (rate_ <= 0.0)
        rate_ = sampleRate; // nothing loaded yet: this is the rate from now on
    if (sampleRate != rate_)
        return; // the device changed rate meanwhile: sync() loads it again
    failed_.erase(path);
    samples_[path] = std::move(sample);
}

std::shared_ptr<const vloom::SampleBuffer> ChannelRackAudio::sample(const std::string& path)
{
    return load(path, kAudioClip, nullptr);
}

std::vector<ChannelRackAudio::LoadError> ChannelRackAudio::sync(
    vloom::Engine& engine, const Project& project, std::size_t pattern, double sampleRate, bool song)
{
    std::vector<LoadError> errors;
    if (sampleRate <= 0.0 || pattern >= project.patterns().size())
        return errors;
    if (sampleRate != rate_) {
        // Samples are stored at the engine's rate: load them again.
        samples_.clear();
        failed_.clear();
        rate_ = sampleRate;
    }

    const auto& channels = project.channels();
    while (voices_.size() < channels.size())
        voices_.push_back(std::make_shared<vloom::ChannelVoices>());

    auto state = std::make_unique<vloom::SequencerState>();
    state->stepCount = project.patternSteps(pattern);
    state->ticksPerStep = kTicksPerStep;
    state->channels.reserve(channels.size());
    const Pattern& p = project.patterns()[pattern];

    for (std::size_t c = 0; c < channels.size(); ++c) {
        const Channel& channel = channels[c];
        vloom::ChannelState out;
        out.voices = voices_[c];
        out.muted = channel.muted;
        out.gain = static_cast<float>(faderToGain(channel.volume));
        out.pan = static_cast<float>(channel.pan);
        out.insert = channel.insert;
        if (c < p.channels.size())
            for (const bool on : p.channels[c].steps)
                out.steps.push_back(on ? 1 : 0);
        out.sample = load(channel.samplePath, c, &errors);
        state->channels.push_back(std::move(out));
    }

    // Song mode: the playlist. Every pattern's steps (clips refer to them by
    // index), the pattern clips and the audio clips, in steps.
    state->song = song;
    if (song) {
        state->patterns.reserve(project.patterns().size());
        for (const Pattern& each : project.patterns()) {
            vloom::SongPattern rows;
            rows.stepCount = each.steps; // a longer clip repeats the pattern
            rows.steps.reserve(each.channels.size());
            for (const ChannelPattern& cp : each.channels) {
                auto& row = rows.steps.emplace_back();
                row.reserve(cp.steps.size());
                for (const bool on : cp.steps)
                    row.push_back(on ? 1 : 0);
            }
            state->patterns.push_back(std::move(rows));
        }
        // Clips on a muted track, or on other tracks while one is solo, stay out.
        const auto audible
            = [&project](int track) { return project.trackAudible(static_cast<std::size_t>(track)); };
        for (const Clip& clip : project.clips())
            if (!clip.muted && audible(clip.track)) {
                vloom::SongPatternClip out;
                out.pattern = static_cast<int>(clip.pattern);
                out.startStep = clip.start / kTicksPerStep;
                out.lengthSteps = project.clipLength(clip) / kTicksPerStep;
                out.offsetSteps = clip.offset / kTicksPerStep; // cut: it goes on inside the pattern
                state->patternClips.push_back(out);
            }
        // Audio clips play their channel's sample through the channel, from
        // their offset into it, for their length (0: to its end).
        for (const AudioClip& clip : project.audioClips())
            if (!clip.muted && audible(clip.track)) {
                vloom::SongAudioClip out;
                out.channel = static_cast<int>(clip.channel);
                out.startStep = clip.start / kTicksPerStep;
                out.offsetSteps = clip.offset / kTicksPerStep;
                out.lengthSteps = clip.length / kTicksPerStep;
                if (clip.fitted()) {
                    // The sample follows the song's tempo: stretched (its
                    // pitch kept) when the stretched copy is ready, resampled
                    // (its pitch following) otherwise.
                    const double speed = tempo_ / clip.sourceBpm;
                    std::shared_ptr<const vloom::SampleBuffer> stretched;
                    if (clip.fit == AudioClip::Fit::Stretch && stretch_
                        && clip.channel < project.channels().size())
                        stretched = stretch_(project.channels()[clip.channel].samplePath, 1.0 / speed);
                    if (stretched != nullptr)
                        out.sample = std::move(stretched);
                    else
                        out.rate = speed;
                }
                state->audioClips.push_back(out);
            }
    }

    // Drop samples nothing uses any more. A note still playing one keeps it
    // alive through the engine's old state until it ends (ADR 0005).
    std::erase_if(samples_, [&](const auto& entry) {
        for (const Channel& channel : channels)
            if (channel.samplePath == entry.first)
                return false;
        return true;
    });
    engine.setSequencerState(std::move(state));
    return errors;
}

void ChannelRackAudio::reset()
{
    voices_.clear();
    samples_.clear();
    failed_.clear();
}

} // namespace vloomdaw
