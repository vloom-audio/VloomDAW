// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/LevelHistoryView.hpp"

#include "core/ParameterScale.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <utility>

namespace vloomdaw {

namespace {

namespace style = editor_style;

constexpr double kPadding = 20.0; ///< above the top level and below the bottom one
constexpr double kGrab = 7.0; ///< how near the marker a press grabs it
constexpr double kHandleWidth = 12.0;
constexpr double kHandleHeight = 16.0;

} // namespace

LevelHistoryView::LevelHistoryView(Look look, QWidget* parent)
    : QWidget(parent)
    , look_(look)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void LevelHistoryView::setRange(double topDb, double bottomDb, std::vector<double> gridDb)
{
    topDb_ = topDb;
    bottomDb_ = bottomDb < topDb ? bottomDb : topDb - 1.0;
    gridDb_ = std::move(gridDb);
    update();
}

void LevelHistoryView::push(double inputDb, double outputDb, double reductionDb)
{
    input_.push(inputDb);
    output_.push(outputDb);
    reduction_.push(reductionDb);
    update();
}

void LevelHistoryView::clear()
{
    input_.clear();
    output_.clear();
    reduction_.clear();
    update();
}

void LevelHistoryView::setMarker(double db, const QString& label)
{
    if (db == markerDb_ && label == markerLabel_)
        return;
    markerDb_ = db;
    markerLabel_ = label;
    update();
}

void LevelHistoryView::setMarkerLabelX(int x)
{
    markerLabelX_ = x;
    update();
}

double LevelHistoryView::pixelsPerDb() const
{
    return std::max(1.0, height() - 2 * kPadding) / (topDb_ - bottomDb_);
}

double LevelHistoryView::yOf(double db) const
{
    if (std::isinf(db) && db < 0.0)
        return height();
    return kPadding + (topDb_ - std::clamp(db, bottomDb_ - kPadding, topDb_ + kPadding)) * pixelsPerDb();
}

double LevelHistoryView::xOf(std::size_t index, std::size_t count) const
{
    return width() - static_cast<double>(count - 1 - index) * kStep;
}

bool LevelHistoryView::onMarker(double y) const { return std::abs(y - yOf(markerDb_)) <= kGrab; }

void LevelHistoryView::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.fillRect(rect(), style::background());

    QFont small = style::font(font(), 10);
    p.setFont(small);
    for (const double db : gridDb_) {
        const double y = std::round(yOf(db)) + 0.5;
        p.setPen(QPen(style::grid(), 1.0));
        p.drawLine(QPointF(0, y), QPointF(width(), y));
        p.setPen(style::muted());
        p.drawText(QRectF(width() - 60, y - 15, 52, 12), Qt::AlignRight | Qt::AlignVCenter,
            QString::fromStdString(formatParameterValue("", db)));
    }

    p.setRenderHint(QPainter::Antialiasing);
    const std::size_t count = std::min(input_.size(), static_cast<std::size_t>(width() / kStep + 2));
    const std::size_t first = input_.size() - count;

    // Each run of known readings is one stretch of the curve; gaps break it.
    const auto curve = [&](const LevelHistory& history, auto&& yAt, double closeY, bool closed) {
        QPainterPath path;
        bool open = false;
        double lastX = 0.0;
        const auto finish = [&] {
            if (open && closed) {
                path.lineTo(lastX, closeY);
                path.closeSubpath();
            }
            open = false;
        };
        for (std::size_t i = 0; i < count; ++i) {
            const double level = history.at(first + i);
            const double x = xOf(i, count);
            if (std::isnan(level)) {
                finish();
                continue;
            }
            const double y = yAt(level);
            if (!open) {
                if (closed) {
                    path.moveTo(x, closeY);
                    path.lineTo(x, y);
                } else {
                    path.moveTo(x, y);
                }
                open = true;
            } else {
                path.lineTo(x, y);
            }
            lastX = x;
        }
        finish();
        return path;
    };
    const auto levelY = [this](double db) { return yOf(db); };
    const auto reductionY = [this](double db) { return std::max(0.0, db) * pixelsPerDb(); };

    const QColor accent = style::accent();
    const QColor text = style::text();
    if (look_.inputArea) {
        QColor fill = text;
        fill.setAlphaF(0.08f);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawPath(curve(input_, levelY, height(), true));
    }
    const QColor outputColour = look_.outputAccent ? accent : text;
    if (look_.outputArea) {
        QColor fill = outputColour;
        fill.setAlphaF(look_.outputAccent ? 0.3f : 0.1f);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawPath(curve(output_, levelY, height(), true));
    }
    if (look_.outputLine) {
        QColor stroke = outputColour;
        if (!look_.outputAccent)
            stroke.setAlphaF(0.7f);
        p.setPen(QPen(stroke, look_.outputAccent ? 2.0 : 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(curve(output_, levelY, 0.0, false));
    }
    if (look_.reduction) {
        QColor fill = accent;
        fill.setAlphaF(0.28f);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawPath(curve(reduction_, reductionY, 0.0, true));
        p.setPen(QPen(accent, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(curve(reduction_, reductionY, 0.0, false));
    }

    if (markerLabel_.isEmpty())
        return;
    const double y = std::round(yOf(markerDb_)) + 0.5;
    p.setRenderHint(QPainter::Antialiasing, false);
    QPen dashed(accent, 1.0);
    dashed.setDashPattern({5.0, 4.0});
    p.setPen(dashed);
    p.drawLine(QPointF(0, y), QPointF(width(), y));
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    p.drawRoundedRect(QRectF(0, y - kHandleHeight / 2, kHandleWidth, kHandleHeight), 3, 3);
    p.setFont(style::font(font(), 10, QFont::DemiBold, true));
    p.setPen(text);
    // Under the line when it is too near the top for the label to fit above.
    const double baseline = y < kPadding + 4 ? y + 16 : y - 6;
    p.drawText(QPointF(markerLabelX_, baseline), markerLabel_);
}

void LevelHistoryView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !onMarkerDragged || !onMarker(event->position().y())) {
        QWidget::mousePressEvent(event);
        return;
    }
    dragging_ = true;
    dragDb_ = markerDb_;
    lastY_ = event->position().y();
    event->accept();
}

void LevelHistoryView::mouseMoveEvent(QMouseEvent* event)
{
    const double y = event->position().y();
    if (!dragging_) {
        setCursor(onMarkerDragged && onMarker(y) ? Qt::SizeVerCursor : Qt::ArrowCursor);
        return;
    }
    const double fine = event->modifiers().testFlag(Qt::ShiftModifier) ? kFineFactor : 1.0;
    dragDb_ = std::clamp(dragDb_ - (y - lastY_) / pixelsPerDb() * fine, bottomDb_, topDb_);
    lastY_ = y;
    onMarkerDragged(dragDb_);
}

void LevelHistoryView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        dragging_ = false;
}

void LevelHistoryView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && onMarkerReset && onMarker(event->position().y()))
        onMarkerReset();
}

} // namespace vloomdaw
