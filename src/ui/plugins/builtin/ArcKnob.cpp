// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/ArcKnob.hpp"

#include "ui/plugins/builtin/EditorStyle.hpp"

#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <numbers>

namespace vloomdaw {

namespace {

namespace style = editor_style;

// The dial is drawn in the designs' 44-unit box, scaled to kKnobDial.
constexpr double kBox = 44.0;
constexpr double kCentre = 22.0;
constexpr double kTrackRadius = 18.0;
constexpr double kTrackWidth = 3.0;
constexpr double kHubRadius = 12.5;
constexpr double kPointerFrom = 5.0;
constexpr double kPointerTo = 11.0;
constexpr double kStartDegrees = 135.0; ///< clockwise from 3 o'clock (y down), as in the designs
constexpr double kSweepDegrees = 270.0;
constexpr int kLabelHeight = 14;
constexpr int kValueHeight = 17;

/// The point at `degrees` (clockwise from 3 o'clock) and `radius` from the centre.
QPointF at(double degrees, double radius)
{
    const double radians = degrees * std::numbers::pi / 180.0;
    return {kCentre + radius * std::cos(radians), kCentre + radius * std::sin(radians)};
}

/// The arc of the track from `from` to `to` (0 .. 1 of the travel).
QPainterPath arc(double from, double to)
{
    QPainterPath path;
    const QRectF circle(kCentre - kTrackRadius, kCentre - kTrackRadius, 2 * kTrackRadius, 2 * kTrackRadius);
    // QPainterPath angles turn counter-clockwise with y up: negate.
    const double start = -(kStartDegrees + kSweepDegrees * from);
    path.arcMoveTo(circle, start);
    path.arcTo(circle, start, -kSweepDegrees * (to - from));
    return path;
}

} // namespace

ArcKnob::ArcKnob(const QString& label, QWidget* parent)
    : ParameterControl(label, parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void ArcKnob::setArcColor(const QColor& color)
{
    arc_ = color;
    update();
}

QSize ArcKnob::sizeHint() const
{
    return {style::kKnobCell, style::kKnobDial + 1 + kLabelHeight + kValueHeight};
}

void ArcKnob::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double left = (width() - style::kKnobDial) / 2.0;
    p.save();
    p.translate(left, 0.0);
    p.scale(style::kKnobDial / kBox, style::kKnobDial / kBox);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(style::strong(), kTrackWidth, Qt::SolidLine, Qt::RoundCap));
    p.drawPath(arc(0.0, 1.0));

    const double now = position();
    const double origin = spec() ? spec()->zeroPosition().value_or(0.0) : 0.0;
    const QColor lit = arc_.isValid() ? arc_ : style::accent();
    if (std::abs(now - origin) > 1e-4 || origin == 0.0) {
        p.setPen(QPen(isEnabled() ? lit : style::muted(), kTrackWidth, Qt::SolidLine, Qt::RoundCap));
        p.drawPath(now >= origin ? arc(origin, now + 1e-4) : arc(now, origin));
    }

    p.setPen(Qt::NoPen);
    p.setBrush(style::background());
    p.drawEllipse(QPointF(kCentre, kCentre), kHubRadius, kHubRadius);
    const double angle = kStartDegrees + kSweepDegrees * now;
    p.setPen(QPen(style::text(), 2.0, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(at(angle, kPointerFrom), at(angle, kPointerTo));
    if (hasFocus()) {
        p.setPen(QPen(lit, 1.0, Qt::DotLine));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(kCentre, kCentre), kTrackRadius + 3.5, kTrackRadius + 3.5);
    }
    p.restore();

    const int top = style::kKnobDial + 1;
    p.setFont(style::font(font(), 10));
    p.setPen(style::muted());
    p.drawText(QRect(0, top, width(), kLabelHeight), Qt::AlignHCenter | Qt::AlignVCenter, label());
    p.setFont(style::font(font(), 12, QFont::DemiBold, true));
    p.setPen(style::text());
    p.drawText(
        QRect(0, top + kLabelHeight, width(), kValueHeight), Qt::AlignHCenter | Qt::AlignVCenter, text());
}

} // namespace vloomdaw
