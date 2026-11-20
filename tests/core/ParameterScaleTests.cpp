// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/ParameterScale.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace vloomdaw;
using Catch::Approx;

TEST_CASE("Units pick the scale a control turns in", "[parameterscale]")
{
    CHECK(defaultScale("Hz", 20.0, 20000.0) == ParameterScale::Log);
    CHECK(defaultScale("ms", 0.05, 250.0) == ParameterScale::Log);
    CHECK(defaultScale(":1", 1.0, 20.0) == ParameterScale::Log);
    CHECK(defaultScale("ms", 0.0, 2000.0) == ParameterScale::Power);
    CHECK(defaultScale("dB", -60.0, 0.0) == ParameterScale::Linear);
    CHECK(defaultScale("%", 0.0, 100.0) == ParameterScale::Linear);
    CHECK(defaultScale("Hz", 0.0, 100.0) == ParameterScale::Linear); // no log from zero
}

TEST_CASE("Positions and values map both ways", "[parameterscale]")
{
    SECTION("linear")
    {
        CHECK(positionOf(ParameterScale::Linear, -60.0, 0.0, -18.0) == Approx(0.7));
        CHECK(valueAt(ParameterScale::Linear, -60.0, 0.0, 0.7, false) == Approx(-18.0));
    }
    SECTION("log: equal turns multiply")
    {
        CHECK(positionOf(ParameterScale::Log, 20.0, 20000.0, 632.455532) == Approx(0.5));
        CHECK(valueAt(ParameterScale::Log, 20.0, 20000.0, 0.5, false) == Approx(632.455532));
        const double low = valueAt(ParameterScale::Log, 1.0, 1000.0, 0.2, false);
        const double high = valueAt(ParameterScale::Log, 1.0, 1000.0, 0.4, false);
        CHECK(high / low == Approx(valueAt(ParameterScale::Log, 1.0, 1000.0, 0.2, false)));
    }
    SECTION("power: fine at the bottom")
    {
        CHECK(valueAt(ParameterScale::Power, 0.0, 2000.0, 0.5, false) == Approx(250.0));
        CHECK(positionOf(ParameterScale::Power, 0.0, 2000.0, 250.0) == Approx(0.5));
    }
    SECTION("round trips")
    {
        for (const ParameterScale scale :
            {ParameterScale::Linear, ParameterScale::Log, ParameterScale::Power})
            for (double position = 0.0; position <= 1.0; position += 0.125)
                CHECK(positionOf(scale, 0.5, 5000.0, valueAt(scale, 0.5, 5000.0, position, false))
                    == Approx(position).margin(1e-12));
    }
    SECTION("kept in range")
    {
        CHECK(positionOf(ParameterScale::Linear, 0.0, 1.0, 5.0) == 1.0);
        CHECK(positionOf(ParameterScale::Log, 1.0, 10.0, -3.0) == 0.0);
        CHECK(valueAt(ParameterScale::Linear, 0.0, 10.0, 2.0, false) == 10.0);
        CHECK(valueAt(ParameterScale::Linear, 0.0, 10.0, std::numeric_limits<double>::quiet_NaN(), false)
            == 0.0);
        CHECK(positionOf(ParameterScale::Linear, 1.0, 1.0, 1.0) == 0.0); // no range
    }
    SECTION("stepped values are whole")
    {
        CHECK(valueAt(ParameterScale::Linear, 0.0, 3.0, 0.4, true) == 1.0);
        CHECK(valueAt(ParameterScale::Linear, 0.0, 3.0, 0.6, true) == 2.0);
    }
    SECTION("log falls back to linear for a range from zero")
    {
        CHECK(positionOf(ParameterScale::Log, 0.0, 10.0, 5.0) == Approx(0.5));
    }
}

TEST_CASE("A range across zero grows its arc from zero", "[parameterscale]")
{
    CHECK(*zeroPosition(ParameterScale::Linear, -12.0, 24.0) == Approx(1.0 / 3.0));
    CHECK(*zeroPosition(ParameterScale::Linear, -1.0, 1.0) == Approx(0.5));
    CHECK_FALSE(zeroPosition(ParameterScale::Linear, -60.0, 0.0));
    CHECK_FALSE(zeroPosition(ParameterScale::Log, 20.0, 500.0));
}

TEST_CASE("Drags and notches move a control", "[parameterscale]")
{
    CHECK(dragPosition(0.5, kDragPixels / 4.0, false) == Approx(0.75));
    CHECK(dragPosition(0.5, kDragPixels / 4.0, true) == Approx(0.525));
    CHECK(dragPosition(0.9, kDragPixels, false) == 1.0);
    CHECK(dragPosition(0.1, -kDragPixels, false) == 0.0);
    CHECK(wheelPosition(0.5, 2.0, false) == Approx(0.5 + 2.0 * kWheelStep));
    CHECK(wheelPosition(0.5, -1.0, true) == Approx(0.5 - kWheelStep * kFineFactor));
    CHECK(stepValue(0.0, 1, 0.0, 1.0) == 1.0);
    CHECK(stepValue(1.0, 1, 0.0, 1.0) == 1.0);
    CHECK(stepValue(2.2, -1, 0.0, 4.0) == 1.0);
}

TEST_CASE("Values read as in the designs", "[parameterscale]")
{
    const std::string minus = "\xE2\x88\x92";
    CHECK(formatParameterValue("dB", -18.0) == minus + "18.0 dB");
    CHECK(formatParameterValue("dB", 3.0) == "+3.0 dB");
    CHECK(formatParameterValue("dB", -0.04) == "0.0 dB");
    CHECK(formatParameterValue(":1", 4.0) == "4.0 : 1");
    CHECK(formatParameterValue(":1", 20.0) == "20 : 1");
    CHECK(formatParameterValue("ms", 10.0) == "10.0 ms");
    CHECK(formatParameterValue("ms", 120.0) == "120 ms");
    CHECK(formatParameterValue("ms", 0.5) == "0.50 ms");
    CHECK(formatParameterValue("ms", 1200.0) == "1.20 s");
    CHECK(formatParameterValue("Hz", 80.0) == "80 Hz");
    CHECK(formatParameterValue("Hz", 6000.0) == "6.00 kHz");
    CHECK(formatParameterValue("Hz", 0.8) == "0.80 Hz");
    CHECK(formatParameterValue("%", 100.0) == "100 %");
    CHECK(formatParameterValue("", 3.0) == "3");
    CHECK(formatParameterValue("st", -2.0) == minus + "2 st");
    CHECK(formatParameterValue("x", 1.5) == "1.50 x");
}
