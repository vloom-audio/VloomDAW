// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/ChannelRackAudio.hpp"

#include <vloom/engine/sample/SampleLoader.hpp>

#include <exception>

namespace vloomdaw {

std::vector<ChannelRackAudio::LoadError> ChannelRackAudio::sync(
    vloom::Engine& engine, const Project& project, std::size_t pattern, double sampleRate)
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
        if (c < p.channels.size())
            for (const bool on : p.channels[c].steps)
                out.steps.push_back(on ? 1 : 0);

        const std::string& path = channel.samplePath;
        if (!path.empty() && !failed_.contains(path)) {
            auto found = samples_.find(path);
            if (found == samples_.end()) {
                std::string error;
                std::shared_ptr<const vloom::SampleBuffer> sample;
                try {
                    sample = vloom::loadSample(path, sampleRate, &error);
                } catch (const std::exception&) {
                    // Out of memory or a file system error: report, keep running.
                    error = "not enough memory to load it";
                }
                if (sample)
                    found = samples_.emplace(path, std::move(sample)).first;
                else {
                    failed_.emplace(path, error);
                    errors.push_back({c, path, error});
                }
            }
            if (found != samples_.end())
                out.sample = found->second;
        }
        state->channels.push_back(std::move(out));
    }
    // Drop samples no channel uses any more. A note still playing one keeps
    // it alive through the engine's old state until it ends (ADR 0005).
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
