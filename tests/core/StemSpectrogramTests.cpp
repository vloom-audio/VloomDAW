// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/StemSpectrogram.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using Catch::Approx;
using vloomdaw::StemSpectrogram;

namespace {

constexpr int kLength = StemSpectrogram::kSegmentFrames;

/// A sine that falls exactly on bin `bin` of the transform.
std::vector<float> sine(int bin, float amplitude)
{
    std::vector<float> wave(kLength);
    for (int i = 0; i < kLength; ++i)
        wave[static_cast<std::size_t>(i)] = amplitude
            * static_cast<float>(std::sin(2.0 * std::numbers::pi * bin * i / StemSpectrogram::kFftSize));
    return wave;
}

float magnitude(const std::vector<float>& planes, int bin, int frame)
{
    const std::size_t at
        = static_cast<std::size_t>(bin) * StemSpectrogram::kFrames + static_cast<std::size_t>(frame);
    return std::hypot(planes[at], planes[StemSpectrogram::kPlane + at]);
}

} // namespace

TEST_CASE("A sine shows in its own bin at the level Demucs expects", "[stems][spectrogram]")
{
    const StemSpectrogram spectrogram;
    const std::vector<float> wave = sine(100, 0.5f);
    std::vector<float> planes(2 * StemSpectrogram::kPlane);
    spectrogram.forward(wave.data(), planes.data());
    // A Hann-windowed sine of amplitude a: a * 4096 / 4 in its bin, here over sqrt(4096).
    for (const int frame : {10, 168, 320}) {
        CHECK(magnitude(planes, 100, frame) == Approx(0.5f * 16.0f).epsilon(0.001));
        CHECK(magnitude(planes, 99, frame) == Approx(0.5f * 8.0f).epsilon(0.001)); // the window's skirt
        CHECK(magnitude(planes, 104, frame) < 0.001f);
        CHECK(magnitude(planes, 1000, frame) < 0.001f);
    }
}

TEST_CASE("Silence gives an empty spectrogram and comes back as silence", "[stems][spectrogram]")
{
    const StemSpectrogram spectrogram;
    const std::vector<float> wave(kLength, 0.0f);
    std::vector<float> planes(2 * StemSpectrogram::kPlane, 1.0f);
    spectrogram.forward(wave.data(), planes.data());
    for (const float value : planes)
        REQUIRE(value == 0.0f);
    std::vector<float> back(kLength, 1.0f);
    spectrogram.inverse(planes.data(), back.data());
    for (const float value : back)
        REQUIRE(value == 0.0f);
}

TEST_CASE("The inverse gives the segment back, but for its very ends", "[stems][spectrogram]")
{
    const StemSpectrogram spectrogram;
    // Something with several tones and a slow swell, away from the top bin.
    std::vector<float> wave(kLength);
    for (int i = 0; i < kLength; ++i) {
        const double t = static_cast<double>(i) / 44100.0;
        wave[static_cast<std::size_t>(i)]
            = static_cast<float>((0.3 * std::sin(2.0 * std::numbers::pi * 110.0 * t)
                                     + 0.2 * std::sin(2.0 * std::numbers::pi * 1234.5 * t)
                                     + 0.1 * std::sin(2.0 * std::numbers::pi * 9000.0 * t))
                * (0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * 0.7 * t)));
    }
    std::vector<float> planes(2 * StemSpectrogram::kPlane);
    spectrogram.forward(wave.data(), planes.data());
    std::vector<float> back(kLength);
    spectrogram.inverse(planes.data(), back.data());

    // Inside, every sample has its four frames: exact. The outer 1536 at each
    // end miss the frames Demucs drops.
    constexpr int kEdge = 1536;
    float worst = 0.0f;
    for (int i = kEdge; i < kLength - kEdge - StemSpectrogram::kHop; ++i)
        worst = std::max(
            worst, std::abs(back[static_cast<std::size_t>(i)] - wave[static_cast<std::size_t>(i)]));
    CHECK(worst < 2e-4f);
    CHECK(std::abs(back[0] - wave[0]) < 1.0f); // incomplete, but bounded
}

TEST_CASE("The transform is linear", "[stems][spectrogram]")
{
    const StemSpectrogram spectrogram;
    const std::vector<float> a = sine(50, 0.25f);
    const std::vector<float> b = sine(700, 0.5f);
    std::vector<float> both(kLength);
    for (int i = 0; i < kLength; ++i)
        both[static_cast<std::size_t>(i)] = a[static_cast<std::size_t>(i)] + b[static_cast<std::size_t>(i)];
    std::vector<float> pa(2 * StemSpectrogram::kPlane);
    std::vector<float> pb(2 * StemSpectrogram::kPlane);
    std::vector<float> pboth(2 * StemSpectrogram::kPlane);
    spectrogram.forward(a.data(), pa.data());
    spectrogram.forward(b.data(), pb.data());
    spectrogram.forward(both.data(), pboth.data());
    float worst = 0.0f;
    for (std::size_t i = 0; i < pboth.size(); i += 97)
        worst = std::max(worst, std::abs(pboth[i] - pa[i] - pb[i]));
    CHECK(worst < 1e-4f);
}
