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
    if (loading_.contains(path))
        return nullptr;
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
    loading_.erase(path);
    if (sample == nullptr || !std::isfinite(sampleRate) || sampleRate <= 0.0)
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

std::shared_ptr<const vloom::SampleBuffer> ChannelRackAudio::cachedSample(const std::string& path) const
{
    const auto found = samples_.find(path);
    return found != samples_.end() ? found->second : nullptr;
}

void ChannelRackAudio::setLoading(const std::string& path, bool loading)
{
    if (path.empty())
        return;
    if (loading) {
        failed_.erase(path);
        loading_.insert(path);
    } else {
        loading_.erase(path);
    }
}

bool ChannelRackAudio::isLoading(const std::string& path) const { return loading_.contains(path); }

namespace {

/// The piano roll's notes as the engine plays them.
std::vector<vloom::Note> engineNotes(const std::vector<Note>& notes)
{
    std::vector<vloom::Note> out;
    out.reserve(notes.size());
    for (const Note& note : notes)
        out.push_back(vloom::Note{.start = note.start,
            .length = note.length,
            .key = note.key,
            .velocity = static_cast<float>(note.velocity) / 128.0f});
    return out;
}

std::vector<vloom::ControlPoint> engineControls(const std::vector<ControlPoint>& controls)
{
    std::vector<vloom::ControlPoint> out;
    out.reserve(controls.size());
    for (const ControlPoint& ctrl : controls) {
        vloom::ControlEvent::Type type = vloom::ControlEvent::Type::Controller;
        switch (ctrl.kind) {
        case ControlKind::PitchBend:
            type = vloom::ControlEvent::Type::PitchBend;
            break;
        case ControlKind::Controller:
            type = vloom::ControlEvent::Type::Controller;
            break;
        case ControlKind::Pressure:
            type = vloom::ControlEvent::Type::Pressure;
            break;
        }
        out.push_back(vloom::ControlPoint{
            .start = ctrl.start,
            .type = type,
            .number = static_cast<std::int16_t>(std::clamp(ctrl.number, 0, 127)),
            .value = static_cast<float>(ctrl.value),
            .channel = static_cast<std::int16_t>(std::clamp(ctrl.midiChannel, 0, 15)),
        });
    }
    return out;
}

} // namespace

std::vector<ChannelRackAudio::LoadError> ChannelRackAudio::sync(
    vloom::Engine& engine, const Project& project, std::size_t pattern, double sampleRate, bool song)
{
    std::vector<LoadError> errors;
    if (auto state = build(project, pattern, sampleRate, song, errors))
        engine.setSequencerState(std::move(state));
    return errors;
}

std::unique_ptr<vloom::SequencerState> ChannelRackAudio::build(
    const Project& project, std::size_t pattern, double sampleRate, bool song, std::vector<LoadError>& errors)
{
    if (sampleRate <= 0.0 || pattern >= project.patterns().size())
        return nullptr;
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
    // The pattern's whole length: its step pads, or further when notes reach past them.
    state->stepCount = project.patternLength(pattern) / kTicksPerStep;
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
        out.pluginOutputs = channel.pluginOutputs;
        out.notesFrom = channel.notesFrom;
        if (c < p.channels.size()) {
            for (const bool on : p.channels[c].steps)
                out.steps.push_back(on ? 1 : 0);
            out.notes = engineNotes(p.channels[c].notes);
            out.controls = engineControls(p.channels[c].controls);
        }
        out.sample = load(channel.samplePath, c, &errors);
        if (plugins_) {
            for (const PluginSlot& neSlot : channel.noteEffects) {
                if (auto ne = plugins_(neSlot.instance)) {
                    ne->setBypassed(neSlot.bypassed);
                    if (!out.noteEffect)
                        out.noteEffect = std::move(ne);
                    else
                        out.noteEffects.push_back(std::move(ne));
                }
            }
            if (channel.instrument) {
                if (auto plugin = plugins_(channel.instrument->instance)) {
                    plugin->setBypassed(channel.instrument->bypassed);
                    out.instrument = vloom::Instrument::Plugin;
                    out.plugin = std::move(plugin);
                }
            }
        }
        state->channels.push_back(std::move(out));
    }

    // Song mode: the playlist. Every pattern's steps (clips refer to them by
    // index), the pattern clips and the audio clips, in steps.
    state->song = song;
    if (song) {
        state->patterns.reserve(project.patterns().size());
        for (std::size_t index = 0; index < project.patterns().size(); ++index) {
            const Pattern& each = project.patterns()[index];
            vloom::SongPattern rows;
            rows.stepCount
                = project.patternLength(index) / kTicksPerStep; // a longer clip repeats the pattern
            rows.steps.reserve(each.channels.size());
            rows.notes.reserve(each.channels.size());
            rows.controls.reserve(each.channels.size());
            for (const ChannelPattern& cp : each.channels) {
                auto& row = rows.steps.emplace_back();
                row.reserve(cp.steps.size());
                for (const bool on : cp.steps)
                    row.push_back(on ? 1 : 0);
                rows.notes.push_back(engineNotes(cp.notes));
                rows.controls.push_back(engineControls(cp.controls));
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
                out.startTick = clip.start;
                out.lengthTicks = project.clipLength(clip);
                out.offsetTicks = clip.offset; // cut: it goes on inside the pattern
                state->patternClips.push_back(out);
            }
        // Audio clips play their channel's sample through the channel, from
        // their offset into it, for their length (0: to its end).
        for (const AudioClip& clip : project.audioClips())
            if (!clip.muted && audible(clip.track)) {
                vloom::SongAudioClip out;
                out.channel = static_cast<int>(clip.channel);
                out.startTick = clip.start;
                out.offsetTicks = clip.offset;
                out.lengthTicks = clip.length;
                if (clip.fitted()) {
                    // The sample follows the song's tempo: stretched (its
                    // pitch kept) when the stretched copy is ready, resampled
                    // (its pitch following) otherwise.
                    const double speed = tempo_ / clip.sourceBpm;
                    if (std::isfinite(speed) && speed > 0.0) {
                        std::shared_ptr<const vloom::SampleBuffer> stretched;
                        if (clip.fit == AudioClip::Fit::Stretch && stretch_
                            && clip.channel < project.channels().size())
                            stretched = stretch_(project.channels()[clip.channel].samplePath, 1.0 / speed);
                        if (stretched != nullptr)
                            out.sample = std::move(stretched);
                        else
                            out.rate = speed;
                    }
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
    return state;
}

void ChannelRackAudio::reset()
{
    voices_.clear();
    samples_.clear();
    failed_.clear();
    loading_.clear();
}

} // namespace vloomdaw
