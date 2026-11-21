// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/AnalyzerSpectrum.hpp"

#include <vloom/dsp/effects/Analyzer.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

using namespace vloomdaw;
using Catch::Approx;
using Analyzer = vloom::dsp::Analyzer;

namespace {

constexpr double kRate = 48000.0;
/// 1500 Hz at 48 kHz falls on a bin of every size from 1024 frames up
/// (46.875 Hz apart at 1024), so it reads its full level.
constexpr double kOnBin = 1500.0;

std::vector<float> sine(double hz, double amplitude, std::size_t frames)
{
    std::vector<float> signal(frames);
    for (std::size_t i = 0; i < frames; ++i)
        signal[i] = static_cast<float>(
            amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kRate));
    return signal;
}

/// An analyzer with no averaging and no tilt: each analyse() reads anew.
AnalyzerSpectrum instant(int size)
{
    AnalyzerSpectrum spectrum(kRate);
    spectrum.setFftSize(size);
    spectrum.setAveragingSeconds(0.0);
    spectrum.setTiltDbPerOctave(0.0);
    return spectrum;
}

} // namespace

TEST_CASE("The analyzer's spectrum reads a sine at its level for every resolution", "[analyzer]")
{
    for (int resolution = 0; resolution < Analyzer::kResolutions; ++resolution) {
        const int size = Analyzer::fftSize(resolution);
        INFO(size);
        AnalyzerSpectrum spectrum = instant(size);
        CHECK(spectrum.fftSize() == size);
        CHECK_FALSE(spectrum.hasSignal());
        spectrum.analyse(sine(kOnBin, 0.5, static_cast<std::size_t>(size)), 0.03);
        CHECK(spectrum.hasSignal());
        CHECK(spectrum.levelDb(kOnBin - 1.0, kOnBin + 1.0) == Approx(20.0 * std::log10(0.5)).margin(0.05));
        // Far from it, the Hann window's skirt is well down.
        CHECK(spectrum.levelDb(200.0, 300.0) < -80.0);
        CHECK(spectrum.levelDb(10000.0, 12000.0) < -80.0);
    }
}

TEST_CASE("The analyzer's spectrum ignores sizes that are not powers of two", "[analyzer]")
{
    AnalyzerSpectrum spectrum(kRate);
    CHECK(spectrum.fftSize() == AnalyzerSpectrum::kDefaultFftSize);
    spectrum.setFftSize(3000);
    spectrum.setFftSize(0);
    spectrum.setFftSize(-4);
    CHECK(spectrum.fftSize() == AnalyzerSpectrum::kDefaultFftSize);
    spectrum.setFftSize(1024);
    CHECK(spectrum.fftSize() == 1024);
    // A short signal is padded with silence before it; NaN reads as silence.
    std::vector<float> bad(512, std::numeric_limits<float>::quiet_NaN());
    spectrum.analyse(bad, 0.03);
    CHECK_FALSE(spectrum.hasSignal());
    CHECK(spectrum.levelDb(100.0, 20000.0) == AnalyzerSpectrum::kFloorDb);
}

TEST_CASE("The analyzer's average covers 63 % of a change in its time", "[analyzer]")
{
    // Medium: 0.25 s. A sine from silence: after 0.25 s the power is 63 %
    // of the sine's, however the time is cut into redraws.
    const double seconds = Analyzer::averagingSeconds(1);
    REQUIRE(seconds == Approx(0.25));
    const std::vector<float> tone = sine(kOnBin, 1.0, 4096);
    const double expected = 10.0 * std::log10(1.0 - std::exp(-1.0));
    for (const int steps : {1, 5, 30}) {
        AnalyzerSpectrum spectrum = instant(4096);
        spectrum.setAveragingSeconds(seconds);
        for (int i = 0; i < steps; ++i)
            spectrum.analyse(tone, seconds / static_cast<double>(steps));
        INFO(steps);
        CHECK(spectrum.levelDb(kOnBin, kOnBin) == Approx(expected).margin(0.05));
    }

    // Slow takes longer than fast to reach the same level.
    AnalyzerSpectrum slow = instant(4096);
    slow.setAveragingSeconds(Analyzer::averagingSeconds(0));
    AnalyzerSpectrum fast = instant(4096);
    fast.setAveragingSeconds(Analyzer::averagingSeconds(2));
    for (int i = 0; i < 3; ++i) {
        slow.analyse(tone, 1.0 / 30.0);
        fast.analyse(tone, 1.0 / 30.0);
    }
    CHECK(fast.levelDb(kOnBin, kOnBin) > slow.levelDb(kOnBin, kOnBin) + 6.0);

    // Silence lets it fall at the same speed, down to the floor.
    for (int i = 0; i < 300; ++i)
        fast.analyse({}, 1.0 / 30.0);
    CHECK_FALSE(fast.hasSignal());
}

TEST_CASE("The analyzer's tilt follows the dsp's tiltDb", "[analyzer]")
{
    AnalyzerSpectrum spectrum = instant(8192);
    std::vector<float> two = sine(375.0, 0.25, 8192); // 375 and 6000 Hz both fall on bins
    const std::vector<float> high = sine(6000.0, 0.25, 8192);
    for (std::size_t i = 0; i < two.size(); ++i)
        two[i] += high[i];
    spectrum.analyse(two, 0.03);
    const double flatLow = spectrum.levelDb(375.0, 375.0);
    const double flatHigh = spectrum.levelDb(6000.0, 6000.0);
    CHECK(flatLow == Approx(flatHigh).margin(0.1));

    spectrum.setTiltDbPerOctave(4.5);
    CHECK(spectrum.tiltDbPerOctave() == 4.5);
    CHECK(spectrum.levelDb(375.0, 375.0) == Approx(flatLow + Analyzer::tiltDb(375.0, 4.5)).margin(0.01));
    CHECK(spectrum.levelDb(6000.0, 6000.0) == Approx(flatHigh + Analyzer::tiltDb(6000.0, 4.5)).margin(0.01));
    CHECK(Analyzer::tiltDb(1000.0, 4.5) == Approx(0.0)); // the pivot
    // Silence stays at the floor, tilted or not.
    CHECK(spectrum.levelDb(15000.0, 16000.0) < -80.0);
}

TEST_CASE("The analyzer holds peaks and freezes", "[analyzer]")
{
    AnalyzerSpectrum spectrum = instant(4096);
    const std::vector<float> loud = sine(kOnBin, 1.0, 4096);
    const std::vector<float> quiet = sine(kOnBin, 0.1, 4096);
    spectrum.analyse(loud, 0.03);
    spectrum.analyse(quiet, 0.03);
    CHECK(spectrum.levelDb(kOnBin, kOnBin) == Approx(-20.0).margin(0.05));
    CHECK(spectrum.peakDb(kOnBin, kOnBin) == Approx(0.0).margin(0.05));

    spectrum.clearPeaks();
    CHECK(spectrum.peakDb(kOnBin, kOnBin) == Approx(-20.0).margin(0.05));
    spectrum.setPeakHold(false);
    CHECK(spectrum.peakDb(kOnBin, kOnBin) == AnalyzerSpectrum::kFloorDb);
    spectrum.analyse(loud, 0.03);
    CHECK(spectrum.peakDb(kOnBin, kOnBin) == AnalyzerSpectrum::kFloorDb);
    spectrum.setPeakHold(true);

    // Frozen: new audio changes nothing.
    spectrum.setFrozen(true);
    spectrum.analyse(quiet, 0.03);
    CHECK(spectrum.levelDb(kOnBin, kOnBin) == Approx(0.0).margin(0.05));
    spectrum.setFrozen(false);
    spectrum.analyse(quiet, 0.03);
    CHECK(spectrum.levelDb(kOnBin, kOnBin) == Approx(-20.0).margin(0.05));

    spectrum.clear();
    CHECK_FALSE(spectrum.hasSignal());
    CHECK(spectrum.peakDb(kOnBin, kOnBin) == AnalyzerSpectrum::kFloorDb);
}
