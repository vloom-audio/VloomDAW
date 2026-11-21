// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/TrailFade.hpp"

#include <algorithm>
#include <cmath>

namespace vloomdaw::motion {

std::uint32_t fadePremultiplied(std::span<std::uint32_t> pixels, double keep) noexcept
{
    // A factor out of 256, rounded down; below 256 so a fade always takes a step.
    const auto factor = static_cast<std::uint32_t>(std::clamp(std::floor(keep * 256.0), 0.0, 255.0));
    std::uint32_t highest = 0;
    for (std::uint32_t& pixel : pixels) {
        if (pixel == 0)
            continue;
        const std::uint32_t a = ((pixel >> 24U) * factor) >> 8U;
        // Each channel scaled alike and rounded down: still <= alpha, as
        // premultiplied colour must be (clamped for input that was not).
        const auto channel
            = [&](unsigned shift) { return std::min(a, (((pixel >> shift) & 0xFFU) * factor) >> 8U); };
        pixel = a == 0 ? 0U : (a << 24U) | (channel(16U) << 16U) | (channel(8U) << 8U) | channel(0U);
        highest = std::max(highest, a);
    }
    return highest;
}

std::uint32_t highestAlpha(std::span<const std::uint32_t> pixels) noexcept
{
    std::uint32_t highest = 0;
    for (const std::uint32_t pixel : pixels)
        highest = std::max(highest, pixel >> 24U);
    return highest;
}

} // namespace vloomdaw::motion
