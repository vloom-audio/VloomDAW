// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QColor>
#include <QPoint>

#include <cstdint>
#include <functional>
#include <optional>

class QMenu;
class QWidget;

namespace vloomdaw {

/// 0xRRGGBB as a colour, and back.
[[nodiscard]] QColor toQColor(std::uint32_t rgb);
[[nodiscard]] std::uint32_t toRgb(const QColor& color);

/// Adds a "Color" submenu to `menu` with a 16-column DAW colour swatch grid,
/// a reset-to-default cell and "Custom…". `chosen` gets the pick.
void addColorMenu(QMenu& menu, std::optional<std::uint32_t> current,
    std::function<void(std::optional<std::uint32_t>)> chosen);

/// Opens the 16-column DAW colour swatch grid popup directly at `globalPos`
/// (e.g. when clicking a track's left colour bar or a mixer strip's colour bar).
void showColorPicker(QWidget* parent, const QPoint& globalPos, std::optional<std::uint32_t> current,
    std::function<void(std::optional<std::uint32_t>)> chosen);

} // namespace vloomdaw
