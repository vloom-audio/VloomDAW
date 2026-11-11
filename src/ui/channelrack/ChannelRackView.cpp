// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/channelrack/ChannelRackView.hpp"

#include "core/Project.hpp"
#include "ui/Theme.hpp"

#include <QMouseEvent>
#include <QPainter>

namespace vloomdaw {

namespace {

constexpr int kRowHeight = 32;
constexpr int kTop = 6;
constexpr int kLedX = 10;
constexpr int kLedSize = 12;
constexpr int kNameX = 32;
constexpr int kNameWidth = 130;
constexpr int kPadsX = kNameX + kNameWidth + 12;
constexpr int kPadWidth = 24;
constexpr int kPadGap = 3;
constexpr int kGroupGap = 7; ///< extra space between groups of four steps
constexpr int kPadInset = 4; ///< vertical inset of a pad in its row

} // namespace

ChannelRackView::ChannelRackView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("channelRackView"));
    setMouseTracking(false);
}

void ChannelRackView::setProject(const Project* project)
{
    project_ = project;
    refresh();
}

void ChannelRackView::setPattern(std::size_t pattern)
{
    pattern_ = pattern;
    update();
}

void ChannelRackView::setSelectedChannel(std::size_t channel)
{
    selected_ = channel;
    update();
}

void ChannelRackView::setPlayStep(int step)
{
    if (step == playStep_)
        return;
    // Repaint only the columns of the old and new step.
    const auto column = [this](int s) {
        if (s < 0 || channelCount() == 0)
            return QRect();
        const QRect top = padRect(0, s);
        return QRect(top.left() - 2, 0, top.width() + 4, height());
    };
    const QRect before = column(playStep_);
    playStep_ = step;
    update(before);
    update(column(playStep_));
}

void ChannelRackView::refresh()
{
    updateGeometry();
    adjustSize();
    update();
}

std::size_t ChannelRackView::channelCount() const { return project_ != nullptr ? project_->channels().size() : 0; }

QSize ChannelRackView::sizeHint() const
{
    const QRect last = padRect(0, kStepsPerBar - 1);
    return {last.right() + 16, kTop + static_cast<int>(channelCount()) * kRowHeight + kTop};
}

QRect ChannelRackView::padRect(std::size_t channel, int step) const
{
    const int x = kPadsX + step * (kPadWidth + kPadGap) + (step / 4) * kGroupGap;
    const int y = kTop + static_cast<int>(channel) * kRowHeight + kPadInset;
    return {x, y, kPadWidth, kRowHeight - 2 * kPadInset};
}

ChannelRackView::Hit ChannelRackView::hitAt(const QPoint& pos) const
{
    const int row = (pos.y() - kTop) / kRowHeight;
    if (pos.y() < kTop || row < 0 || static_cast<std::size_t>(row) >= channelCount())
        return {};
    const auto channel = static_cast<std::size_t>(row);
    if (pos.x() >= kLedX - 4 && pos.x() < kNameX - 2)
        return {Part::Mute, channel, -1};
    if (pos.x() >= kNameX && pos.x() < kNameX + kNameWidth)
        return {Part::Name, channel, -1};
    for (int s = 0; s < kStepsPerBar; ++s)
        if (padRect(channel, s).adjusted(-1, -kPadInset, 1, kPadInset).contains(pos))
            return {Part::Step, channel, s};
    return {};
}

void ChannelRackView::paintEvent(QPaintEvent* /*event*/)
{
    const Theme::Colors& c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (project_ == nullptr)
        return;

    const QColor patternColor = Theme::patternColor(pattern_);
    for (std::size_t ch = 0; ch < channelCount(); ++ch) {
        const Channel& channel = project_->channels()[ch];
        const int y = kTop + static_cast<int>(ch) * kRowHeight;

        // Mute light: lit = playing (FL Studio style).
        p.setPen(Qt::NoPen);
        p.setBrush(channel.muted ? c.border : c.playing);
        p.drawEllipse(QRectF(kLedX, y + (kRowHeight - kLedSize) / 2.0, kLedSize, kLedSize));

        // Name button.
        const QRect nameRect(kNameX, y + kPadInset, kNameWidth, kRowHeight - 2 * kPadInset);
        p.setBrush(ch == selected_ ? c.raised : c.panel);
        p.setPen(QPen(ch == selected_ ? c.accent : c.border, 1));
        p.drawRoundedRect(QRectF(nameRect).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        p.setPen(channel.muted ? c.textDim : c.text);
        p.drawText(nameRect.adjusted(8, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
            QFontMetrics(font()).elidedText(QString::fromStdString(channel.name), Qt::ElideRight, nameRect.width() - 14));

        // Step pads: alternate groups of four are shaded differently.
        for (int s = 0; s < kStepsPerBar; ++s) {
            const QRectF pad = QRectF(padRect(ch, s)).adjusted(0.5, 0.5, -0.5, -0.5);
            const bool on = project_->step(pattern_, ch, s);
            const bool evenGroup = (s / 4) % 2 == 0;
            QColor fill = on ? patternColor : (evenGroup ? c.raised : c.panel);
            if (on && channel.muted)
                fill = fill.darker(180);
            p.setBrush(fill);
            p.setPen(QPen(s == playStep_ ? c.text : c.border, s == playStep_ ? 1.5 : 1.0));
            p.drawRoundedRect(pad, 3, 3);
        }
    }
}

void ChannelRackView::mousePressEvent(QMouseEvent* event)
{
    const Hit hit = hitAt(event->position().toPoint());
    if (project_ == nullptr || hit.part == Part::None)
        return;

    if (event->button() == Qt::LeftButton && hit.part == Part::Mute) {
        emit muteToggled(hit.channel);
        return;
    }
    if (event->button() == Qt::LeftButton && hit.part == Part::Name) {
        emit channelSelected(hit.channel);
        return;
    }
    if (hit.part == Part::Step && (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)) {
        // Left toggles; right clears. Dragging paints the same state.
        painting_ = true;
        paintChannel_ = hit.channel;
        paintState_ = event->button() == Qt::LeftButton && !project_->step(pattern_, hit.channel, hit.step);
        lastPaintedStep_ = hit.step;
        emit stepSet(hit.channel, hit.step, paintState_);
    }
}

void ChannelRackView::mouseMoveEvent(QMouseEvent* event)
{
    if (!painting_)
        return;
    const Hit hit = hitAt(event->position().toPoint());
    if (hit.part != Part::Step || hit.channel != paintChannel_ || hit.step == lastPaintedStep_)
        return;
    lastPaintedStep_ = hit.step;
    emit stepSet(hit.channel, hit.step, paintState_);
}

void ChannelRackView::mouseReleaseEvent(QMouseEvent* /*event*/)
{
    painting_ = false;
    lastPaintedStep_ = -1;
}

void ChannelRackView::mouseDoubleClickEvent(QMouseEvent* event)
{
    const Hit hit = hitAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && hit.part == Part::Name)
        emit channelOpened(hit.channel);
    else
        mousePressEvent(event); // fast clicks on pads still toggle
}

} // namespace vloomdaw
