// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/Decibels.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <limits>

using namespace vloomdaw;
using Catch::Approx;

TEST_CASE("Gain and decibels convert both ways", "[decibels]")
{
    CHECK(gainToDb(1.0) == Approx(0.0));
    CHECK(gainToDb(0.5) == Approx(-6.0206).epsilon(1e-4));
    CHECK(dbToGain(-6.0206) == Approx(0.5).epsilon(1e-4));
    CHECK(std::isinf(gainToDb(0.0)));
    CHECK(std::isinf(gainToDb(std::numeric_limits<double>::quiet_NaN())));
    CHECK(dbToGain(-std::numeric_limits<double>::infinity()) == 0.0);
}

TEST_CASE("Decibels read like a mixer", "[decibels]")
{
    CHECK(formatDb(1.0) == "0.0 dB");
    CHECK(formatDb(0.5) == "-6.0 dB");
    CHECK(formatDb(2.0) == "+6.0 dB");
    CHECK(formatDb(0.0) == "-inf dB");
    CHECK(formatDb(0.9999) == "0.0 dB"); // never "-0.0"
}

TEST_CASE("The fader has unity at 80 % and silence at the bottom", "[decibels]")
{
    CHECK(faderToGain(kUnityFaderPosition) == Approx(1.0));
    CHECK(faderToGain(0.0) == 0.0);
    CHECK(gainToDb(faderToGain(1.0)) == Approx(5.81).epsilon(1e-3));
    CHECK(faderToGain(-1.0) == 0.0); // clamped
    CHECK(faderToGain(2.0) == faderToGain(1.0));
}

TEST_CASE("Fader position and gain round-trip", "[decibels]")
{
    const double position = GENERATE(0.0, 0.1, 0.33, 0.5, 0.8, 0.95, 1.0);
    CHECK(gainToFader(faderToGain(position)) == Approx(position).margin(1e-9));
}

TEST_CASE("Balance turns down only the side panned away from", "[decibels]")
{
    CHECK(balanceGains(0.0) == std::pair{1.0, 1.0});
    CHECK(balanceGains(-1.0) == std::pair{1.0, 0.0});
    CHECK(balanceGains(1.0) == std::pair{0.0, 1.0});
    CHECK(balanceGains(0.5) == std::pair{0.5, 1.0});
    CHECK(balanceGains(5.0) == std::pair{0.0, 1.0}); // clamped
    CHECK(balanceGains(std::numeric_limits<double>::quiet_NaN()) == std::pair{1.0, 1.0});
}
