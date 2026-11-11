// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/Timeline/Timeline.hpp"

#include "core/Project.hpp"
#include "ui/Theme.hpp"

#include <QFontMetrics>
#include <QPainter>

#include <cmath>
#include <limits>

namespace vloomdaw::timeline {

namespace {

constexpr double kMinLineSpacing = 6.0; ///< pixels; denser lines are skipped

/// First multiple of `step` at or before the left edge of `rect`.
long long firstMultiple(const QRect& rect, const Scale& scale, int step)
{
    return static_cast<long long>(std::floor(scale.tick(rect.left()) / step)) * step;
}

} // namespace

void paintRuler(QPainter& painter, const QRect& rect, const Scale& scale)
{
    const Theme::Colors& c = Theme::colors();
    painter.save();
    painter.setClipRect(rect);
    painter.fillRect(rect, c.panel);
    painter.fillRect(QRect(rect.left(), rect.bottom(), rect.width(), 1), c.border);

    QFont font = painter.font();
    font.setPointSizeF(font.pointSizeF() > 0 ? font.pointSizeF() * 0.8 : 8.0);
    painter.setFont(font);

    // Label every bar, or every few when bars are narrow.
    const double barPixels = kTicksPerBar * scale.pixelsPerTick;
    const int labelEvery = std::max(1, static_cast<int>(std::ceil(28.0 / barPixels)));
    const bool beatMarks = kTicksPerBeat * scale.pixelsPerTick >= kMinLineSpacing;
    for (long long t = firstMultiple(rect, scale, kTicksPerBeat); scale.x(static_cast<double>(t)) <= rect.right();
         t += kTicksPerBeat) {
        if (t < 0)
            continue;
        const int x = static_cast<int>(std::lround(scale.x(static_cast<double>(t))));
        const long long bar = t / kTicksPerBar;
        if (t % kTicksPerBar == 0) {
            painter.fillRect(QRect(x, rect.top() + rect.height() / 2, 1, rect.height() / 2), c.textDim);
            if (bar % labelEvery == 0) {
                painter.setPen(c.text);
                painter.drawText(QRect(x + 4, rect.top(), 60, rect.height() - 2), Qt::AlignLeft | Qt::AlignVCenter,
                    QString::number(bar + 1));
            }
        } else if (beatMarks) {
            painter.fillRect(QRect(x, rect.bottom() - 5, 1, 5), c.border);
        }
    }
    painter.restore();
}

void paintGrid(QPainter& painter, const QRect& rect, const Scale& scale, int minorTicks, GridStyle style)
{
    const Theme::Colors& c = Theme::colors();
    painter.save();
    painter.setClipRect(rect);
    const bool calm = style == GridStyle::Calm;
    QColor minor = c.border;
    minor.setAlpha(calm ? 40 : 110);
    QColor beat = c.border;
    beat.setAlpha(calm ? 70 : 255);
    QColor bar = calm ? c.border : c.textDim;
    bar.setAlpha(calm ? 255 : 150);
    QColor phrase = c.textDim; // every 4th bar (Calm only)
    phrase.setAlpha(80);
    constexpr long long kTicksPerPhrase = 4LL * kTicksPerBar;

    int step = minorTicks;
    while (step * scale.pixelsPerTick < kMinLineSpacing && step < kTicksPerBar)
        step *= 2;
    for (long long t = firstMultiple(rect, scale, step); scale.x(static_cast<double>(t)) <= rect.right(); t += step) {
        if (t < 0)
            continue;
        const QColor& color = calm && t % kTicksPerPhrase == 0 ? phrase
            : t % kTicksPerBar == 0                           ? bar
            : t % kTicksPerBeat == 0                          ? beat
                                                              : minor;
        const int x = static_cast<int>(std::lround(scale.x(static_cast<double>(t))));
        painter.fillRect(QRect(x, rect.top(), 1, rect.height()), color);
    }
    painter.restore();
}

double playheadX(const QRect& rect, const Scale& scale, double tick)
{
    if (tick < 0.0)
        return std::numeric_limits<double>::quiet_NaN();
    const double x = scale.x(tick);
    if (x < scale.left || x < rect.left() || x > rect.right() + 1)
        return std::numeric_limits<double>::quiet_NaN();
    return x;
}

void paintPlayhead(QPainter& painter, const QRect& rect, const Scale& scale, double tick)
{
    const double x = playheadX(rect, scale, tick);
    if (std::isnan(x))
        return;
    // Anti-aliased at its exact position, so it glides instead of stepping
    // a whole pixel at a time.
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(QRectF(x - 1.0, rect.top(), 2.0, rect.height()), Theme::colors().playing);
    painter.restore();
}

QRect playheadRect(const QRect& rect, const Scale& scale, double tick)
{
    const double x = playheadX(rect, scale, tick);
    if (std::isnan(x))
        return {};
    return {static_cast<int>(std::floor(x)) - 2, rect.top(), 5, rect.height()};
}

bool playheadMoved(const QRect& rect, const Scale& scale, double from, double to)
{
    const double a = playheadX(rect, scale, from);
    const double b = playheadX(rect, scale, to);
    if (std::isnan(a) || std::isnan(b))
        return std::isnan(a) != std::isnan(b);
    return std::lround(a * 4.0) != std::lround(b * 4.0); // quarter-pixel steps
}

} // namespace vloomdaw::timeline
