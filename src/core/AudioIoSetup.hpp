// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace vloomdaw {

/// A named Mono or Stereo bus mapped to physical hardware audio channels
/// (`0 .. kMaxHardwareChannels - 1`) in the Audio I/O Setup matrix.
struct AudioIoBus {
    std::string name;
    bool stereo = true; ///< `false`: Mono (`M` on `leftChannel`), `true`: Stereo (`L` / `R`)
    int leftChannel = 0; ///< 0-based physical channel (`M` when mono, `L` when stereo)
    int rightChannel = 1; ///< 0-based physical `R` channel when `stereo`

    friend bool operator==(const AudioIoBus&, const AudioIoBus&) = default;
};

/// Persisted Audio Driver mode and Input / Output channel routing matrix.
/// Pure C++20 data model (`core/`), completely independent of Qt.
struct AudioIoConfig {
    static constexpr int kMaxHardwareChannels = 8;
    static constexpr std::size_t kMaxBuses = 16;

    /// When `true`, picking an output device automatically pairs the matching
    /// capture endpoint of the same hardware interface (unified interface mode).
    bool unifiedDevice = false;

    std::vector<AudioIoBus> inputs;
    std::vector<AudioIoBus> outputs;

    /// Active default input bus (`0 .. inputs.size()-1`) used for recording & monitoring.
    int activeInputBus = 2;
    /// Active master output bus (`0 .. outputs.size()-1`).
    int activeOutputBus = 0;

    friend bool operator==(const AudioIoConfig&, const AudioIoConfig&) = default;
};

/// Returns the factory-default Studio I/O matrix:
/// Inputs: `IN 0` (Mono ch 0), `IN 1` (Mono ch 1), `IN 0 + 1` (Stereo ch 0+1, active).
/// Outputs: `Main` (Stereo ch 0+1, active).
[[nodiscard]] AudioIoConfig defaultAudioIoConfig();

/// Clamps channel indices, bus counts, names, and active indices into valid bounds.
[[nodiscard]] AudioIoConfig clampedAudioIoConfig(AudioIoConfig config);

/// Serialises / deserialises `AudioIoConfig` as compact JSON for settings storage.
[[nodiscard]] std::string serializeAudioIoConfig(const AudioIoConfig& config);
[[nodiscard]] AudioIoConfig deserializeAudioIoConfig(const std::string& jsonText);

/// Returns the human-readable native audio driver name for the current OS.
[[nodiscard]] const char* nativeAudioDriverName() noexcept;

} // namespace vloomdaw
