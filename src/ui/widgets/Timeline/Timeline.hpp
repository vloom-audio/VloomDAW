// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QRect>

class QPainter;

namespace vloomdaw::timeline {

/// Height of the bar-number ruler above a timeline.
inline constexpr int kRulerHeight = 22;

/// Maps ticks to pixels for a horizontally scrolled timeline that starts at
/// `left` (widget x of tick `firstTick`).
struct Scale {
    double firstTick = 0.0;
    double pixelsPerTick = 0.0;
    int left = 0;

    [[nodiscard]] double x(double tick) const { return left + (tick - firstTick) * pixelsPerTick; }
    [[nodiscard]] double tick(double x) const { return firstTick + (x - left) / pixelsPerTick; }
};

/// Bar numbers (1-based) and beat marks across `rect`.
void paintRuler(QPainter& painter, const QRect& rect, const Scale& scale);
/// How loud the grid is.
enum class GridStyle {
    Dense, ///< editing notes: bars stand out, beats and steps clearly visible
    Calm, ///< arranging: beats barely visible, bars quiet, every 4th bar a step stronger
};

/// Vertical grid lines over `rect`: every `minorTicks` faint, beats and bars
/// stronger. Lines closer than a few pixels are skipped.
void paintGrid(QPainter& painter, const QRect& rect, const Scale& scale, int minorTicks,
    GridStyle style = GridStyle::Dense);
/// Playhead line at `tick` over `rect` (nothing when it is off screen or < 0).
void paintPlayhead(QPainter& painter, const QRect& rect, const Scale& scale, double tick);
/// x of the playhead line (fractional pixels), or NaN when it is not shown.
[[nodiscard]] double playheadX(const QRect& rect, const Scale& scale, double tick);
/// Area paintPlayhead() covers at `tick` within `rect` (with pixels to
/// spare for anti-aliasing), or an empty rect when the line is not shown.
/// Use it to repaint only what a moving playhead changes.
[[nodiscard]] QRect playheadRect(const QRect& rect, const Scale& scale, double tick);
/// True when the line at `to` looks different from `from` (moved at least a
/// quarter pixel, or appeared / disappeared): only then is a repaint needed.
[[nodiscard]] bool playheadMoved(const QRect& rect, const Scale& scale, double from, double to);

} // namespace vloomdaw::timeline
