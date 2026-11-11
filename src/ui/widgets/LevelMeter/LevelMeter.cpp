// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/LevelMeter/LevelMeter.hpp"

#include "core/Decibels.hpp"
#include "ui/Theme.hpp"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace vloomdaw {

namespace {

constexpr double kFloorDb = -60.0;
constexpr double kTopDb = 6.0;
constexpr double kFallDbPerSecond = 24.0;
constexpr double kHoldSeconds = 1.5;
constexpr int kClipLight = 4; ///< pixels at the top for the clip light
constexpr int kGap = 2; ///< between the two bars

/// Linear peak to 0..1 of the meter scale.
double toScale(float peak)
{
    const double db = gainToDb(peak);
    if (std::isinf(db))
        return 0.0;
    return std::clamp((db - kFloorDb) / (kTopDb - kFloorDb), 0.0, 1.0);
}

double scaleOf(double db) { return (db - kFloorDb) / (kTopDb - kFloorDb); }

} // namespace

LevelMeter::LevelMeter(QWidget* parent)
    : QWidget(parent)
{
    setToolTip(tr("Output level (click to reset the clip light)"));
    // Paints every pixel, so its 30 Hz updates do not repaint the strip.
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void LevelMeter::feed(Channel& channel, float peak)
{
    if (!(peak >= 0.0f))
        return; // NaN or negative: ignore
    const double scaled = toScale(peak);
    channel.level = std::max(channel.level, scaled);
    if (scaled >= channel.hold) {
        channel.hold = scaled;
        channel.holdAge = 0.0;
    }
    if (peak > 1.0f)
        channel.clipped = true;
}

void LevelMeter::addPeaks(float left, float right)
{
    feed(channels_[0], left);
    feed(channels_[1], right);
}

void LevelMeter::tick(double seconds)
{
    bool changed = false;
    const double fall = kFallDbPerSecond * seconds / (kTopDb - kFloorDb);
    for (Channel& c : channels_) {
        const double before = c.level;
        c.level = std::max(0.0, c.level - fall);
        c.holdAge += seconds;
        if (c.holdAge > kHoldSeconds && c.hold > c.level) {
            c.hold = c.level;
            changed = true;
        }
        changed = changed || c.level != before;
    }
    if (changed)
        update();
}

void LevelMeter::reset()
{
    for (Channel& c : channels_)
        c = Channel{};
    update();
}

void LevelMeter::mousePressEvent(QMouseEvent* /*event*/) { reset(); }

void LevelMeter::paintEvent(QPaintEvent* /*event*/)
{
    const Theme::Colors& colors = Theme::colors();
    QPainter p(this);

    p.fillRect(rect(), colors.display); // one dark housing for both bars
    const int barWidth = (width() - kGap) / 2;
    const int top = kClipLight + 1;
    const int barHeight = height() - top;
    const int unityY = top + static_cast<int>((1.0 - scaleOf(0.0)) * barHeight);
    const int warnY = top + static_cast<int>((1.0 - scaleOf(-6.0)) * barHeight);

    for (int ch = 0; ch < 2; ++ch) {
        const Channel& c = channels_[ch];
        const int x = ch * (barWidth + kGap);

        p.fillRect(x, top, barWidth, barHeight, colors.display);

        // Lit part, coloured by zone: normal, last 6 dB before 0, over 0.
        const int litTop = top + static_cast<int>((1.0 - c.level) * barHeight);
        const int bottom = top + barHeight;
        const auto band = [&](int from, int to, const QColor& color) {
            const int y0 = std::max(from, litTop);
            if (to > y0)
                p.fillRect(x, y0, barWidth, to - y0, color);
        };
        band(warnY, bottom, colors.displayText);
        band(unityY, warnY, colors.accent);
        band(top, unityY, colors.danger);

        if (c.hold > 0.0) {
            const int holdY = top + static_cast<int>((1.0 - c.hold) * barHeight);
            p.fillRect(x, std::min(holdY, bottom - 1), barWidth, 1, colors.text);
        }

        p.fillRect(x, 0, barWidth, kClipLight, c.clipped ? colors.danger : colors.border);
    }

    // 0 dB mark across both bars.
    p.fillRect(0, unityY, width(), 1, colors.border);
}

} // namespace vloomdaw
