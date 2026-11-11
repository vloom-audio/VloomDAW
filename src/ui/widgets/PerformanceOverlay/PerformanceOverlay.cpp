// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/PerformanceOverlay/PerformanceOverlay.hpp"

#include "ui/Theme.hpp"

#include <QFontDatabase>
#include <QFontMetrics>
#include <QPainter>

namespace vloomdaw {

namespace {

constexpr int kPadding = 8;
constexpr int kLines = 5;
constexpr double kFrameBudgetMs = 16.7; ///< one 60 Hz frame

} // namespace

PerformanceOverlay::PerformanceOverlay(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("performanceOverlay"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    font_ = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font_.setPointSizeF(font().pointSizeF() > 0 ? font().pointSizeF() * 0.9 : 8.0);
    resize(sizeHint());
}

QSize PerformanceOverlay::sizeHint() const
{
    const QFontMetrics fm(font_);
    return {fm.horizontalAdvance(QStringLiteral("paint 99.9 avg  999.9 max ms")) + 2 * kPadding,
        kLines * fm.lineSpacing() + 2 * kPadding};
}

void PerformanceOverlay::setFigures(const FrameStats::Snapshot& stats, float audioLoad, double expectedTickMs)
{
    stats_ = stats;
    audioLoad_ = audioLoad;
    expectedTickMs_ = expectedTickMs;
    update();
}

void PerformanceOverlay::paintEvent(QPaintEvent* /*event*/)
{
    const Theme::Colors& c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor back = c.base;
    back.setAlpha(225);
    p.setPen(QPen(c.border, 1));
    p.setBrush(back);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);

    p.setFont(font_);
    const QFontMetrics fm(font_);
    int y = kPadding + fm.ascent();
    const auto line = [&](const QString& text, bool bad) {
        p.setPen(bad ? c.danger : c.displayText);
        p.drawText(kPadding, y, text);
        y += fm.lineSpacing();
    };
    const auto num = [](double v, int decimals) { return QString::number(v, 'f', decimals); };

    // FPS counts repaints: low while idle is fine, it only matters while
    // something moves. Paint over one frame or late ticks are the warnings.
    line(QStringLiteral("fps   %1").arg(num(stats_.fps, 0)), false);
    line(QStringLiteral("paint %1 avg  %2 max ms").arg(num(stats_.paintAvgMs, 1), num(stats_.paintMaxMs, 1)),
        stats_.paintMaxMs > kFrameBudgetMs);
    line(QStringLiteral("tick  %1 avg  %2 max ms").arg(num(stats_.tickAvgMs, 1), num(stats_.tickMaxMs, 1)),
        stats_.tickMaxMs > expectedTickMs_ * 2.0);
    // What the UI could do if the screen allowed: per-frame work and the
    // frame rate it would cap at. Below the screen rate means it cannot keep up.
    line(QStringLiteral("work  %1 ms  max ~%2 fps").arg(num(stats_.workAvgMs, 2), num(stats_.headroomFps, 0)),
        stats_.headroomFps > 0.0 && stats_.headroomFps < 1000.0 / expectedTickMs_);
    line(QStringLiteral("audio %1 %").arg(num(audioLoad_ * 100.0, 1)), audioLoad_ > 0.7f);
}

} // namespace vloomdaw
