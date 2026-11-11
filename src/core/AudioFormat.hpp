// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <array>

namespace vloomdaw {

/// Sample rates offered in the settings.
inline constexpr std::array<int, 4> kSampleRates{44100, 48000, 88200, 96000};
/// Buffer sizes (frames) offered in the settings.
inline constexpr std::array<int, 6> kBufferSizes{64, 128, 256, 512, 1024, 2048};

inline constexpr int kDefaultSampleRate = 48000;
inline constexpr int kDefaultBufferFrames = 256;

/// Output latency added by one buffer, in milliseconds. 0 for a bad rate.
double bufferLatencyMs(int bufferFrames, int sampleRate);

/// `value` if it is one of the offered sample rates, else the default.
int validSampleRate(int value);
/// `value` if it is one of the offered buffer sizes, else the default.
int validBufferFrames(int value);

} // namespace vloomdaw
