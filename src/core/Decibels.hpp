// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <string>
#include <utility>

namespace vloomdaw {

/// Gain below this reads as silence (-inf dB).
inline constexpr double kSilentGain = 1e-6;

/// Linear gain to decibels; -infinity for silence.
[[nodiscard]] double gainToDb(double gain);
/// Decibels to linear gain; 0 for -infinity.
[[nodiscard]] double dbToGain(double db);
/// "-6.0 dB", "+2.5 dB", "0.0 dB" or "-inf dB".
[[nodiscard]] std::string formatDb(double gain);

/// Mixer fader position (0 = bottom, 1 = top) to linear gain.
///
/// Cubic taper, so equal fader travel feels like equal loudness steps:
/// unity sits at kUnityFaderPosition, the top is +5.8 dB, the bottom silent.
[[nodiscard]] double faderToGain(double position);
/// Inverse of faderToGain(), clamped to 0..1.
[[nodiscard]] double gainToFader(double gain);
inline constexpr double kUnityFaderPosition = 0.8;

/// Stereo balance: gains for left and right at `pan` (-1 left .. +1 right).
/// The side panned away from is turned down; the other stays at 1. This is
/// the same law the engine uses for its master pan.
[[nodiscard]] std::pair<double, double> balanceGains(double pan);

} // namespace vloomdaw
