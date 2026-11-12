// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <string_view>

namespace vloomdaw {

/// Pitch classes per octave.
inline constexpr int kPitchClasses = 12;

/// Name of pitch class `pitchClass` (0 = C .. 11 = B), with sharps:
/// "C", "C#", "D", … "B". Any integer works; it wraps around the octave,
/// so -1 is "B" and 12 is "C".
[[nodiscard]] std::string_view noteName(int pitchClass);

/// Pitch class 0..11 of any integer (MIDI note number, pitch class, …).
[[nodiscard]] constexpr int pitchClassOf(int note)
{
    const int pc = note % kPitchClasses;
    return pc < 0 ? pc + kPitchClasses : pc;
}

} // namespace vloomdaw
