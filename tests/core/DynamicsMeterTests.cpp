// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/DynamicsMeter.hpp"

#include <vloom/dsp/effects/Compressor.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>

using namespace vloomdaw;
using Catch::Approx;

TEST_CASE("Peak levels read in dB full scale", "[dynamicsmeter]")
{
    const std::array<float, 4> half{0.1f, -0.5f, 0.25f, 0.0f};
    CHECK(peakDb(half) == Approx(-6.0206).epsilon(1e-4));
    const std::array<float, 2> full{1.0f, -1.0f};
    CHECK(peakDb(full) == Approx(0.0));
    const std::array<float, 3> silent{};
    CHECK(std::isinf(peakDb(silent)));
    CHECK(std::isinf(peakDb({})));
    const std::array<float, 2> broken{std::numeric_limits<float>::quiet_NaN(), 0.5f};
    CHECK(peakDb(broken) == Approx(-6.0206).epsilon(1e-4));
}

TEST_CASE("A level history keeps the newest levels, oldest first", "[dynamicsmeter]")
{
    LevelHistory history(3);
    CHECK(history.size() == 0);
    CHECK(std::isnan(history.newest()));
    history.push(-1.0);
    history.push(-2.0);
    CHECK(history.size() == 2);
    CHECK(history.at(0) == -1.0);
    CHECK(history.newest() == -2.0);
    history.push(-3.0);
    history.push(-4.0); // -1 falls off
    CHECK(history.size() == 3);
    CHECK(history.at(0) == -2.0);
    CHECK(history.at(1) == -3.0);
    CHECK(history.at(2) == -4.0);
    CHECK(std::isnan(history.at(3)));
    history.push(kNoLevel);
    CHECK(std::isnan(history.newest()));
    history.clear();
    CHECK(history.size() == 0);
    CHECK(history.capacity() == 3);
}

TEST_CASE("The compressor's transfer curve matches the effect's", "[dynamicsmeter]")
{
    // Below the knee nothing changes; far above it the ratio holds.
    CHECK(compressorOutputDb(-40.0, -18.0, 4.0, 6.0, 0.0, false) == Approx(-40.0));
    CHECK(compressorOutputDb(-2.0, -18.0, 4.0, 0.0, 0.0, false) == Approx(-18.0 + 16.0 / 4.0));
    CHECK(compressorOutputDb(-40.0, -18.0, 4.0, 6.0, 3.0, false) == Approx(-37.0));

    // Auto makeup adds half the reduction at 0 dBFS, as dsp::Compressor does.
    const double atFullScale = vloom::dsp::Compressor::staticGainDb(0.0, -18.0, 4.0, 6.0);
    CHECK(compressorMakeupDb(-18.0, 4.0, 6.0, 2.0, true) == Approx(2.0 - 0.5 * atFullScale));
    CHECK(compressorMakeupDb(-18.0, 4.0, 6.0, 2.0, false) == Approx(2.0));

    // The effect itself agrees, at its own settings.
    vloom::dsp::Compressor compressor;
    compressor.setParameter(vloom::dsp::Compressor::kThreshold, -24.0);
    compressor.setParameter(vloom::dsp::Compressor::kRatio, 3.0);
    compressor.setParameter(vloom::dsp::Compressor::kKnee, 10.0);
    compressor.setParameter(vloom::dsp::Compressor::kMakeup, 1.5);
    compressor.setParameter(vloom::dsp::Compressor::kAutoMakeup, 1.0);
    for (double input = -60.0; input <= 0.0; input += 3.0)
        CHECK(compressorOutputDb(input, -24.0, 3.0, 10.0, 1.5, true) == Approx(compressor.transferDb(input)));
}

TEST_CASE("The input level is worked out from the output and the reduction", "[dynamicsmeter]")
{
    CHECK(inputFromOutputDb(-20.0, 4.0, 0.0) == Approx(-16.0));
    CHECK(inputFromOutputDb(-20.0, 4.0, 3.0) == Approx(-19.0)); // makeup came after
    CHECK(inputFromOutputDb(-20.0, -1.0, 0.0) == Approx(-20.0)); // never a negative reduction
    CHECK(std::isnan(inputFromOutputDb(-std::numeric_limits<double>::infinity(), 4.0, 0.0)));
    CHECK(std::isnan(inputFromOutputDb(kNoLevel, 4.0, 0.0)));
}
