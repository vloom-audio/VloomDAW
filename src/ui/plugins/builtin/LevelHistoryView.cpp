// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/LevelHistoryView.hpp"

#include "core/ParameterScale.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"

#include <QLinearGradient>
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
constexpr std::size_t kGlowReadings = 40; ///< the newest stretch of the trace that glows

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
    if (animationOn()) {
        // Scroll by one step over the time the readings come apart.
        interval_ = motion::approach(interval_, std::clamp(sincePush_, 0.01, 0.1), 1.0, 4.0);
        sincePush_ = 0.0;
    }
    if (!ticking_)
        update(); // else the next frame shows it
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
    // The newest comes in from the right as the view scrolls towards the next.
    return width() + kStep * (1.0 - scrolled()) - static_cast<double>(count - 1 - index) * kStep;
}

double LevelHistoryView::scrolled() const
{
    return animationOn() && interval_ > 0.0 ? std::clamp(sincePush_ / interval_, 0.0, 1.0) : 1.0;
}

void LevelHistoryView::pulseMarker(double strength)
{
    if (!animationOn())
        return;
    markerPulse_ = std::max(markerPulse_, std::clamp(strength, 0.0, 1.0));
    update();
}

bool LevelHistoryView::advance(const motion::AudioFeatures& audio, double dt)
{
    ticking_ = true;
    sincePush_ += dt;
    const double live = audio.liveliness();
    glow_ = audio.level() * live;
    curtainDb_ = motion::approach(curtainDb_, audio.reductionDb() * live, dt, 0.04);
    markerPulse_ *= std::exp(-dt / 0.3);
    update();
    return glow_ > 0.002 || curtainDb_ > 0.05 || markerPulse_ > 0.01;
}

void LevelHistoryView::rest()
{
    ticking_ = false;
    glow_ = 0.0;
    curtainDb_ = 0.0;
    markerPulse_ = 0.0;
    sincePush_ = interval_;
    update();
}

void LevelHistoryView::animationSwitched(bool /*on*/) { rest(); }

void LevelHistoryView::paintGrid(QPainter& p) const
{
    p.fillRect(rect(), style::background());
    p.setFont(style::font(font(), 10));
    for (const double db : gridDb_) {
        const double y = std::round(yOf(db)) + 0.5;
        p.setPen(QPen(style::grid(), 1.0));
        p.drawLine(QPointF(0, y), QPointF(width(), y));
        p.setPen(style::muted());
        p.drawText(QRectF(width() - 60, y - 15, 52, 12), Qt::AlignRight | Qt::AlignVCenter,
            QString::fromStdString(formatParameterValue("", db)));
    }
}

bool LevelHistoryView::onMarker(double y) const { return std::abs(y - yOf(markerDb_)) <= kGrab; }

void LevelHistoryView::paintEvent(QPaintEvent* /*event*/)
{
    const AnimationPaintTimer timer;
    QPainter p(this);
    std::uint64_t gridKey = motion_paint::keyWith(motion_paint::keyWith(0, topDb_), bottomDb_);
    for (const double db : gridDb_)
        gridKey = motion_paint::keyWith(gridKey, db);
    gridLayer_.draw(p, *this, gridKey, [this](QPainter& layer) { paintGrid(layer); });

    p.setRenderHint(QPainter::Antialiasing);
    const std::size_t count = std::min(input_.size(), static_cast<std::size_t>(width() / kStep + 3));
    const std::size_t first = input_.size() - count;

    // Each run of known readings is one stretch of the curve; gaps break it.
    const auto curve
        = [&](const LevelHistory& history, auto&& yAt, double closeY, bool closed, std::size_t from = 0) {
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
              for (std::size_t i = from; i < count; ++i) {
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
    const bool moving = animationOn();
    if (moving && look_.reduction && curtainDb_ > 0.05) {
        // The gain reduction now, hanging from the top: a curtain that
        // breathes with it, its edge glowing.
        const double depth = curtainDb_ * pixelsPerDb();
        QLinearGradient curtain(QPointF(0, 0), QPointF(0, depth));
        curtain.setColorAt(0.0, motion_paint::faded(accent, 0.02));
        curtain.setColorAt(1.0, motion_paint::faded(accent, 0.14));
        p.fillRect(QRectF(0, 0, width(), depth), curtain);
        motion_paint::glowLine(p, QLineF(0, depth, width(), depth), motion_paint::faded(accent, 0.6),
            std::min(1.0, curtainDb_ / 6.0), 8.0);
    }
    // At 60 frames a second the areas are filled without antialiasing and
    // the lines are 1 px (Qt's fast path; a wider pen costs 15 times as
    // much): the lines over the areas' edges hide the difference.
    p.setRenderHint(QPainter::Antialiasing, !moving);
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
        const QPainterPath trace = curve(output_, levelY, 0.0, false);
        const double penWidth = moving ? 1.0 : look_.outputAccent ? 2.0 : 1.5;
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(stroke, penWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(trace);
        if (moving && glow_ > 0.002) {
            // A live trace: its newest stretch glows with the level, brightest at its head.
            const std::size_t from = count > kGlowReadings ? count - kGlowReadings : 0;
            motion_paint::glowStroke(p, curve(output_, levelY, 0.0, false, from), stroke, glow_, 6.0);
            const double newest = output_.newest();
            if (count > 0 && std::isfinite(newest)) {
                const QPointF head(xOf(count - 1, count), yOf(newest));
                motion_paint::glowDot(p, head, 7.0 + 7.0 * glow_, outputColour, glow_);
                motion_paint::glowDot(p, head, 3.0, text, glow_);
            }
        }
    }
    if (look_.reduction) {
        QColor fill = accent;
        fill.setAlphaF(0.28f);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.setRenderHint(QPainter::Antialiasing, !moving);
        p.drawPath(curve(reduction_, reductionY, 0.0, true));
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(accent, moving ? 1.0 : 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
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
    if (moving && markerPulse_ > 0.01) {
        // The input crossed it: the line flashes.
        motion_paint::glowLine(
            p, QLineF(0, y, width(), y), motion_paint::faded(accent, markerPulse_), markerPulse_, 8.0);
        motion_paint::glowDot(
            p, QPointF(kHandleWidth / 2, y), 10.0 + 10.0 * markerPulse_, accent, markerPulse_);
    }
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
