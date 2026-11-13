// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/playlist/PlaylistView.hpp"

#include "ui/DroppedFiles.hpp"
#include "ui/Theme.hpp"
#include "ui/widgets/Timeline/Timeline.hpp"

#include <QDragEnterEvent>
#include <QFileInfo>
#include <QHelpEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolTip>
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
    setAcceptDrops(true); // WAV files from the browser or the desktop
    viewport()->setAcceptDrops(true);
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

void PlaylistView::setAudioInfo(std::function<const ClipAudio*(const std::string& path)> info)
{
    audioInfo_ = std::move(info);
    refresh();
}

void PlaylistView::setTempo(double bpm)
{
    if (!(bpm > 0.0) || bpm == tempo_)
        return;
    tempo_ = bpm;
    if (project_ != nullptr && !project_->audioClips().empty())
        refresh(); // audio clips change length in bars
}

const ClipAudio* PlaylistView::audioOf(const AudioClip& clip) const
{
    if (!audioInfo_ || project_ == nullptr || clip.channel >= project_->channels().size())
        return nullptr;
    return audioInfo_(project_->channels()[clip.channel].samplePath);
}

int PlaylistView::audioLength(const AudioClip& clip) const
{
    const ClipAudio* info = audioOf(clip);
    if (info == nullptr || !(info->seconds > 0.0))
        return kTicksPerBar;
    const double ticks = info->seconds * tempo_ / 60.0 * kTicksPerBeat;
    return std::max(1, static_cast<int>(std::min(ticks, 1e9)));
}

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
    if (project_ != nullptr) {
        for (const Clip& clip : project_->clips())
            end = std::max(end, clip.start + project_->clipLength(clip) + 8 * kTicksPerBar);
        for (const AudioClip& clip : project_->audioClips())
            end = std::max(end, clip.start + audioLength(clip) + 8 * kTicksPerBar);
    }
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

QRectF PlaylistView::clipRect(const AudioClip& clip) const
{
    const double x = kHeaderWidth + clip.start * pixelsPerTick() - horizontalScrollBar()->value();
    return {x, static_cast<double>(yOfTrack(clip.track)), audioLength(clip) * pixelsPerTick(),
        static_cast<double>(kTrackHeight)};
}

std::optional<PlaylistView::ClipRef> PlaylistView::clipAt(const QPoint& pos) const
{
    if (project_ == nullptr || !gridRect().contains(pos))
        return std::nullopt;
    // Audio clips are drawn on top: they are hit first.
    const auto& audio = project_->audioClips();
    for (std::size_t i = audio.size(); i-- > 0;)
        if (clipRect(audio[i]).contains(pos))
            return ClipRef{true, i};
    const auto& clips = project_->clips();
    for (std::size_t i = clips.size(); i-- > 0;)
        if (clipRect(clips[i]).contains(pos))
            return ClipRef{false, i};
    return std::nullopt;
}

void PlaylistView::paintAudioClip(QPainter& p, const AudioClip& clip, bool dragged) const
{
    const Theme::Colors& c = Theme::colors();
    const QRectF r = clipRect(clip).adjusted(1.0, 2.0, -1.0, -2.0);
    QColor color = c.accent;
    QColor body = color;
    body.setAlpha(dragged ? 110 : 60);
    p.setPen(QPen(color, 1));
    p.setBrush(body);
    p.drawRoundedRect(r, 3, 3);

    // Waveform: one line per pixel column, the peak of the time it covers.
    const ClipAudio* info = audioOf(clip);
    const QRectF wave = r.adjusted(2, 14, -2, -2);
    if (info != nullptr && !info->peaks.empty() && info->peakSeconds > 0.0 && wave.height() > 2) {
        const double secondsPerPixel = 60.0 / (tempo_ * kTicksPerBeat) / pixelsPerTick();
        const QRectF visible = wave.intersected(QRectF(viewport()->rect()));
        const double mid = wave.center().y();
        const double half = wave.height() / 2.0;
        QColor line = color;
        line.setAlpha(200);
        p.setPen(QPen(line, 1));
        for (int x = static_cast<int>(visible.left()); x < static_cast<int>(visible.right()); ++x) {
            const double from = (x - r.left()) * secondsPerPixel;
            const auto first = static_cast<std::size_t>(std::max(0.0, from / info->peakSeconds));
            const auto last
                = static_cast<std::size_t>(std::max(0.0, (from + secondsPerPixel) / info->peakSeconds));
            float peak = 0.0f;
            for (std::size_t i = first; i <= last && i < info->peaks.size(); ++i)
                peak = std::max(peak, info->peaks[i]);
            const double h = std::min(1.0f, peak) * half;
            if (h >= 0.5)
                p.drawLine(QPointF(x + 0.5, mid - h), QPointF(x + 0.5, mid + h));
        }
    }

    const QRectF title(r.left(), r.top(), r.width(), 13);
    p.fillRect(title.adjusted(1, 1, -1, 0), color);
    p.setPen(c.base);
    QFont f = p.font();
    f.setPointSizeF(f.pointSizeF() > 0 ? f.pointSizeF() * 0.75 : 7.0);
    f.setBold(true);
    p.setFont(f);
    const QString name = clip.channel < project_->channels().size()
        ? QString::fromStdString(project_->channels()[clip.channel].name)
        : QString();
    p.drawText(title.adjusted(4, 0, -2, 0), Qt::AlignLeft | Qt::AlignVCenter,
        p.fontMetrics().elidedText(name, Qt::ElideRight, static_cast<int>(title.width()) - 6));
}

std::optional<std::pair<int, int>> PlaylistView::dropTarget(const QPoint& pos) const
{
    if (project_ == nullptr || !gridRect().contains(pos))
        return std::nullopt;
    const int track = std::min(trackAt(pos.y()), trackCount()); // below the last: a new track
    return std::pair{track, std::max(0, snapDown(tickAt(pos.x()), kTicksPerBar))};
}

void PlaylistView::dragEnterEvent(QDragEnterEvent* event)
{
    if (project_ != nullptr && !droppedWav(event->mimeData()).isEmpty())
        event->acceptProposedAction();
    else
        event->ignore();
}

void PlaylistView::dragMoveEvent(QDragMoveEvent* event)
{
    const auto target
        = droppedWav(event->mimeData()).isEmpty() ? std::nullopt : dropTarget(event->position().toPoint());
    if (!target) {
        event->ignore();
        if (dropHint_) {
            dropHint_.reset();
            viewport()->update();
        }
        return;
    }
    event->acceptProposedAction();
    const double x = kHeaderWidth + target->second * pixelsPerTick() - horizontalScrollBar()->value();
    dropHint_ = QRectF(x, yOfTrack(target->first), pixelsPerBar_, kTrackHeight);
    viewport()->update();
}

void PlaylistView::dragLeaveEvent(QDragLeaveEvent* /*event*/)
{
    dropHint_.reset();
    viewport()->update();
}

void PlaylistView::dropEvent(QDropEvent* event)
{
    dropHint_.reset();
    viewport()->update();
    const QString path = droppedWav(event->mimeData());
    const auto target = dropTarget(event->position().toPoint());
    if (path.isEmpty() || !target) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    emit audioDropped(path, target->first, target->second);
}

void PlaylistView::updateScrollBars()
{
    const QRect grid = gridRect();
    horizontalScrollBar()->setPageStep(grid.width());
    horizontalScrollBar()->setRange(
        0, std::max(0, static_cast<int>(lengthTicks() * pixelsPerTick()) - grid.width()));
    verticalScrollBar()->setPageStep(grid.height());
    verticalScrollBar()->setRange(0, std::max(0, trackCount() * kTrackHeight - grid.height()));
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
    for (int t = 0; t < trackCount() && !area.isEmpty(); ++t) {
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
            body.setAlpha(dragging_ && !dragClip_.audio && i == dragClip_.index ? 140 : 90);
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
        const auto& audio = project_->audioClips();
        for (std::size_t i = 0; i < audio.size(); ++i)
            if (clipRect(audio[i]).intersects(area))
                paintAudioClip(p, audio[i], dragging_ && dragClip_.audio && i == dragClip_.index);
        if (dropHint_) {
            QPen pen(c.accent, 1.5, Qt::DashLine);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(dropHint_->adjusted(1, 2, -1, -2), 3, 3);
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
    for (int t = 0; t < trackCount(); ++t) {
        const QRect h(0, yOfTrack(t), kHeaderWidth, kTrackHeight);
        if (!h.intersects(headers))
            continue;
        const QRect text = h.adjusted(10, 0, -6, 0);
        p.setPen(c.text);
        p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft,
            p.fontMetrics().elidedText(
                QString::fromStdString(project_->tracks()[static_cast<std::size_t>(t)].name), Qt::ElideRight,
                text.width()));
        p.fillRect(QRect(0, h.bottom(), kHeaderWidth, 1), c.border);
    }
    p.restore();
    p.fillRect(QRect(kHeaderWidth - 1, headers.top(), 1, headers.height()), c.border);
    p.fillRect(QRect(0, 0, kHeaderWidth, timeline::kRulerHeight), c.panel);
}

void PlaylistView::beginDrag(ClipRef clip, const QPoint& pos)
{
    if (project_ == nullptr
        || clip.index >= (clip.audio ? project_->audioClips().size() : project_->clips().size()))
        return;
    dragging_ = true;
    dragClip_ = clip;
    if (clip.audio)
        dragStartAudio_ = project_->audioClips()[clip.index];
    else
        dragStartClip_ = project_->clips()[clip.index];
    dragStartTick_ = tickAt(pos.x());
    dragStartTrack_ = trackAt(pos.y());
    viewport()->setCursor(Qt::ClosedHandCursor);
}

bool PlaylistView::viewportEvent(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        if (const int track = headerTrackAt(help->pos()); track >= 0) {
            QToolTip::showText(help->globalPos(),
                tr("%1\nDouble-click to rename; right-click for more")
                    .arg(QString::fromStdString(project_->tracks()[static_cast<std::size_t>(track)].name)),
                viewport());
            return true;
        }
        QToolTip::hideText();
        return true;
    }
    return QAbstractScrollArea::viewportEvent(event);
}

int PlaylistView::trackCount() const
{
    return project_ != nullptr ? static_cast<int>(project_->tracks().size()) : 0;
}

int PlaylistView::headerTrackAt(const QPoint& pos) const
{
    if (pos.x() < 0 || pos.x() >= kHeaderWidth || pos.y() < timeline::kRulerHeight)
        return -1;
    const int track = trackAt(pos.y());
    return track >= 0 && track < trackCount() ? track : -1;
}

void PlaylistView::mousePressEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    if (const int track = headerTrackAt(pos); track >= 0 && event->button() == Qt::RightButton) {
        QMenu menu(this);
        const auto t = static_cast<std::size_t>(track);
        menu.addAction(tr("Rename…"), this, [this, t] { emit trackRenameRequested(t); });
        menu.addSeparator();
        menu.addAction(tr("Move Up"), this, [this, t] { emit trackMoveRequested(t, t - 1); })
            ->setEnabled(t > 0);
        menu.addAction(tr("Move Down"), this, [this, t] { emit trackMoveRequested(t, t + 1); })
            ->setEnabled(static_cast<int>(t) + 1 < trackCount());
        menu.addSeparator();
        QAction* remove
            = menu.addAction(tr("Delete “%1”").arg(QString::fromStdString(project_->tracks()[t].name)), this,
                [this, t] { emit trackRemoveRequested(t); });
        remove->setEnabled(trackCount() > 1);
        menu.exec(event->globalPosition().toPoint());
        return;
    }
    if (project_ == nullptr || !gridRect().contains(pos))
        return;
    const auto hit = clipAt(pos);
    if (event->button() == Qt::RightButton) {
        if (hit && hit->audio)
            emit audioClipRemoved(hit->index);
        else if (hit)
            emit clipRemoved(hit->index);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    if (hit) {
        beginDrag(*hit, pos);
        return;
    }
    const int track = trackAt(pos.y());
    if (track < 0 || track >= trackCount())
        return;
    emit clipAdded(Clip{currentPattern_, track, snapDown(tickAt(pos.x()), kTicksPerBar)});
    if (!project_->clips().empty())
        beginDrag(ClipRef{false, project_->clips().size() - 1}, pos);
}

void PlaylistView::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    if (!dragging_) {
        viewport()->setCursor(clipAt(pos) ? Qt::OpenHandCursor : Qt::ArrowCursor);
        return;
    }
    const int grid = event->modifiers().testFlag(Qt::ShiftModifier) ? kTicksPerBeat : kTicksPerBar;
    const double moved = tickAt(pos.x()) - dragStartTick_;
    const int tracks = trackAt(pos.y()) - dragStartTrack_;
    if (dragClip_.audio) {
        AudioClip clip = dragStartAudio_;
        clip.start = snapNearest(dragStartAudio_.start + moved, grid);
        clip.track = dragStartAudio_.track + tracks;
        emit audioClipChanged(dragClip_.index, clip);
        return;
    }
    Clip clip = dragStartClip_;
    clip.start = snapNearest(dragStartClip_.start + moved, grid);
    clip.track = dragStartClip_.track + tracks;
    emit clipChanged(dragClip_.index, clip);
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
    if (const int track = headerTrackAt(event->position().toPoint());
        track >= 0 && event->button() == Qt::LeftButton) {
        emit trackRenameRequested(static_cast<std::size_t>(track));
        return;
    }
    if (event->button() == Qt::LeftButton)
        if (const auto hit = clipAt(event->position().toPoint()); hit && !hit->audio && project_ != nullptr) {
            emit patternPicked(project_->clips()[hit->index].pattern);
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
