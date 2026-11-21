// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/TunerReading.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace vloomdaw;
using Catch::Approx;

TEST_CASE("The tuner names the note its readouts point at", "[tuner]")
{
    const TunerNote a4 = tunerNote(69.0, 0.0);
    CHECK(a4.name == "A");
    CHECK(a4.octave == 4);
    CHECK(a4.midi == 69);
    const TunerNote a2 = tunerNote(45.08, 8.0); // 110.5 Hz
    CHECK(a2.name == "A");
    CHECK(a2.octave == 2);
    CHECK(tunerNote(60.0, 0.0).name == "C");
    CHECK(tunerNote(60.0, 0.0).octave == 4);
    CHECK(tunerNote(61.0, 0.0).name == "C#");
    CHECK(tunerNote(59.0, 0.0).octave == 3); // B3
    CHECK(tunerNote(0.0, 0.0).octave == -1);

    // Half-way: the note the cents are measured from, whichever way it rounds.
    CHECK(tunerNote(69.5, 50.0).midi == 69);
    CHECK(tunerNote(69.5, -50.0).midi == 70);
    CHECK(tunerNote(69.49, -49.0).midi == 70);

    // None.
    CHECK(tunerNote(std::numeric_limits<double>::quiet_NaN(), 0.0).midi == -1);
    CHECK(tunerNote(69.0, std::numeric_limits<double>::infinity()).name.empty());
    CHECK(tunerNote(-3.0, 0.0).midi == -1);
    CHECK(tunerNote(140.0, 0.0).midi == -1);
}

TEST_CASE("The tuner reads in tune within five cents", "[tuner]")
{
    CHECK(inTune(0.0));
    CHECK(inTune(5.0));
    CHECK(inTune(-4.9));
    CHECK_FALSE(inTune(5.1));
    CHECK_FALSE(inTune(-20.0));
    CHECK_FALSE(inTune(std::numeric_limits<double>::quiet_NaN()));
}

TEST_CASE("The tuner's needle moves smoothly within a note and jumps to a new one", "[tuner]")
{
    NeedleSmoother needle(0.08);
    CHECK_FALSE(needle.active());
    // The first reading puts it there at once.
    CHECK(needle.step(10.0, 69, 1.0 / 30.0) == 10.0);
    CHECK(needle.active());
    // Then 63 % of the way in 0.08 s, however the time is cut.
    double at = 0.0;
    for (int i = 0; i < 8; ++i)
        at = needle.step(-10.0, 69, 0.01);
    CHECK(at == Approx(10.0 - 20.0 * (1.0 - std::exp(-1.0))).margin(1e-9));
    // A new note: no sweep across.
    CHECK(needle.step(-30.0, 70, 1.0 / 30.0) == -30.0);
    // Clamped to the meter.
    CHECK(needle.step(80.0, 71, 1.0 / 30.0) == 50.0);
    // No pitch, then a pitch again: afresh.
    CHECK(needle.step(std::numeric_limits<double>::quiet_NaN(), 71, 0.03) == 0.0);
    CHECK_FALSE(needle.active());
    CHECK(needle.step(20.0, 71, 0.03) == 20.0);
    needle.reset();
    CHECK(needle.step(-5.0, -1, 0.03) == 0.0);
    CHECK_FALSE(needle.active());
}
