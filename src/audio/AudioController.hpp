// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/AudioFormat.hpp"

#include <vloom/engine/Engine.hpp>
#include <vloom/engine/io/AudioDevice.hpp>
#include <vloom/engine/io/AudioInput.hpp>
#include <vloom/engine/io/MidiInput.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace vloomdaw {

struct OutputDeviceList {
    std::vector<std::string> names;
    int defaultIndex = -1; ///< index of the system default, -1 if unknown
};

/// Owns the engine and the hardware audio output, audio input, and MIDI input
/// devices, wiring them together in a real-time-safe manner.
///
/// Lives on the UI thread. Only callbacks run on real-time audio/MIDI threads.
/// Deliberately Qt-free so it can be unit-tested and reused by other front ends.
class AudioController {
public:
    AudioController() = default;
    ~AudioController();

    AudioController(const AudioController&) = delete;
    AudioController& operator=(const AudioController&) = delete;

    [[nodiscard]] OutputDeviceList outputDevices();
    [[nodiscard]] OutputDeviceList inputDevices();
    [[nodiscard]] static std::vector<std::string> midiInputDevices();

    /// (Re)opens output device `index` (-1 = system default) and starts audio
    /// with the format set by setFormat(). If an audio input was open, reopens
    /// it at the output device's new sample rate.
    bool openDevice(int index, std::string* error = nullptr);
    void closeDevice();

    /// Opens and starts audio input capture on `index` (-1 = system default),
    /// pushing captured frames into `Engine::pushAudioInput()`.
    bool openInputDevice(int index, std::string* error = nullptr);
    void closeInputDevice();

    /// Opens and starts hardware MIDI input `index` (`0 .. midiInputDevices().size()-1`),
    /// forwarding live messages to `Engine::playMidi(liveMidiChannel(), msg)`.
    bool openMidiInput(int index, std::string* error = nullptr);
    void closeMidiInput();

    /// Sets which channel-rack index (`0 .. N-1`) receives live hardware MIDI input.
    void setLiveMidiChannel(int channel) noexcept
    {
        liveMidiChannel_.store(channel < 0 ? 0 : channel, std::memory_order_relaxed);
    }
    [[nodiscard]] int liveMidiChannel() const noexcept
    {
        return liveMidiChannel_.load(std::memory_order_relaxed);
    }

    /// Sample rate and buffer size requested on the next openDevice(). The
    /// device may run at a different rate; sampleRate() reports the real one.
    void setFormat(int sampleRate, int bufferFrames) noexcept;
    [[nodiscard]] int requestedSampleRate() const noexcept { return requestedSampleRate_; }

    [[nodiscard]] vloom::Engine& engine() noexcept { return engine_; }
    [[nodiscard]] const vloom::Engine& engine() const noexcept { return engine_; }

    [[nodiscard]] bool isRunning() const noexcept { return device_.isRunning(); }
    [[nodiscard]] std::string deviceName() const { return device_.deviceName(); }
    [[nodiscard]] int sampleRate() const noexcept { return device_.sampleRate(); }
    [[nodiscard]] int outputChannels() const noexcept { return device_.channels(); }
    [[nodiscard]] int bufferFrames() const noexcept { return bufferFrames_; }

    /// Real-time channel matrix routing for the active Input Bus and Master Output Bus.
    void setActiveInputRouting(bool stereo, int leftChannel, int rightChannel) noexcept;
    void setActiveOutputRouting(bool stereo, int leftChannel, int rightChannel) noexcept;

    [[nodiscard]] bool isInputRunning() const noexcept { return input_.isRunning(); }
    [[nodiscard]] std::string inputDeviceName() const { return input_.deviceName(); }
    [[nodiscard]] int inputSampleRate() const noexcept { return input_.sampleRate(); }
    [[nodiscard]] int inputChannels() const noexcept { return input_.channels(); }
    [[nodiscard]] float takeInputPeak(int channel) noexcept;
    /// Per-physical-channel peak (`0 .. 7`) for the Audio I/O Setup matrix meters.
    [[nodiscard]] float takeHardwareInputPeak(int channel) noexcept;
    /// Drains ~200 Hz live input waveform peak buckets for real-time recording clip preview.
    [[nodiscard]] std::size_t takeInputWavePeaks(std::span<float> out) noexcept;
    void clearInputWavePeaks() noexcept;
    [[nodiscard]] double inputWavePeakSeconds() const noexcept;

    [[nodiscard]] bool isMidiRunning() const noexcept { return midi_.isRunning(); }
    [[nodiscard]] std::string midiDeviceName() const { return midi_.deviceName(); }
    [[nodiscard]] std::uint64_t midiActivityCount() const noexcept
    {
        return midiActivityCount_.load(std::memory_order_relaxed);
    }

private:
    static constexpr std::size_t kWaveRingSize = 4096; ///< power-of-two (~20 s at 200 peaks/s)
    static constexpr std::size_t kMaxHwChannels = 8;

    [[nodiscard]] vloom::AudioInput::Callback makeInputCallback();
    bool openInputWithFallback(int index, std::string* error = nullptr);
    bool reopenInputAtCurrentRate();

    vloom::Engine engine_;
    vloom::AudioDevice device_;
    vloom::AudioInput input_;
    vloom::MidiInput midi_;
    int requestedSampleRate_ = kDefaultSampleRate;
    int bufferFrames_ = kDefaultBufferFrames;
    bool hasRequestedInput_ = false;
    int requestedInputIndex_ = -1;
    std::atomic<int> liveMidiChannel_{0};
    std::atomic<bool> inStereo_{true};
    std::atomic<int> inLeft_{0};
    std::atomic<int> inRight_{1};
    std::atomic<bool> outStereo_{true};
    std::atomic<int> outLeft_{0};
    std::atomic<int> outRight_{1};
    std::atomic<float> inputPeakL_{0.0f};
    std::atomic<float> inputPeakR_{0.0f};
    std::array<std::atomic<float>, kMaxHwChannels> hwInputPeaks_{};
    std::array<float, vloom::AudioDevice::kMaxBlockFrames> outScratchL_{};
    std::array<float, vloom::AudioDevice::kMaxBlockFrames> outScratchR_{};
    std::atomic<int> waveFramesPerPeak_{240};
    float waveBucketPeak_ = 0.0f;
    int waveBucketFrames_ = 0;
    std::array<float, kWaveRingSize> waveRing_{};
    std::atomic<std::size_t> waveWrite_{0};
    std::atomic<std::size_t> waveRead_{0};
    std::atomic<std::uint64_t> midiActivityCount_{0};
};

} // namespace vloomdaw
