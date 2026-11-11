// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/NoteNames.hpp"

#include <array>

namespace vloomdaw {

std::string_view noteName(int pitchClass)
{
    static constexpr std::array<std::string_view, kPitchClasses> kNames{
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return kNames[static_cast<std::size_t>(pitchClassOf(pitchClass))];
}

} // namespace vloomdaw
