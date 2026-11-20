// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/TimeEffectParts.hpp"

#include "core/ParameterScale.hpp"
#include "core/TimeEffects.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace vloomdaw {

namespace {

namespace style = editor_style;

constexpr double kHandleRadius = 8.0;
constexpr double kGrab = 12.0; ///< how near the handle a press takes it
constexpr int kHoldTicks = 15; ///< readings an output peak is held (half a second)

constexpr int kSegmentLabelPixels = 10;
constexpr int kSegmentPixels = 11;
constexpr int kSegmentHeight = 28;
constexpr int kSegmentGap = 4; ///< label to buttons
constexpr int kSegmentPadding = 14; ///< each side of a button's text

} // namespace

namespace time_effects {

QColor voiceColour(int index)
{
    static constexpr std::array<QRgb, 4> kColours{0x5eead4, 0xfacc15, 0xf472b6, 0xa78bfa};
    return QColor::fromRgb(kColours[static_cast<std::size_t>(std::clamp(index, 0, 3))]);
}

int panelReach() { return style::kPanelHeight + style::kPanelMargin; }

QString signedPercentText(double percent)
{
    const long whole = std::lround(percent);
    if (whole > 0)
        return QStringLiteral("+%1 %").arg(whole);
    if (whole < 0)
        return QStringLiteral("−%1 %").arg(-whole);
    return QStringLiteral("0 %");
}

QString degreesText(double degrees) { return QStringLiteral("%1°").arg(std::lround(degrees)); }

QString timeText(double seconds)
{
    return QString::fromStdString(formatParameterValue("ms", seconds * 1000.0));
}

void paintTimeAxis(
    QPainter& painter, double left, double right, double span, double top, double bottom, double labelY)
{
    if (!(span > 0.0) || !(right > left))
        return;
    painter.save();
    const double step = tickStep(span, std::max(1, static_cast<int>((right - left) / 110.0)));
    painter.setFont(style::font(painter.font(), 10));
    for (int i = 1; i * step <= span * (1.0 + 1e-9); ++i) {
        const double x = std::round(left + i * step / span * (right - left)) + 0.5;
        painter.setPen(QPen(style::grid(), 1.0));
        painter.drawLine(QPointF(x, top), QPointF(x, bottom));
        painter.setPen(style::muted());
        painter.drawText(QPointF(x + 4.0, labelY), timeText(i * step));
    }
    painter.restore();
}

} // namespace time_effects

// --- HandleDisplay ------------------------------------------------------------

HandleDisplay::HandleDisplay(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
}

void HandleDisplay::paintHandle(QPainter& painter, QPointF centre) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(style::accent(), 2.5));
    painter.setBrush(style::background());
    painter.drawEllipse(centre, kHandleRadius, kHandleRadius);
    painter.restore();
}

bool HandleDisplay::onHandle(QPointF point) const
{
    const std::optional<QPointF> at = handle();
    if (!at)
        return false;
    const QPointF away = point - *at;
    return std::hypot(away.x(), away.y()) <= kGrab;
}

void HandleDisplay::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !onHandleDragged || !onHandle(event->position())) {
        QWidget::mousePressEvent(event);
        return;
    }
    dragging_ = true;
    pressedAt_ = event->position();
    handleAt_ = handle().value_or(pressedAt_);
    event->accept();
}

void HandleDisplay::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_) {
        setCursor(onHandleDragged && onHandle(event->position()) ? cursor_ : Qt::ArrowCursor);
        return;
    }
    const double fine = event->modifiers().testFlag(Qt::ShiftModifier) ? kFineFactor : 1.0;
    onHandleDragged(handleAt_ + (event->position() - pressedAt_) * fine);
}

void HandleDisplay::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !dragging_)
        return;
    dragging_ = false;
    if (onDragFinished)
        onDragFinished();
    update();
}

void HandleDisplay::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && onHandleReset && onHandle(event->position()))
        onHandleReset();
}

// --- ResponseDisplay ------------------------------------------------------------

namespace {

constexpr double kResponseTopDb = 12.0;
constexpr double kResponseBottomDb = -36.0; ///< at the plot's bottom; deeper runs on under the panel
constexpr double kResponseTop = 16.0;
constexpr double kResponseLabelRoom = 24.0;

} // namespace

ResponseDisplay::ResponseDisplay(Axis axis, QWidget* parent)
    : HandleDisplay(parent)
    , axis_(std::move(axis))
{
    setHandleCursor(Qt::SizeHorCursor);
}

void ResponseDisplay::setCurves(
    Response now, std::vector<Response> sweeps, std::optional<double> handleHz, const QString& handleLabel)
{
    now_ = std::move(now);
    sweeps_ = std::move(sweeps);
    handleHz_ = handleHz;
    handleLabel_ = handleLabel;
    ++key_;
    update();
}

void ResponseDisplay::setMotion(
    MovingResponse response, std::function<double(double)> amountAt, double rateHz)
{
    moving_ = std::move(response);
    amountAt_ = std::move(amountAt);
    rateHz_ = std::isfinite(rateHz) ? std::max(rateHz, 0.0) : 0.0;
    update();
}

bool ResponseDisplay::advance(const motion::AudioFeatures& audio, double dt)
{
    // The LFO turns at its real rate; how far the sweep shows eases with the
    // sound, so it settles on the response the parameters give when it stops.
    phase_ = std::fmod(phase_ + rateHz_ * dt, 1.0);
    live_ = motion::approach(live_, audio.liveliness(), dt, 0.3);
    glow_ = audio.level() * audio.liveliness();
    if (moving_)
        update();
    return moving_ && (live_ > 0.002 || glow_ > 0.002);
}

void ResponseDisplay::rest()
{
    live_ = 0.0;
    glow_ = 0.0;
    update();
}

void ResponseDisplay::animationSwitched(bool /*on*/) { rest(); }

double ResponseDisplay::xOf(double hz) const
{
    const double share = axis_.log ? std::log(hz / axis_.fromHz) / std::log(axis_.toHz / axis_.fromHz)
                                   : (hz - axis_.fromHz) / (axis_.toHz - axis_.fromHz);
    return share * width();
}

double ResponseDisplay::hzAt(double x) const
{
    const double share = std::clamp(x / std::max(1, width()), 0.0, 1.0);
    return axis_.log ? axis_.fromHz * std::pow(axis_.toHz / axis_.fromHz, share)
                     : axis_.fromHz + share * (axis_.toHz - axis_.fromHz);
}

double ResponseDisplay::plotBottom() const
{
    return std::max(kResponseTop + 60.0, height() - time_effects::panelReach() - kResponseLabelRoom);
}

double ResponseDisplay::yOf(double db) const
{
    const double y = kResponseTop
        + (kResponseTopDb - db) / (kResponseTopDb - kResponseBottomDb) * (plotBottom() - kResponseTop);
    return std::clamp(y, 2.0, static_cast<double>(height()) + 2.0);
}

QPainterPath ResponseDisplay::trace(const Response& response) const
{
    QPainterPath path;
    for (int x = 0; x <= width(); ++x) {
        const QPointF point(x, yOf(response(hzAt(x))));
        if (x == 0)
            path.moveTo(point);
        else
            path.lineTo(point);
    }
    return path;
}

std::optional<QPointF> ResponseDisplay::handle() const
{
    if (!handleHz_)
        return std::nullopt;
    return QPointF(xOf(*handleHz_), std::round(yOf(0.0)) + 0.5);
}

void ResponseDisplay::paintEvent(QPaintEvent* /*event*/)
{
    const AnimationPaintTimer timer;
    QPainter p(this);
    if (animationOn() && moving_) {
        back_.draw(p, *this, key_, [this](QPainter& layer) { paintBack(layer); });
        paintMoving(p);
    } else {
        paintBack(p);
        paintStill(p);
    }
    paintHandleAndLabel(p);
}

void ResponseDisplay::paintBack(QPainter& p) const
{
    p.fillRect(rect(), style::background());
    p.setFont(style::font(font(), 10));
    for (const auto& [hz, name] : axis_.grid) {
        const double x = std::round(xOf(hz)) + 0.5;
        p.setPen(QPen(style::grid(), 1.0));
        p.drawLine(QPointF(x, 0), QPointF(x, height()));
        p.setPen(style::muted());
        p.drawText(QPointF(x + 4.0, plotBottom() + 14.0), name);
    }
    const double zero = std::round(yOf(0.0)) + 0.5;
    p.setPen(QPen(style::line(), 1.0));
    p.drawLine(QPointF(0, zero), QPointF(width(), zero));
    p.setPen(style::muted());
    p.drawText(
        QRectF(width() - 60, zero - 16, 52, 14), Qt::AlignRight | Qt::AlignBottom, QStringLiteral("0 dB"));
    if (!now_)
        return;

    p.setRenderHint(QPainter::Antialiasing);
    QColor dash = style::accent();
    dash.setAlphaF(0.45f);
    QPen dashed(dash, 1.5);
    dashed.setDashPattern({4.0 / 1.5, 4.0 / 1.5});
    p.setBrush(Qt::NoBrush);
    for (const Response& sweep : sweeps_) {
        p.setPen(dashed);
        p.drawPath(trace(sweep));
    }
    p.setRenderHint(QPainter::Antialiasing, false);
}

void ResponseDisplay::paintStill(QPainter& p) const
{
    if (!now_)
        return;
    p.setRenderHint(QPainter::Antialiasing);
    const QPainterPath curve = trace(now_);
    QPainterPath area = curve;
    area.lineTo(width(), height());
    area.lineTo(0, height());
    area.closeSubpath();
    QColor fill = style::accent();
    fill.setAlphaF(0.16f);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawPath(area);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(style::accent(), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(curve);
}

void ResponseDisplay::paintMoving(QPainter& p)
{
    // Where the sweep is: the plugin's LFO (amountAt_) scaled by how far the
    // sweep shows (none in silence: the response the parameters give).
    const double amount = amountAt_ ? live_ * amountAt_(phase_) : 0.0;
    const int step = std::max(3, width() / 300);
    points_.clear();
    for (int x = 0; x <= width() + step; x += step) {
        const double hz = hzAt(static_cast<double>(x));
        // Kept on the plot: a notch's depth below it is not seen and its
        // edges would cost.
        points_.emplace_back(static_cast<double>(x), std::min(yOf(moving_(hz, amount)), plotBottom()));
    }
    if (points_.size() < 2)
        return;

    // The area under it, brighter with the sound: one polygon, not
    // antialiased, at a third of the resolution and smoothed up when drawn
    // (at full size the comb's many steep edges cost over a millisecond).
    QColor fill = style::accent();
    fill.setAlphaF(static_cast<float>(0.14 + 0.22 * glow_));
    const QSize small((width() + 2) / 3, (height() + 2) / 3);
    if (area_.size() != small)
        area_ = QImage(small, QImage::Format_ARGB32_Premultiplied);
    area_.fill(Qt::transparent);
    const std::size_t count = points_.size();
    points_.emplace_back(points_.back().x(), static_cast<double>(height()));
    points_.emplace_back(0.0, static_cast<double>(height()));
    {
        QPainter q(&area_);
        q.scale(1.0 / 3.0, 1.0 / 3.0);
        q.setPen(Qt::NoPen);
        q.setBrush(fill);
        q.drawPolygon(points_.data(), static_cast<int>(points_.size()));
    }
    points_.resize(count);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRectF(0.0, 0.0, 3.0 * small.width(), 3.0 * small.height()), area_);
    // The curve 1 px wide: Qt's fast path.
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(style::accent(), 1.0));
    p.drawPolyline(points_.data(), static_cast<int>(points_.size()));

    // A glow in each notch (a low point well under 0 dB) and on the peaks,
    // as bright as the sound.
    if (glow_ <= 0.01)
        return;
    const double notchY = yOf(-6.0);
    motion_paint::GlowBatch glow(p, style::accent());
    for (std::size_t i = 1; i + 1 < points_.size(); ++i) {
        const double y = points_[i].y();
        if (y > notchY && y >= points_[i - 1].y() && y > points_[i + 1].y())
            glow.dot(points_[i], 14.0, 0.85 * glow_);
        else if (y <= points_[i - 1].y() && y < points_[i + 1].y())
            glow.dot(points_[i], 9.0, 0.45 * glow_);
    }
}

void ResponseDisplay::paintHandleAndLabel(QPainter& p) const
{
    if (const auto at = handle()) {
        p.setRenderHint(QPainter::Antialiasing);
        paintHandle(p, *at);
        p.setFont(style::font(font(), 10, QFont::DemiBold, true));
        p.setPen(style::text());
        const double x = at->x() + 14.0 + 120.0 > width() ? at->x() - 14.0 - 120.0 : at->x() + 14.0;
        p.drawText(QRectF(x, at->y() - 22.0, 120.0, 14.0),
            (x < at->x() ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter, handleLabel_);
    }
}

// --- SegmentedChoice -------------------------------------------------------------

SegmentedChoice::SegmentedChoice(const QString& label, QStringList options, QWidget* parent)
    : QWidget(parent)
    , label_(label)
    , options_(std::move(options))
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setAccessibleName(label);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void SegmentedChoice::setCurrent(int index)
{
    index = std::clamp(index, 0, std::max(0, static_cast<int>(options_.size()) - 1));
    if (index == current_)
        return;
    current_ = index;
    update();
}

QSize SegmentedChoice::sizeHint() const
{
    const QFontMetrics label(style::font(font(), kSegmentLabelPixels));
    const QFontMetrics words(style::font(font(), kSegmentPixels, QFont::Bold));
    int widest = 0;
    for (const QString& option : options_)
        widest = std::max(widest, words.horizontalAdvance(option));
    const int buttons = static_cast<int>(options_.size()) * (widest + 2 * kSegmentPadding) + 4;
    return {
        std::max(buttons, label.horizontalAdvance(label_)), label.height() + kSegmentGap + kSegmentHeight};
}

QRectF SegmentedChoice::segment(int index) const
{
    const QFontMetrics label(style::font(font(), kSegmentLabelPixels));
    const QRectF frame(0.5, label.height() + kSegmentGap + 0.5, width() - 1.0, kSegmentHeight - 1.0);
    const auto count = static_cast<double>(std::max<qsizetype>(1, options_.size()));
    const double each = (frame.width() - 4.0 - 2.0 * static_cast<double>(options_.size() - 1)) / count;
    return {frame.left() + 2.0 + index * (each + 2.0), frame.top() + 2.0, each, frame.height() - 4.0};
}

void SegmentedChoice::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QFont labelFont = style::font(font(), kSegmentLabelPixels);
    const QFontMetrics label(labelFont);
    p.setFont(labelFont);
    p.setPen(style::muted());
    p.drawText(QRect(0, 0, width(), label.height()), Qt::AlignLeft | Qt::AlignVCenter, label_);

    const QRectF frame(0.5, label.height() + kSegmentGap + 0.5, width() - 1.0, kSegmentHeight - 1.0);
    p.setPen(QPen(hasFocus() ? style::accent() : style::strong(), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(frame, 7.0, 7.0);
    for (int i = 0; i < options_.size(); ++i) {
        const QRectF button = segment(i);
        const bool lit = i == current_;
        if (lit) {
            p.setPen(Qt::NoPen);
            p.setBrush(isEnabled() ? style::accent() : style::muted());
            p.drawRoundedRect(button, 5.0, 5.0);
        }
        p.setFont(style::font(font(), kSegmentPixels, lit ? QFont::Bold : QFont::Normal));
        p.setPen(lit ? style::background() : style::text());
        p.drawText(button, Qt::AlignCenter, options_[i]);
    }
}

void SegmentedChoice::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    for (int i = 0; i < options_.size(); ++i) {
        if (segment(i).contains(event->position()) && i != current_) {
            current_ = i;
            update();
            emit chosen(i);
            return;
        }
    }
}

void SegmentedChoice::keyPressEvent(QKeyEvent* event)
{
    const int step = event->key() == Qt::Key_Left ? -1 : event->key() == Qt::Key_Right ? 1 : 0;
    const int next = std::clamp(current_ + step, 0, static_cast<int>(options_.size()) - 1);
    if (step == 0 || next == current_) {
        QWidget::keyPressEvent(event);
        return;
    }
    current_ = next;
    update();
    emit chosen(next);
}

// --- OutputLevel -------------------------------------------------------------------

OutputLevel::OutputLevel(Readout* readout)
    : readout_(readout)
{
    readout_->setWidestText(QStringLiteral("−100.0 dB"));
    readout_->setText(QStringLiteral("–"));
    readout_->setToolTip(QObject::tr("The peak of what the effect puts out"));
}

void OutputLevel::push(bool running, double outputDb)
{
    if (!running || std::isnan(outputDb)) {
        held_ = -std::numeric_limits<double>::infinity();
        ticks_ = 0;
        readout_->setText(QStringLiteral("–"));
        return;
    }
    if (outputDb >= held_ || ++ticks_ >= kHoldTicks) {
        held_ = outputDb;
        ticks_ = 0;
    }
    readout_->setText(std::isinf(held_) ? QObject::tr("Silent")
                                        : QString::fromStdString(formatParameterValue("dB", held_)));
}

} // namespace vloomdaw
