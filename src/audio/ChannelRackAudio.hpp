// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/Project.hpp"

#include <vloom/engine/Engine.hpp>
#include <vloom/engine/sequencer/SequencerState.hpp>

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vloomdaw {

/// Turns the project's channel rack into what the engine plays.
///
/// sync() builds a vloom::SequencerState for one pattern and publishes it.
/// Each channel keeps the same vloom::ChannelVoices across syncs, so editing
/// steps never cuts a ringing note. Samples are loaded once per path and
/// sample rate and shared between states.
///
/// UI thread only. Qt-free, like AudioController.
class ChannelRackAudio {
public:
    struct LoadError {
        std::size_t channel = 0; ///< kAudioClip for a playlist audio clip
        std::string path;
        std::string message;
    };
    static constexpr std::size_t kAudioClip = static_cast<std::size_t>(-1);

    /// Publishes `pattern`'s channel rack to `engine`, at `sampleRate` (the
    /// device's rate; nothing is published while it is <= 0). Loads samples
    /// not loaded yet. Returns the samples that failed to load this time; a
    /// path that failed once is not retried (or reported) again until
    /// reset() or a rate change.
    /// With `song`, the engine plays the playlist (pattern and audio clips)
    /// instead of looping `pattern`.
    std::vector<LoadError> sync(vloom::Engine& engine, const Project& project, std::size_t pattern,
        double sampleRate, bool song = false);

    /// The sample at `path` as the engine plays it (loading it if needed);
    /// null if it cannot be loaded. For the playlist's clip lengths and
    /// waveforms. Uses the rate of the last sync().
    [[nodiscard]] std::shared_ptr<const vloom::SampleBuffer> sample(const std::string& path);

    /// Forgets channels and samples, e.g. for a new project. Notes still
    /// ringing fade out once the next sync() publishes the new rack.
    void reset();

private:
    /// Cached or loaded at rate_; null on failure (recorded in failed_; the
    /// error goes to `errors` the first time).
    std::shared_ptr<const vloom::SampleBuffer> load(
        const std::string& path, std::size_t owner, std::vector<LoadError>* errors);

    std::vector<std::shared_ptr<vloom::ChannelVoices>> voices_; ///< index = channel
    std::map<std::string, std::shared_ptr<const vloom::SampleBuffer>> samples_; ///< by path, at rate_
    std::map<std::string, std::string> failed_; ///< path -> error, at rate_
    double rate_ = 0.0;
};

} // namespace vloomdaw
