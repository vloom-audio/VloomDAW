// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/TransferCurve.hpp"

#include "ui/plugins/builtin/EditorStyle.hpp"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace vloomdaw {

namespace {

namespace style = editor_style;

constexpr double kRadius = 10.0;
constexpr int kPoints = 96;

} // namespace

TransferCurve::TransferCurve(
    std::function<double(double)> curve, double minimum, double maximum, QWidget* parent)
    : QWidget(parent)
    , curve_(std::move(curve))
    , minimum_(minimum)
    , maximum_(maximum > minimum ? maximum : minimum + 1.0)
    , input_(std::numeric_limits<double>::quiet_NaN())
{
    setFixedSize(sizeHint());
    setAttribute(Qt::WA_TransparentForMouseEvents); // the display under it takes the mouse
}

void TransferCurve::setInput(double input)
{
    if (input == input_ || (std::isnan(input) && std::isnan(input_)))
        return;
    input_ = input;
    update();
}

void TransferCurve::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath clip;
    clip.addRoundedRect(frame, kRadius, kRadius);
    p.setPen(QPen(style::strong(), 1.0));
    p.setBrush(style::panelFill());
    p.drawPath(clip);
    p.setClipPath(clip);

    const QRectF plot = frame.adjusted(1, 1, -1, -1);
    const double span = maximum_ - minimum_;
    const auto x = [&](double v) { return plot.left() + (v - minimum_) / span * plot.width(); };
    const auto y = [&](double v) {
        return plot.bottom()
            - (std::clamp(v, minimum_ - span, maximum_ + span) - minimum_) / span * plot.height();
    };

    QPen diagonal(style::strong(), 1.2);
    diagonal.setDashPattern({5.0 / 1.2 * 0.6, 6.0 / 1.2 * 0.6});
    p.setPen(diagonal);
    p.drawLine(QPointF(plot.left(), plot.bottom()), QPointF(plot.right(), plot.top()));

    if (!curve_)
        return;
    QPainterPath line;
    for (int i = 0; i <= kPoints; ++i) {
        const double in = minimum_ + span * i / kPoints;
        const QPointF point(x(in), y(curve_(in)));
        if (i == 0)
            line.moveTo(point);
        else
            line.lineTo(point);
    }
    QPainterPath fill = line;
    fill.lineTo(plot.right(), plot.bottom() + 2);
    fill.lineTo(plot.left(), plot.bottom() + 2);
    fill.closeSubpath();
    QColor shade = style::accent();
    shade.setAlphaF(0.14f);
    p.setPen(Qt::NoPen);
    p.setBrush(shade);
    p.drawPath(fill);
    p.setPen(QPen(style::accent(), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(line);

    if (std::isfinite(input_) && input_ >= minimum_ && input_ <= maximum_) {
        p.setPen(Qt::NoPen);
        p.setBrush(style::text());
        p.drawEllipse(QPointF(x(input_), y(curve_(input_))), 4.0, 4.0);
    }
}

} // namespace vloomdaw
