// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/playlist/PlaylistView.hpp"

#include "ui/Theme.hpp"
#include "ui/widgets/Timeline/Timeline.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace vloomdaw {

namespace {

constexpr int kHeaderWidth = 110;
constexpr int kTrackHeight = 36;
constexpr int kMinSongBars = 32;
constexpr double kMinPixelsPerBar = 16.0;
constexpr double kMaxPixelsPerBar = 400.0;

int snapNearest(double tick, int grid) { return static_cast<int>(std::lround(tick / grid)) * grid; }
int snapDown(double tick, int grid) { return static_cast<int>(std::floor(tick / grid)) * grid; }

} // namespace

PlaylistView::PlaylistView(QWidget* parent)
    : QAbstractScrollArea(parent)
{
    setObjectName(QStringLiteral("playlistView"));
    setFrameShape(QFrame::NoFrame);
    viewport()->setMouseTracking(true);
    // paintEvent() covers every pixel it is asked for: skip the background.
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    horizontalScrollBar()->setSingleStep(32);
    verticalScrollBar()->setSingleStep(kTrackHeight);
}

void PlaylistView::setProject(const Project* project)
{
    project_ = project;
    refresh();
}

void PlaylistView::setCurrentPattern(std::size_t pattern) { currentPattern_ = pattern; }

void PlaylistView::setPlayhead(double tick)
{
    if (tick == playhead_)
        return;
    // Repaint only the strips under the old and new line, and only when the
    // line moves (see PianoRollView::setPlayhead()).
    const timeline::Scale scale{
        horizontalScrollBar()->value() / pixelsPerTick(), pixelsPerTick(), kHeaderWidth};
    const QRect area(0, 0, viewport()->width(), viewport()->height());
    if (!timeline::playheadMoved(area, scale, playhead_, tick)) {
        playhead_ = tick;
        return;
    }
    const QRect before = timeline::playheadRect(area, scale, playhead_);
    playhead_ = tick;
    viewport()->update(before);
    viewport()->update(timeline::playheadRect(area, scale, playhead_));
}

void PlaylistView::refresh()
{
    updateScrollBars();
    viewport()->update();
}

double PlaylistView::pixelsPerTick() const { return pixelsPerBar_ / kTicksPerBar; }

int PlaylistView::lengthTicks() const
{
    int end = kMinSongBars * kTicksPerBar;
    if (project_ != nullptr)
        for (const Clip& clip : project_->clips())
            end = std::max(end, clip.start + project_->clipLength(clip) + 8 * kTicksPerBar);
    return end;
}

QRect PlaylistView::gridRect() const
{
    const QRect v = viewport()->rect();
    return {
        kHeaderWidth, timeline::kRulerHeight, v.width() - kHeaderWidth, v.height() - timeline::kRulerHeight};
}

double PlaylistView::tickAt(int x) const
{
    return (x - kHeaderWidth + horizontalScrollBar()->value()) / pixelsPerTick();
}

int PlaylistView::trackAt(int y) const
{
    return (y - timeline::kRulerHeight + verticalScrollBar()->value()) / kTrackHeight;
}

int PlaylistView::yOfTrack(int track) const
{
    return timeline::kRulerHeight + track * kTrackHeight - verticalScrollBar()->value();
}

QRectF PlaylistView::clipRect(const Clip& clip) const
{
    const double x = kHeaderWidth + clip.start * pixelsPerTick() - horizontalScrollBar()->value();
    const double w = project_ != nullptr ? project_->clipLength(clip) * pixelsPerTick() : pixelsPerBar_;
    return {x, static_cast<double>(yOfTrack(clip.track)), w, static_cast<double>(kTrackHeight)};
}

std::optional<std::size_t> PlaylistView::clipAt(const QPoint& pos) const
{
    if (project_ == nullptr || !gridRect().contains(pos))
        return std::nullopt;
    const auto& clips = project_->clips();
    for (std::size_t i = clips.size(); i-- > 0;)
        if (clipRect(clips[i]).contains(pos))
            return i;
    return std::nullopt;
}

void PlaylistView::updateScrollBars()
{
    const QRect grid = gridRect();
    horizontalScrollBar()->setPageStep(grid.width());
    horizontalScrollBar()->setRange(
        0, std::max(0, static_cast<int>(lengthTicks() * pixelsPerTick()) - grid.width()));
    verticalScrollBar()->setPageStep(grid.height());
    verticalScrollBar()->setRange(0, std::max(0, Project::kPlaylistTracks * kTrackHeight - grid.height()));
}

void PlaylistView::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}

void PlaylistView::scrollContentsBy(int /*dx*/, int /*dy*/) { viewport()->update(); }

void PlaylistView::paintEvent(QPaintEvent* event)
{
    const Theme::Colors& c = Theme::colors();
    QPainter p(viewport());
    const QRect grid = gridRect();
    const timeline::Scale scale{
        horizontalScrollBar()->value() / pixelsPerTick(), pixelsPerTick(), kHeaderWidth};

    // Only what Qt asked for: a moving playhead repaints a thin strip.
    const QRect area = grid.intersected(event->rect());

    // Track lanes, alternately shaded (below the last track: the darker shade).
    p.fillRect(area, c.rail);
    QColor trackLine = c.border;
    trackLine.setAlpha(110);
    for (int t = 0; t < Project::kPlaylistTracks && !area.isEmpty(); ++t) {
        const QRect lane = QRect(area.left(), yOfTrack(t), area.width(), kTrackHeight).intersected(area);
        if (lane.isEmpty())
            continue;
        if (t % 2 == 1)
            p.fillRect(lane, c.sidebar);
        // The alternate shading already separates tracks; the line only
        // sharpens the edge, so keep it faint.
        const int lineY = yOfTrack(t) + kTrackHeight - 1;
        if (lineY >= area.top() && lineY <= area.bottom())
            p.fillRect(QRect(area.left(), lineY, area.width(), 1), trackLine);
    }
    timeline::paintGrid(p, area, scale, kTicksPerBeat, timeline::GridStyle::Calm);

    // Clips: a coloured header with the pattern name over a tinted body.
    if (project_ != nullptr && !area.isEmpty()) {
        p.save();
        p.setClipRect(area);
        p.setRenderHint(QPainter::Antialiasing);
        const auto& clips = project_->clips();
        for (std::size_t i = 0; i < clips.size(); ++i) {
            const QRectF r = clipRect(clips[i]).adjusted(1.0, 2.0, -1.0, -2.0);
            if (!r.intersects(area))
                continue;
            QColor color = Theme::patternColor(clips[i].pattern);
            QColor body = color;
            body.setAlpha(dragging_ && i == dragIndex_ ? 140 : 90);
            p.setPen(QPen(color, 1));
            p.setBrush(body);
            p.drawRoundedRect(r, 3, 3);
            const QRectF title(r.left(), r.top(), r.width(), 13);
            p.fillRect(title.adjusted(1, 1, -1, 0), color);
            p.setPen(c.base);
            QFont f = font();
            f.setPointSizeF(f.pointSizeF() > 0 ? f.pointSizeF() * 0.75 : 7.0);
            f.setBold(true);
            p.setFont(f);
            const auto& name = project_->patterns()[clips[i].pattern].name;
            p.drawText(
                title.adjusted(4, 0, -2, 0), Qt::AlignLeft | Qt::AlignVCenter, QString::fromStdString(name));
        }
        p.restore();
    }

    timeline::paintPlayhead(p, grid, scale, playhead_);
    const QRect ruler(kHeaderWidth, 0, grid.width(), timeline::kRulerHeight);
    if (event->rect().intersects(ruler))
        timeline::paintRuler(p, ruler, scale);

    // Track headers (skipped when only the grid needs repainting).
    const QRect headers(0, timeline::kRulerHeight, kHeaderWidth, grid.height());
    if (!event->rect().intersects(QRect(0, 0, kHeaderWidth, viewport()->height())))
        return;
    p.save();
    p.setClipRect(headers);
    p.fillRect(headers, c.panel);
    for (int t = 0; t < Project::kPlaylistTracks; ++t) {
        const QRect h(0, yOfTrack(t), kHeaderWidth, kTrackHeight);
        p.setPen(c.text);
        p.drawText(h.adjusted(10, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, tr("Track %1").arg(t + 1));
        p.fillRect(QRect(0, h.bottom(), kHeaderWidth, 1), c.border);
    }
    p.restore();
    p.fillRect(QRect(kHeaderWidth - 1, headers.top(), 1, headers.height()), c.border);
    p.fillRect(QRect(0, 0, kHeaderWidth, timeline::kRulerHeight), c.panel);
}

void PlaylistView::beginDrag(std::size_t index, const QPoint& pos)
{
    if (project_ == nullptr || index >= project_->clips().size())
        return;
    dragging_ = true;
    dragIndex_ = index;
    dragStartClip_ = project_->clips()[index];
    dragStartTick_ = tickAt(pos.x());
    dragStartTrack_ = trackAt(pos.y());
    viewport()->setCursor(Qt::ClosedHandCursor);
}

void PlaylistView::mousePressEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    if (project_ == nullptr || !gridRect().contains(pos))
        return;
    const auto hit = clipAt(pos);
    if (event->button() == Qt::RightButton) {
        if (hit)
            emit clipRemoved(*hit);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    if (hit) {
        beginDrag(*hit, pos);
        return;
    }
    const int track = trackAt(pos.y());
    if (track < 0 || track >= Project::kPlaylistTracks)
        return;
    emit clipAdded(Clip{currentPattern_, track, snapDown(tickAt(pos.x()), kTicksPerBar)});
    if (!project_->clips().empty())
        beginDrag(project_->clips().size() - 1, pos);
}

void PlaylistView::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    if (!dragging_) {
        viewport()->setCursor(clipAt(pos) ? Qt::OpenHandCursor : Qt::ArrowCursor);
        return;
    }
    const int grid = event->modifiers().testFlag(Qt::ShiftModifier) ? kTicksPerBeat : kTicksPerBar;
    Clip clip = dragStartClip_;
    clip.start = snapNearest(dragStartClip_.start + (tickAt(pos.x()) - dragStartTick_), grid);
    clip.track = dragStartClip_.track + (trackAt(pos.y()) - dragStartTrack_);
    emit clipChanged(dragIndex_, clip);
}

void PlaylistView::mouseReleaseEvent(QMouseEvent* /*event*/)
{
    if (!dragging_)
        return;
    dragging_ = false;
    viewport()->unsetCursor();
    refresh(); // the song may have grown
}

void PlaylistView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        if (const auto hit = clipAt(event->position().toPoint()); hit && project_ != nullptr) {
            emit patternPicked(project_->clips()[*hit].pattern);
            return;
        }
    QAbstractScrollArea::mouseDoubleClickEvent(event);
}

void PlaylistView::wheelEvent(QWheelEvent* event)
{
    if (!event->modifiers().testFlag(Qt::ControlModifier)) {
        QAbstractScrollArea::wheelEvent(event);
        return;
    }
    const int notches = event->angleDelta().y() / 120;
    if (notches == 0)
        return;
    const int mouseX = static_cast<int>(event->position().x());
    const double anchor = tickAt(mouseX);
    pixelsPerBar_ = std::clamp(pixelsPerBar_ * std::pow(1.25, notches), kMinPixelsPerBar, kMaxPixelsPerBar);
    updateScrollBars();
    horizontalScrollBar()->setValue(static_cast<int>(anchor * pixelsPerTick()) - (mouseX - kHeaderWidth));
    viewport()->update();
    event->accept();
}

} // namespace vloomdaw
