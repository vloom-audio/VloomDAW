// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/TrailFade.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

using namespace vloomdaw::motion;

namespace {

constexpr std::uint32_t argb(std::uint32_t a, std::uint32_t r, std::uint32_t g, std::uint32_t b)
{
    return (a << 24U) | (r << 16U) | (g << 8U) | b;
}

} // namespace

TEST_CASE("A slow fade ends at exactly zero, with no residue", "[motion]")
{
    // Faint pixels a fade by rounding to the nearest step would keep for ever.
    std::array<std::uint32_t, 5> pixels{
        argb(1, 1, 0, 1), argb(3, 2, 3, 1), argb(12, 12, 6, 0), argb(255, 255, 128, 64), 0};
    std::uint32_t left = 255;
    int fades = 0;
    while (left > 0 && fades < 1000) {
        left = fadePremultiplied(pixels, 0.96);
        ++fades;
    }
    CHECK(left == 0);
    CHECK(fades < 200); // 0.96 a time: about 130 fades from full
    for (const std::uint32_t pixel : pixels)
        CHECK(pixel == 0U);
    CHECK(highestAlpha(pixels) == 0U);
}

TEST_CASE("A fade keeps colour within alpha and reports what is left", "[motion]")
{
    std::array<std::uint32_t, 3> pixels{argb(200, 200, 100, 50), argb(40, 40, 40, 40), argb(10, 30, 0, 0)};
    const std::uint32_t left = fadePremultiplied(pixels, 0.5);
    CHECK(left == 100U);
    CHECK(pixels[0] == argb(100, 100, 50, 25));
    CHECK(pixels[1] == argb(20, 20, 20, 20));
    CHECK(pixels[2] == argb(5, 5, 0, 0)); // red was over alpha: clamped
    CHECK(highestAlpha(pixels) == 100U);
}

TEST_CASE("Keeping everything still takes a step; keeping nothing clears", "[motion]")
{
    std::array<std::uint32_t, 1> pixel{argb(2, 2, 2, 2)};
    (void)fadePremultiplied(pixel, 1.0);
    CHECK(pixel[0] == argb(1, 1, 1, 1));
    CHECK(fadePremultiplied(pixel, 0.0) == 0U);
    CHECK(pixel[0] == 0U);
}
