// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QColor>

#include <cstdint>
#include <functional>
#include <optional>

class QMenu;

namespace vloomdaw {

/// 0xRRGGBB as a colour, and back.
[[nodiscard]] QColor toQColor(std::uint32_t rgb);
[[nodiscard]] std::uint32_t toRgb(const QColor& color);

/// Adds a "Color" submenu to `menu`: a row of preset swatches, "Custom…"
/// (a colour dialog) and "Default" (no colour). `chosen` gets the pick;
/// `current` is ticked.
void addColorMenu(QMenu& menu, std::optional<std::uint32_t> current,
    std::function<void(std::optional<std::uint32_t>)> chosen);

} // namespace vloomdaw
