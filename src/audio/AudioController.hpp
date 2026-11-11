// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/AudioFormat.hpp"

#include <vloom/engine/Engine.hpp>
#include <vloom/engine/io/AudioDevice.hpp>

#include <string>
#include <vector>

namespace vloomdaw {

struct OutputDeviceList {
    std::vector<std::string> names;
    int defaultIndex = -1; ///< index of the system default, -1 if unknown
};

/// Owns the engine and the hardware output device and wires them together.
///
/// Lives on the UI thread. Only Engine::process() runs on the audio thread.
/// Deliberately Qt-free so it can be unit-tested and reused by other front ends.
class AudioController {
public:
    AudioController() = default;
    ~AudioController();

    AudioController(const AudioController&) = delete;
    AudioController& operator=(const AudioController&) = delete;

    [[nodiscard]] OutputDeviceList outputDevices();

    /// (Re)opens output device `index` (-1 = system default) and starts audio
    /// with the format set by setFormat().
    bool openDevice(int index, std::string* error = nullptr);

    /// Sample rate and buffer size requested on the next openDevice(). The
    /// device may run at a different rate; sampleRate() reports the real one.
    void setFormat(int sampleRate, int bufferFrames) noexcept;
    [[nodiscard]] int requestedSampleRate() const noexcept { return requestedSampleRate_; }
    void closeDevice();

    [[nodiscard]] vloom::Engine& engine() noexcept { return engine_; }
    [[nodiscard]] const vloom::Engine& engine() const noexcept { return engine_; }

    [[nodiscard]] bool isRunning() const noexcept { return device_.isRunning(); }
    [[nodiscard]] std::string deviceName() const { return device_.deviceName(); }
    [[nodiscard]] int sampleRate() const noexcept { return device_.sampleRate(); }
    [[nodiscard]] int bufferFrames() const noexcept { return bufferFrames_; }

private:
    vloom::Engine engine_;
    vloom::AudioDevice device_;
    int requestedSampleRate_ = kDefaultSampleRate;
    int bufferFrames_ = kDefaultBufferFrames;
};

} // namespace vloomdaw
