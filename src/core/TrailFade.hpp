// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <cstdint>
#include <span>

namespace vloomdaw::motion {

/// Fades premultiplied 8-bit ARGB pixels (0xAARRGGBB, as QImage's
/// Format_ARGB32_Premultiplied keeps them) to `keep` (0 .. 1) of their
/// strength, rounding down: every pixel that is not already transparent
/// loses at least one step (even at `keep` 1), so a fade repeated often
/// enough ends at exactly zero, never at a faint residue (rounding to the
/// nearest step, as QPainter's opacity does, leaves values of a few steps
/// stuck for ever). Colour never exceeds alpha afterwards. Returns the
/// highest alpha left (0: nothing visible).
std::uint32_t fadePremultiplied(std::span<std::uint32_t> pixels, double keep) noexcept;

/// The highest alpha in `pixels` (0: all transparent).
[[nodiscard]] std::uint32_t highestAlpha(std::span<const std::uint32_t> pixels) noexcept;

} // namespace vloomdaw::motion
