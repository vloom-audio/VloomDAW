// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/ChoiceButtons.hpp"

#include "ui/plugins/builtin/EditorStyle.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <utility>

namespace vloomdaw {

namespace {

namespace style = editor_style;

constexpr int kCaptionPixels = 10;
constexpr int kCaptionHeight = 14;
constexpr int kGap = 4; ///< caption to row
constexpr int kRowHeight = 28;
constexpr double kRadius = 7.0;
constexpr double kInnerRadius = 5.0;
constexpr double kPad = 2.0; ///< frame to buttons, and between them

} // namespace

ChoiceButtons::ChoiceButtons(const QString& caption, QStringList choices, int width, QWidget* parent)
    : QWidget(parent)
    , caption_(caption)
    , choices_(std::move(choices))
    , width_(width)
{
    setFocusPolicy(Qt::TabFocus);
    setCursor(Qt::PointingHandCursor);
    setAccessibleName(caption);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void ChoiceButtons::setCurrent(int index)
{
    if (index < 0 || index >= choices_.size() || index == current_)
        return;
    current_ = index;
    setAccessibleDescription(choices_.at(index));
    update();
}

QSize ChoiceButtons::sizeHint() const { return {width_, kCaptionHeight + kGap + kRowHeight}; }

QRectF ChoiceButtons::rowRect() const
{
    return {0.5, kCaptionHeight + kGap + 0.5, width() - 1.0, kRowHeight - 1.0};
}

QRectF ChoiceButtons::buttonRect(int index) const
{
    const QRectF row = rowRect().adjusted(kPad, kPad, -kPad, -kPad);
    const auto count = static_cast<double>(std::max<qsizetype>(1, choices_.size()));
    const double each = (row.width() - kPad * (count - 1.0)) / count;
    return {row.left() + index * (each + kPad), row.top(), each, row.height()};
}

void ChoiceButtons::choose(int index)
{
    if (index < 0 || index >= choices_.size() || index == current_)
        return;
    setCurrent(index);
    if (onChosen)
        onChosen(index);
}

void ChoiceButtons::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setFont(style::font(font(), kCaptionPixels));
    p.setPen(style::muted());
    p.drawText(QRect(0, 0, width(), kCaptionHeight), Qt::AlignLeft | Qt::AlignVCenter, caption_);

    p.setPen(QPen(hasFocus() ? style::accent() : style::strong(), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(rowRect(), kRadius, kRadius);

    const QFont normal = style::font(font(), 11);
    const QFont bold = style::font(font(), 11, QFont::Bold);
    for (int i = 0; i < choices_.size(); ++i) {
        const QRectF button = buttonRect(i);
        const bool chosen = i == current_;
        if (chosen) {
            p.setPen(Qt::NoPen);
            p.setBrush(isEnabled() ? style::accent() : style::strong());
            p.drawRoundedRect(button, kInnerRadius, kInnerRadius);
        }
        p.setFont(chosen ? bold : normal);
        p.setPen(chosen ? style::background() : style::text());
        p.drawText(button, Qt::AlignCenter, choices_.at(i));
    }
}

void ChoiceButtons::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    for (int i = 0; i < choices_.size(); ++i)
        if (buttonRect(i).adjusted(-kPad, -kPad, kPad, kPad).contains(event->position())) {
            choose(i);
            break;
        }
    event->accept();
}

void ChoiceButtons::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Down:
        choose(current_ - 1);
        break;
    case Qt::Key_Right:
    case Qt::Key_Up:
        choose(current_ + 1);
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

} // namespace vloomdaw
