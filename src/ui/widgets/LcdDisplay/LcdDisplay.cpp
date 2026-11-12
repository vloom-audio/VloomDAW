// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/LcdDisplay/LcdDisplay.hpp"

#include "ui/Theme.hpp"

#include <QFontDatabase>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace vloomdaw {

namespace {

constexpr int kPaddingX = 12;
constexpr int kPaddingY = 5;
constexpr int kSeparatorWidth = 14; ///< gap between fields, holds the separator
constexpr int kCaptionGap = 2; ///< between digits and caption
constexpr qreal kRadius = 6.0;
constexpr qreal kDotSize = 3.0;

int separatorWidth(LcdDisplay::Separator separator)
{
    return separator == LcdDisplay::Separator::Rule ? kSeparatorWidth + 8 : kSeparatorWidth;
}

} // namespace

LcdDisplay::LcdDisplay(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    // Paints every pixel (the corners in the tool bar colour), so an update
    // does not make Qt repaint the style-sheeted tool bar behind it.
    setAttribute(Qt::WA_OpaquePaintEvent);

    digitFont_ = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    digitFont_.setPointSizeF(digitFont_.pointSizeF() * 1.4);
    digitFont_.setBold(true);

    captionFont_ = font();
    captionFont_.setPointSizeF(captionFont_.pointSizeF() * 0.62);
    captionFont_.setBold(true);
    captionFont_.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
}

QString LcdDisplay::segmentsFor(const Field& field)
{
    // A value longer than its mask (bar 10000) gets more segments, not clipped.
    if (field.value.size() > field.mask.size())
        return QString(field.value.size(), QLatin1Char('8'));
    return field.mask;
}

int LcdDisplay::fieldWidth(const Field& field) const
{
    return std::max(QFontMetrics(digitFont_).horizontalAdvance(segmentsFor(field)),
        QFontMetrics(captionFont_).horizontalAdvance(field.caption));
}

int LcdDisplay::contentWidth() const
{
    int width = 0;
    for (qsizetype i = 0; i < fields_.size(); ++i) {
        width += fieldWidth(fields_[i]);
        if (i + 1 < fields_.size())
            width += separatorWidth(fields_[i].after);
    }
    return width;
}

int LcdDisplay::contentLeft() const { return (width() - contentWidth()) / 2; }

void LcdDisplay::setFields(QList<Field> fields)
{
    const int oldWidth = contentWidth();
    fields_ = std::move(fields);
    if (contentWidth() != oldWidth)
        updateGeometry();
    update();
}

void LcdDisplay::setFieldValue(qsizetype index, const QString& value)
{
    if (index < 0 || index >= fields_.size() || fields_[index].value == value)
        return;
    const int oldWidth = contentWidth();
    fields_[index].value = value;
    if (contentWidth() != oldWidth)
        updateGeometry();
    update();
}

QSize LcdDisplay::sizeHint() const
{
    const int height = QFontMetrics(digitFont_).ascent() + kCaptionGap + QFontMetrics(captionFont_).height()
        + 2 * kPaddingY;
    return {contentWidth() + 2 * kPaddingX, height};
}

QRect LcdDisplay::fieldRect(qsizetype index) const
{
    int x = contentLeft();
    for (qsizetype i = 0; i < fields_.size(); ++i) {
        const int w = fieldWidth(fields_[i]);
        if (i == index)
            return {x, 0, w, height()};
        x += w + separatorWidth(fields_[i].after);
    }
    return {};
}

qsizetype LcdDisplay::fieldAt(const QPoint& pos) const
{
    for (qsizetype i = 0; i < fields_.size(); ++i) {
        // Include half of the separator on each side so there are no dead zones.
        const QRect r = fieldRect(i).adjusted(-kSeparatorWidth / 2, 0, kSeparatorWidth / 2, 0);
        if (r.contains(pos))
            return i;
    }
    return -1;
}

void LcdDisplay::paintEvent(QPaintEvent* /*event*/)
{
    const Theme::Colors& c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    p.fillRect(rect(), c.panel); // the tool bar behind the rounded corners
    // Recessed panel: flat fill with a hairline border.
    const QRectF panel = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(panel, kRadius, kRadius);
    p.fillPath(path, c.display);
    p.setPen(QPen(c.border, 1.0));
    p.drawPath(path);

    const QFontMetrics digits(digitFont_);
    const QFontMetrics caption(captionFont_);
    const int contentHeight = digits.ascent() + kCaptionGap + caption.height();
    const int baseline = (height() - contentHeight) / 2 + digits.ascent();
    const int captionTop = baseline + kCaptionGap;
    const qreal midDigit = baseline - digits.ascent() * 0.36;

    int x = contentLeft();
    for (qsizetype i = 0; i < fields_.size(); ++i) {
        const Field& f = fields_[i];
        const int w = fieldWidth(f);
        const QString segments = segmentsFor(f);
        const int segmentsX = x + (w - digits.horizontalAdvance(segments)) / 2;
        const int valueX = segmentsX + digits.horizontalAdvance(segments) - digits.horizontalAdvance(f.value);

        p.setFont(digitFont_);
        p.setPen(c.displayUnlit);
        p.drawText(segmentsX, baseline, segments);
        p.setPen(c.displayText);
        p.drawText(valueX, baseline, f.value);

        p.setFont(captionFont_);
        p.setPen(c.textDim);
        p.drawText(QRect(x, captionTop, w, caption.height()), Qt::AlignHCenter | Qt::AlignTop, f.caption);

        x += w;
        if (i + 1 == fields_.size())
            break;

        const int gap = separatorWidth(f.after);
        const qreal cx = x + gap / 2.0;
        p.setPen(Qt::NoPen);
        switch (f.after) {
        case Separator::Dot:
            p.setBrush(c.displayText);
            p.drawEllipse(QPointF(cx, midDigit), kDotSize / 2, kDotSize / 2);
            break;
        case Separator::Colon:
            p.setBrush(c.displayText);
            p.drawEllipse(QPointF(cx, midDigit - 3.5), kDotSize / 2, kDotSize / 2);
            p.drawEllipse(QPointF(cx, midDigit + 3.5), kDotSize / 2, kDotSize / 2);
            break;
        case Separator::Rule:
            p.setPen(QPen(c.border, 1.0));
            p.drawLine(QPointF(cx, kPaddingY + 2), QPointF(cx, height() - kPaddingY - 2));
            break;
        }
        p.setBrush(Qt::NoBrush);
        x += gap;
    }
}

} // namespace vloomdaw
