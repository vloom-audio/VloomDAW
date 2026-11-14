// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/playlist/PlaylistView.hpp"

#include "ui/DroppedFiles.hpp"
#include "ui/Theme.hpp"
#include "ui/widgets/ColorMenu/ColorMenu.hpp"
#include "ui/widgets/Timeline/Timeline.hpp"

#include <QDragEnterEvent>
#include <QFileInfo>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QScrollBar>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vloomdaw {

namespace {

constexpr int kHeaderWidth = 170;
constexpr int kTrackButton = 16; ///< mute / solo button size
constexpr int kTrackButtonGap = 3;
constexpr int kIndent = 12; ///< per sub-track level, in the header
constexpr int kArrowWidth = 12; ///< the collapse arrow
constexpr int kResizeGrip = 4; ///< bottom strip of a header that resizes the track
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
    setFocusPolicy(Qt::ClickFocus); // Delete, Ctrl+A and Escape for the selection
    verticalScrollBar()->setSingleStep(PlaylistTrack::kDefaultHeight);

    inlineEditor_ = new QLineEdit(viewport());
    inlineEditor_->hide();
    inlineEditor_->setObjectName(QStringLiteral("playlistTrackNameEditor"));
    inlineEditor_->setMaxLength(static_cast<int>(Project::kMaxTrackNameLength));
    inlineEditor_->installEventFilter(this);
    connect(inlineEditor_, &QLineEdit::editingFinished, this, &PlaylistView::commitTrackNameEdit);
}

void PlaylistView::setProject(const Project* project)
{
    project_ = project;
    refresh();
}

void PlaylistView::setCurrentPattern(std::size_t pattern) { source_ = ClipSource{false, pattern}; }

QColor PlaylistView::trackColorOr(int track, const QColor& fallback) const
{
    if (project_ == nullptr || track < 0 || static_cast<std::size_t>(track) >= project_->tracks().size())
        return fallback;
    const auto& color = project_->tracks()[static_cast<std::size_t>(track)].color;
    return color ? toQColor(*color) : fallback;
}

bool PlaylistView::placeSource(int track, int start)
{
    if (project_ == nullptr)
        return false;
    if (!source_.audio) {
        if (source_.index >= project_->patterns().size())
            return false;
        emit clipAdded(Clip{source_.index, track, start});
        return true;
    }
    if (source_.index >= project_->channels().size()
        || project_->channels()[source_.index].samplePath.empty())
        return false; // the channel is gone, or has no sample to place
    emit audioClipAdded(AudioClip{source_.index, track, start});
    return true;
}

int PlaylistView::sourceLength() const
{
    if (project_ == nullptr)
        return kTicksPerBar;
    if (source_.audio)
        return std::max(kTicksPerStep, audioLength(AudioClip{source_.index, 0, 0}));
    return std::max(kTicksPerStep, project_->patternLength(source_.index));
}

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

int PlaylistView::sampleTicks(const AudioClip& clip) const
{
    const ClipAudio* info = audioOf(clip);
    if (info == nullptr || !(info->seconds > 0.0))
        return 0;
    // A clip fitted to the song's tempo lasts its own beats whatever the tempo.
    const double ticks = info->seconds * clipTempo(clip) / 60.0 * kTicksPerBeat;
    return std::max(1, static_cast<int>(std::min(ticks, 1e9)));
}

int PlaylistView::audioLength(const AudioClip& clip) const
{
    // The part of the sample the clip plays: from its offset, for its length
    // when it has one, never past the sample's end.
    const int whole = sampleTicks(clip);
    if (whole == 0)
        return clip.length > 0 ? clip.length : kTicksPerBar; // unknown sample
    const int left = std::max(1, whole - clip.offset);
    return clip.length > 0 ? std::min(clip.length, left) : left;
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
    syncLayout();
    if (editingTrack_ >= 0 && inlineEditor_ != nullptr && inlineEditor_->isVisible())
        inlineEditor_->setGeometry(trackNameRect(editingTrack_));
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

void PlaylistView::syncLayout()
{
    syncSelection();
    const int count = trackCount();
    tops_.assign(static_cast<std::size_t>(count) + 1, 0);
    for (int t = 0; t < count; ++t) {
        const auto i = static_cast<std::size_t>(t);
        tops_[i + 1] = tops_[i] + (project_->trackVisible(i) ? project_->tracks()[i].height : 0);
    }
}

int PlaylistView::trackAt(int y) const
{
    const int content = y - timeline::kRulerHeight + verticalScrollBar()->value();
    if (content < 0)
        return -1;
    // The last track starting at or above `content`; hidden tracks share
    // their top with the next shown one, which comes later and wins. Below
    // the last track: the track count.
    const auto it = std::upper_bound(tops_.begin(), tops_.end(), content);
    return static_cast<int>(it - tops_.begin()) - 1;
}

int PlaylistView::yOfTrack(int track) const
{
    const int last = static_cast<int>(tops_.size()) - 1;
    const int top = track <= last ? tops_[static_cast<std::size_t>(std::max(0, track))]
                                  : tops_.back() + (track - last) * PlaylistTrack::kDefaultHeight;
    return timeline::kRulerHeight + top - verticalScrollBar()->value();
}

int PlaylistView::trackHeight(int track) const
{
    if (track < 0 || track + 1 >= static_cast<int>(tops_.size()))
        return PlaylistTrack::kDefaultHeight; // below the last: a new track
    const auto i = static_cast<std::size_t>(track);
    return tops_[i + 1] - tops_[i];
}

QRectF PlaylistView::clipRect(const Clip& clip) const
{
    const double x = kHeaderWidth + clip.start * pixelsPerTick() - horizontalScrollBar()->value();
    const double w = project_ != nullptr ? project_->clipLength(clip) * pixelsPerTick() : pixelsPerBar_;
    return {x, static_cast<double>(yOfTrack(clip.track)), w, static_cast<double>(trackHeight(clip.track))};
}

QRectF PlaylistView::clipRect(const AudioClip& clip) const
{
    const double x = kHeaderWidth + clip.start * pixelsPerTick() - horizontalScrollBar()->value();
    return {x, static_cast<double>(yOfTrack(clip.track)), audioLength(clip) * pixelsPerTick(),
        static_cast<double>(trackHeight(clip.track))};
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

void PlaylistView::paintAudioClip(QPainter& p, const AudioClip& clip, bool dragged, bool selected) const
{
    const Theme::Colors& c = Theme::colors();
    const QRectF r = clipRect(clip).adjusted(1.0, 2.0, -1.0, -2.0);
    // A muted clip is grey; otherwise its track's colour when it has one.
    QColor color = clip.muted ? c.textDim : trackColorOr(clip.track, c.accent);
    QColor body = color;
    body.setAlpha(dragged ? 110 : 60);
    p.setPen(QPen(color, 1));
    p.setBrush(body);
    p.drawRoundedRect(r, 3, 3);

    // Waveform: one line per pixel column, the peak of the time it covers.
    const ClipAudio* info = audioOf(clip);
    const QRectF wave = r.adjusted(2, 14, -2, -2);
    if (info != nullptr && !info->peaks.empty() && info->peakSeconds > 0.0 && wave.height() > 2) {
        const double secondsPerPixel = 60.0 / (clipTempo(clip) * kTicksPerBeat) / pixelsPerTick();
        const QRectF visible = wave.intersected(QRectF(viewport()->rect()));
        const double mid = wave.center().y();
        const double half = wave.height() / 2.0;
        QColor line = color;
        line.setAlpha(200);
        p.setPen(QPen(line, 1));
        for (int x = static_cast<int>(visible.left()); x < static_cast<int>(visible.right()); ++x) {
            // The clip shows its sample from its offset (a cut or trimmed clip).
            const double from
                = (x - r.left()) * secondsPerPixel + clip.offset * 60.0 / (clipTempo(clip) * kTicksPerBeat);
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
    QFont f = font(); // the widget's: the painter's may already be the small title font
    f.setPointSizeF(f.pointSizeF() > 0 ? f.pointSizeF() * 0.75 : 7.0);
    f.setBold(true);
    p.setFont(f);
    QString name = clip.channel < project_->channels().size()
        ? QString::fromStdString(project_->channels()[clip.channel].name)
        : QString();
    if (clip.fitted()) // its own tempo, which it is fitted from
        name += QStringLiteral("  ·  %1 BPM")
                    .arg(QString::number(
                        clip.sourceBpm, 'f', clip.sourceBpm == std::round(clip.sourceBpm) ? 0 : 1));
    p.drawText(title.adjusted(4, 0, -2, 0), Qt::AlignLeft | Qt::AlignVCenter,
        p.fontMetrics().elidedText(name, Qt::ElideRight, static_cast<int>(title.width()) - 6));
    if (selected) {
        p.setPen(QPen(c.text, 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    }
}

std::optional<std::pair<int, int>> PlaylistView::dropTarget(const QPoint& pos) const
{
    if (project_ == nullptr || !gridRect().contains(pos))
        return std::nullopt;
    const int track = std::min(trackAt(pos.y()), trackCount()); // below the last: a new track
    return std::pair{track, std::max(0, snapDown(tickAt(pos.x()), snapTicks(Qt::NoModifier)))};
}

void PlaylistView::dragEnterEvent(QDragEnterEvent* event)
{
    if (project_ != nullptr && !droppedAudio(event->mimeData()).isEmpty())
        event->acceptProposedAction();
    else
        event->ignore();
}

void PlaylistView::dragMoveEvent(QDragMoveEvent* event)
{
    syncLayout();
    const auto target
        = droppedAudio(event->mimeData()).isEmpty() ? std::nullopt : dropTarget(event->position().toPoint());
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
    dropHint_ = QRectF(x, yOfTrack(target->first), pixelsPerBar_, trackHeight(target->first));
    viewport()->update();
}

void PlaylistView::dragLeaveEvent(QDragLeaveEvent* /*event*/)
{
    dropHint_.reset();
    viewport()->update();
}

void PlaylistView::dropEvent(QDropEvent* event)
{
    syncLayout();
    dropHint_.reset();
    viewport()->update();
    const QString path = droppedAudio(event->mimeData());
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
    syncLayout();
    const QRect grid = gridRect();
    horizontalScrollBar()->setPageStep(grid.width());
    horizontalScrollBar()->setRange(
        0, std::max(0, static_cast<int>(lengthTicks() * pixelsPerTick()) - grid.width()));
    verticalScrollBar()->setPageStep(grid.height());
    verticalScrollBar()->setRange(0, std::max(0, tops_.back() - grid.height()));
}

void PlaylistView::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars(); // also lays the tracks out
    if (editingTrack_ >= 0 && inlineEditor_->isVisible())
        inlineEditor_->setGeometry(trackNameRect(editingTrack_));
}

void PlaylistView::scrollContentsBy(int /*dx*/, int /*dy*/)
{
    if (editingTrack_ >= 0 && inlineEditor_->isVisible())
        inlineEditor_->setGeometry(trackNameRect(editingTrack_));
    viewport()->update();
}

void PlaylistView::paintEvent(QPaintEvent* event)
{
    syncLayout(); // the project may have changed since the last event
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
    int row = 0; // shown tracks: hidden sub-tracks do not count for the shading
    for (int t = 0; t < trackCount() && !area.isEmpty(); ++t) {
        const int height = trackHeight(t);
        if (height == 0)
            continue;
        const bool odd = row++ % 2 == 1;
        const QRect lane = QRect(area.left(), yOfTrack(t), area.width(), height).intersected(area);
        if (lane.isEmpty())
            continue;
        if (odd)
            p.fillRect(lane, c.sidebar);
        if (const auto& color = project_->tracks()[static_cast<std::size_t>(t)].color) {
            QColor tint = toQColor(*color);
            tint.setAlpha(28); // a hint of the track's colour behind its clips
            p.fillRect(lane, tint);
        }
        // The alternate shading already separates tracks; the line only
        // sharpens the edge, so keep it faint.
        const int lineY = yOfTrack(t) + height - 1;
        if (lineY >= area.top() && lineY <= area.bottom())
            p.fillRect(QRect(area.left(), lineY, area.width(), 1), trackLine);
    }
    // Down to steps when zoomed in: the lines show what clips snap to.
    timeline::paintGrid(p, area, scale, kTicksPerStep, timeline::GridStyle::Calm);

    // Clips: a coloured header with the pattern name over a tinted body.
    if (project_ != nullptr && !area.isEmpty()) {
        p.save();
        p.setClipRect(area);
        p.setRenderHint(QPainter::Antialiasing);
        const auto& clips = project_->clips();
        for (std::size_t i = 0; i < clips.size(); ++i) {
            if (trackHeight(clips[i].track) == 0)
                continue; // a hidden sub-track
            const QRectF r = clipRect(clips[i]).adjusted(1.0, 2.0, -1.0, -2.0);
            if (!r.intersects(area))
                continue;
            p.setOpacity(project_->trackAudible(static_cast<std::size_t>(clips[i].track)) ? 1.0 : 0.35);
            QColor color = clips[i].muted
                ? c.textDim
                : trackColorOr(clips[i].track, Theme::patternColor(clips[i].pattern));
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
            if (isSelected(ClipRef{false, i})) {
                p.setPen(QPen(c.text, 2));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
            }
        }
        const auto& audio = project_->audioClips();
        for (std::size_t i = 0; i < audio.size(); ++i)
            if (trackHeight(audio[i].track) > 0 && clipRect(audio[i]).intersects(area)) {
                p.setOpacity(project_->trackAudible(static_cast<std::size_t>(audio[i].track)) ? 1.0 : 0.35);
                paintAudioClip(p, audio[i], dragging_ && dragClip_.audio && i == dragClip_.index,
                    isSelected(ClipRef{true, i}));
            }
        p.setOpacity(1.0);
        if (dropHint_) {
            QPen pen(c.accent, 1.5, Qt::DashLine);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(dropHint_->adjusted(1, 2, -1, -2), 3, 3);
        }
        if (sliceLine_) {
            // Slice: the cut line over the tracks it reaches.
            const int top = std::min(sliceLine_->fromTrack, sliceLine_->toTrack);
            const int bottom = std::max(sliceLine_->fromTrack, sliceLine_->toTrack);
            const double x
                = kHeaderWidth + sliceLine_->tick * pixelsPerTick() - horizontalScrollBar()->value();
            const double y0 = yOfTrack(top);
            const double y1 = yOfTrack(bottom) + trackHeight(bottom);
            p.setPen(QPen(c.text, 1.5));
            p.drawLine(QPointF(std::floor(x) + 0.5, y0), QPointF(std::floor(x) + 0.5, y1));
            p.setPen(Qt::NoPen);
            p.setBrush(c.text);
            QPolygonF tip; // a small marker at the top, like a blade
            tip << QPointF(x - 4, y0) << QPointF(x + 4, y0) << QPointF(x, y0 + 5);
            p.drawPolygon(tip);
        }
        if (band_) {
            // Select: the box being dragged. Zoom: the range, full height.
            QColor fill = c.accent;
            fill.setAlpha(40);
            const QRect box = tool_ == Tool::Zoom
                ? QRect(band_->left(), grid.top(), band_->width(), grid.height())
                : *band_;
            p.setPen(QPen(c.accent, 1, Qt::DashLine));
            p.setBrush(fill);
            p.drawRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5));
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
    QColor guide = c.border;
    guide.setAlpha(160);
    for (int t = 0; t < trackCount(); ++t) {
        const QRect h(0, yOfTrack(t), kHeaderWidth, trackHeight(t));
        if (h.height() == 0 || !h.intersects(headers))
            continue;
        const PlaylistTrack& track = project_->tracks()[static_cast<std::size_t>(t)];
        if (track.color) // the track's colour as a bar along the left edge
            p.fillRect(QRect(0, h.top(), 3, h.height() - 1), toQColor(*track.color));
        // Sub-tracks: a guide line for each group they are in.
        for (int level = 0; level < track.depth; ++level)
            p.fillRect(QRect(4 + level * kIndent + kArrowWidth / 2, h.top(), 1, h.height()), guide);
        if (project_->hasSubTracks(static_cast<std::size_t>(t))) {
            // Down when the sub-tracks show, right when they are hidden.
            const QRectF a = QRectF(arrowRect(t)).adjusted(2.5, 0, -2.5, 0);
            const double cy = a.center().y();
            const double half = a.width() / 2.0;
            QPolygonF arrow;
            if (track.collapsed)
                arrow << QPointF(a.left() + half * 0.4, cy - half) << QPointF(a.left() + half * 1.6, cy)
                      << QPointF(a.left() + half * 0.4, cy + half);
            else
                arrow << QPointF(a.left(), cy - half * 0.6) << QPointF(a.right(), cy - half * 0.6)
                      << QPointF(a.center().x(), cy + half * 0.6);
            p.save();
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(c.textDim);
            p.drawPolygon(arrow);
            p.restore();
        }
        if (t != editingTrack_ || inlineEditor_ == nullptr || !inlineEditor_->isVisible()) {
            const QRect text = trackNameRect(t);
            p.setPen(project_->trackAudible(static_cast<std::size_t>(t)) ? c.text : c.textDim);
            p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft,
                p.fontMetrics().elidedText(
                    QString::fromStdString(project_->tracks()[static_cast<std::size_t>(t)].name),
                    Qt::ElideRight, text.width()));
        }
        paintTrackButton(p, muteButtonRect(t), QStringLiteral("track-mute"), track.muted, c.accent);
        paintTrackButton(p, soloButtonRect(t), QStringLiteral("track-solo"), track.solo, c.playing);
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
    viewport()->setCursor(Qt::ClosedHandCursor);
}

bool PlaylistView::viewportEvent(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        syncLayout();
        auto* help = static_cast<QHelpEvent*>(event);
        if (const int track = headerTrackAt(help->pos()); track >= 0) {
            if (resizeEdgeAt(help->pos()) >= 0) {
                QToolTip::showText(
                    help->globalPos(), tr("Drag to resize; double-click for the default height"), viewport());
                return true;
            }
            if (project_->hasSubTracks(static_cast<std::size_t>(track))
                && arrowRect(track).contains(help->pos())) {
                QToolTip::showText(help->globalPos(),
                    project_->tracks()[static_cast<std::size_t>(track)].collapsed ? tr("Show sub-tracks")
                                                                                  : tr("Hide sub-tracks"),
                    viewport());
                return true;
            }
            if (muteButtonRect(track).contains(help->pos())) {
                QToolTip::showText(help->globalPos(), tr("Mute this track and its sub-tracks"), viewport());
                return true;
            }
            if (soloButtonRect(track).contains(help->pos())) {
                QToolTip::showText(
                    help->globalPos(), tr("Solo: only solo tracks (and their sub-tracks) play"), viewport());
                return true;
            }
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

QRect PlaylistView::arrowRect(int track) const
{
    const int depth = project_ != nullptr && track >= 0 && track < trackCount()
        ? project_->tracks()[static_cast<std::size_t>(track)].depth
        : 0;
    return {4 + depth * kIndent, yOfTrack(track), kArrowWidth, trackHeight(track)};
}

QRect PlaylistView::trackNameRect(int track) const
{
    const QRect h(0, yOfTrack(track), kHeaderWidth, trackHeight(track));
    return h.adjusted(arrowRect(track).right() + 4, 0, -(6 + 2 * (kTrackButton + kTrackButtonGap)), 0);
}

int PlaylistView::resizeEdgeAt(const QPoint& pos) const
{
    if (pos.x() < 0 || pos.x() >= kHeaderWidth || pos.y() < timeline::kRulerHeight)
        return -1;
    const int track = trackAt(pos.y());
    if (track < 0 || track >= trackCount())
        return -1;
    return pos.y() >= yOfTrack(track) + trackHeight(track) - kResizeGrip ? track : -1;
}

QRect PlaylistView::soloButtonRect(int track) const
{
    return {kHeaderWidth - 6 - kTrackButton, yOfTrack(track) + (trackHeight(track) - kTrackButton) / 2,
        kTrackButton, kTrackButton};
}

QRect PlaylistView::muteButtonRect(int track) const
{
    return soloButtonRect(track).translated(-(kTrackButton + kTrackButtonGap), 0);
}

bool PlaylistView::toggleTrackButton(int track, const QPoint& pos)
{
    if (project_ == nullptr || track < 0 || track >= trackCount())
        return false;
    const auto t = static_cast<std::size_t>(track);
    if (project_->hasSubTracks(t) && arrowRect(track).contains(pos)) {
        emit trackCollapseToggled(t, !project_->tracks()[t].collapsed);
        return true;
    }
    if (muteButtonRect(track).contains(pos)) {
        emit trackMuteToggled(t, !project_->tracks()[t].muted);
        return true;
    }
    if (soloButtonRect(track).contains(pos)) {
        emit trackSoloToggled(t, !project_->tracks()[t].solo);
        return true;
    }
    return false;
}

void PlaylistView::paintTrackButton(
    QPainter& p, const QRect& r, const QString& icon, bool on, const QColor& onColor) const
{
    const Theme::Colors& c = Theme::colors();
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    // Styled like the mixer's mute and solo buttons (Theme.cpp): a tint of
    // `onColor` when on, the window colour when off.
    QColor tint = onColor;
    tint.setAlpha(0x40);
    p.setPen(QPen(on ? onColor : c.border, 1));
    p.setBrush(on ? tint : c.window);
    p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    // The icon in one flat colour.
    constexpr int kIcon = 12;
    const qreal dpr = p.device() != nullptr ? p.device()->devicePixelRatioF() : 1.0;
    QPixmap glyph = Theme::icon(icon).pixmap(QSize(kIcon, kIcon), dpr);
    {
        QPainter recolour(&glyph);
        recolour.setCompositionMode(QPainter::CompositionMode_SourceIn);
        recolour.fillRect(glyph.rect(), on ? onColor : c.textDim);
    }
    p.drawPixmap(
        QRect(r.x() + (r.width() - kIcon) / 2, r.y() + (r.height() - kIcon) / 2, kIcon, kIcon), glyph);
    p.restore();
}

void PlaylistView::startTrackNameEdit(int track)
{
    if (project_ == nullptr || track < 0 || track >= trackCount())
        return;
    editingTrack_ = track;
    const auto& t = project_->tracks()[static_cast<std::size_t>(track)];
    const auto& c = Theme::colors();
    inlineEditor_->setStyleSheet(QStringLiteral("QLineEdit#playlistTrackNameEditor {"
                                                "  background-color: %1;"
                                                "  color: %2;"
                                                "  border: 1px solid %3;"
                                                "  border-radius: 2px;"
                                                "  padding: 1px 4px;"
                                                "  font-size: 11px;"
                                                "}")
            .arg(c.base.name(), c.text.name(), c.accent.name()));
    inlineEditor_->setMaxLength(static_cast<int>(Project::kMaxTrackNameLength));
    inlineEditor_->setGeometry(trackNameRect(track));
    inlineEditor_->setText(t.named ? QString::fromStdString(t.name) : QString());
    inlineEditor_->setPlaceholderText(QString::fromStdString(t.name));
    inlineEditor_->selectAll();
    inlineEditor_->show();
    inlineEditor_->setFocus();
}

void PlaylistView::commitTrackNameEdit()
{
    if (editingTrack_ < 0 || project_ == nullptr
        || static_cast<std::size_t>(editingTrack_) >= project_->tracks().size()) {
        if (inlineEditor_ != nullptr)
            inlineEditor_->hide();
        editingTrack_ = -1;
        return;
    }
    const int track = editingTrack_;
    editingTrack_ = -1;
    const auto& t = project_->tracks()[static_cast<std::size_t>(track)];
    const QString current = t.named ? QString::fromStdString(t.name) : QString();
    const QString newName = inlineEditor_->text().trimmed();
    inlineEditor_->hide();
    if (newName != current)
        emit trackNameEdited(static_cast<std::size_t>(track), newName);
}

bool PlaylistView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == inlineEditor_ && event->type() == QEvent::KeyPress) {
        const auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            editingTrack_ = -1;
            inlineEditor_->hide();
            viewport()->update();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            commitTrackNameEdit();
            return true;
        }
    }
    return QAbstractScrollArea::eventFilter(watched, event);
}

void PlaylistView::mousePressEvent(QMouseEvent* event)
{
    syncLayout();
    const QPoint pos = event->position().toPoint();
    if (editingTrack_ >= 0 && inlineEditor_->isVisible()) {
        if (!inlineEditor_->geometry().contains(pos))
            commitTrackNameEdit();
    }
    if (const int track = resizeEdgeAt(pos); track >= 0 && event->button() == Qt::LeftButton) {
        resizingTrack_ = track;
        resizeStartY_ = pos.y();
        resizeStartHeight_ = project_->tracks()[static_cast<std::size_t>(track)].height;
        return;
    }
    if (const int track = headerTrackAt(pos);
        track >= 0 && event->button() == Qt::LeftButton && toggleTrackButton(track, pos))
        return;
    if (const int track = headerTrackAt(pos); track >= 0 && event->button() == Qt::RightButton) {
        QMenu menu(this);
        const auto t = static_cast<std::size_t>(track);
        menu.addAction(tr("Rename…"), this, [this, track] { startTrackNameEdit(track); });
        addColorMenu(menu, project_->tracks()[t].color,
            [this, t](std::optional<std::uint32_t> color) { emit trackColorChanged(t, color); });
        menu.addSeparator();
        menu.addAction(tr("Move Up"), this, [this, t] { emit trackMoveRequested(t, false); })
            ->setEnabled(project_->canMoveTrackGroup(t, false));
        menu.addAction(tr("Move Down"), this, [this, t] { emit trackMoveRequested(t, true); })
            ->setEnabled(project_->canMoveTrackGroup(t, true));
        menu.addSeparator();
        menu.addAction(tr("Make Sub-track"), this, [this, t] { emit trackIndentRequested(t); })
            ->setEnabled(project_->canIndentTrack(t));
        menu.addAction(tr("Move Out of Group"), this, [this, t] { emit trackOutdentRequested(t); })
            ->setEnabled(project_->tracks()[t].depth > 0);
        menu.addSeparator();
        QAction* remove
            = menu.addAction(tr("Delete “%1”").arg(QString::fromStdString(project_->tracks()[t].name)), this,
                [this, t] { emit trackRemoveRequested(t); });
        remove->setEnabled(trackCount() > 1);
        menu.exec(event->globalPosition().toPoint());
        return;
    }
    if (project_ == nullptr)
        return;
    // Playback: from anywhere right of the headers, the ruler too.
    if (tool_ == Tool::Playback && event->button() == Qt::LeftButton && pos.x() >= kHeaderWidth) {
        playing_ = true;
        lastPlaybackTick_ = std::max(0.0, tickAt(pos.x()));
        emit playbackRequested(lastPlaybackTick_);
        return;
    }
    if (!gridRect().contains(pos))
        return;
    syncSelection();
    const auto hit = clipAt(pos);
    if (event->button() == Qt::RightButton) {
        if (tool_ == Tool::Zoom) {
            zoomAround(pos.x(), 1.0 / 1.5);
            return;
        }
        // An audio clip's title strip opens its menu (fit to tempo); the rest
        // of a clip deletes it, as in FL Studio.
        if (hit && hit->audio) {
            const QRectF r = clipRect(project_->audioClips()[hit->index]);
            if (pos.y() < r.top() + 16.0) {
                emit audioClipMenuRequested(hit->index, event->globalPosition().toPoint());
                return;
            }
        }
        if (hit && hit->audio)
            emit audioClipRemoved(hit->index);
        else if (hit)
            emit clipRemoved(hit->index);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    const bool ctrl = event->modifiers().testFlag(Qt::ControlModifier);
    // The clip clicked last is what Draw and Paint place next.
    if (hit && (tool_ == Tool::Draw || tool_ == Tool::Paint || tool_ == Tool::Select)) {
        const ClipSource picked{hit->audio,
            hit->audio ? project_->audioClips()[hit->index].channel : project_->clips()[hit->index].pattern};
        if (!(picked == source_)) {
            source_ = picked;
            emit sourcePicked(picked);
        }
    }
    if (tool_ == Tool::Draw || tool_ == Tool::Paint || tool_ == Tool::Select)
        if (const auto edge = edgeAt(pos)) {
            // Trim or stretch the clip by its edge.
            resizingClip_ = true;
            resizeClip_ = edge->first;
            resizeRight_ = edge->second;
            if (resizeClip_.audio)
                resizeStartAudio_ = project_->audioClips()[resizeClip_.index];
            else
                resizeStartClip_ = project_->clips()[resizeClip_.index];
            return;
        }
    switch (tool_) {
    case Tool::Slice: {
        // The line stretches across the tracks the drag passes; the cut is
        // made on release.
        const int track = std::clamp(trackAt(pos.y()), 0, std::max(0, trackCount() - 1));
        slicing_ = true;
        sliceLine_ = SliceLine{sliceTick(pos.x(), event->modifiers()), track, track};
        viewport()->update();
        return;
    }
    case Tool::Delete:
        deleting_ = true;
        if (hit && hit->audio)
            emit audioClipRemoved(hit->index);
        else if (hit)
            emit clipRemoved(hit->index);
        return;
    case Tool::Mute:
        if (!hit)
            return;
        muting_ = true;
        muteState_
            = hit->audio ? !project_->audioClips()[hit->index].muted : !project_->clips()[hit->index].muted;
        muteAt(pos);
        return;
    case Tool::Zoom:
    case Tool::Select:
        if (tool_ == Tool::Select && hit) {
            if (ctrl) { // Ctrl+click: in or out of the selection
                auto& flags = hit->audio ? selectedAudio_ : selectedClips_;
                flags[hit->index] = flags[hit->index] != 0 ? 0 : 1;
                viewport()->update();
                return;
            }
            if (!isSelected(*hit)) {
                clearSelection();
                (hit->audio ? selectedAudio_ : selectedClips_)[hit->index] = 1;
            }
            beginGroupDrag(pos);
            return;
        }
        bandStart_ = pos;
        bandAdds_ = ctrl;
        band_ = QRect(pos, QSize(1, 1));
        viewport()->update();
        return;
    case Tool::Playback:
        return;
    case Tool::Draw:
    case Tool::Paint:
        break;
    }
    if (hit) {
        if (isSelected(*hit) && selectionSize() > 1)
            beginGroupDrag(pos);
        else
            beginDrag(*hit, pos);
        return;
    }
    const int track = trackAt(pos.y());
    if (track < 0 || track >= trackCount())
        return;
    clearSelection();
    const int start = snapDown(tickAt(pos.x()), snapTicks(event->modifiers()));
    if (!placeSource(track, start))
        return;
    if (tool_ == Tool::Paint) {
        // Lay the source again in every slot the drag passes on this track.
        painting_ = true;
        paintTrack_ = track;
        paintStart_ = start;
        paintLength_ = sourceLength();
        paintLastSlot_ = 0;
        return;
    }
    // Keep dragging to put the new clip in place (it was added last).
    const std::size_t count = source_.audio ? project_->audioClips().size() : project_->clips().size();
    if (count > 0)
        beginDrag(ClipRef{source_.audio, count - 1}, pos);
}

void PlaylistView::mouseMoveEvent(QMouseEvent* event)
{
    syncLayout();
    const QPoint pos = event->position().toPoint();
    if (resizingTrack_ >= 0) {
        if (resizingTrack_ < trackCount())
            emit trackHeightChanged(
                static_cast<std::size_t>(resizingTrack_), resizeStartHeight_ + pos.y() - resizeStartY_);
        return;
    }
    if (playing_) {
        // Scrub: jump when the pointer has moved a few pixels.
        const double tick = std::max(0.0, tickAt(pos.x()));
        if (std::abs(tick - lastPlaybackTick_) * pixelsPerTick() >= 3.0) {
            lastPlaybackTick_ = tick;
            emit playbackRequested(tick);
        }
        return;
    }
    if (band_) {
        band_ = QRect(bandStart_, pos).normalized().intersected(gridRect());
        viewport()->update();
        return;
    }
    if (painting_) {
        paintTo(tickAt(pos.x()));
        return;
    }
    if (tool_ == Tool::Slice) {
        // The line follows the pointer; while the button is down it keeps its
        // place in time and reaches down or up to the track under the pointer.
        const int track = std::clamp(trackAt(pos.y()), 0, std::max(0, trackCount() - 1));
        std::optional<SliceLine> line;
        if (slicing_ && sliceLine_)
            line = SliceLine{sliceLine_->tick, sliceLine_->fromTrack, track};
        else if (gridRect().contains(pos) && trackAt(pos.y()) < trackCount())
            line = SliceLine{sliceTick(pos.x(), event->modifiers()), track, track};
        const bool changed = line.has_value() != sliceLine_.has_value()
            || (line
                && (line->tick != sliceLine_->tick || line->fromTrack != sliceLine_->fromTrack
                    || line->toTrack != sliceLine_->toTrack));
        sliceLine_ = line;
        if (changed)
            viewport()->update();
        updateToolCursor(pos);
        return;
    }
    if (resizingClip_) {
        resizeClipTo(tickAt(pos.x()), snapTicks(event->modifiers()));
        return;
    }
    if (deleting_) {
        if (const auto hit = clipAt(pos); hit && hit->audio)
            emit audioClipRemoved(hit->index);
        else if (hit)
            emit clipRemoved(hit->index);
        return;
    }
    if (muting_) {
        muteAt(pos);
        return;
    }
    const int grid = snapTicks(event->modifiers());
    if (groupDragging_) {
        moveGroup(pos, grid);
        return;
    }
    if (!dragging_) {
        updateToolCursor(pos);
        return;
    }
    const double moved = tickAt(pos.x()) - dragStartTick_;
    // The track under the pointer (Project clamps it): with tracks of their
    // own heights and hidden sub-tracks, counting rows moved would be wrong.
    const int track = std::max(0, trackAt(pos.y()));
    if (dragClip_.audio) {
        AudioClip clip = dragStartAudio_;
        clip.start = snapNearest(dragStartAudio_.start + moved, grid);
        clip.track = track;
        emit audioClipChanged(dragClip_.index, clip);
        return;
    }
    Clip clip = dragStartClip_;
    clip.start = snapNearest(dragStartClip_.start + moved, grid);
    clip.track = track;
    emit clipChanged(dragClip_.index, clip);
}

void PlaylistView::mouseReleaseEvent(QMouseEvent* event)
{
    if (resizingTrack_ >= 0) {
        resizingTrack_ = -1;
        viewport()->unsetCursor();
        return;
    }
    if (playing_) {
        playing_ = false;
        emit playbackReleased();
        return;
    }
    if (band_) {
        const QRect band = *band_;
        band_.reset();
        syncLayout();
        if (tool_ == Tool::Zoom) {
            if (band.width() > 4)
                zoomToRange(tickAt(band.left()), tickAt(band.right()));
            else
                zoomAround(bandStart_.x(), 1.5); // a click zooms in
        } else {
            selectIn(band, bandAdds_);
        }
        viewport()->update();
        return;
    }
    if (slicing_) {
        slicing_ = false;
        if (sliceLine_)
            sliceAcross(sliceLine_->tick, sliceLine_->fromTrack, sliceLine_->toTrack);
        refresh();
        return;
    }
    const bool grew = painting_ || groupDragging_ || dragging_ || resizingClip_;
    resizingClip_ = false;
    painting_ = false;
    deleting_ = false;
    muting_ = false;
    groupDragging_ = false;
    if (!grew)
        return;
    dragging_ = false;
    updateToolCursor(event->position().toPoint());
    refresh(); // the song may have grown
}

void PlaylistView::mouseDoubleClickEvent(QMouseEvent* event)
{
    syncLayout();
    const QPoint pos = event->position().toPoint();
    if (const int track = resizeEdgeAt(pos); track >= 0 && event->button() == Qt::LeftButton) {
        emit trackHeightChanged(static_cast<std::size_t>(track), PlaylistTrack::kDefaultHeight);
        return;
    }
    // A quick second click on a header button arrives here instead of as a press.
    if (const int track = headerTrackAt(pos);
        track >= 0 && event->button() == Qt::LeftButton && toggleTrackButton(track, pos))
        return;
    if (const int track = headerTrackAt(pos);
        track >= 0 && event->button() == Qt::LeftButton && trackNameRect(track).contains(pos)) {
        startTrackNameEdit(track);
        return;
    }
    // Only Draw opens a clip's pattern; a quick second click with the other
    // tools is another click.
    if (tool_ != Tool::Draw && pos.x() >= kHeaderWidth) {
        mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton)
        if (const auto hit = clipAt(pos); hit && !hit->audio && project_ != nullptr) {
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
    zoomAround(static_cast<int>(event->position().x()), std::pow(1.25, notches));
    event->accept();
}

void PlaylistView::zoomAround(int anchorX, double factor)
{
    const double anchor = tickAt(anchorX);
    pixelsPerBar_ = std::clamp(pixelsPerBar_ * factor, kMinPixelsPerBar, kMaxPixelsPerBar);
    updateScrollBars();
    horizontalScrollBar()->setValue(static_cast<int>(anchor * pixelsPerTick()) - (anchorX - kHeaderWidth));
    viewport()->update();
}

void PlaylistView::zoomToRange(double from, double to)
{
    from = std::max(0.0, from);
    if (!(to > from))
        return;
    const double width = std::max(1, gridRect().width());
    pixelsPerBar_ = std::clamp(width / (to - from) * kTicksPerBar, kMinPixelsPerBar, kMaxPixelsPerBar);
    updateScrollBars();
    horizontalScrollBar()->setValue(static_cast<int>(from * pixelsPerTick()));
    viewport()->update();
}

void PlaylistView::setTool(Tool tool)
{
    if (tool == tool_)
        return;
    tool_ = tool;
    band_.reset();
    sliceLine_.reset();
    slicing_ = false;
    viewport()->unsetCursor();
    viewport()->update();
}

void PlaylistView::updateToolCursor(const QPoint& pos)
{
    if (resizeEdgeAt(pos) >= 0) {
        viewport()->setCursor(Qt::SizeVerCursor);
        return;
    }
    if (const int track = headerTrackAt(pos); track >= 0) {
        const bool button = muteButtonRect(track).contains(pos) || soloButtonRect(track).contains(pos)
            || (project_->hasSubTracks(static_cast<std::size_t>(track)) && arrowRect(track).contains(pos));
        viewport()->setCursor(trackNameRect(track).contains(pos) ? Qt::IBeamCursor
                : button                                         ? Qt::PointingHandCursor
                                                                 : Qt::ArrowCursor);
        return;
    }
    const bool onClip = clipAt(pos).has_value();
    if ((tool_ == Tool::Draw || tool_ == Tool::Paint || tool_ == Tool::Select) && edgeAt(pos)) {
        viewport()->setCursor(Qt::SizeHorCursor);
        return;
    }
    switch (tool_) {
    case Tool::Slice:
        viewport()->setCursor(Qt::CrossCursor);
        break;
    case Tool::Draw:
        viewport()->setCursor(onClip ? Qt::OpenHandCursor : Qt::ArrowCursor);
        break;
    case Tool::Paint:
        viewport()->setCursor(onClip ? Qt::OpenHandCursor : Qt::CrossCursor);
        break;
    case Tool::Delete:
    case Tool::Mute:
        viewport()->setCursor(onClip ? Qt::PointingHandCursor : Qt::ArrowCursor);
        break;
    case Tool::Select:
        viewport()->setCursor(onClip ? Qt::SizeAllCursor : Qt::CrossCursor);
        break;
    case Tool::Zoom:
        viewport()->setCursor(Qt::CrossCursor);
        break;
    case Tool::Playback:
        viewport()->setCursor(pos.x() >= kHeaderWidth ? Qt::SplitHCursor : Qt::ArrowCursor);
        break;
    }
}

bool PlaylistView::isSelected(ClipRef clip) const
{
    const auto& flags = clip.audio ? selectedAudio_ : selectedClips_;
    return clip.index < flags.size() && flags[clip.index] != 0;
}

std::size_t PlaylistView::selectionSize() const
{
    return static_cast<std::size_t>(std::count(selectedClips_.begin(), selectedClips_.end(), 1)
        + std::count(selectedAudio_.begin(), selectedAudio_.end(), 1));
}

void PlaylistView::clearSelection()
{
    std::fill(selectedClips_.begin(), selectedClips_.end(), 0);
    std::fill(selectedAudio_.begin(), selectedAudio_.end(), 0);
}

void PlaylistView::syncSelection()
{
    const std::size_t clips = project_ != nullptr ? project_->clips().size() : 0;
    const std::size_t audio = project_ != nullptr ? project_->audioClips().size() : 0;
    if (selectedClips_.size() == clips && selectedAudio_.size() == audio)
        return;
    // Clips were added or removed: indices moved, so the selection is stale.
    selectedClips_.assign(clips, 0);
    selectedAudio_.assign(audio, 0);
}

void PlaylistView::selectIn(const QRect& band, bool add)
{
    syncSelection();
    if (!add)
        clearSelection();
    const QRectF box(band);
    for (std::size_t i = 0; i < project_->clips().size(); ++i)
        if (trackHeight(project_->clips()[i].track) > 0 && clipRect(project_->clips()[i]).intersects(box))
            selectedClips_[i] = 1;
    for (std::size_t i = 0; i < project_->audioClips().size(); ++i)
        if (trackHeight(project_->audioClips()[i].track) > 0
            && clipRect(project_->audioClips()[i]).intersects(box))
            selectedAudio_[i] = 1;
}

void PlaylistView::beginGroupDrag(const QPoint& pos)
{
    groupStartClips_.clear();
    groupStartAudio_.clear();
    for (std::size_t i = 0; i < selectedClips_.size(); ++i)
        if (selectedClips_[i] != 0)
            groupStartClips_.emplace_back(i, project_->clips()[i]);
    for (std::size_t i = 0; i < selectedAudio_.size(); ++i)
        if (selectedAudio_[i] != 0)
            groupStartAudio_.emplace_back(i, project_->audioClips()[i]);
    if (groupStartClips_.empty() && groupStartAudio_.empty())
        return;
    groupDragging_ = true;
    dragStartTick_ = tickAt(pos.x());
    groupStartTrack_ = std::max(0, trackAt(pos.y()));
    viewport()->setCursor(Qt::ClosedHandCursor);
    viewport()->update();
}

void PlaylistView::moveGroup(const QPoint& pos, int grid)
{
    // Every clip moves by the same snapped amount, kept inside the song and
    // the tracks; the earliest clip snaps to the grid.
    int earliest = std::numeric_limits<int>::max();
    int top = std::numeric_limits<int>::max();
    int bottom = 0;
    const auto bounds = [&](int start, int track) {
        earliest = std::min(earliest, start);
        top = std::min(top, track);
        bottom = std::max(bottom, track);
    };
    for (const auto& [i, clip] : groupStartClips_)
        bounds(clip.start, clip.track);
    for (const auto& [i, clip] : groupStartAudio_)
        bounds(clip.start, clip.track);
    const double moved = tickAt(pos.x()) - dragStartTick_;
    const int shift = std::max(0, snapNearest(earliest + moved, grid)) - earliest;
    const int tracks
        = std::clamp(std::max(0, trackAt(pos.y())) - groupStartTrack_, -top, trackCount() - 1 - bottom);
    PlaylistClipEdit edit;
    for (auto [i, clip] : groupStartClips_) {
        clip.start += shift;
        clip.track += tracks;
        edit.clips.emplace_back(i, clip);
    }
    for (auto [i, clip] : groupStartAudio_) {
        clip.start += shift;
        clip.track += tracks;
        edit.audioClips.emplace_back(i, clip);
    }
    emit clipsChanged(edit);
}

void PlaylistView::paintTo(double tick)
{
    const auto slot = static_cast<long long>(std::floor((tick - paintStart_) / paintLength_));
    const long long step = slot >= paintLastSlot_ ? 1 : -1;
    for (long long k = paintLastSlot_ + step; k != slot + step; k += step) {
        const long long at = paintStart_ + k * static_cast<long long>(paintLength_);
        if (at < 0 || at > std::numeric_limits<int>::max() - paintLength_)
            continue;
        const int start = static_cast<int>(at);
        // A slot already holding a clip on this track stays as it is.
        bool taken = false;
        for (const Clip& clip : project_->clips())
            taken = taken
                || (clip.track == paintTrack_ && clip.start < start + paintLength_
                    && start < clip.start + project_->clipLength(clip));
        for (const AudioClip& clip : project_->audioClips())
            taken = taken
                || (clip.track == paintTrack_ && clip.start < start + paintLength_
                    && start < clip.start + audioLength(clip));
        if (!taken)
            placeSource(paintTrack_, start);
    }
    paintLastSlot_ = slot;
}

std::optional<std::pair<PlaylistView::ClipRef, bool>> PlaylistView::edgeAt(const QPoint& pos) const
{
    const auto hit = clipAt(pos);
    if (!hit)
        return std::nullopt;
    const QRectF r
        = hit->audio ? clipRect(project_->audioClips()[hit->index]) : clipRect(project_->clips()[hit->index]);
    constexpr double kGrip = 5.0;
    if (r.width() < 4 * kGrip)
        return std::nullopt; // too narrow: leave it for moving
    if (pos.x() >= r.right() - kGrip)
        return std::pair{*hit, true};
    if (pos.x() <= r.left() + kGrip)
        return std::pair{*hit, false};
    return std::nullopt;
}

void PlaylistView::resizeClipTo(double tick, int grid)
{
    const int at = snapNearest(tick, grid);
    // start, offset, length of the clip as it was when the drag began, its
    // length made explicit; and how far its content reaches (audio: the sample).
    const bool audio = resizeClip_.audio;
    const int start = audio ? resizeStartAudio_.start : resizeStartClip_.start;
    const int offset = audio ? resizeStartAudio_.offset : resizeStartClip_.offset;
    const int length = audio ? audioLength(resizeStartAudio_) : project_->clipLength(resizeStartClip_);
    const int whole = audio ? sampleTicks(resizeStartAudio_) : 0; // 0: no limit (a pattern repeats)
    const int end = start + length;
    int newStart = start;
    int newLength = length;
    if (resizeRight_) {
        int newEnd = std::max(at, start + kTicksPerStep);
        if (whole > 0)
            newEnd = std::min(newEnd, start + (whole - offset)); // no further than the sample's end
        newLength = newEnd - start;
    } else {
        // The left edge stops where the content starts (offset 0): before
        // the start of the sample or pattern there is nothing to show.
        newStart = std::clamp(at, std::max(0, start - offset), end - kTicksPerStep);
        newLength = end - newStart;
    }
    const int newOffset = offset + (newStart - start);
    if (audio) {
        AudioClip clip = resizeStartAudio_;
        clip.start = newStart;
        clip.offset = newOffset;
        clip.length = newLength;
        emit audioClipChanged(resizeClip_.index, clip);
        return;
    }
    Clip clip = resizeStartClip_;
    clip.start = newStart;
    clip.offset = newOffset;
    clip.length = newLength;
    emit clipChanged(resizeClip_.index, clip);
}

int PlaylistView::snapTicks(Qt::KeyboardModifiers modifiers) const
{
    if (modifiers.testFlag(Qt::ShiftModifier))
        return kTicksPerStep;
    switch (snap_) {
    case Snap::Bar:
        return kTicksPerBar;
    case Snap::Beat:
        return kTicksPerBeat;
    case Snap::Step:
        return kTicksPerStep;
    case Snap::Auto:
        break;
    }
    // Auto: from a step, doubled until the lines are far enough apart to aim at.
    constexpr double kMinSnapPixels = 12.0;
    int grid = kTicksPerStep;
    while (grid * pixelsPerTick() < kMinSnapPixels && grid < kTicksPerBar)
        grid *= 2;
    return grid;
}

int PlaylistView::sliceTick(int x, Qt::KeyboardModifiers modifiers) const
{
    return std::max(0, snapNearest(tickAt(x), snapTicks(modifiers)));
}

void PlaylistView::sliceAcross(int tick, int fromTrack, int toTrack)
{
    const int top = std::min(fromTrack, toTrack);
    const int bottom = std::max(fromTrack, toTrack);
    const auto crosses = [&](int track, int start, int length) {
        return track >= top && track <= bottom && trackHeight(track) > 0 && start < tick
            && tick < start + length;
    };
    // New parts are appended: the indices of the clips looked at stay valid.
    const std::size_t clipCount = project_->clips().size();
    for (std::size_t i = 0; i < clipCount; ++i) {
        const Clip clip = project_->clips()[i];
        const int length = project_->clipLength(clip);
        if (!crosses(clip.track, clip.start, length))
            continue;
        Clip first = clip;
        first.length = tick - clip.start;
        Clip second = clip;
        second.start = tick;
        second.offset = clip.offset + (tick - clip.start); // goes on where the first part stops
        second.length = clip.start + length - tick;
        emit clipChanged(i, first);
        emit clipAdded(second);
    }
    const std::size_t audioCount = project_->audioClips().size();
    for (std::size_t i = 0; i < audioCount; ++i) {
        const AudioClip clip = project_->audioClips()[i];
        const int length = audioLength(clip);
        if (!crosses(clip.track, clip.start, length))
            continue;
        AudioClip first = clip;
        first.length = tick - clip.start;
        AudioClip second = clip;
        second.start = tick;
        second.offset = clip.offset + (tick - clip.start);
        second.length = clip.start + length - tick;
        emit audioClipChanged(i, first);
        emit audioClipAdded(second);
    }
}

void PlaylistView::leaveEvent(QEvent* event)
{
    // The slice line follows the pointer only while it is over the playlist.
    if (sliceLine_ && !slicing_) {
        sliceLine_.reset();
        viewport()->update();
    }
    QAbstractScrollArea::leaveEvent(event);
}

void PlaylistView::muteAt(const QPoint& pos)
{
    const auto hit = clipAt(pos);
    if (!hit)
        return;
    if (hit->audio) {
        AudioClip clip = project_->audioClips()[hit->index];
        if (clip.muted != muteState_) {
            clip.muted = muteState_;
            emit audioClipChanged(hit->index, clip);
        }
        return;
    }
    Clip clip = project_->clips()[hit->index];
    if (clip.muted != muteState_) {
        clip.muted = muteState_;
        emit clipChanged(hit->index, clip);
    }
}

void PlaylistView::keyPressEvent(QKeyEvent* event)
{
    syncSelection();
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && selectionSize() > 0) {
        std::vector<std::size_t> clips;
        std::vector<std::size_t> audio;
        for (std::size_t i = 0; i < selectedClips_.size(); ++i)
            if (selectedClips_[i] != 0)
                clips.push_back(i);
        for (std::size_t i = 0; i < selectedAudio_.size(); ++i)
            if (selectedAudio_[i] != 0)
                audio.push_back(i);
        clearSelection();
        emit clipsRemoved(clips, audio);
        return;
    }
    if (event->matches(QKeySequence::SelectAll) && project_ != nullptr) {
        std::fill(selectedClips_.begin(), selectedClips_.end(), 1);
        std::fill(selectedAudio_.begin(), selectedAudio_.end(), 1);
        viewport()->update();
        return;
    }
    if (event->key() == Qt::Key_Escape && selectionSize() > 0) {
        clearSelection();
        viewport()->update();
        return;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

} // namespace vloomdaw
