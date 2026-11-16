// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/Fader/Fader.hpp"

#include "core/Decibels.hpp"
#include "ui/Theme.hpp"

#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace vloomdaw {

namespace {
constexpr int kCapHeight = 20;
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

void Fader::setCapTint(std::optional<QColor> tint)
{
    if (capTint_ == tint)
        return;
    capTint_ = tint;
    update();
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

    // Studio One-style scale ticks along the slot (+6, 0, -6, -12, -24, -48 dB).
    if (trackBottom > trackTop + 20) {
        constexpr double kTickPositions[] = {1.0, 0.8, 0.63, 0.5, 0.32, 0.14};
        QColor tickCol = c.textDim;
        tickCol.setAlpha(110);
        for (double pos : kTickPositions) {
            const int ty = std::clamp(
                trackTop + static_cast<int>(std::lround((1.0 - pos) * travel())), trackTop, trackBottom);
            const bool isUnity = std::abs(pos - kUnityFaderPosition) < 0.01;
            const int tickW = isUnity ? 5 : 3;
            p.fillRect(QRect(1, ty, tickW, 1), isUnity ? c.text : tickCol);
            p.fillRect(QRect(width() - 1 - tickW, ty, tickW, 1), isUnity ? c.text : tickCol);
        }

        // Recessed slot track + active fill below the cap.
        p.setPen(Qt::NoPen);
        p.setBrush(c.display);
        p.drawRoundedRect(
            QRectF(cx - kTrackWidth / 2.0, trackTop, kTrackWidth, trackBottom - trackTop), 2, 2);

        const int capY = std::clamp(capCentreY(), trackTop, trackBottom);
        if (trackBottom > capY) {
            const QColor activeFill = capTint_.value_or(c.accent);
            p.setBrush(activeFill);
            p.drawRoundedRect(QRectF(cx - kTrackWidth / 2.0, capY, kTrackWidth, trackBottom - capY), 2, 2);
        }
    }

    // Studio One-style sculpted metallic / cyan fader cap with centre Hairline.
    const int capH = std::min(height(), kCapHeight);
    const int capTop = std::clamp(capCentreY() - capH / 2, 0, std::max(0, height() - capH));
    const QRectF cap(2.5, capTop + 0.5, std::max(8.0, width() - 5.0), std::max(4.0, capH - 1.0));

    QLinearGradient grad(cap.topLeft(), cap.bottomLeft());
    if (capTint_.has_value()) {
        const QColor base = *capTint_;
        grad.setColorAt(0.0, base.darker(155));
        grad.setColorAt(0.28, base.lighter(135));
        grad.setColorAt(0.50, base.lighter(165));
        grad.setColorAt(0.72, base);
        grad.setColorAt(1.0, base.darker(175));
    } else {
        grad.setColorAt(0.0, QColor(0x52, 0x58, 0x62));
        grad.setColorAt(0.28, QColor(0xd4, 0xd9, 0xe2));
        grad.setColorAt(0.50, QColor(0xf1, 0xf5, 0xf9));
        grad.setColorAt(0.72, QColor(0x9a, 0xa2, 0xae));
        grad.setColorAt(1.0, QColor(0x3a, 0x3f, 0x47));
    }

    p.setPen(QPen(QColor(0x14, 0x17, 0x1b), 1.0));
    p.setBrush(grad);
    p.drawRoundedRect(cap, 2.5, 2.5);

    // Dark grip grooves + crisp white/dark centre hairline.
    p.fillRect(
        QRectF(cap.left() + 2.0, cap.center().y() - 0.8, cap.width() - 4.0, 1.6), QColor(0x11, 0x14, 0x18));
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
