// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <span>
#include <string_view>

namespace vloomdaw {

/// Scales the key display offers, in menu order.
enum class Scale {
    Major,
    NaturalMinor,
    HarmonicMinor,
    MelodicMinor,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    Locrian,
    MajorPentatonic,
    MinorPentatonic,
    Blues,
    Chromatic,
};

struct ScaleInfo {
    Scale scale;
    std::string_view name; ///< "Natural minor"
    std::string_view shortName; ///< at most 4 characters, for the LCD: "MIN"
    std::span<const int> intervals; ///< semitones above the root, ascending, starting at 0
};

/// Every scale, in the order of the Scale enum.
[[nodiscard]] std::span<const ScaleInfo> allScales();
[[nodiscard]] const ScaleInfo& scaleInfo(Scale scale);

/// True when `note` (any integer: MIDI note or pitch class) belongs to
/// `scale` built on `root` (any integer, taken modulo 12).
[[nodiscard]] bool scaleContains(Scale scale, int root, int note);

} // namespace vloomdaw
