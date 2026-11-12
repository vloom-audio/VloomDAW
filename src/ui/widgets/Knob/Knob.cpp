// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/Knob/Knob.hpp"

#include "ui/Theme.hpp"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace vloomdaw {

namespace {

constexpr int kDial = 26; ///< knob diameter in pixels
constexpr int kCaptionGap = 2;
constexpr double kDragPixels = 150.0; ///< drag distance for the whole range
constexpr double kFineFactor = 0.1; ///< with Shift held
constexpr double kWheelFraction = 0.02; ///< of the range per wheel notch

// The arc runs clockwise from 7:30 to 4:30, leaving a gap at the bottom.
// Angles are QPainter's: degrees counter-clockwise from 3 o'clock.
constexpr double kStartAngle = 225.0;
constexpr double kSweep = 270.0;

QFont captionFont(const QFont& base)
{
    QFont f = base;
    if (base.pointSizeF() > 0)
        f.setPointSizeF(base.pointSizeF() * 0.62);
    else
        f.setPixelSize(std::max(7, static_cast<int>(base.pixelSize() * 0.62)));
    f.setBold(true);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
    return f;
}

} // namespace

Knob::Knob(const QString& caption, double minimum, double maximum, double defaultValue, QWidget* parent)
    : QWidget(parent)
    , caption_(caption)
    , minimum_(minimum)
    , maximum_(maximum)
    , default_(std::clamp(defaultValue, minimum, maximum))
    , value_(default_)
    , format_([](double v) { return QString::number(v, 'f', 1); })
{
    setCursor(Qt::SizeVerCursor);
    setFocusPolicy(Qt::NoFocus);
    updateToolTip();
}

void Knob::setValueText(std::function<QString(double)> format)
{
    format_ = std::move(format);
    updateToolTip();
}

void Knob::setDescription(const QString& text)
{
    description_ = text;
    updateToolTip();
}

QSize Knob::sizeHint() const
{
    const QFontMetrics caption(captionFont(font()));
    const int width = std::max(kDial + 4, caption.horizontalAdvance(caption_) + 4);
    return {width, kDial + kCaptionGap + caption.height()};
}

void Knob::setValue(double value)
{
    value = std::clamp(value, minimum_, maximum_);
    if (value == value_)
        return;
    value_ = value;
    updateToolTip();
    update();
}

double Knob::fraction() const
{
    return maximum_ > minimum_ ? (value_ - minimum_) / (maximum_ - minimum_) : 0.0;
}

void Knob::changeByUser(double value)
{
    const double before = value_;
    setValue(value);
    if (value_ != before) {
        emit valueChanged(value_);
        // Show the value while turning, where the user is looking.
        QToolTip::showText(mapToGlobal(QPoint(width() / 2, height())), toolTip(), this);
    }
}

void Knob::updateToolTip()
{
    const QString value = format_ ? format_(value_) : QString::number(value_);
    const QString hint = tr("Drag or scroll (Shift: fine), double-click to reset");
    setToolTip(description_.isEmpty() ? QStringLiteral("%1\n%2").arg(value, hint)
                                      : QStringLiteral("%1: %2\n%3").arg(description_, value, hint));
}

void Knob::paintEvent(QPaintEvent* /*event*/)
{
    const Theme::Colors& c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF dial((width() - kDial) / 2.0, 0.0, kDial, kDial);
    constexpr double kTrack = 2.5;
    const QRectF arcRect = dial.adjusted(kTrack / 2, kTrack / 2, -kTrack / 2, -kTrack / 2);

    // Track, then the lit part: from the left end, or from the centre when
    // the range spans zero.
    QPen pen(c.border, kTrack, Qt::SolidLine, Qt::RoundCap);
    p.setPen(pen);
    p.drawArc(arcRect, static_cast<int>(kStartAngle * 16), static_cast<int>(-kSweep * 16));

    const bool bipolar = minimum_ < 0.0 && maximum_ > 0.0;
    const double origin = bipolar ? (0.0 - minimum_) / (maximum_ - minimum_) : 0.0;
    const double from = kStartAngle - origin * kSweep;
    const double span = -(fraction() - origin) * kSweep;
    if (std::abs(span) > 0.5) {
        pen.setColor(c.accent);
        p.setPen(pen);
        p.drawArc(arcRect, static_cast<int>(from * 16), static_cast<int>(span * 16));
    }

    // Body and pointer.
    const QRectF body = dial.adjusted(5, 5, -5, -5);
    p.setPen(QPen(c.border, 1));
    p.setBrush(c.raised);
    p.drawEllipse(body);

    const double angle = (kStartAngle - fraction() * kSweep) * std::numbers::pi / 180.0;
    const QPointF centre = body.center();
    const double r = body.width() / 2.0;
    const QPointF dir(std::cos(angle), -std::sin(angle));
    p.setPen(QPen(c.text, 1.8, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(centre + dir * (r * 0.25), centre + dir * (r * 0.85));

    // Caption.
    p.setFont(captionFont(font()));
    p.setPen(c.textDim);
    const QRectF captionRect(0, kDial + kCaptionGap, width(), height() - kDial - kCaptionGap);
    p.drawText(captionRect, Qt::AlignHCenter | Qt::AlignTop, caption_);
}

void Knob::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    dragging_ = true;
    dragStartY_ = event->position().y();
    dragStartValue_ = value_;
}

void Knob::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_)
        return;
    double perPixel = (maximum_ - minimum_) / kDragPixels;
    if (event->modifiers().testFlag(Qt::ShiftModifier))
        perPixel *= kFineFactor;
    changeByUser(dragStartValue_ + (dragStartY_ - event->position().y()) * perPixel);
}

void Knob::mouseReleaseEvent(QMouseEvent* event)
{
    dragging_ = false;
    QWidget::mouseReleaseEvent(event);
}

void Knob::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        changeByUser(default_);
}

void Knob::wheelEvent(QWheelEvent* event)
{
    // Some platforms turn Shift+wheel into horizontal scrolling.
    const QPoint delta = event->angleDelta();
    const int steps = (delta.y() != 0 ? delta.y() : delta.x()) / 120;
    if (steps == 0) {
        event->ignore();
        return;
    }
    double step = (maximum_ - minimum_) * kWheelFraction;
    if (event->modifiers().testFlag(Qt::ShiftModifier))
        step *= kFineFactor;
    changeByUser(value_ + steps * step);
    event->accept();
}

} // namespace vloomdaw
