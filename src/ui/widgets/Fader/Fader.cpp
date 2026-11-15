// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/Fader/Fader.hpp"

#include "core/Decibels.hpp"
#include "ui/Theme.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace vloomdaw {

namespace {
constexpr int kCapHeight = 18;
constexpr int kTrackWidth = 4;
constexpr double kFineFactor = 0.1;
constexpr double kWheelStep = 0.02;
} // namespace

Fader::Fader(QWidget* parent)
    : QWidget(parent)
    , value_(kUnityFaderPosition)
{
    setMinimumHeight(50);
    setCursor(Qt::SizeVerCursor);
    setFocusPolicy(Qt::NoFocus);
    updateToolTip();
}

int Fader::travel() const { return std::max(1, height() - kCapHeight); }

int Fader::capCentreY() const
{
    const int minY = kCapHeight / 2;
    const int maxY = std::max(minY, height() - kCapHeight / 2);
    const int y = minY + static_cast<int>((1.0 - value_) * travel());
    return std::clamp(y, minY, maxY);
}

void Fader::setValue(double position)
{
    position = std::clamp(position, 0.0, 1.0);
    if (position == value_)
        return;
    value_ = position;
    updateToolTip();
    update();
}

void Fader::changeByUser(double position)
{
    const double before = value_;
    setValue(position);
    if (value_ != before) {
        emit valueChanged(value_);
        QToolTip::showText(mapToGlobal(QPoint(width(), capCentreY())), toolTip(), this);
    }
}

void Fader::updateToolTip()
{
    setToolTip(tr("%1\nDrag or scroll (Shift: fine), double-click for 0 dB")
            .arg(QString::fromStdString(formatDb(faderToGain(value_)))));
}

void Fader::paintEvent(QPaintEvent* /*event*/)
{
    const Theme::Colors& c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int cx = width() / 2;
    const int trackTop = std::clamp(kCapHeight / 2, 0, height() / 2);
    const int trackBottom = std::max(trackTop, height() - kCapHeight / 2);

    // Track, the lit part below the cap, and the unity mark.
    p.setPen(Qt::NoPen);
    if (trackBottom > trackTop) {
        p.setBrush(c.display);
        p.drawRoundedRect(
            QRectF(cx - kTrackWidth / 2.0, trackTop, kTrackWidth, trackBottom - trackTop), 2, 2);
        const int capY = std::clamp(capCentreY(), trackTop, trackBottom);
        if (trackBottom > capY) {
            p.setBrush(c.accent);
            p.drawRoundedRect(QRectF(cx - kTrackWidth / 2.0, capY, kTrackWidth, trackBottom - capY), 2, 2);
        }
        const int unityY = std::clamp(
            kCapHeight / 2 + static_cast<int>((1.0 - kUnityFaderPosition) * travel()), trackTop, trackBottom);
        p.fillRect(QRect(0, unityY, 4, 1), c.textDim);
        p.fillRect(QRect(width() - 4, unityY, 4, 1), c.textDim);
    }

    // Cap with a centre line.
    const int capH = std::min(height(), kCapHeight);
    const int capTop = std::clamp(capCentreY() - capH / 2, 0, std::max(0, height() - capH));
    const QRectF cap(1, capTop, width() - 2, capH);
    p.setPen(QPen(c.border, 1));
    p.setBrush(c.raised);
    p.drawRoundedRect(cap, 3, 3);
    p.fillRect(QRectF(cap.left() + 3, cap.center().y(), cap.width() - 6, 1.5), c.text);
}

void Fader::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    // A press off the cap jumps it there; then drag relative.
    const double y = event->position().y();
    const int t = travel();
    if (t > 0 && std::abs(y - capCentreY()) > kCapHeight / 2.0)
        changeByUser(std::clamp(1.0 - (y - kCapHeight / 2.0) / t, 0.0, 1.0));
    dragging_ = true;
    dragStartY_ = y;
    dragStartValue_ = value_;
}

void Fader::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_)
        return;
    const int t = travel();
    if (t <= 0)
        return;
    double perPixel = 1.0 / t;
    if (event->modifiers().testFlag(Qt::ShiftModifier))
        perPixel *= kFineFactor;
    changeByUser(dragStartValue_ + (dragStartY_ - event->position().y()) * perPixel);
}

void Fader::mouseReleaseEvent(QMouseEvent* event)
{
    dragging_ = false;
    QWidget::mouseReleaseEvent(event);
}

void Fader::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        changeByUser(kUnityFaderPosition);
}

void Fader::wheelEvent(QWheelEvent* event)
{
    const QPoint delta = event->angleDelta();
    const int steps = (delta.y() != 0 ? delta.y() : delta.x()) / 120;
    if (steps == 0) {
        event->ignore();
        return;
    }
    double step = kWheelStep;
    if (event->modifiers().testFlag(Qt::ShiftModifier))
        step *= kFineFactor;
    changeByUser(value_ + steps * step);
    event->accept();
}

} // namespace vloomdaw
