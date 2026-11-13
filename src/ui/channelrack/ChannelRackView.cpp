// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/channelrack/ChannelRackView.hpp"

#include "core/Decibels.hpp"
#include "core/Project.hpp"
#include "ui/DroppedFiles.hpp"
#include "ui/Theme.hpp"

#include <QDir>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QHelpEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace vloomdaw {

namespace {

constexpr int kRowHeight = 32;
constexpr int kTop = 6;
constexpr int kLedX = 10;
constexpr int kLedSize = 12;
constexpr int kVolumeX = 30; ///< volume knob
constexpr int kVolumeSize = 20;
constexpr int kPanX = kVolumeX + kVolumeSize + 4; ///< pan knob, the same size
constexpr int kInsertX = kPanX + kVolumeSize + 6; ///< mixer insert box
constexpr int kInsertWidth = 30;
constexpr int kNameX = kInsertX + kInsertWidth + 8;
constexpr double kVolumePerPixel = 0.005; ///< knob drag; Shift: a fifth of it
constexpr double kPanPerPixel = 0.01; ///< the whole pan range in 200 pixels, like volume

/// "C", "L 35", "R 100".
QString panText(double pan)
{
    const int percent = static_cast<int>(std::lround(std::abs(pan) * 100.0));
    if (percent == 0)
        return QStringLiteral("C");
    return QStringLiteral("%1 %2").arg(pan < 0 ? QLatin1Char('L') : QLatin1Char('R')).arg(percent);
}
constexpr int kNameWidth = 130;
constexpr int kPadsX = kNameX + kNameWidth + 12;
constexpr int kPadWidth = 24;
constexpr int kPadGap = 3;
constexpr int kGroupGap = 7; ///< extra space between groups of four steps
constexpr int kBarGap = 10; ///< and more between bars
constexpr int kPadInset = 4; ///< vertical inset of a pad in its row

} // namespace

ChannelRackView::ChannelRackView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("channelRackView"));
    setMouseTracking(false);
    setAcceptDrops(true);
}

void ChannelRackView::setProject(const Project* project)
{
    project_ = project;
    refresh();
}

void ChannelRackView::setPattern(std::size_t pattern)
{
    pattern_ = pattern;
    refresh(); // patterns can have different numbers of pads
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

std::size_t ChannelRackView::channelCount() const
{
    return project_ != nullptr ? project_->channels().size() : 0;
}

int ChannelRackView::stepCount() const
{
    return project_ != nullptr ? project_->stepCount(pattern_) : kStepsPerBar;
}

QRect ChannelRackView::addBarRect(std::size_t channel) const
{
    const QRect last = padRect(channel, stepCount() - 1);
    return {last.right() + 1 + kGroupGap, last.top(), kPadWidth, last.height()};
}

QSize ChannelRackView::sizeHint() const
{
    const QRect last = addBarRect(0);
    // One spare row: dropping a file there adds a channel.
    return {last.right() + 16, kTop + static_cast<int>(channelCount() + 1) * kRowHeight + kTop};
}

QRect ChannelRackView::padRect(std::size_t channel, int step) const
{
    const int x
        = kPadsX + step * (kPadWidth + kPadGap) + (step / 4) * kGroupGap + (step / kStepsPerBar) * kBarGap;
    const int y = kTop + static_cast<int>(channel) * kRowHeight + kPadInset;
    return {x, y, kPadWidth, kRowHeight - 2 * kPadInset};
}

ChannelRackView::Hit ChannelRackView::hitAt(const QPoint& pos) const
{
    const int row = (pos.y() - kTop) / kRowHeight;
    if (pos.y() < kTop || row < 0 || static_cast<std::size_t>(row) >= channelCount())
        return {};
    const auto channel = static_cast<std::size_t>(row);
    if (pos.x() >= kLedX - 4 && pos.x() < kVolumeX - 2)
        return {Part::Mute, channel, -1};
    if (pos.x() >= kVolumeX - 2 && pos.x() < kVolumeX + kVolumeSize + 2)
        return {Part::Volume, channel, -1};
    if (pos.x() >= kPanX && pos.x() < kPanX + kVolumeSize + 2)
        return {Part::Pan, channel, -1};
    if (pos.x() >= kInsertX && pos.x() < kInsertX + kInsertWidth)
        return {Part::Insert, channel, -1};
    if (pos.x() >= kNameX && pos.x() < kNameX + kNameWidth)
        return {Part::Name, channel, -1};
    if (pos.x() < kPadsX - 1)
        return {};
    // Pads are evenly spaced within a group of four: find the one under x.
    const int steps = stepCount();
    for (int s = 0; s < steps; ++s) {
        const QRect pad = padRect(channel, s).adjusted(-1, -kPadInset, 1, kPadInset);
        if (pad.contains(pos))
            return {Part::Step, channel, s};
        if (pad.left() > pos.x())
            return {};
    }
    if (steps < Project::kMaxSteps
        && addBarRect(channel).adjusted(-1, -kPadInset, 1, kPadInset).contains(pos))
        return {Part::AddBar, channel, -1};
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

        // Volume knob: an arc from 7 to 5 o'clock, lit up to the value.
        const QRectF knob(
            kVolumeX + 2, y + (kRowHeight - kVolumeSize) / 2.0 + 2, kVolumeSize - 4, kVolumeSize - 4);
        constexpr int kStart = 225 * 16; // Qt angles: 1/16 degree, counter-clockwise from 3 o'clock
        constexpr int kSpan = -270 * 16;
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(c.border, 2.5, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(knob, kStart, kSpan);
        p.setPen(QPen(channel.muted ? c.textDim : c.accent, 2.5, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(knob, kStart, static_cast<int>(kSpan * channel.volume));

        // Pan knob: the same arc, lit from 12 o'clock towards the side.
        const QRectF panKnob = knob.translated(kPanX - kVolumeX, 0);
        constexpr int kNoon = 90 * 16;
        p.setPen(QPen(c.border, 2.5, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(panKnob, kStart, kSpan);
        p.setPen(QPen(channel.muted ? c.textDim : c.accent, 2.5, Qt::SolidLine, Qt::RoundCap));
        const int panSpan = static_cast<int>(-channel.pan * 135 * 16); // right: clockwise (negative)
        if (panSpan != 0)
            p.drawArc(panKnob, kNoon, panSpan);
        else
            p.drawPoint(QPointF(panKnob.center().x(), panKnob.top()));

        // Mixer insert box: "-" for the master, else the insert number.
        const QRect insertRect(kInsertX, y + kPadInset + 3, kInsertWidth, kRowHeight - 2 * kPadInset - 6);
        p.setBrush(c.panel);
        p.setPen(QPen(c.border, 1));
        p.drawRoundedRect(QRectF(insertRect).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        p.setPen(channel.insert == 0 ? c.textDim : c.text);
        p.drawText(insertRect, Qt::AlignCenter,
            channel.insert == 0 ? QStringLiteral("-") : QString::number(channel.insert));

        // Name button.
        const QRect nameRect(kNameX, y + kPadInset, kNameWidth, kRowHeight - 2 * kPadInset);
        p.setBrush(ch == selected_ ? c.raised : c.panel);
        p.setPen(QPen(ch == selected_ ? c.accent : c.border, 1));
        p.drawRoundedRect(QRectF(nameRect).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        // Dim names without a sample: they make no sound until one is dropped.
        p.setPen(channel.muted || channel.samplePath.empty() ? c.textDim : c.text);
        p.drawText(nameRect.adjusted(8, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
            QFontMetrics(font()).elidedText(
                QString::fromStdString(channel.name), Qt::ElideRight, nameRect.width() - 14));

        // Step pads: alternate groups of four are shaded differently.
        const int steps = stepCount();
        for (int s = 0; s < steps; ++s) {
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
        if (steps < Project::kMaxSteps) {
            // "+": one bar of pads more.
            const QRectF add = QRectF(addBarRect(ch)).adjusted(0.5, 0.5, -0.5, -0.5);
            QPen dashed(c.border, 1.0, Qt::DashLine);
            p.setPen(dashed);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(add, 3, 3);
            p.setPen(c.textDim);
            p.drawText(add, Qt::AlignCenter, QStringLiteral("+"));
        }
    }

    // Drop target while a WAV file is dragged over.
    if (dropRow_ >= 0) {
        const int y = kTop + dropRow_ * kRowHeight;
        const QRectF row(kNameX - 2, y + 1, padRect(0, stepCount() - 1).right() - kNameX + 4, kRowHeight - 2);
        QPen pen(c.accent, 1.5);
        if (static_cast<std::size_t>(dropRow_) >= channelCount()) {
            pen.setStyle(Qt::DashLine);
            p.setPen(c.textDim);
            p.drawText(row.adjusted(10, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, tr("New channel"));
        }
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(row, 5, 5);
    }
}

std::size_t ChannelRackView::dropRowAt(int y) const
{
    if (y < kTop)
        return 0;
    return std::min(static_cast<std::size_t>((y - kTop) / kRowHeight), channelCount());
}

void ChannelRackView::setDropRow(int row)
{
    if (row == dropRow_)
        return;
    dropRow_ = row;
    update();
}

void ChannelRackView::dragEnterEvent(QDragEnterEvent* event)
{
    if (project_ == nullptr || droppedWav(event->mimeData()).isEmpty())
        return; // ignored: the cursor shows it cannot drop
    event->acceptProposedAction();
    setDropRow(static_cast<int>(dropRowAt(event->position().toPoint().y())));
}

void ChannelRackView::dragMoveEvent(QDragMoveEvent* event)
{
    event->acceptProposedAction();
    setDropRow(static_cast<int>(dropRowAt(event->position().toPoint().y())));
}

void ChannelRackView::dragLeaveEvent(QDragLeaveEvent* /*event*/) { setDropRow(-1); }

void ChannelRackView::dropEvent(QDropEvent* event)
{
    const QString path = droppedWav(event->mimeData());
    const std::size_t row = dropRowAt(event->position().toPoint().y());
    setDropRow(-1);
    if (path.isEmpty())
        return;
    event->acceptProposedAction();
    emit sampleDropped(row, path);
}

bool ChannelRackView::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip && project_ != nullptr) {
        auto* help = static_cast<QHelpEvent*>(event);
        const Hit hit = hitAt(help->pos());
        if (hit.part == Part::Volume) {
            const double volume = project_->channels()[hit.channel].volume;
            QToolTip::showText(help->globalPos(),
                tr("Volume %1\nDrag or scroll; Shift: fine; double-click: 0 dB")
                    .arg(QString::fromStdString(formatDb(faderToGain(volume)))),
                this);
            return true;
        }
        if (hit.part == Part::Pan) {
            QToolTip::showText(help->globalPos(),
                tr("Pan %1\nDrag or scroll; Shift: fine; double-click: centre")
                    .arg(panText(project_->channels()[hit.channel].pan)),
                this);
            return true;
        }
        if (hit.part == Part::Insert) {
            QToolTip::showText(help->globalPos(),
                tr("Plays into: %1\nClick to choose a mixer insert, or scroll")
                    .arg(insertLabel(project_->channels()[hit.channel].insert)),
                this);
            return true;
        }
        if (hit.part == Part::AddBar) {
            QToolTip::showText(help->globalPos(),
                tr("Add a bar: %1 step pads more for every channel").arg(kStepsPerBar), this);
            return true;
        }
        if (hit.part == Part::Name) {
            const std::string& path = project_->channels()[hit.channel].samplePath;
            QToolTip::showText(help->globalPos(),
                path.empty() ? tr("No sample: drop a WAV file here")
                             : QDir::toNativeSeparators(QString::fromStdString(path)),
                this);
            return true;
        }
        QToolTip::hideText();
        event->ignore();
        return true;
    }
    return QWidget::event(event);
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
    if (event->button() == Qt::LeftButton && hit.part == Part::AddBar) {
        emit addBarRequested();
        return;
    }
    if (event->button() == Qt::LeftButton && (hit.part == Part::Volume || hit.part == Part::Pan)) {
        draggedKnob_ = hit.part;
        knobChannel_ = hit.channel;
        const Channel& channel = project_->channels()[hit.channel];
        knobAtPress_ = hit.part == Part::Volume ? channel.volume : channel.pan;
        pressY_ = event->position().y();
        return;
    }
    if (event->button() == Qt::LeftButton && hit.part == Part::Insert) {
        chooseInsert(hit.channel, event->position().toPoint());
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
    if (draggedKnob_ != Part::None) {
        const bool pan = draggedKnob_ == Part::Pan;
        double perPixel = pan ? kPanPerPixel : kVolumePerPixel;
        if (event->modifiers().testFlag(Qt::ShiftModifier))
            perPixel /= 5;
        const double value = knobAtPress_ + (pressY_ - event->position().y()) * perPixel;
        if (pan)
            emit panChanged(knobChannel_, std::clamp(value, -1.0, 1.0));
        else
            emit volumeChanged(knobChannel_, std::clamp(value, 0.0, 1.0));
        return;
    }
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
    draggedKnob_ = Part::None;
    painting_ = false;
    lastPaintedStep_ = -1;
}

void ChannelRackView::mouseDoubleClickEvent(QMouseEvent* event)
{
    const Hit hit = hitAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && hit.part == Part::Name)
        emit channelOpened(hit.channel);
    else if (event->button() == Qt::LeftButton && hit.part == Part::Volume)
        emit volumeChanged(hit.channel, kUnityFaderPosition);
    else if (event->button() == Qt::LeftButton && hit.part == Part::Pan)
        emit panChanged(hit.channel, 0.0);
    else
        mousePressEvent(event); // fast clicks on pads still toggle
}

void ChannelRackView::wheelEvent(QWheelEvent* event)
{
    const Hit hit = hitAt(event->position().toPoint());
    const int notches = event->angleDelta().y() / 120;
    if (project_ == nullptr || notches == 0
        || (hit.part != Part::Volume && hit.part != Part::Pan && hit.part != Part::Insert)) {
        event->ignore(); // let the rack scroll
        return;
    }
    const Channel& channel = project_->channels()[hit.channel];
    if (hit.part == Part::Volume) {
        emit volumeChanged(hit.channel, std::clamp(channel.volume + 0.02 * notches, 0.0, 1.0));
    } else if (hit.part == Part::Pan) {
        emit panChanged(hit.channel, std::clamp(channel.pan + 0.05 * notches, -1.0, 1.0));
    } else {
        const int last = static_cast<int>(insertNames_.size()) - 1;
        emit insertChanged(hit.channel, std::clamp(channel.insert + notches, 0, std::max(last, 0)));
    }
    event->accept();
}

void ChannelRackView::setInsertNames(const QStringList& names)
{
    insertNames_ = names.isEmpty() ? QStringList{QStringLiteral("Master")} : names;
    update();
}

QString ChannelRackView::insertLabel(int insert) const
{
    if (insert >= 0 && insert < insertNames_.size())
        return insertNames_[insert];
    return tr("Insert %1").arg(insert);
}

void ChannelRackView::chooseInsert(std::size_t channel, const QPoint& at)
{
    QMenu menu(this);
    const int current = project_->channels()[channel].insert;
    for (int i = 0; i < insertNames_.size(); ++i) {
        QAction* action
            = menu.addAction(insertNames_[i], this, [this, channel, i] { emit insertChanged(channel, i); });
        action->setCheckable(true);
        action->setChecked(i == current);
        if (i == 0)
            menu.addSeparator();
    }
    menu.exec(mapToGlobal(at));
}

} // namespace vloomdaw
