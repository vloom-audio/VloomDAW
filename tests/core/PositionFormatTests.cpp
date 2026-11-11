// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/PositionFormat.hpp"

#include <vloom/engine/time/MusicalTime.hpp>

#include <catch2/catch_test_macros.hpp>

using vloomdaw::formatPosition;

TEST_CASE("Start of song is 001.1.000", "[core]") { CHECK(formatPosition(0.0, 4) == "001.1.000"); }

TEST_CASE("Bars, beats and ticks are zero-padded", "[core]")
{
    CHECK(formatPosition(5.0 * vloom::kPpq + 12.0, 4) == "002.2.012");
}

TEST_CASE("Bar numbers grow past three digits", "[core]")
{
    CHECK(formatPosition(1000.0 * 4 * vloom::kPpq, 4) == "1001.1.000");
}

TEST_CASE("One beat at 120 BPM lasts half a second", "[core]")
{
    CHECK(vloomdaw::ticksToSeconds(vloom::kPpq, 120.0) == 0.5);
    CHECK(vloomdaw::ticksToSeconds(vloom::kPpq, 0.0) == 0.0);
}

TEST_CASE("Time is shown as M:SS.mmm", "[core]")
{
    using vloomdaw::formatTime;
    CHECK(formatTime(0.0) == "0:00.000");
    CHECK(formatTime(2.276) == "0:02.276");
    CHECK(formatTime(725.5) == "12:05.500");
    CHECK(formatTime(-3.0) == "0:00.000");
}

TEST_CASE("Milliseconds never round up into the next second", "[core]")
{
    CHECK(vloomdaw::formatTime(59.9996) == "0:59.999");
}
