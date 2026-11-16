// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/MdxSpectrogram.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>
#include <vector>

using Catch::Approx;
using vloomdaw::AnySizeFft;
using vloomdaw::MdxSpectrogram;

namespace {

/// Values that repeat for a seed, without <random>'s differences between libraries.
std::vector<float> noise(std::size_t count, unsigned seed)
{
    std::vector<float> out(count);
    for (float& value : out) {
        seed = seed * 1664525U + 1013904223U;
        value = static_cast<float>(seed >> 8) / static_cast<float>(1U << 24) * 2.0f - 1.0f;
    }
    return out;
}

} // namespace

TEST_CASE("A transform of any size matches the definition", "[stems][spectrogram]")
{
    // Sizes with the factors of the MDX-Net windows (3, 5, 15 times a power of two), and a prime.
    for (const int size : {1, 2, 12, 15, 30, 64, 97, 120}) {
        INFO(size);
        const AnySizeFft fft(size);
        const std::vector<float> values
            = noise(static_cast<std::size_t>(2 * size), 7U + static_cast<unsigned>(size));
        std::vector<std::complex<float>> data(static_cast<std::size_t>(size));
        for (int n = 0; n < size; ++n)
            data[static_cast<std::size_t>(n)]
                = {values[static_cast<std::size_t>(2 * n)], values[static_cast<std::size_t>(2 * n + 1)]};
        const std::vector<std::complex<float>> original = data;
        std::vector<std::complex<float>> scratch;
        fft.transform(data.data(), false, scratch);
        for (int k = 0; k < size; ++k) {
            std::complex<double> expected{0.0, 0.0};
            for (int n = 0; n < size; ++n) {
                const double angle = -2.0 * std::numbers::pi * k * n / size;
                expected += std::complex<double>(original[static_cast<std::size_t>(n)])
                    * std::complex<double>(std::cos(angle), std::sin(angle));
            }
            CHECK(data[static_cast<std::size_t>(k)].real() == Approx(expected.real()).margin(1e-3));
            CHECK(data[static_cast<std::size_t>(k)].imag() == Approx(expected.imag()).margin(1e-3));
        }
        // And back: the inverse is unscaled.
        fft.transform(data.data(), true, scratch);
        for (int n = 0; n < size; ++n) {
            CHECK(data[static_cast<std::size_t>(n)].real() / static_cast<float>(size)
                == Approx(original[static_cast<std::size_t>(n)].real()).margin(1e-4));
            CHECK(data[static_cast<std::size_t>(n)].imag() / static_cast<float>(size)
                == Approx(original[static_cast<std::size_t>(n)].imag()).margin(1e-4));
        }
    }
    CHECK_THROWS_AS(AnySizeFft(0), std::invalid_argument);
}

TEST_CASE("An MDX spectrogram shows a sine in its own bin, unscaled", "[stems][spectrogram]")
{
    // A window of 120 at a hop of 16: not a power of two, and not a whole number of hops.
    const MdxSpectrogram spectrogram(120, 16, 61, 33);
    REQUIRE(spectrogram.chunk() == 512);
    std::vector<float> wave(512);
    constexpr int kBin = 9;
    for (int i = 0; i < 512; ++i)
        wave[static_cast<std::size_t>(i)]
            = 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * kBin * i / 120.0));
    std::vector<float> planes(2 * spectrogram.plane());
    spectrogram.forward(wave.data(), planes.data());
    const auto magnitude = [&](int bin, int frame) {
        const std::size_t at = static_cast<std::size_t>(bin) * 33 + static_cast<std::size_t>(frame);
        return std::hypot(planes[at], planes[spectrogram.plane() + at]);
    };
    // A Hann window's sum is half its length: 0.5 (amplitude) * 0.5 (one side) * 60.
    CHECK(magnitude(kBin, 16) == Approx(15.0f).epsilon(0.01));
    CHECK(magnitude(kBin + 3, 16) < 0.01f);
    CHECK(magnitude(kBin - 3, 16) < 0.01f);
}

TEST_CASE("An MDX spectrogram with every bin gives the chunk back", "[stems][spectrogram]")
{
    const MdxSpectrogram spectrogram(120, 16, 61, 33);
    const std::vector<float> wave = noise(512, 3U);
    std::vector<float> planes(2 * spectrogram.plane());
    // In two halves, as a caller with two threads would.
    spectrogram.forward(wave.data(), planes.data(), 0, 16);
    spectrogram.forward(wave.data(), planes.data(), 16, 33);
    std::vector<float> back(512, 9.0f);
    spectrogram.inverse(planes.data(), back.data());
    for (std::size_t i = 0; i < wave.size(); ++i)
        CHECK(back[i] == Approx(wave[i]).margin(2e-4));
}

TEST_CASE("An MDX spectrogram without its top bins loses only what was there", "[stems][spectrogram]")
{
    // The models keep the lower bins only: a low sine passes, a high one is gone.
    const MdxSpectrogram spectrogram(120, 16, 30, 33);
    std::vector<float> low(512);
    std::vector<float> high(512);
    for (int i = 0; i < 512; ++i) {
        low[static_cast<std::size_t>(i)]
            = static_cast<float>(std::sin(2.0 * std::numbers::pi * 5 * i / 120.0));
        high[static_cast<std::size_t>(i)]
            = static_cast<float>(std::sin(2.0 * std::numbers::pi * 45 * i / 120.0));
    }
    std::vector<float> planes(2 * spectrogram.plane());
    std::vector<float> back(512);
    spectrogram.forward(low.data(), planes.data());
    spectrogram.inverse(planes.data(), back.data());
    for (std::size_t i = 60; i < 452; ++i) // away from the reflected ends
        CHECK(back[i] == Approx(low[i]).margin(2e-3));
    spectrogram.forward(high.data(), planes.data());
    spectrogram.inverse(planes.data(), back.data());
    for (std::size_t i = 60; i < 452; ++i)
        CHECK(std::abs(back[i]) < 2e-3f);
}

TEST_CASE("An MDX spectrogram refuses shapes it cannot do", "[stems][spectrogram]")
{
    CHECK_THROWS_AS(MdxSpectrogram(121, 16, 61, 33), std::invalid_argument); // odd window
    CHECK_THROWS_AS(MdxSpectrogram(120, 16, 62, 33), std::invalid_argument); // more bins than there are
    CHECK_THROWS_AS(
        MdxSpectrogram(120, 16, 61, 4), std::invalid_argument); // chunk shorter than half a window
    CHECK_THROWS_AS(MdxSpectrogram(120, 0, 61, 33), std::invalid_argument);
}
