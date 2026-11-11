// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/NoteNames.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace vloomdaw;

TEST_CASE("Pitch classes are named with sharps", "[notes]")
{
    CHECK(noteName(0) == "C");
    CHECK(noteName(1) == "C#");
    CHECK(noteName(4) == "E");
    CHECK(noteName(9) == "A");
    CHECK(noteName(11) == "B");
}

TEST_CASE("Note names wrap around the octave", "[notes]")
{
    CHECK(noteName(12) == "C");
    CHECK(noteName(-1) == "B");
    CHECK(noteName(60) == "C"); // middle C as a MIDI note
    CHECK(noteName(69) == "A"); // A440
    CHECK(noteName(-13) == "B");
}

TEST_CASE("Any integer maps to a pitch class 0..11", "[notes]")
{
    for (int n = -50; n <= 50; ++n) {
        const int pc = pitchClassOf(n);
        CHECK(pc >= 0);
        CHECK(pc < kPitchClasses);
        CHECK((n - pc) % kPitchClasses == 0);
    }
}
