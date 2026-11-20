// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/PillToggle.hpp"

#include "ui/plugins/builtin/EditorStyle.hpp"

#include <QFontMetrics>
#include <QPainter>

#include <algorithm>

namespace vloomdaw {

namespace {

namespace style = editor_style;

constexpr int kPillWidth = 26;
constexpr int kPillHeight = 14;
constexpr int kDot = 10;
constexpr int kStackedWidth = 56;
constexpr int kStackedGap = 6; ///< pill to label, stacked
constexpr int kInlineGap = 6; ///< label to pill, inline
constexpr int kLabelPixels = 10;
constexpr int kInlinePixels = 11;
constexpr int kInlineHeight = 20;

} // namespace

PillToggle::PillToggle(const QString& label, Layout layout, QWidget* parent)
    : QAbstractButton(parent)
    , layout_(layout)
{
    setText(label);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setAccessibleName(label);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize PillToggle::sizeHint() const
{
    if (layout_ == Layout::Inline) {
        const QFontMetrics metrics(style::font(font(), kInlinePixels));
        const int labelWidth = text().isEmpty() ? 0 : metrics.horizontalAdvance(text()) + kInlineGap;
        return {labelWidth + kPillWidth, kInlineHeight};
    }
    const QFontMetrics metrics(style::font(font(), kLabelPixels));
    const QRect words
        = metrics.boundingRect(QRect(0, 0, kStackedWidth, 100), Qt::AlignHCenter | Qt::TextWordWrap, text());
    return {kStackedWidth, kPillHeight + kStackedGap + words.height()};
}

void PillToggle::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRectF pill;
    if (layout_ == Layout::Inline) {
        const QFont labelFont = style::font(font(), kInlinePixels);
        p.setFont(labelFont);
        p.setPen(style::muted());
        p.drawText(QRect(0, 0, width() - kPillWidth, height()), Qt::AlignLeft | Qt::AlignVCenter, text());
        pill = QRectF(width() - kPillWidth, (height() - kPillHeight) / 2.0, kPillWidth, kPillHeight);
    } else {
        const int top = std::max(0, (height() - sizeHint().height()) / 2);
        pill = QRectF((width() - kPillWidth) / 2.0, top, kPillWidth, kPillHeight);
        p.setFont(style::font(font(), kLabelPixels));
        p.setPen(style::muted());
        p.drawText(QRect(0, top + kPillHeight + kStackedGap, width(), height()),
            Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, text());
    }

    const bool on = isChecked();
    p.setPen(Qt::NoPen);
    p.setBrush(on ? (isEnabled() ? style::accent() : style::muted()) : style::strong());
    p.drawRoundedRect(pill, kPillHeight / 2.0, kPillHeight / 2.0);
    const double inset = (kPillHeight - kDot) / 2.0;
    const double x = on ? pill.right() - inset - kDot : pill.left() + inset;
    p.setBrush(on ? style::text() : style::muted());
    p.drawEllipse(QRectF(x, pill.top() + inset, kDot, kDot));
    if (hasFocus()) {
        p.setPen(QPen(style::accent(), 1.0, Qt::DotLine));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(pill.adjusted(-2.5, -2.5, 2.5, 2.5), kPillHeight / 2.0 + 2, kPillHeight / 2.0 + 2);
    }
}

} // namespace vloomdaw
