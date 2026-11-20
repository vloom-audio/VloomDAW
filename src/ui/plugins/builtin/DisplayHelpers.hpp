// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QPainterPath>
#include <QRectF>
#include <QStringList>

#include <cstddef>
#include <span>
#include <vector>

class QPainter;

namespace vloom {
struct ParameterInfo;
}

namespace vloomdaw {

class BuiltinEditorWindow;

/// Small pieces the displays of the built-in effects' editors share.
namespace display {

/// Every parameter value of `editor` by id, 0 .. `count` - 1: the settings
/// for EffectPreview.
[[nodiscard]] std::vector<double> settingsOf(const BuiltinEditorWindow& editor, std::size_t count);

/// The names of a choice parameter's values; empty for none.
[[nodiscard]] QStringList choiceLabels(const vloom::ParameterInfo* parameter);

/// A log frequency axis, 20 Hz .. 20 kHz across `width` px.
inline constexpr double kLowHz = 20.0;
inline constexpr double kHighHz = 20000.0;
[[nodiscard]] double xOfHz(double hz, double left, double width);
[[nodiscard]] double hzAtX(double x, double left, double width);

/// Upright lines at 50, 100, 200, 500 Hz, 1k, 2k, 5k and 10k across `area`,
/// each labelled at `labelY`, in the designs' grid look.
void drawFrequencyGrid(QPainter& p, const QRectF& area, double labelY);

/// A line through `samples` across `area` (one sample per step of its
/// width), 0 at `centreY`, `unitPixels` up for 1. `held`: each sample
/// holds level until the next (a staircase).
[[nodiscard]] QPainterPath wavePath(
    std::span<const float> samples, const QRectF& area, double centreY, double unitPixels, bool held);

} // namespace display

} // namespace vloomdaw
