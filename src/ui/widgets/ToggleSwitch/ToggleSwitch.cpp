// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/ToggleSwitch/ToggleSwitch.hpp"

#include "ui/Theme.hpp"

#include <QPainter>

namespace vloomdaw {

ToggleSwitch::ToggleSwitch(QWidget* parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void ToggleSwitch::paintEvent(QPaintEvent* /*event*/)
{
    const Theme::Colors& c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF track = QRectF(rect()).adjusted(1, 1, -1, -1);
    const double radius = track.height() / 2.0;
    p.setPen(QPen(isChecked() ? c.accent : c.border, 1));
    p.setBrush(isChecked() ? c.accent : c.base);
    p.drawRoundedRect(track, radius, radius);

    const double knob = track.height() - 6.0;
    const double x = isChecked() ? track.right() - 3.0 - knob : track.left() + 3.0;
    p.setPen(Qt::NoPen);
    p.setBrush(isChecked() ? c.base : (underMouse() ? c.text : c.textDim));
    p.drawEllipse(QRectF(x, track.top() + 3.0, knob, knob));

    if (!isEnabled()) {
        QColor veil = c.window;
        veil.setAlpha(140);
        p.setBrush(veil);
        p.drawRoundedRect(track, radius, radius);
    }
}

} // namespace vloomdaw
