// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/Readout.hpp"

#include "ui/plugins/builtin/EditorStyle.hpp"

#include <QEvent>
#include <QFontMetrics>
#include <QPainter>

#include <algorithm>

namespace vloomdaw {

namespace {

namespace style = editor_style;

constexpr int kTextPixels = 11;
constexpr int kGap = 5; ///< between the label and the value
constexpr int kHeight = 20;

} // namespace

Readout::Readout(const QString& label, QWidget* parent)
    : ParameterControl(label, parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void Readout::setWidestText(const QString& text)
{
    widest_ = text;
    updateGeometry();
}

int Readout::valueWidth() const
{
    const QFontMetrics metrics(style::font(font(), kTextPixels, QFont::DemiBold, true));
    return std::max(metrics.horizontalAdvance(widest_), metrics.horizontalAdvance(text()));
}

QSize Readout::sizeHint() const
{
    const QFontMetrics metrics(style::font(font(), kTextPixels));
    const int labelWidth = label().isEmpty() ? 0 : metrics.horizontalAdvance(label()) + kGap;
    return {labelWidth + valueWidth() + 2, kHeight};
}

void Readout::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::FontChange)
        updateGeometry();
    ParameterControl::changeEvent(event);
}

void Readout::paintEvent(QPaintEvent* /*event*/)
{
    // A longer text than it was sized for: ask for the room.
    if (sizeHint().width() > width())
        updateGeometry();
    QPainter p(this);
    const QFont labelFont = style::font(font(), kTextPixels);
    int x = 0;
    if (!label().isEmpty()) {
        p.setFont(labelFont);
        p.setPen(style::muted());
        p.drawText(QRect(0, 0, width(), height()), Qt::AlignLeft | Qt::AlignVCenter, label());
        x = QFontMetrics(labelFont).horizontalAdvance(label()) + kGap;
    }
    p.setFont(style::font(font(), kTextPixels, QFont::DemiBold, true));
    p.setPen(isEnabled() ? style::text() : style::muted());
    p.drawText(QRect(x, 0, width() - x, height()), Qt::AlignLeft | Qt::AlignVCenter, text());
    if (hasFocus()) {
        p.setPen(QPen(style::accent(), 1.0));
        p.drawLine(x, height() - 2, width() - 2, height() - 2);
    }
}

} // namespace vloomdaw
