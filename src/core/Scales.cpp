// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/Scales.hpp"

#include "core/NoteNames.hpp"

#include <algorithm>
#include <array>

namespace vloomdaw {

namespace {

constexpr std::array kMajor{0, 2, 4, 5, 7, 9, 11};
constexpr std::array kNaturalMinor{0, 2, 3, 5, 7, 8, 10};
constexpr std::array kHarmonicMinor{0, 2, 3, 5, 7, 8, 11};
constexpr std::array kMelodicMinor{0, 2, 3, 5, 7, 9, 11};
constexpr std::array kDorian{0, 2, 3, 5, 7, 9, 10};
constexpr std::array kPhrygian{0, 1, 3, 5, 7, 8, 10};
constexpr std::array kLydian{0, 2, 4, 6, 7, 9, 11};
constexpr std::array kMixolydian{0, 2, 4, 5, 7, 9, 10};
constexpr std::array kLocrian{0, 1, 3, 5, 6, 8, 10};
constexpr std::array kMajorPentatonic{0, 2, 4, 7, 9};
constexpr std::array kMinorPentatonic{0, 3, 5, 7, 10};
constexpr std::array kBlues{0, 3, 5, 6, 7, 10};
constexpr std::array kChromatic{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

const std::array<ScaleInfo, 13> kScales{{
    {Scale::Major, "Major", "MAJ", kMajor},
    {Scale::NaturalMinor, "Natural minor", "MIN", kNaturalMinor},
    {Scale::HarmonicMinor, "Harmonic minor", "HMIN", kHarmonicMinor},
    {Scale::MelodicMinor, "Melodic minor", "MMIN", kMelodicMinor},
    {Scale::Dorian, "Dorian", "DOR", kDorian},
    {Scale::Phrygian, "Phrygian", "PHR", kPhrygian},
    {Scale::Lydian, "Lydian", "LYD", kLydian},
    {Scale::Mixolydian, "Mixolydian", "MIX", kMixolydian},
    {Scale::Locrian, "Locrian", "LOC", kLocrian},
    {Scale::MajorPentatonic, "Major pentatonic", "PMAJ", kMajorPentatonic},
    {Scale::MinorPentatonic, "Minor pentatonic", "PMIN", kMinorPentatonic},
    {Scale::Blues, "Blues", "BLUE", kBlues},
    {Scale::Chromatic, "Chromatic", "CHR", kChromatic},
}};

} // namespace

std::span<const ScaleInfo> allScales() { return kScales; }

const ScaleInfo& scaleInfo(Scale scale)
{
    const auto index = static_cast<std::size_t>(scale);
    return index < kScales.size() ? kScales[index] : kScales.front();
}

bool scaleContains(Scale scale, int root, int note)
{
    const int degree = pitchClassOf(note - root);
    const auto intervals = scaleInfo(scale).intervals;
    return std::binary_search(intervals.begin(), intervals.end(), degree);
}

} // namespace vloomdaw
