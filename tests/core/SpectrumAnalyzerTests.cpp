// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/SpectrumAnalyzer.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

using namespace vloomdaw;
using Catch::Approx;

namespace {

constexpr double kRate = 48000.0;
/// A tone between two analysed frequencies reads up to this much low (the
/// window spreads it over both): 1000 Hz at 48 kHz is such a tone.
constexpr double kBetweenBinsDb = 1.5;

std::vector<float> sine(double hz, double amplitude, std::size_t frames = SpectrumAnalyzer::kFrames)
{
    std::vector<float> signal(frames);
    for (std::size_t i = 0; i < frames; ++i)
        signal[i] = static_cast<float>(
            amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kRate));
    return signal;
}

} // namespace

TEST_CASE("A full-scale sine reads 0 dB at its frequency and little elsewhere", "[spectrum]")
{
    SpectrumAnalyzer analyzer(kRate);
    CHECK_FALSE(analyzer.hasSignal());
    analyzer.analyse(sine(1000.0, 1.0));
    CHECK(analyzer.hasSignal());
    CHECK(analyzer.levelDb(980.0, 1020.0) == Approx(0.0).margin(kBetweenBinsDb));
    CHECK(analyzer.levelDb(100.0, 200.0) < -60.0);
    CHECK(analyzer.levelDb(8000.0, 12000.0) < -60.0);
}

TEST_CASE("Half the amplitude reads 6 dB less", "[spectrum]")
{
    SpectrumAnalyzer analyzer(kRate);
    analyzer.analyse(sine(3000.0, 0.5));
    CHECK(analyzer.levelDb(2950.0, 3050.0) == Approx(-6.02).margin(0.5));
}

TEST_CASE("Levels fall slowly once the sound is gone, down to silence", "[spectrum]")
{
    SpectrumAnalyzer analyzer(kRate);
    analyzer.analyse(sine(1000.0, 1.0));
    const double before = analyzer.levelDb(980.0, 1020.0);
    const std::vector<float> silence(SpectrumAnalyzer::kFrames, 0.0f);
    analyzer.analyse(silence);
    CHECK(analyzer.levelDb(980.0, 1020.0) == Approx(before - SpectrumAnalyzer::kFallDb).margin(1e-9));
    for (int i = 0; i < 200; ++i)
        analyzer.analyse(silence);
    CHECK(analyzer.levelDb(980.0, 1020.0) == SpectrumAnalyzer::kFloorDb);
    CHECK_FALSE(analyzer.hasSignal());
}

TEST_CASE("Short, long and bad input are handled", "[spectrum]")
{
    SpectrumAnalyzer analyzer(kRate);
    analyzer.analyse(sine(1000.0, 1.0, 1024)); // short: silence before it
    CHECK(analyzer.levelDb(900.0, 1100.0) > -40.0);
    analyzer.clear();
    CHECK_FALSE(analyzer.hasSignal());

    analyzer.analyse(sine(1000.0, 1.0, SpectrumAnalyzer::kFrames * 3)); // long: the newest part
    CHECK(analyzer.levelDb(980.0, 1020.0) == Approx(0.0).margin(kBetweenBinsDb));

    analyzer.clear();
    std::vector<float> bad(SpectrumAnalyzer::kFrames, std::numeric_limits<float>::quiet_NaN());
    analyzer.analyse(bad); // counted as silence
    CHECK_FALSE(analyzer.hasSignal());
    analyzer.analyse({});
    CHECK(analyzer.levelDb(std::numeric_limits<double>::quiet_NaN(), 100.0) == SpectrumAnalyzer::kFloorDb);
    CHECK(analyzer.levelDb(1.0e9, -5.0) == SpectrumAnalyzer::kFloorDb); // past both ends: clamped
}
