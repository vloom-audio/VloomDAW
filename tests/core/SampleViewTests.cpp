// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/SampleView.hpp"
#include "core/TimeEffects.hpp"

#include <vloom/dsp/Envelope.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

using namespace vloomdaw;
using Catch::Approx;

TEST_CASE("Sample peaks take the lowest and highest of each slice over every channel", "[sampleview]")
{
    const std::array<float, 6> left{0.1f, -0.2f, 0.5f, 0.0f, -0.9f, 0.3f};
    const std::array<float, 6> right{0.0f, 0.4f, -0.1f, 0.2f, 0.0f, 0.0f};
    const std::array<const float*, 2> channels{left.data(), right.data()};
    const std::vector<PeakColumn> peaks = samplePeaks(channels, 6, 3);
    REQUIRE(peaks.size() == 3);
    CHECK(peaks[0] == PeakColumn{-0.2f, 0.4f});
    CHECK(peaks[1] == PeakColumn{-0.1f, 0.5f});
    CHECK(peaks[2] == PeakColumn{-0.9f, 0.3f});

    // Fewer frames than columns: a column a frame. None: nothing.
    CHECK(samplePeaks(channels, 2, 100).size() == 2);
    CHECK(samplePeaks(channels, 0, 100).empty());
    CHECK(samplePeaks({}, 6, 3).empty());

    // A broken value reads as silence.
    const std::array<float, 2> broken{std::numeric_limits<float>::quiet_NaN(), 0.25f};
    const std::array<const float*, 1> one{broken.data()};
    CHECK(samplePeaks(one, 2, 1)[0] == PeakColumn{0.0f, 0.25f});
}

TEST_CASE("A column of a picture takes the peaks it covers", "[sampleview]")
{
    const std::vector<PeakColumn> peaks{{-0.1f, 0.1f}, {-0.5f, 0.2f}, {-0.2f, 0.8f}, {0.0f, 0.0f}};
    CHECK(peakBetween(peaks, 0.0, 0.5) == PeakColumn{-0.5f, 0.2f});
    CHECK(peakBetween(peaks, 0.25, 0.75) == PeakColumn{-0.5f, 0.8f});
    // Narrower than a peak: the one it is in.
    CHECK(peakBetween(peaks, 0.55, 0.56) == PeakColumn{-0.2f, 0.8f});
    // Out of range: kept in.
    CHECK(peakBetween(peaks, 1.0, 1.2) == PeakColumn{0.0f, 0.0f});
    CHECK(peakBetween(peaks, -1.0, 0.1) == PeakColumn{-0.1f, 0.1f});
    CHECK(peakBetween({}, 0.0, 1.0) == PeakColumn{});
}

TEST_CASE("The envelope drawn follows vloom-dsp's", "[sampleview]")
{
    constexpr float kRate = 48000.0f;
    const double attack = 0.01;
    const double decay = 0.3;
    const double sustain = 0.4;
    const double release = 0.2;
    const auto shape = vloom::dsp::EnvelopeShape::make(kRate, static_cast<float>(attack), 0.0f,
        static_cast<float>(decay), static_cast<float>(sustain), static_cast<float>(release));
    vloom::dsp::Envelope envelope;
    envelope.start();
    double level = 0.0;
    const int held = static_cast<int>(0.5 * kRate);
    for (int frame = 1; frame <= held; ++frame) {
        level = envelope.next(shape);
        if (frame % 2400 == 0) // every 50 ms
            CHECK(envelopeLevel(frame / static_cast<double>(kRate), attack, decay, sustain)
                == Approx(level).margin(2e-3));
    }
    CHECK(envelopeLevel(0.005, attack, decay, sustain) == Approx(0.5));
    CHECK(envelopeLevel(0.0, attack, decay, sustain) == 0.0);
    CHECK(envelopeLevel(100.0, attack, decay, sustain) == Approx(sustain));

    envelope.release();
    for (int frame = 1; frame <= 4800; ++frame) {
        const double now = envelope.next(shape);
        if (frame % 960 == 0)
            CHECK(
                releaseLevel(level, frame / static_cast<double>(kRate), release) == Approx(now).margin(2e-3));
    }
    CHECK(releaseLevel(0.5, 0.0, release) == 0.5);
    CHECK(releaseLevel(1.0, release, release) == Approx(std::exp(-5.0)));
}

TEST_CASE("A sample tuned up plays faster", "[sampleview]")
{
    CHECK(playbackSpeed(0.0, 0.0) == 1.0);
    CHECK(playbackSpeed(12.0, 0.0) == Approx(2.0));
    CHECK(playbackSpeed(-12.0, 0.0) == Approx(0.5));
    CHECK(playbackSpeed(0.0, 1200.0) == Approx(2.0));
    CHECK(playbackSpeed(7.0, 0.0) == Approx(1.4983).epsilon(1e-4));
}

TEST_CASE("A time line gets 1, 2 or 5 steps that fit", "[sampleview]")
{
    CHECK(tickStep(2.0, 4) == Approx(0.5));
    CHECK(tickStep(2.0, 8) == Approx(0.5));
    CHECK(tickStep(2.0, 10) == Approx(0.2));
    CHECK(tickStep(0.3, 6) == Approx(0.05));
    CHECK(tickStep(45.0, 6) == Approx(10.0));
    CHECK(tickStep(1.0, 10) == Approx(0.1));
    CHECK(tickStep(0.0, 4) == 1.0);
    CHECK(tickStep(std::numeric_limits<double>::infinity(), 4) == 1.0);

    CHECK(tickLabel(0.5, 0.5) == "0.5 s");
    CHECK(tickLabel(2.0, 0.5) == "2.0 s");
    CHECK(tickLabel(0.25, 0.05) == "250 ms");
    CHECK(tickLabel(20.0, 10.0) == "20 s");
    CHECK(tickLabel(10.5, 0.5) == "10.5 s");
}

TEST_CASE("A pan reads as the designs have it", "[sampleview]")
{
    CHECK(panText(0.0) == "C");
    CHECK(panText(0.4) == "C");
    CHECK(panText(-20.0) == "20 L");
    CHECK(panText(100.0) == "100 R");
}
