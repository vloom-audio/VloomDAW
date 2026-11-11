// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/Scales.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace vloomdaw;

TEST_CASE("Scale table matches the enum and is well formed", "[scales]")
{
    const auto scales = allScales();
    for (std::size_t i = 0; i < scales.size(); ++i) {
        const ScaleInfo& s = scales[i];
        CAPTURE(s.name);
        CHECK(static_cast<std::size_t>(s.scale) == i);
        CHECK(&scaleInfo(s.scale) == &s);
        CHECK(!s.shortName.empty());
        CHECK(s.shortName.size() <= 4); // fits the LCD field
        REQUIRE(!s.intervals.empty());
        CHECK(s.intervals.front() == 0);
        CHECK(std::is_sorted(s.intervals.begin(), s.intervals.end()));
        CHECK(std::adjacent_find(s.intervals.begin(), s.intervals.end()) == s.intervals.end());
        CHECK(s.intervals.back() < 12);
    }
}

TEST_CASE("C major is the white keys", "[scales]")
{
    for (int pc : {0, 2, 4, 5, 7, 9, 11})
        CHECK(scaleContains(Scale::Major, 0, pc));
    for (int pc : {1, 3, 6, 8, 10})
        CHECK_FALSE(scaleContains(Scale::Major, 0, pc));
}

TEST_CASE("Scales transpose with the root and work on MIDI notes", "[scales]")
{
    // A natural minor = C major's notes.
    for (int pc = 0; pc < 12; ++pc)
        CHECK(scaleContains(Scale::NaturalMinor, 9, pc) == scaleContains(Scale::Major, 0, pc));
    // D major has F# (66) and C# (61), not F (65) or C (60).
    CHECK(scaleContains(Scale::Major, 2, 66));
    CHECK(scaleContains(Scale::Major, 2, 61));
    CHECK_FALSE(scaleContains(Scale::Major, 2, 65));
    CHECK_FALSE(scaleContains(Scale::Major, 2, 60));
    // Negative notes and roots wrap too.
    CHECK(scaleContains(Scale::Major, -12, -1)); // C major, B
}

TEST_CASE("Harmonic minor raises the seventh", "[scales]")
{
    CHECK(scaleContains(Scale::HarmonicMinor, 9, 8)); // A harmonic minor has G#
    CHECK_FALSE(scaleContains(Scale::HarmonicMinor, 9, 7)); // but not G
}

TEST_CASE("Chromatic contains everything", "[scales]")
{
    for (int n = -24; n < 24; ++n)
        CHECK(scaleContains(Scale::Chromatic, 5, n));
}
