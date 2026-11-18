// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/playlist/PlaylistView.hpp"

#include "core/Decibels.hpp"
#include "core/TrackRouting.hpp"
#include "ui/DroppedFiles.hpp"
#include "ui/Theme.hpp"
#include "ui/plugins/PluginDrag.hpp"
#include "ui/widgets/ColorMenu/ColorMenu.hpp"
#include "ui/widgets/Timeline/Timeline.hpp"

#include <QApplication>
#include <QCursor>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QScrollBar>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace vloomdaw {

namespace {

constexpr int kColorBarWidth = 6; ///< clickable colour strip on the left edge of each track header
constexpr int kTrackNumWidth = 20; ///< 1-based track number column
constexpr int kTrackButton = 17; ///< M / S button size
constexpr int kTrackButtonGap = 2;
constexpr int kBadgeSize = 14; ///< small waveform / MIDI badge before the VU meter
constexpr int kIndent = 10; ///< per sub-track level, in the header
constexpr int kArrowWidth = 11; ///< the collapse arrow
constexpr int kResizeGrip = 4; ///< bottom strip of a header that resizes the track
constexpr int kHeaderDividerGrip = 4; ///< right edge of the header column that resizes its width
constexpr int kMeterAreaWidth = 11; ///< right-edge stereo VU meter strip (housing + padding)
constexpr int kMeterBarWidth = 3;
constexpr int kMeterBarGap = 1;
constexpr int kSliderHeight = 7;
constexpr int kSliderShowHeight = 48; ///< minimum track height that shows Volume & Pan bars
constexpr int kMenuRow0Height = 78; ///< minimum track height that reveals Channel/Source row
constexpr int kMenuRow1Height = 100; ///< minimum track height that reveals Mixer Insert row
constexpr int kExpandedInspectorHeight = 104;
constexpr int kWaveTileWidth = 256;
constexpr int kMaxWaveTiles = 384;
constexpr int kMinSongBars = 32;
constexpr int kLoopLaneHeight = 8; ///< Studio One-style Loop Bar strip above the bar-number ruler
constexpr int kLoopLaneHitHeight = 11; ///< mouse hit height at the top of the Time Bar
constexpr int kPlaylistRulerHeight = kLoopLaneHeight + timeline::kRulerHeight;
constexpr int kBottomAddTrackSpace
    = 116; ///< guaranteed empty space below the last track for right-clicking Add Track
constexpr double kMinPixelsPerBar = 16.0;
constexpr double kMaxPixelsPerBar = 400.0;

constexpr double kMeterFloorDb = -60.0;
constexpr double kMeterTopDb = 6.0;
constexpr double kMeterFallDbPerSec = 26.0;
constexpr double kMeterHoldSec = 1.2;

int snapNearest(double tick, int grid) { return static_cast<int>(std::lround(tick / grid)) * grid; }
int snapDown(double tick, int grid) { return static_cast<int>(std::floor(tick / grid)) * grid; }

double meterScale(float peak)
{
    if (!(peak > 0.0f))
        return 0.0;
    const double db = gainToDb(peak);
    if (std::isinf(db))
        return 0.0;
    return std::clamp((db - kMeterFloorDb) / (kMeterTopDb - kMeterFloorDb), 0.0, 1.0);
}

double meterDbScale(double db) { return (db - kMeterFloorDb) / (kMeterTopDb - kMeterFloorDb); }

QString formatPanText(double pan)
{
    const int pct = static_cast<int>(std::lround(std::abs(pan) * 100.0));
    if (pct == 0)
        return PlaylistView::tr("Center");
    return pan < 0.0 ? PlaylistView::tr("%1% L").arg(pct) : PlaylistView::tr("%1% R").arg(pct);
}

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
    viewport()->installEventFilter(this); // a held playback ends when its release is lost
    connect(inlineEditor_, &QLineEdit::editingFinished, this, &PlaylistView::commitTrackNameEdit);
}

void PlaylistView::setProject(const Project* project)
{
    project_ = project;
    clearWaveCache();
    refresh();
}

void PlaylistView::setMixer(const MixerModel* mixer)
{
    mixer_ = mixer;
    updateHeaders();
}

void PlaylistView::setHeaderWidth(int width)
{
    const int clamped = std::clamp(width, kMinHeaderWidth, kMaxHeaderWidth);
    if (clamped == headerWidth_)
        return;
    headerWidth_ = clamped;
    if (editingTrack_ >= 0 && inlineEditor_ != nullptr && inlineEditor_->isVisible())
        inlineEditor_->setGeometry(trackNameRect(editingTrack_));
    updateScrollBars();
    viewport()->update();
}

void PlaylistView::repaintTrackRow(int track)
{
    if (track < 0 || track >= trackCount())
        return;
    const int h = trackHeight(track);
    if (h > 0)
        viewport()->update(QRect(0, yOfTrack(track), viewport()->width(), h));
}

void PlaylistView::setSelectedTrack(int track)
{
    syncLayout();
    const int count = trackCount();
    const int clamped = count > 0 ? std::clamp(track, 0, count - 1) : -1;
    if (clamped == selectedTrack_)
        return;
    const int prev = selectedTrack_;
    selectedTrack_ = clamped;
    repaintTrackRow(prev);
    repaintTrackRow(selectedTrack_);
}

void PlaylistView::setArmedTrack(int track)
{
    syncLayout();
    const int next = (track >= 0 && track < trackCount()) ? track : -1;
    if (next == armedTrack_)
        return;
    const int prev = armedTrack_;
    armedTrack_ = next;
    repaintTrackRow(prev);
    repaintTrackRow(armedTrack_);
}

void PlaylistView::setMonitoredTrack(int track)
{
    syncLayout();
    const int next = (track >= 0 && track < trackCount()) ? track : -1;
    if (next == monitoredTrack_)
        return;
    const int prev = monitoredTrack_;
    monitoredTrack_ = next;
    repaintTrackRow(prev);
    repaintTrackRow(monitoredTrack_);
}

void PlaylistView::setInputDeviceLabel(const QString& label)
{
    if (label == inputDeviceLabel_)
        return;
    inputDeviceLabel_ = label;
    updateHeaders();
}

QRect PlaylistView::liveRecordingRect(const LiveRecordingView& rec) const
{
    if (rec.track < 0 || rec.track >= trackCount() || rec.endTick <= rec.startTick)
        return {};
    const int h = trackHeight(rec.track);
    if (h <= 0)
        return {};
    const double ppt = pixelsPerTick();
    const int scrollX = horizontalScrollBar()->value();
    const int x1 = headerWidth_ + static_cast<int>(std::floor(rec.startTick * ppt)) - scrollX;
    const int x2 = std::max(x1 + 4, headerWidth_ + static_cast<int>(std::ceil(rec.endTick * ppt)) - scrollX);
    return QRect(x1, yOfTrack(rec.track), x2 - x1 + 2, h);
}

void PlaylistView::setLiveRecording(const std::optional<LiveRecordingView>& rec)
{
    syncLayout();
    QRect dirty;
    if (liveRecording_)
        dirty = liveRecordingRect(*liveRecording_);
    const int prevEnd = liveRecording_ ? static_cast<int>(liveRecording_->endTick) : 0;
    liveRecording_ = rec;
    if (liveRecording_) {
        if (static_cast<int>(liveRecording_->endTick) / kTicksPerBar != prevEnd / kTicksPerBar)
            updateScrollBars();
        dirty = dirty.isEmpty() ? liveRecordingRect(*liveRecording_)
                                : dirty.united(liveRecordingRect(*liveRecording_));
    }
    if (!dirty.isEmpty())
        viewport()->update(dirty.intersected(gridRect()).adjusted(-2, -2, 2, 2));
}

void PlaylistView::selectTrackByUser(int track)
{
    syncLayout();
    const int count = trackCount();
    if (count <= 0)
        return;
    const int clamped = std::clamp(track, 0, count - 1);
    setSelectedTrack(clamped);
    emit trackSelected(static_cast<std::size_t>(clamped));
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
    clearWaveCache();
    refresh();
}

void PlaylistView::clearWaveCache() { waveTiles_.clear(); }

void PlaylistView::setTempo(double bpm)
{
    if (!(bpm > 0.0) || bpm == tempo_)
        return;
    tempo_ = bpm;
    if (project_ != nullptr && !project_->audioClips().empty()) {
        clearWaveCache();
        refresh(); // audio clips change length in bars
    }
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
    const double ticks = info->seconds * clipTempo(clip) / 60.0 * kTicksPerBeat;
    return std::max(1, static_cast<int>(std::min(ticks, 1e9)));
}

int PlaylistView::audioLength(const AudioClip& clip) const
{
    const int whole = sampleTicks(clip);
    if (whole == 0) {
        if (clip.length > 0)
            return clip.length;
        const ClipAudio* info = audioOf(clip);
        return info != nullptr && info->loading ? 8 * kTicksPerBar : kTicksPerBar;
    }
    const int left = std::max(1, whole - clip.offset);
    return clip.length > 0 ? std::min(clip.length, left) : left;
}

void PlaylistView::setPlayhead(double tick)
{
    if (tick == playhead_)
        return;
    const double ppt = pixelsPerTick();
    const int scrollX = horizontalScrollBar()->value();
    const timeline::Scale scale{scrollX / ppt, ppt, headerWidth_};
    const QRect area(0, 0, viewport()->width(), viewport()->height());

    if (followPlayhead_ && tick > 0.0 && playhead_ >= 0.0 && !playing_ && !dragging_ && !groupDragging_
        && !resizingClip_) {
        const QRect grid = gridRect();
        if (grid.width() > 80) {
            const double prevX = headerWidth_ + playhead_ * ppt - scrollX;
            const double nextX = headerWidth_ + tick * ppt - scrollX;
            const double rightMargin = grid.right() - 28.0;
            if (prevX >= headerWidth_ && prevX <= grid.right() && nextX > rightMargin) {
                playhead_ = tick;
                const int targetScroll = std::max(0, static_cast<int>(tick * ppt) - grid.width() / 6);
                horizontalScrollBar()->setValue(targetScroll);
                return;
            }
        }
    }

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

void PlaylistView::updateGrid()
{
    viewport()->update(
        QRect(headerWidth_, 0, std::max(0, viewport()->width() - headerWidth_), viewport()->height()));
}

void PlaylistView::updateHeaders() { viewport()->update(QRect(0, 0, headerWidth_, viewport()->height())); }

void PlaylistView::updateTrackMeters(const std::vector<std::pair<float, float>>& insertPeaks, double dt)
{
    if (project_ == nullptr)
        return;
    const std::size_t n = project_->tracks().size();
    if (meters_.size() != n)
        meters_.resize(n);

    const double fall = kMeterFallDbPerSec * std::max(0.0, dt) / (kMeterTopDb - kMeterFloorDb);
    bool anyMoved = false;

    for (std::size_t t = 0; t < n; ++t) {
        float peakL = 0.0f;
        float peakR = 0.0f;
        if ((project_->trackAudible(t) || static_cast<int>(t) == armedTrack_
                || static_cast<int>(t) == monitoredTrack_)
            && mixer_ != nullptr) {
            const int ins = trackMixerInsert(*project_, *mixer_, t);
            if (ins >= 0 && static_cast<std::size_t>(ins) < insertPeaks.size()) {
                peakL = insertPeaks[static_cast<std::size_t>(ins)].first;
                peakR = insertPeaks[static_cast<std::size_t>(ins)].second;
            }
        }
        const float incoming[2] = {peakL, peakR};
        TrackMeter& m = meters_[t];
        const int h = std::max(1, trackHeight(static_cast<int>(t)) - 6);
        for (int ch = 0; ch < 2; ++ch) {
            const int prevPx = static_cast<int>(std::lround(m.level[ch] * h));
            const int prevHoldPx = static_cast<int>(std::lround(m.hold[ch] * h));
            m.level[ch] = std::max(0.0, m.level[ch] - fall);
            const double scaled = meterScale(incoming[ch]);
            if (scaled > m.level[ch])
                m.level[ch] = scaled;
            if (scaled >= m.hold[ch] && scaled > 0.0) {
                m.hold[ch] = scaled;
                m.holdAge[ch] = 0.0;
            } else {
                m.holdAge[ch] += dt;
                if (m.holdAge[ch] > kMeterHoldSec && m.hold[ch] > m.level[ch])
                    m.hold[ch] = m.level[ch];
            }
            if (incoming[ch] > 1.0f)
                m.clipped[ch] = true;
            else if (m.level[ch] <= 0.001)
                m.clipped[ch] = false;

            const int newPx = static_cast<int>(std::lround(m.level[ch] * h));
            const int newHoldPx = static_cast<int>(std::lround(m.hold[ch] * h));
            if ((newPx != prevPx || newHoldPx != prevHoldPx) && project_->trackVisible(t))
                anyMoved = true;
        }
    }

    if (anyMoved) {
        viewport()->update(QRect(headerWidth_ - kMeterAreaWidth, kPlaylistRulerHeight, kMeterAreaWidth - 1,
            std::max(0, viewport()->height() - kPlaylistRulerHeight)));
    }
}

double PlaylistView::pixelsPerTick() const { return pixelsPerBar_ / kTicksPerBar; }

int PlaylistView::lengthTicks() const
{
    int end = kMinSongBars * kTicksPerBar;
    if (loopEnd_ > loopStart_)
        end = std::max(end, loopEnd_ + 8 * kTicksPerBar);
    if (liveRecording_)
        end = std::max(end, static_cast<int>(std::ceil(liveRecording_->endTick)) + 8 * kTicksPerBar);
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
    return {headerWidth_, kPlaylistRulerHeight, std::max(0, v.width() - headerWidth_),
        std::max(0, v.height() - kPlaylistRulerHeight)};
}

QRect PlaylistView::rulerRect() const
{
    const QRect v = viewport()->rect();
    return {headerWidth_, 0, std::max(0, v.width() - headerWidth_), kPlaylistRulerHeight};
}

double PlaylistView::tickAt(int x) const
{
    return (x - headerWidth_ + horizontalScrollBar()->value()) / pixelsPerTick();
}

void PlaylistView::syncLayout()
{
    syncSelection();
    const int count = trackCount();
    selectedTrack_ = count > 0 ? std::clamp(selectedTrack_, 0, count - 1) : -1;
    tops_.assign(static_cast<std::size_t>(count) + 1, 0);
    for (int t = 0; t < count; ++t) {
        const auto i = static_cast<std::size_t>(t);
        tops_[i + 1] = tops_[i] + (project_->trackVisible(i) ? project_->tracks()[i].height : 0);
    }
    if (meters_.size() != static_cast<std::size_t>(count))
        meters_.resize(static_cast<std::size_t>(count));
}

int PlaylistView::trackAt(int y) const
{
    const int content = y - kPlaylistRulerHeight + verticalScrollBar()->value();
    if (content < 0)
        return -1;
    const auto it = std::upper_bound(tops_.begin(), tops_.end(), content);
    return static_cast<int>(it - tops_.begin()) - 1;
}

int PlaylistView::yOfTrack(int track) const
{
    const int last = static_cast<int>(tops_.size()) - 1;
    const int top = track <= last ? tops_[static_cast<std::size_t>(std::max(0, track))]
                                  : tops_.back() + (track - last) * PlaylistTrack::kDefaultHeight;
    return kPlaylistRulerHeight + top - verticalScrollBar()->value();
}

int PlaylistView::trackHeight(int track) const
{
    if (track < 0 || track + 1 >= static_cast<int>(tops_.size()))
        return PlaylistTrack::kDefaultHeight;
    const auto i = static_cast<std::size_t>(track);
    return tops_[i + 1] - tops_[i];
}

QRectF PlaylistView::clipRect(const Clip& clip) const
{
    const double x = headerWidth_ + clip.start * pixelsPerTick() - horizontalScrollBar()->value();
    const double w = project_ != nullptr ? project_->clipLength(clip) * pixelsPerTick() : pixelsPerBar_;
    return {x, static_cast<double>(yOfTrack(clip.track)), w, static_cast<double>(trackHeight(clip.track))};
}

QRectF PlaylistView::clipRect(const AudioClip& clip) const
{
    const double x = headerWidth_ + clip.start * pixelsPerTick() - horizontalScrollBar()->value();
    return {x, static_cast<double>(yOfTrack(clip.track)), audioLength(clip) * pixelsPerTick(),
        static_cast<double>(trackHeight(clip.track))};
}

std::optional<PlaylistView::ClipRef> PlaylistView::clipAt(const QPoint& pos) const
{
    if (project_ == nullptr || !gridRect().contains(pos))
        return std::nullopt;
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

QPixmap PlaylistView::waveTile(const ClipAudio* info, int tileIndex, int waveH, double secondsPerPixel,
    const QColor& lineColor, qreal dpr) const
{
    const unsigned gen = Theme::generation();
    if (gen != cachedThemeGen_) {
        cachedThemeGen_ = gen;
        waveTiles_.clear();
    }

    const auto nsPerPx = static_cast<long long>(std::llround(secondsPerPixel * 1e9));
    const WaveTileKey key{info, info->peaks.size(), tileIndex, waveH, nsPerPx, lineColor.rgba(),
        static_cast<int>(std::lround(dpr * 100.0))};
    if (const auto it = waveTiles_.find(key); it != waveTiles_.end())
        return it->second;

    if (waveTiles_.size() >= static_cast<std::size_t>(kMaxWaveTiles))
        waveTiles_.clear();

    const QSize physSize(static_cast<int>(std::ceil(kWaveTileWidth * dpr)),
        std::max(1, static_cast<int>(std::ceil(waveH * dpr))));
    QPixmap tile(physSize);
    tile.setDevicePixelRatio(dpr);
    tile.fill(Qt::transparent);

    double stepSec = info->peakSeconds;
    const float* peaks = info->peaks.data();
    std::size_t peakCount = info->peaks.size();
    for (const PeakMip& mip : info->mips) {
        if (!mip.peaks.empty() && mip.stepSeconds <= secondsPerPixel) {
            stepSec = mip.stepSeconds;
            peaks = mip.peaks.data();
            peakCount = mip.peaks.size();
        }
    }

    if (peakCount > 0 && stepSec > 0.0) {
        const double invStep = 1.0 / stepSec;
        const double mid = waveH * 0.5;
        const double half = waveH * 0.5;
        const double tileStartSec = static_cast<double>(tileIndex) * kWaveTileWidth * secondsPerPixel;

        std::array<QLineF, kWaveTileWidth> lines{};
        int lineCount = 0;
        for (int col = 0; col < kWaveTileWidth; ++col) {
            const double fromSec = tileStartSec + col * secondsPerPixel;
            const double toSec = fromSec + secondsPerPixel;
            const double rawFirst = fromSec * invStep;
            if (rawFirst >= static_cast<double>(peakCount))
                break;
            const auto first = static_cast<std::size_t>(std::max(0.0, rawFirst));
            const auto last
                = std::min(peakCount - 1, static_cast<std::size_t>(std::max(0.0, toSec * invStep)));
            float peak = 0.0f;
            for (std::size_t i = first; i <= last; ++i) {
                if (peaks[i] > peak)
                    peak = peaks[i];
            }
            const double h = std::min(1.0f, peak) * half;
            if (h >= 0.5)
                lines[static_cast<std::size_t>(lineCount++)] = QLineF(col + 0.5, mid - h, col + 0.5, mid + h);
        }

        if (lineCount > 0) {
            QPainter tp(&tile);
            tp.setRenderHint(QPainter::Antialiasing, false);
            tp.setPen(QPen(lineColor, 1));
            tp.drawLines(lines.data(), lineCount);
        }
    }

    waveTiles_.emplace(key, tile);
    return tile;
}

void PlaylistView::paintAudioClip(
    QPainter& p, const AudioClip& clip, const QRect& clipArea, bool dragged, bool selected) const
{
    const Theme::Colors& c = Theme::colors();
    const QRectF r = clipRect(clip).adjusted(1.0, 2.0, -1.0, -2.0);
    QColor color = clip.muted ? c.textDim : trackColorOr(clip.track, c.accent);
    QColor body = color;
    body.setAlpha(dragged ? 110 : 60);
    p.setPen(QPen(color, 1));
    p.setBrush(body);
    p.drawRoundedRect(r, 3, 3);

    const ClipAudio* info = audioOf(clip);
    const QRectF wave = r.adjusted(2, 14, -2, -2);
    const int waveH = static_cast<int>(std::round(wave.height()));
    if (info != nullptr && !info->peaks.empty() && info->peakSeconds > 0.0 && waveH > 2) {
        const double secondsPerPixel = 60.0 / (clipTempo(clip) * kTicksPerBeat) / pixelsPerTick();
        const QRect vis
            = wave.intersected(QRectF(clipArea)).toAlignedRect().intersected(wave.toAlignedRect());
        if (!vis.isEmpty() && secondsPerPixel > 0.0) {
            const double offsetPx
                = (clip.offset * 60.0 / (clipTempo(clip) * kTicksPerBeat)) / secondsPerPixel;
            const long long originX = std::llround(wave.left() - offsetPx);
            const long long firstTile
                = std::max(0LL, (static_cast<long long>(vis.left()) - originX) / kWaveTileWidth);
            const long long lastTile
                = std::max(0LL, (static_cast<long long>(vis.right() - 1) - originX) / kWaveTileWidth);
            QColor line = color;
            line.setAlpha(210);
            const qreal dpr = p.device() != nullptr ? p.device()->devicePixelRatioF() : 1.0;

            p.save();
            p.setClipRect(vis);
            p.setRenderHint(QPainter::Antialiasing, false);
            for (long long ti = firstTile; ti <= lastTile && ti <= 1'000'000LL; ++ti) {
                const QPixmap tile = waveTile(info, static_cast<int>(ti), waveH, secondsPerPixel, line, dpr);
                const auto drawX = static_cast<int>(originX + ti * kWaveTileWidth);
                p.drawPixmap(QPointF(drawX, wave.top()), tile);
            }
            p.restore();
        }
    } else if (info != nullptr && info->loading && wave.height() > 8 && wave.width() > 36) {
        QFont loadingFont = font();
        loadingFont.setPointSizeF(loadingFont.pointSizeF() > 0 ? loadingFont.pointSizeF() * 0.75 : 7.0);
        p.setFont(loadingFont);
        QColor loadingText = color.lighter(135);
        loadingText.setAlpha(200);
        p.setPen(loadingText);
        p.drawText(wave.adjusted(4, 0, -4, 0), Qt::AlignLeft | Qt::AlignVCenter, tr("Loading…"));
    }

    const QRectF title(r.left(), r.top(), r.width(), 13);
    if (title.intersects(clipArea)) {
        p.fillRect(title.adjusted(1, 1, -1, 0), color);
        p.setPen(c.base);
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() > 0 ? f.pointSizeF() * 0.75 : 7.0);
        f.setBold(true);
        p.setFont(f);
        QString name = clip.channel < project_->channels().size()
            ? QString::fromStdString(project_->channels()[clip.channel].name)
            : QString();
        if (clip.fitted())
            name += QStringLiteral("  ·  %1 BPM")
                        .arg(QString::number(
                            clip.sourceBpm, 'f', clip.sourceBpm == std::round(clip.sourceBpm) ? 0 : 1));
        p.drawText(title.adjusted(4, 0, -2, 0), Qt::AlignLeft | Qt::AlignVCenter,
            p.fontMetrics().elidedText(name, Qt::ElideRight, static_cast<int>(title.width()) - 6));
    }
    if (selected) {
        p.setPen(QPen(c.text, 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    }
}

std::optional<std::pair<int, int>> PlaylistView::dropTarget(const QPoint& pos) const
{
    if (project_ == nullptr || pos.y() < kPlaylistRulerHeight || pos.y() >= viewport()->height()
        || pos.x() < 0 || pos.x() >= viewport()->width()) {
        return std::nullopt;
    }
    const int rawTrack = trackAt(pos.y());
    const int track = rawTrack < 0 ? 0 : std::min(rawTrack, trackCount());
    const int startTick
        = pos.x() >= headerWidth_ ? std::max(0, snapDown(tickAt(pos.x()), snapTicks(Qt::NoModifier))) : 0;
    return std::pair{track, startTick};
}

void PlaylistView::dragEnterEvent(QDragEnterEvent* event)
{
    if (!droppedProjectFile(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    if (project_ != nullptr
        && (droppedPlugin(event->mimeData()).has_value() || !droppedAudio(event->mimeData()).isEmpty()
            || !droppedMidiFiles(event->mimeData()).isEmpty())) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void PlaylistView::dragMoveEvent(QDragMoveEvent* event)
{
    if (!droppedProjectFile(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    syncLayout();
    const auto plug = droppedPlugin(event->mimeData());
    const QStringList audioFiles = plug.has_value() ? QStringList{} : droppedAudioFiles(event->mimeData());
    const QStringList midiFiles
        = (plug.has_value() || !audioFiles.isEmpty()) ? QStringList{} : droppedMidiFiles(event->mimeData());
    const qsizetype itemCount
        = plug.has_value() ? 1 : (!audioFiles.isEmpty() ? audioFiles.size() : midiFiles.size());
    const auto target = itemCount > 0 ? dropTarget(event->position().toPoint()) : std::nullopt;
    if (!target) {
        event->ignore();
        if (!dropHints_.empty()) {
            dropHints_.clear();
            lastDropTarget_.reset();
            lastDropCount_ = 0;
            viewport()->update();
        }
        return;
    }
    event->acceptProposedAction();
    if (lastDropTarget_ == target && lastDropCount_ == itemCount && !dropHints_.empty())
        return;
    lastDropTarget_ = target;
    lastDropCount_ = itemCount;

    const double x = headerWidth_ + target->second * pixelsPerTick() - horizontalScrollBar()->value();
    dropHints_.clear();
    if (plug.has_value()) {
        const int t = std::clamp(target->first, 0, Project::kMaxPlaylistTracks - 1);
        dropHints_.emplace_back(x, yOfTrack(t), pixelsPerBar_ * 2.0, trackHeight(t));
    } else {
        const double w = !midiFiles.isEmpty() ? pixelsPerBar_ * 4.0 : pixelsPerBar_;
        const std::vector<int> tracks
            = project_->dropTracks(target->first, static_cast<std::size_t>(itemCount));
        dropHints_.reserve(tracks.size());
        int lastTrack = -1;
        for (const int t : tracks) {
            if (t == lastTrack)
                continue;
            lastTrack = t;
            dropHints_.emplace_back(x, yOfTrack(t), w, trackHeight(t));
        }
    }
    updateGrid();
}

void PlaylistView::dragLeaveEvent(QDragLeaveEvent* /*event*/)
{
    if (!dropHints_.empty()) {
        dropHints_.clear();
        lastDropTarget_.reset();
        lastDropCount_ = 0;
        updateGrid();
    }
}

void PlaylistView::dropEvent(QDropEvent* event)
{
    syncLayout();
    dropHints_.clear();
    lastDropTarget_.reset();
    lastDropCount_ = 0;
    updateGrid();

    if (const QString projPath = droppedProjectFile(event->mimeData()); !projPath.isEmpty()) {
        event->acceptProposedAction();
        emit projectFileDropped(projPath);
        return;
    }

    const auto target = dropTarget(event->position().toPoint());
    if (!target) {
        event->ignore();
        return;
    }
    if (const auto plug = droppedPlugin(event->mimeData())) {
        event->acceptProposedAction();
        emit pluginDroppedOnPlaylist(*plug, target->first, target->second);
        return;
    }
    const QStringList paths = droppedAudioFiles(event->mimeData());
    if (!paths.isEmpty()) {
        event->acceptProposedAction();
        emit audioDropped(paths, target->first, target->second);
        return;
    }
    const QStringList midiPaths = droppedMidiFiles(event->mimeData());
    if (!midiPaths.isEmpty()) {
        event->acceptProposedAction();
        emit midiFilesDropped(midiPaths, target->first, target->second);
        return;
    }
    event->ignore();
}

void PlaylistView::updateScrollBars()
{
    syncLayout();
    const QRect grid = gridRect();
    horizontalScrollBar()->setPageStep(grid.width());
    horizontalScrollBar()->setRange(
        0, std::max(0, static_cast<int>(lengthTicks() * pixelsPerTick()) - grid.width()));
    verticalScrollBar()->setPageStep(grid.height());
    verticalScrollBar()->setRange(0, std::max(0, tops_.back() + kBottomAddTrackSpace - grid.height()));
}

void PlaylistView::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
    if (editingTrack_ >= 0 && inlineEditor_->isVisible())
        inlineEditor_->setGeometry(trackNameRect(editingTrack_));
}

void PlaylistView::scrollContentsBy(int /*dx*/, int /*dy*/)
{
    if (editingTrack_ >= 0 && inlineEditor_->isVisible())
        inlineEditor_->setGeometry(trackNameRect(editingTrack_));
    viewport()->update();
}

void PlaylistView::paintTrackMeter(QPainter& p, int track) const
{
    const QRect mr = trackMeterRect(track);
    if (mr.height() < 6 || mr.width() < 2)
        return;
    const Theme::Colors& c = Theme::colors();
    p.fillRect(mr, c.display);

    const auto idx = static_cast<std::size_t>(track);
    const TrackMeter m = idx < meters_.size() ? meters_[idx] : TrackMeter{};
    const int top = mr.top() + 1;
    const int barH = std::max(1, mr.height() - 2);
    const int unityY = top + static_cast<int>((1.0 - meterDbScale(0.0)) * barH);
    const int warnY = top + static_cast<int>((1.0 - meterDbScale(-6.0)) * barH);

    for (int ch = 0; ch < 2; ++ch) {
        const int x = mr.left() + 1 + ch * (kMeterBarWidth + kMeterBarGap);
        const double lvl = std::clamp(m.level[ch], 0.0, 1.0);
        if (lvl > 0.0) {
            const int litTop = top + static_cast<int>((1.0 - lvl) * barH);
            const int bottom = top + barH;
            const auto band = [&](int from, int to, const QColor& col) {
                const int y0 = std::max(from, litTop);
                if (to > y0)
                    p.fillRect(x, y0, kMeterBarWidth, to - y0, col);
            };
            band(warnY, bottom, c.displayText);
            band(unityY, warnY, c.accent);
            band(top, unityY, c.danger);
        }
        if (m.hold[ch] > 0.005) {
            const int holdY = top + static_cast<int>((1.0 - std::clamp(m.hold[ch], 0.0, 1.0)) * barH);
            p.fillRect(x, std::clamp(holdY, top, top + barH - 1), kMeterBarWidth, 1,
                m.clipped[ch] ? c.danger : c.text);
        }
    }
}

void PlaylistView::paintPillButton(
    QPainter& p, const QRect& r, const QString& label, bool on, const QColor& onColor) const
{
    const Theme::Colors& c = Theme::colors();
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor fill = on ? onColor : c.window;
    if (on)
        fill.setAlpha(210);
    p.setPen(QPen(on ? onColor.lighter(115) : c.border, 1));
    p.setBrush(fill);
    p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 2.5, 2.5);

    QFont f = font();
    f.setPointSizeF(f.pointSizeF() > 0 ? f.pointSizeF() * 0.78 : 7.5);
    f.setBold(true);
    p.setFont(f);
    p.setPen(on ? c.base : c.textDim);
    p.drawText(r, Qt::AlignCenter, label);
    p.restore();
}

void PlaylistView::paintRecordArmButton(QPainter& p, const QRect& r, bool armed) const
{
    const Theme::Colors& c = Theme::colors();
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor fill = armed ? c.danger : c.window;
    if (armed)
        fill.setAlpha(65);
    p.setPen(QPen(armed ? c.danger : c.border, 1));
    p.setBrush(fill);
    p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 2.5, 2.5);

    const QIcon armIcon
        = Theme::icon(armed ? QStringLiteral("track-arm-active") : QStringLiteral("track-arm"));
    armIcon.paint(&p, r.adjusted(2, 2, -2, -2));
    p.restore();
}

void PlaylistView::paintInspectorBox(
    QPainter& p, const QRect& r, const QString& prefix, const QString& text, bool active) const
{
    if (r.isEmpty())
        return;
    const Theme::Colors& c = Theme::colors();
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(c.border, 1));
    p.setBrush(c.window);
    p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 2.5, 2.5);

    QFont f = font();
    f.setPointSizeF(f.pointSizeF() > 0 ? f.pointSizeF() * 0.78 : 7.5);
    f.setBold(true);
    p.setFont(f);
    const int prefixW = p.fontMetrics().horizontalAdvance(prefix) + 8;
    p.setPen(active ? c.accent : c.textDim);
    p.drawText(QRect(r.left() + 5, r.top(), prefixW, r.height()), Qt::AlignLeft | Qt::AlignVCenter, prefix);

    // Subtle divider after prefix badge.
    QColor sep = c.border;
    sep.setAlpha(150);
    p.fillRect(QRect(r.left() + prefixW + 1, r.top() + 3, 1, r.height() - 6), sep);

    f.setBold(false);
    p.setFont(f);
    p.setPen(active ? c.text : c.textDim);
    const QRect labelRect(r.left() + prefixW + 6, r.top(), std::max(0, r.width() - prefixW - 20), r.height());
    p.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter,
        p.fontMetrics().elidedText(text, Qt::ElideMiddle, labelRect.width()));

    // Dropdown chevron on the right.
    const double cx = r.right() - 8.5;
    const double cy = r.center().y() + 0.5;
    QPolygonF chevron;
    chevron << QPointF(cx - 3.0, cy - 1.5) << QPointF(cx + 3.0, cy - 1.5) << QPointF(cx, cy + 2.2);
    p.setPen(Qt::NoPen);
    p.setBrush(c.textDim);
    p.drawPolygon(chevron);
    p.restore();
}

void PlaylistView::paintTrackHeader(QPainter& p, int track) const
{
    const QRect h(0, yOfTrack(track), headerWidth_, trackHeight(track));
    if (h.height() <= 0)
        return;

    const Theme::Colors& c = Theme::colors();
    const auto t = static_cast<std::size_t>(track);
    const PlaylistTrack& pt = project_->tracks()[t];
    const bool audible
        = mixer_ != nullptr ? trackEffectiveAudible(*project_, *mixer_, t) : project_->trackAudible(t);
    const bool selected = track == selectedTrack_;

    // Subtle alternating / tinted track header surface, elevated Fender Studio-style when selected.
    p.fillRect(h, selected ? c.raised : c.panel);
    if (selected) {
        QColor selWash = c.text;
        selWash.setAlpha(22);
        p.fillRect(h, selWash);
    }
    if (pt.color) {
        QColor headerTint = toQColor(*pt.color);
        headerTint.setAlpha(selected ? 26 : 14);
        p.fillRect(h, headerTint);
    }

    // Left 6 px Studio One / Fender Studio colour strip (brighter when selected).
    QColor unsetStrip = selected ? c.accent : c.textDim;
    if (!selected)
        unsetStrip.setAlpha(75);
    const QColor stripColor
        = pt.color ? (selected ? toQColor(*pt.color).lighter(118) : toQColor(*pt.color)) : unsetStrip;
    p.fillRect(QRect(0, h.top(), kColorBarWidth, h.height() - 1), stripColor);

    if (selected) {
        p.save();
        QColor outline = pt.color ? toQColor(*pt.color).lighter(125) : c.text;
        outline.setAlpha(95);
        p.setPen(QPen(outline, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRect(kColorBarWidth, h.top(), std::max(1, headerWidth_ - kColorBarWidth - 2),
            std::max(1, h.height() - 2)));
        p.restore();
    }

    // Sub-track group guide lines.
    QColor guide = c.border;
    guide.setAlpha(160);
    for (int level = 0; level < pt.depth; ++level)
        p.fillRect(
            QRect(kColorBarWidth + 3 + level * kIndent + kArrowWidth / 2, h.top(), 1, h.height()), guide);

    if (project_->hasSubTracks(t)) {
        const QRectF a = QRectF(arrowRect(track)).adjusted(2.0, 0, -2.0, 0);
        const double cy = a.center().y();
        const double half = a.width() / 2.0;
        QPolygonF arrow;
        if (pt.collapsed)
            arrow << QPointF(a.left() + half * 0.4, cy - half) << QPointF(a.left() + half * 1.6, cy)
                  << QPointF(a.left() + half * 0.4, cy + half);
        else
            arrow << QPointF(a.left(), cy - half * 0.6) << QPointF(a.right(), cy - half * 0.6)
                  << QPointF(a.center().x(), cy + half * 0.6);
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(c.textDim);
        p.drawPolygon(arrow);
        p.restore();
    }

    // Track number (1..N).
    {
        QFont numFont = font();
        numFont.setPointSizeF(numFont.pointSizeF() > 0 ? numFont.pointSizeF() * 0.82 : 8.0);
        numFont.setBold(true);
        p.setFont(numFont);
        p.setPen(selected ? c.text : c.textDim);
        p.drawText(trackNumberRect(track), Qt::AlignCenter, QString::number(track + 1));
    }

    // Studio One [M], [S], [Record Arm] & [Input Monitor] buttons with Vloom SVG icons.
    paintPillButton(p, muteButtonRect(track), QStringLiteral("M"), pt.muted, c.accent);
    paintPillButton(p, soloButtonRect(track), QStringLiteral("S"), pt.solo, c.playing);
    paintRecordArmButton(p, armButtonRect(track), track == armedTrack_);
    {
        const QRect monR = monitorButtonRect(track);
        const bool monOn = track == monitoredTrack_;
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        QColor fill = monOn ? c.accent : c.window;
        if (monOn)
            fill.setAlpha(55);
        p.setPen(QPen(monOn ? c.accent : c.border, 1));
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(monR).adjusted(0.5, 0.5, -0.5, -0.5), 2.5, 2.5);
        const QIcon monIcon
            = Theme::icon(monOn ? QStringLiteral("input-monitor-active") : QStringLiteral("input-monitor"));
        const QRect iconR = monR.adjusted(2, 2, -2, -2);
        monIcon.paint(&p, iconR);
        p.restore();
    }

    // Track name.
    if (track != editingTrack_ || inlineEditor_ == nullptr || !inlineEditor_->isVisible()) {
        const QRect textRect = trackNameRect(track);
        if (textRect.width() > 8) {
            QFont nameFont = font();
            if (selected)
                nameFont.setWeight(QFont::DemiBold);
            p.setFont(nameFont);
            p.setPen((audible || selected) ? c.text : c.textDim);
            p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                p.fontMetrics().elidedText(
                    QString::fromStdString(pt.name), Qt::ElideMiddle, textRect.width()));
        }
    }

    // Small waveform / instrument keyboard badge before the VU meter.
    std::optional<std::size_t> primaryCh = trackPrimaryChannel(*project_, t);
    if (!primaryCh && pt.insert > 0)
        primaryCh = insertPrimaryChannel(*project_, static_cast<std::size_t>(pt.insert));
    const auto isMidiOrInstChannel = [&](std::size_t chIdx) {
        const Channel& ch = project_->channels()[chIdx];
        if (ch.instrument.has_value())
            return true;
        if (!ch.samplePath.empty())
            return false;
        for (const Clip& pc : project_->clips()) {
            if (pc.track == track)
                return true;
        }
        return std::any_of(
            project_->patterns().begin(), project_->patterns().end(), [chIdx](const Pattern& pat) {
                return chIdx < pat.channels.size()
                    && (!pat.channels[chIdx].notes.empty() || !pat.channels[chIdx].controls.empty());
            });
    };
    const bool isInstTrack = primaryCh && isMidiOrInstChannel(*primaryCh);
    const QRect badge = trackBadgeRect(track);
    if (!badge.isEmpty() && primaryCh) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        const QColor badgeCol = pt.color ? toQColor(*pt.color).lighter(120) : c.textDim;
        const double bx = badge.left() + 1.0;
        const double bw = badge.width() - 2.0;
        const double cy = badge.center().y();
        if (isInstTrack) {
            // Mini Studio One piano keyboard glyph (instrument or pure MIDI channel).
            const QRectF kb(bx, cy - 4.5, bw, 9.0);
            p.setPen(QPen(badgeCol, 1.1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(kb, 1.2, 1.2);
            p.fillRect(QRectF(bx + bw * 0.22, kb.top(), 1.8, 5.2), badgeCol);
            p.fillRect(QRectF(bx + bw * 0.58, kb.top(), 1.8, 5.2), badgeCol);
        } else {
            // Mini Studio One waveform glyph.
            p.setPen(QPen(badgeCol, 1.2));
            QPolygonF waveGlyph;
            waveGlyph << QPointF(bx, cy) << QPointF(bx + bw * 0.2, cy - 4.0)
                      << QPointF(bx + bw * 0.38, cy + 3.5) << QPointF(bx + bw * 0.56, cy - 5.0)
                      << QPointF(bx + bw * 0.76, cy + 4.0) << QPointF(bx + bw, cy);
            p.drawPolyline(waveGlyph);
        }
        p.restore();
    }

    // Horizontal Volume & Pan bars (shown at standard height >= 48 px).
    const QRect volRect = volumeBarRect(track);
    const QRect panRect = panBarRect(track);
    if (!volRect.isEmpty() && !panRect.isEmpty()) {
        const double vol = (draggingSlider_ == HeaderSlider::Volume && sliderTrack_ == track)
            ? sliderValue_
            : (mixer_ != nullptr ? trackMixVolume(*project_, *mixer_, t) : pt.volume);
        const double pan = (draggingSlider_ == HeaderSlider::Pan && sliderTrack_ == track)
            ? sliderValue_
            : (mixer_ != nullptr ? trackMixPan(*project_, *mixer_, t) : pt.pan);

        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);

        // Volume bar: recessed pill + fill + 0 dB unity tick at 0.8.
        p.setPen(QPen(c.border, 1));
        p.setBrush(c.display);
        p.drawRoundedRect(QRectF(volRect).adjusted(0.5, 0.5, -0.5, -0.5), 3.0, 3.0);
        const double fillW = std::clamp(vol, 0.0, 1.0) * (volRect.width() - 2);
        if (fillW >= 1.0) {
            QColor vFill = (draggingSlider_ == HeaderSlider::Volume && sliderTrack_ == track)
                ? c.accent
                : (pt.color ? toQColor(*pt.color) : c.text);
            vFill.setAlpha(audible ? 185 : 95);
            p.setPen(Qt::NoPen);
            p.setBrush(vFill);
            p.drawRoundedRect(
                QRectF(volRect.left() + 1, volRect.top() + 1, fillW, volRect.height() - 2), 2.0, 2.0);
        }
        const int unityX
            = volRect.left() + 1 + static_cast<int>(std::lround(kUnityFaderPosition * (volRect.width() - 2)));
        QColor tickCol = c.text;
        tickCol.setAlpha(110);
        p.fillRect(QRect(unityX, volRect.top() + 1, 1, volRect.height() - 2), tickCol);

        // Pan bar: recessed pill + bipolar center fill + bright indicator dot.
        p.setPen(QPen(c.border, 1));
        p.setBrush(c.display);
        p.drawRoundedRect(QRectF(panRect).adjusted(0.5, 0.5, -0.5, -0.5), 3.0, 3.0);
        const double midX = panRect.center().x() + 0.5;
        const double thumbX = panRect.left() + 3.0
            + (std::clamp(pan, -1.0, 1.0) * 0.5 + 0.5) * std::max(1.0, panRect.width() - 6.0);
        if (std::abs(pan) > 0.01) {
            QColor pFill = (draggingSlider_ == HeaderSlider::Pan && sliderTrack_ == track)
                ? c.accent
                : (pt.color ? toQColor(*pt.color) : c.textDim);
            pFill.setAlpha(175);
            p.fillRect(QRectF(std::min(midX, thumbX), panRect.top() + 1.5, std::abs(thumbX - midX),
                           panRect.height() - 3.0),
                pFill);
        }
        p.fillRect(QRect(static_cast<int>(midX), panRect.top() + 1, 1, panRect.height() - 2), c.border);
        p.setPen(Qt::NoPen);
        p.setBrush((draggingSlider_ == HeaderSlider::Pan && sliderTrack_ == track) ? c.accent : c.text);
        p.drawEllipse(QPointF(thumbX, panRect.center().y() + 0.5), 2.6, 2.6);

        p.restore();
    }

    // Progressive expandable inspector rows when track is stretched taller.
    const QRect chRow = inspectorRowRect(track, 0);
    if (!chRow.isEmpty()) {
        QString chPrefix = tr("CH");
        QString chText = tr("No Channel · Assign…");
        if (primaryCh) {
            const Channel& ch = project_->channels()[*primaryCh];
            bool isMono = false;
            if (mixer_ != nullptr) {
                const int ins = trackMixerInsert(*project_, *mixer_, t);
                if (ins > 0 && static_cast<std::size_t>(ins) < mixer_->size())
                    isMono = mixer_->track(static_cast<std::size_t>(ins)).mono;
            }
            if (ch.instrument) {
                chText
                    = QStringLiteral("%1  ·  %2")
                          .arg(QString::fromStdString(ch.name), QString::fromStdString(ch.instrument->name));
            } else if (isInstTrack) {
                chText = QStringLiteral("%1  ·  %2")
                             .arg(QString::fromStdString(ch.name), tr("MIDI · No Instrument"));
            } else if (ch.samplePath.empty()) {
                chPrefix = tr("IN");
                if (!inputDeviceLabel_.isEmpty()) {
                    chText = QStringLiteral("%1  ·  %2")
                                 .arg(inputDeviceLabel_, isMono ? tr("Mono") : tr("Stereo (L+R)"));
                } else {
                    chText = isMono ? tr("Audio Input · Mono") : tr("Audio Input · Stereo (L+R)");
                }
            } else {
                const QString mode = isMono ? tr("Mono") : tr("Stereo");
                chText = QStringLiteral("%1  ·  %2").arg(QString::fromStdString(ch.name), mode);
            }
        }
        paintInspectorBox(p, chRow, chPrefix, chText, primaryCh.has_value());
    }

    const QRect routeRow = inspectorRowRect(track, 1);
    if (!routeRow.isEmpty()) {
        QString outText = tr("Unrouted");
        bool routed = false;
        if (mixer_ != nullptr) {
            const int ins = trackMixerInsert(*project_, *mixer_, t);
            if (ins == 0) {
                outText = tr("Master");
                routed = true;
            } else if (ins > 0 && static_cast<std::size_t>(ins) < mixer_->size()) {
                const MixerTrack& mt = mixer_->track(static_cast<std::size_t>(ins));
                const QString dstName
                    = (mt.output > 0 && static_cast<std::size_t>(mt.output) < mixer_->size())
                    ? QString::fromStdString(mixer_->track(static_cast<std::size_t>(mt.output)).name)
                    : tr("Main");
                outText = mt.named
                    ? tr("Ins %1 · %2 → %3").arg(ins).arg(QString::fromStdString(mt.name), dstName)
                    : tr("Insert %1 → %2").arg(ins).arg(dstName);
                routed = true;
            }
        }
        paintInspectorBox(p, routeRow, tr("OUT"), outText, routed);
    }

    // Live Stereo VU meter along the right edge.
    paintTrackMeter(p, track);

    // Bottom divider line.
    p.fillRect(QRect(0, h.bottom(), headerWidth_, 1), c.border);
}

void PlaylistView::paintLiveRecording(QPainter& p, const QRect& clipArea) const
{
    if (!liveRecording_)
        return;
    const LiveRecordingView& rec = *liveRecording_;
    const QRect raw = liveRecordingRect(rec);
    if (raw.isEmpty() || !raw.intersects(clipArea))
        return;

    const Theme::Colors& c = Theme::colors();
    const QRectF r = QRectF(raw).adjusted(0.5, 1.0, -1.5, -2.0);
    if (r.width() <= 1.0 || r.height() <= 4.0)
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    QColor bodyBg = c.danger;
    bodyBg.setAlpha(46);
    p.fillRect(r, c.window);
    p.fillRect(r, bodyBg);

    const double headerH = std::min(15.0, r.height() * 0.36);
    QColor headerBg = c.danger;
    headerBg.setAlpha(215);
    p.fillRect(QRectF(r.left(), r.top(), r.width(), headerH), headerBg);

    if (r.width() > 28.0 && headerH >= 11.0) {
        QFont hf = font();
        hf.setPointSizeF(hf.pointSizeF() > 0 ? hf.pointSizeF() * 0.78 : 7.5);
        hf.setBold(true);
        p.setFont(hf);
        p.setPen(c.text);
        const QString title
            = rec.label.isEmpty() ? tr("● Recording…") : tr("● %1 · Recording…").arg(rec.label);
        const QRectF trRect(r.left() + 5.0, r.top(), std::max(0.0, r.width() - 10.0), headerH);
        p.drawText(trRect, Qt::AlignLeft | Qt::AlignVCenter,
            p.fontMetrics().elidedText(title, Qt::ElideRight, static_cast<int>(trRect.width())));
    }

    // Real-time live growing waveform ("สเกลคลื่นเสียงขณะอัด").
    const QRectF waveArea(r.left() + 1.0, r.top() + headerH + 1.0, std::max(0.0, r.width() - 2.0),
        std::max(0.0, r.height() - headerH - 2.0));
    if (waveArea.height() >= 6.0 && waveArea.width() >= 1.0) {
        const double midY = waveArea.center().y();
        const double halfH = waveArea.height() * 0.46;

        QColor zeroLine = c.danger;
        zeroLine.setAlpha(85);
        p.setPen(QPen(zeroLine, 1.0));
        p.drawLine(QPointF(waveArea.left(), midY), QPointF(waveArea.right(), midY));

        if (rec.peaks != nullptr && !rec.peaks->empty() && rec.peakSeconds > 0.0 && tempo_ > 0.0) {
            const auto& peaks = *rec.peaks;
            const int xStart = std::max(static_cast<int>(std::floor(waveArea.left())), clipArea.left());
            const int xEnd = std::min(static_cast<int>(std::ceil(waveArea.right())), clipArea.right());
            const double ppt = pixelsPerTick();
            const int scrollX = horizontalScrollBar()->value();

            QColor waveFill = c.danger.lighter(135);
            waveFill.setAlpha(210);
            p.setRenderHint(QPainter::Antialiasing, false);
            const double secPerTick = 60.0 / (tempo_ * static_cast<double>(kTicksPerBeat));
            for (int x = xStart; x < xEnd; ++x) {
                const double t0
                    = std::max(0.0, (static_cast<double>(x - headerWidth_ + scrollX) / ppt) - rec.startTick);
                const double t1 = std::max(
                    t0, (static_cast<double>(x + 1 - headerWidth_ + scrollX) / ppt) - rec.startTick);
                const double s0 = t0 * secPerTick;
                const double s1 = t1 * secPerTick;
                const auto i0 = static_cast<std::size_t>(std::floor(s0 / rec.peakSeconds));
                if (i0 >= peaks.size())
                    break;
                const auto i1 = std::min(peaks.size(),
                    std::max(i0 + 1, static_cast<std::size_t>(std::ceil(s1 / rec.peakSeconds))));
                float pk = 0.0f;
                for (std::size_t k = i0; k < i1; ++k)
                    pk = std::max(pk, peaks[k]);
                const double amp = std::clamp(static_cast<double>(pk), 0.0, 1.0) * halfH;
                if (amp >= 0.5) {
                    const int yTop = static_cast<int>(std::floor(midY - amp));
                    const int yBot = static_cast<int>(std::ceil(midY + amp));
                    p.fillRect(x, yTop, 1, std::max(1, yBot - yTop), waveFill);
                }
            }
            p.setRenderHint(QPainter::Antialiasing, true);
        }
    }

    QColor borderCol = c.danger.lighter(115);
    borderCol.setAlpha(235);
    p.setPen(QPen(borderCol, 1.4));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r, 3.0, 3.0);
    p.restore();
}

void PlaylistView::paintEvent(QPaintEvent* event)
{
    syncLayout();
    const Theme::Colors& c = Theme::colors();
    QPainter p(viewport());
    const QRegion dirtyRegion = event->region();

    const QRect grid = gridRect();
    const timeline::Scale scale{
        horizontalScrollBar()->value() / pixelsPerTick(), pixelsPerTick(), headerWidth_};
    const QRegion gridDirty = dirtyRegion.intersected(grid);
    const QRect area = gridDirty.boundingRect();

    // Track lanes & grid lines (bounded strictly to gridDirty so header VU updates
    // never inflate the playhead repaint into a full-screen bounding box).
    if (!area.isEmpty()) {
        p.save();
        p.setClipRegion(gridDirty);
        p.fillRect(area, c.rail);
        QColor trackLine = c.border;
        trackLine.setAlpha(110);
        int row = 0;
        for (int t = 0; t < trackCount(); ++t) {
            const int height = trackHeight(t);
            if (height == 0)
                continue;
            const bool odd = row++ % 2 == 1;
            const QRect lane = QRect(area.left(), yOfTrack(t), area.width(), height).intersected(area);
            if (lane.isEmpty())
                continue;
            if (odd)
                p.fillRect(lane, c.sidebar);
            if (t == selectedTrack_) {
                QColor selLane = c.text;
                selLane.setAlpha(14);
                p.fillRect(lane, selLane);
            }
            if (const auto& color = project_->tracks()[static_cast<std::size_t>(t)].color) {
                QColor tint = toQColor(*color);
                tint.setAlpha(28);
                p.fillRect(lane, tint);
            }
            const int lineY = yOfTrack(t) + height - 1;
            if (lineY >= area.top() && lineY <= area.bottom())
                p.fillRect(QRect(area.left(), lineY, area.width(), 1), trackLine);
        }
        const int finest = (!snapEnabled_)
            ? kTicksPerBeat
            : (snap_ == Snap::Auto || snap_ == Snap::HalfStep ? kTicksPerStep / 2 : kTicksPerStep);
        timeline::paintGrid(p, area, scale, finest, timeline::GridStyle::Calm);
        p.restore();
    }

    // Pattern & Audio clips inside `gridDirty`.
    if (project_ != nullptr && !area.isEmpty()) {
        p.save();
        p.setClipRegion(gridDirty);
        p.setRenderHint(QPainter::Antialiasing, true);
        QFont clipTitleFont = font();
        clipTitleFont.setPointSizeF(clipTitleFont.pointSizeF() > 0 ? clipTitleFont.pointSizeF() * 0.75 : 7.0);
        clipTitleFont.setBold(true);

        const auto trackAudibleNow = [this](int trk) {
            const auto idx = static_cast<std::size_t>(trk);
            return mixer_ != nullptr ? trackEffectiveAudible(*project_, *mixer_, idx)
                                     : project_->trackAudible(idx);
        };
        const auto& clips = project_->clips();
        for (std::size_t i = 0; i < clips.size(); ++i) {
            if (trackHeight(clips[i].track) == 0)
                continue;
            const QRectF r = clipRect(clips[i]).adjusted(1.0, 2.0, -1.0, -2.0);
            if (!r.intersects(area))
                continue;
            p.setOpacity(trackAudibleNow(clips[i].track) ? 1.0 : 0.35);
            QColor color = clips[i].muted
                ? c.textDim
                : trackColorOr(clips[i].track, Theme::patternColor(clips[i].pattern));
            QColor body = color;
            body.setAlpha(dragging_ && !dragClip_.audio && i == dragClip_.index ? 140 : 90);
            p.setPen(QPen(color, 1));
            p.setBrush(body);
            p.drawRoundedRect(r, 3, 3);
            const QRectF title(r.left(), r.top(), r.width(), 13);
            if (title.intersects(area)) {
                p.fillRect(title.adjusted(1, 1, -1, 0), color);
                p.setPen(c.base);
                p.setFont(clipTitleFont);
                const auto& name = project_->patterns()[clips[i].pattern].name;
                p.drawText(title.adjusted(4, 0, -2, 0), Qt::AlignLeft | Qt::AlignVCenter,
                    QString::fromStdString(name));
            }
            // Render miniature MIDI notes (or step-sequencer ticks) inside the pattern clip body.
            const QRectF noteArea = r.adjusted(2.0, 14.5, -2.0, -2.5);
            if (noteArea.width() > 2.0 && noteArea.height() >= 5.0 && noteArea.intersects(area)
                && clips[i].pattern < project_->patterns().size()) {
                const Pattern& pat = project_->patterns()[clips[i].pattern];
                const int patLen = std::max(1, project_->patternLength(clips[i].pattern));
                const int clipLen = std::max(1, project_->clipLength(clips[i]));
                const double pxPerTick = pixelsPerTick();

                int minKey = 127;
                int maxKey = 0;
                bool hasNotes = false;
                for (const ChannelPattern& cp : pat.channels) {
                    for (const Note& n : cp.notes) {
                        minKey = std::min(minKey, n.key);
                        maxKey = std::max(maxKey, n.key);
                        hasNotes = true;
                    }
                }

                p.save();
                p.setClipRect(noteArea.intersected(area));
                QColor noteBarCol = clips[i].muted ? c.textDim.lighter(135) : color.lighter(175);
                noteBarCol.setAlpha(235);

                if (hasNotes) {
                    if (maxKey - minKey < 12) {
                        const int mid = (minKey + maxKey) / 2;
                        minKey = std::max(0, mid - 6);
                        maxKey = std::min(127, minKey + 12);
                    }
                    const double span = static_cast<double>(std::max(1, maxKey - minKey + 1));
                    const double noteH = std::clamp(noteArea.height() / span, 2.2, 5.5);

                    const double visRelLeft = std::max(0.0, (area.left() - r.left()) / pxPerTick);
                    const double visRelRight
                        = std::min<double>(clipLen, (area.right() + 2.0 - r.left()) / pxPerTick);
                    const int firstRep
                        = std::max(0, static_cast<int>((clips[i].offset + visRelLeft) / patLen));
                    const int lastRep
                        = std::max(firstRep, static_cast<int>((clips[i].offset + visRelRight) / patLen));

                    p.setPen(Qt::NoPen);
                    p.setBrush(noteBarCol);
                    for (int rep = firstRep; rep <= lastRep; ++rep) {
                        const int repBase = rep * patLen - clips[i].offset;
                        for (const ChannelPattern& cp : pat.channels) {
                            for (const Note& n : cp.notes) {
                                const int relStart = repBase + n.start;
                                const int relEnd = relStart + n.length;
                                if (relEnd <= 0 || relStart >= clipLen)
                                    continue;
                                const int clStart = std::max(0, relStart);
                                const int clEnd = std::min(clipLen, relEnd);
                                const double x0 = r.left() + clStart * pxPerTick;
                                const double w = std::max(2.0, (clEnd - clStart) * pxPerTick - 0.6);
                                if (x0 + w < area.left() || x0 > area.right())
                                    continue;
                                const double norm
                                    = static_cast<double>(maxKey - std::clamp(n.key, minKey, maxKey))
                                    / std::max(1.0, span - 1.0);
                                const double y0
                                    = noteArea.top() + norm * std::max(0.0, noteArea.height() - noteH);
                                p.drawRoundedRect(QRectF(x0, y0, w, noteH), 1.0, 1.0);
                            }
                        }
                    }
                } else {
                    // Fallback: draw active step-sequencer ticks when no piano-roll notes exist.
                    std::vector<std::size_t> stepChannels;
                    for (std::size_t ch = 0; ch < pat.channels.size(); ++ch) {
                        const auto& steps = pat.channels[ch].steps;
                        if (std::any_of(steps.begin(), steps.end(), [](bool on) { return on; }))
                            stepChannels.push_back(ch);
                    }
                    if (!stepChannels.empty()) {
                        const double rowH = std::clamp(
                            noteArea.height() / static_cast<double>(stepChannels.size()), 2.0, 5.0);
                        const int firstRep = clips[i].offset / patLen;
                        const int lastRep = (clips[i].offset + clipLen - 1) / patLen;
                        p.setPen(Qt::NoPen);
                        p.setBrush(noteBarCol);
                        for (int rep = firstRep; rep <= lastRep; ++rep) {
                            const int repBase = rep * patLen - clips[i].offset;
                            for (std::size_t rowIdx = 0; rowIdx < stepChannels.size(); ++rowIdx) {
                                const auto& steps = pat.channels[stepChannels[rowIdx]].steps;
                                const double y0 = noteArea.top()
                                    + (static_cast<double>(rowIdx) / static_cast<double>(stepChannels.size()))
                                        * std::max(0.0, noteArea.height() - rowH);
                                for (std::size_t s = 0; s < steps.size(); ++s) {
                                    if (!steps[s])
                                        continue;
                                    const int relStart = repBase + static_cast<int>(s) * kTicksPerStep;
                                    const int relEnd = relStart + kTicksPerStep;
                                    if (relEnd <= 0 || relStart >= clipLen)
                                        continue;
                                    const double x0 = r.left() + std::max(0, relStart) * pxPerTick;
                                    const double w = std::max(2.0,
                                        (std::min(clipLen, relEnd) - std::max(0, relStart)) * pxPerTick
                                            - 1.0);
                                    p.drawRoundedRect(QRectF(x0, y0, w, rowH), 1.0, 1.0);
                                }
                            }
                        }
                    }
                }
                p.restore();
            }
            if (isSelected(ClipRef{false, i})) {
                p.setPen(QPen(c.text, 2));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
            }
        }
        const auto& audio = project_->audioClips();
        for (std::size_t i = 0; i < audio.size(); ++i) {
            if (trackHeight(audio[i].track) > 0 && clipRect(audio[i]).intersects(area)) {
                p.setOpacity(trackAudibleNow(audio[i].track) ? 1.0 : 0.35);
                paintAudioClip(p, audio[i], area, dragging_ && dragClip_.audio && i == dragClip_.index,
                    isSelected(ClipRef{true, i}));
            }
        }
        p.setOpacity(1.0);
        paintLiveRecording(p, area);
        if (dropHints_.empty() && trackCount() == 0 && grid.width() > 220 && grid.height() > 90) {
            QFont emptyFont = font();
            emptyFont.setPointSizeF(emptyFont.pointSizeF() > 0 ? emptyFont.pointSizeF() * 0.92 : 9.5);
            p.setFont(emptyFont);
            QColor hintCol = c.textDim;
            hintCol.setAlpha(155);
            p.setPen(hintCol);
            p.drawText(grid.adjusted(24, 0, -24, 0), Qt::AlignCenter | Qt::TextWordWrap,
                tr("Right-click to Add Track (Ctrl+T), or drag Audio, MIDI (.mid) & VSTi Instruments here"));
        }
        if (!dropHints_.empty()) {
            QPen pen(c.accent, 1.5, Qt::DashLine);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            for (const QRectF& hint : dropHints_)
                if (hint.intersects(area))
                    p.drawRoundedRect(hint.adjusted(1, 2, -1, -2), 3, 3);
        }
        if (sliceLine_) {
            const int top = std::min(sliceLine_->fromTrack, sliceLine_->toTrack);
            const int bottom = std::max(sliceLine_->fromTrack, sliceLine_->toTrack);
            const double x
                = headerWidth_ + sliceLine_->tick * pixelsPerTick() - horizontalScrollBar()->value();
            const double y0 = yOfTrack(top);
            const double y1 = yOfTrack(bottom) + trackHeight(bottom);
            p.setPen(QPen(c.text, 1.5));
            p.drawLine(QPointF(std::floor(x) + 0.5, y0), QPointF(std::floor(x) + 0.5, y1));
            p.setPen(Qt::NoPen);
            p.setBrush(c.text);
            QPolygonF tip;
            tip << QPointF(x - 4, y0) << QPointF(x + 4, y0) << QPointF(x, y0 + 5);
            p.drawPolygon(tip);
        }
        if (band_) {
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

    if (!dirtyRegion.intersected(grid).isEmpty()) {
        p.save();
        p.setClipRegion(dirtyRegion.intersected(grid));
        if (hasLoopRange()) {
            const double lx1 = scale.x(loopStart_);
            const double lx2 = scale.x(loopEnd_);
            if (lx2 >= grid.left() && lx1 <= grid.right()) {
                const QColor baseCol = loopActive_ ? c.accent : c.textDim;
                if (loopActive_) {
                    QColor wash = baseCol;
                    wash.setAlpha(13);
                    const double cl1 = std::max<double>(grid.left(), lx1);
                    const double cl2 = std::min<double>(grid.right() + 1, lx2);
                    if (cl2 > cl1)
                        p.fillRect(QRectF(cl1, grid.top(), cl2 - cl1, grid.height()), wash);
                }
                QColor lineCol = baseCol;
                lineCol.setAlpha(loopActive_ ? 105 : 55);
                p.setPen(QPen(lineCol, 1.0, loopActive_ ? Qt::SolidLine : Qt::DashLine));
                if (lx1 >= grid.left() && lx1 <= grid.right())
                    p.drawLine(QPointF(lx1, grid.top()), QPointF(lx1, grid.bottom()));
                if (lx2 >= grid.left() && lx2 <= grid.right())
                    p.drawLine(QPointF(lx2, grid.top()), QPointF(lx2, grid.bottom()));
            }
        }
        timeline::paintPlayhead(p, grid, scale, playhead_);
        p.restore();
    }

    const QRect ruler(headerWidth_, 0, grid.width(), kPlaylistRulerHeight);
    const QRegion rulerDirty = dirtyRegion.intersected(ruler);
    if (!rulerDirty.isEmpty()) {
        p.save();
        p.setClipRegion(rulerDirty);
        p.fillRect(ruler, c.panel);
        // Subtle darker track trough along the top Loop Lane
        QColor trough = c.window;
        trough.setAlpha(155);
        p.fillRect(QRect(ruler.left(), 0, ruler.width(), kLoopLaneHeight), trough);
        const QRect barRuler(ruler.left(), kLoopLaneHeight, ruler.width(), timeline::kRulerHeight);
        timeline::paintRuler(p, barRuler, scale);

        if (hasLoopRange()) {
            const double fx1 = scale.x(loopStart_);
            const double fx2 = scale.x(loopEnd_);
            if (fx2 >= ruler.left() - 8.0 && fx1 <= ruler.right() + 8.0) {
                p.save();
                p.setRenderHint(QPainter::Antialiasing, true);
                const QColor barBase = loopActive_ ? c.accent : c.textDim;

                // Soft vertical glow wash across the ruler inside [loopStart, loopEnd]
                QColor rulerWash = barBase;
                rulerWash.setAlpha(loopActive_ ? 32 : 14);
                const double wx1 = std::max<double>(ruler.left(), fx1);
                const double wx2 = std::min<double>(ruler.right() + 1, fx2);
                if (wx2 > wx1)
                    p.fillRect(QRectF(wx1, kLoopLaneHeight, wx2 - wx1, barRuler.height() - 1), rulerWash);

                // Studio One-style top Loop Bar + inward triangular locator flags
                QColor fill = barBase;
                fill.setAlpha(loopActive_ ? 242 : 135);
                const double topY = 1.0;
                const double barBottomY = static_cast<double>(kLoopLaneHeight);
                const double flagBottomY = static_cast<double>(kLoopLaneHeight) + 5.5;
                const double span = std::max(2.0, fx2 - fx1);
                const double earW = std::min(6.5, span * 0.45);

                QPainterPath loopShape;
                loopShape.moveTo(fx1, topY);
                loopShape.lineTo(fx2, topY);
                loopShape.lineTo(fx2, flagBottomY);
                loopShape.lineTo(fx2 - earW, barBottomY);
                loopShape.lineTo(fx1 + earW, barBottomY);
                loopShape.lineTo(fx1, flagBottomY);
                loopShape.closeSubpath();
                p.fillPath(loopShape, fill);

                // Tactile top rim sheen
                QColor sheen = c.text;
                sheen.setAlpha(loopActive_ ? 85 : 30);
                p.setPen(QPen(sheen, 1.0));
                p.drawLine(QPointF(fx1 + 0.5, topY + 0.5), QPointF(fx2 - 0.5, topY + 0.5));
                p.restore();
            }
        }

        timeline::paintPlayheadHead(p, barRuler, scale, playhead_);
        p.restore();
    }

    // Track headers: intersect `dirtyRegion` with the header column independently
    // so simultaneous playhead + VU meter updates still take the fast meter-only path!
    const QRect fullHeaderCol(0, 0, headerWidth_, viewport()->height());
    const QRegion headerColDirty = dirtyRegion.intersected(fullHeaderCol);
    if (!headerColDirty.isEmpty()) {
        const QRect hdrBounds = headerColDirty.boundingRect();
        const bool meterOnly = project_ != nullptr && hdrBounds.right() < headerWidth_ - 1
            && hdrBounds.left() >= headerWidth_ - kMeterAreaWidth && hdrBounds.top() >= kPlaylistRulerHeight;

        if (meterOnly) {
            p.save();
            p.setClipRegion(headerColDirty);
            for (int t = 0; t < trackCount(); ++t) {
                const int th = trackHeight(t);
                if (th <= 0)
                    continue;
                const QRect h(0, yOfTrack(t), headerWidth_, th);
                if (h.intersects(hdrBounds))
                    paintTrackMeter(p, t);
            }
            p.restore();
        } else {
            const QRect headers(0, kPlaylistRulerHeight, headerWidth_, grid.height());
            const QRect dirtyHeaders = headers.intersected(hdrBounds);
            if (!dirtyHeaders.isEmpty()) {
                p.save();
                p.setClipRegion(headerColDirty.intersected(headers));
                p.fillRect(dirtyHeaders, c.panel);
                if (project_ != nullptr) {
                    for (int t = 0; t < trackCount(); ++t) {
                        const QRect h(0, yOfTrack(t), headerWidth_, trackHeight(t));
                        if (h.height() > 0 && h.intersects(dirtyHeaders))
                            paintTrackHeader(p, t);
                    }
                    // Subtle Studio One-style "+ Add Track" zone in the empty area below the last track.
                    const int emptyTop = std::max(kPlaylistRulerHeight, yOfTrack(trackCount()));
                    const int emptyH = viewport()->height() - emptyTop;
                    if (emptyH >= 28) {
                        const QRect hintBox(10, emptyTop + 8, std::max(40, headerWidth_ - 20), 34);
                        if (hintBox.intersects(dirtyHeaders)) {
                            p.save();
                            p.setRenderHint(QPainter::Antialiasing, true);
                            QColor borderCol = c.border;
                            borderCol.setAlpha(150);
                            p.setPen(QPen(borderCol, 1.0, Qt::DashLine));
                            QColor fillCol = c.window;
                            fillCol.setAlpha(90);
                            p.setBrush(fillCol);
                            p.drawRoundedRect(QRectF(hintBox).adjusted(0.5, 0.5, -0.5, -0.5), 5.0, 5.0);
                            QFont hintFont = font();
                            hintFont.setPointSizeF(
                                hintFont.pointSizeF() > 0 ? hintFont.pointSizeF() * 0.85 : 8.5);
                            p.setFont(hintFont);
                            p.setPen(c.textDim);
                            p.drawText(hintBox, Qt::AlignCenter, tr("+ Right-click to Add Track"));
                            p.restore();
                        }
                    }
                }
                p.restore();
            }

            const QRect topCorner(0, 0, headerWidth_, kPlaylistRulerHeight);
            if (hdrBounds.intersects(topCorner)) {
                p.fillRect(topCorner, c.panel);
                QFont hdrFont = font();
                hdrFont.setPointSizeF(hdrFont.pointSizeF() > 0 ? hdrFont.pointSizeF() * 0.75 : 7.0);
                hdrFont.setBold(true);
                p.setFont(hdrFont);
                p.setPen(c.textDim);
                p.drawText(topCorner.adjusted(10, 0, -14, 0), Qt::AlignLeft | Qt::AlignVCenter, tr("TRACKS"));
                for (int dy = -4; dy <= 4; dy += 4)
                    p.fillRect(QRect(headerWidth_ - 6, topCorner.center().y() + dy, 2, 2), c.textDim);
                p.fillRect(QRect(0, kPlaylistRulerHeight - 1, headerWidth_, 1), c.border);
            }

            p.fillRect(QRect(headerWidth_ - 1, 0, 1, viewport()->height()),
                resizingHeaderWidth_ ? c.accent : c.border);
        }
    }

    // Track drag-reorder insertion indicator across headers & timeline.
    if (draggingTrackOrder_ && trackDropSlot_ >= 0 && trackDropSlot_ <= trackCount()) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        const int lineY
            = std::clamp(yOfTrack(trackDropSlot_), kPlaylistRulerHeight + 1, viewport()->height() - 2);
        p.fillRect(QRect(0, lineY - 1, viewport()->width(), 3), c.accent);
        p.setPen(Qt::NoPen);
        p.setBrush(c.accent.lighter(125));
        p.drawEllipse(QPointF(8.0, lineY + 0.5), 4.0, 4.0);
        p.restore();
    }
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

void PlaylistView::showSliderTooltip(
    int track, HeaderSlider slider, double value, const QPoint& globalPos) const
{
    if (project_ == nullptr || track < 0 || track >= trackCount())
        return;
    const QString trackName
        = QString::fromStdString(project_->tracks()[static_cast<std::size_t>(track)].name);
    if (slider == HeaderSlider::Volume) {
        const QString db = QString::fromStdString(formatDb(faderToGain(value)));
        QToolTip::showText(globalPos, tr("%1 · Volume: %2").arg(trackName, db), viewport());
    } else if (slider == HeaderSlider::Pan) {
        QToolTip::showText(globalPos, tr("%1 · Pan: %2").arg(trackName, formatPanText(value)), viewport());
    }
}

bool PlaylistView::viewportEvent(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        syncLayout();
        auto* help = static_cast<QHelpEvent*>(event);
        if (onHeaderDivider(help->pos())) {
            QToolTip::showText(help->globalPos(),
                tr("Drag to widen or narrow track headers; double-click to reset"), viewport());
            return true;
        }
        if (rulerRect().contains(help->pos())) {
            if (help->pos().y() < kLoopLaneHitHeight) {
                QToolTip::showText(help->globalPos(),
                    tr("Loop Bar: drag to set loop range, drag edges/body to adjust, click to toggle active, "
                       "right-click for Loop / Mixdown options (Ctrl+L loops selected clips)"),
                    viewport());
            } else {
                QToolTip::showText(help->globalPos(),
                    tr("Timeline Ruler: click/drag to scrub, Ctrl+drag to set Loop Bar, scroll wheel to "
                       "zoom, "
                       "right-click for Loop / Mixdown menu"),
                    viewport());
            }
            return true;
        }
        if (const int track = headerTrackAt(help->pos()); track >= 0) {
            const auto t = static_cast<std::size_t>(track);
            if (resizeEdgeAt(help->pos()) >= 0) {
                QToolTip::showText(help->globalPos(),
                    tr("Drag vertically to reveal Volume/Pan and Channel/Output menus; double-click to "
                       "toggle"),
                    viewport());
                return true;
            }
            if (trackColorRect(track).contains(help->pos())) {
                QToolTip::showText(help->globalPos(), tr("Click to change track color"), viewport());
                return true;
            }
            if (project_->hasSubTracks(t) && arrowRect(track).contains(help->pos())) {
                QToolTip::showText(help->globalPos(),
                    project_->tracks()[t].collapsed ? tr("Show sub-tracks") : tr("Hide sub-tracks"),
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
            if (armButtonRect(track).contains(help->pos())) {
                const QString devInfo = inputDeviceLabel_.isEmpty()
                    ? tr("right-click to select microphone")
                    : tr("Input: %1 — right-click to switch").arg(inputDeviceLabel_);
                QToolTip::showText(help->globalPos(),
                    tr("Record Arm (%1): arm this track to record live Audio or MIDI when pressing Play")
                        .arg(devInfo),
                    viewport());
                return true;
            }
            if (monitorButtonRect(track).contains(help->pos())) {
                const QString devInfo = inputDeviceLabel_.isEmpty()
                    ? tr("right-click to select microphone")
                    : tr("Input: %1 — right-click to switch").arg(inputDeviceLabel_);
                QToolTip::showText(help->globalPos(),
                    tr("Input Monitor (%1): hear live audio input through this track's Mixer FX chain")
                        .arg(devInfo),
                    viewport());
                return true;
            }
            if (volumeBarRect(track).contains(help->pos())) {
                const double vol = mixer_ != nullptr ? trackMixVolume(*project_, *mixer_, t)
                                                     : project_->tracks()[t].volume;
                QToolTip::showText(help->globalPos(),
                    tr("Volume: %1\nDrag to adjust (Shift: fine); double-click for 0.0 dB")
                        .arg(QString::fromStdString(formatDb(faderToGain(vol)))),
                    viewport());
                return true;
            }
            if (panBarRect(track).contains(help->pos())) {
                const double pan
                    = mixer_ != nullptr ? trackMixPan(*project_, *mixer_, t) : project_->tracks()[t].pan;
                QToolTip::showText(help->globalPos(),
                    tr("Pan: %1\nDrag to adjust (Shift: fine); double-click for Center")
                        .arg(formatPanText(pan)),
                    viewport());
                return true;
            }
            if (inspectorRowRect(track, 0).contains(help->pos())) {
                QToolTip::showText(
                    help->globalPos(), tr("Channel / source assigned to this track"), viewport());
                return true;
            }
            if (inspectorRowRect(track, 1).contains(help->pos())) {
                QToolTip::showText(
                    help->globalPos(), tr("Mixer insert output routing (click to route)"), viewport());
                return true;
            }
            if (trackMeterRect(track).contains(help->pos())) {
                QToolTip::showText(help->globalPos(), tr("Stereo track level meter (L | R)"), viewport());
                return true;
            }
            QToolTip::showText(help->globalPos(),
                tr("%1\nDouble-click name to rename; drag bottom edge down for Channel & routing menus")
                    .arg(QString::fromStdString(project_->tracks()[t].name)),
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

bool PlaylistView::onHeaderDivider(const QPoint& pos) const
{
    return pos.y() >= 0 && pos.y() < viewport()->height()
        && std::abs(pos.x() - (headerWidth_ - 1)) <= kHeaderDividerGrip;
}

int PlaylistView::headerTrackAt(const QPoint& pos) const
{
    if (pos.x() < 0 || pos.x() >= headerWidth_ - kHeaderDividerGrip || pos.y() < kPlaylistRulerHeight)
        return -1;
    const int track = trackAt(pos.y());
    return track >= 0 && track < trackCount() ? track : -1;
}

QRect PlaylistView::trackColorRect(int track) const
{
    return {0, yOfTrack(track), kColorBarWidth + 2, trackHeight(track)};
}

int PlaylistView::headerTopRowY(int track) const
{
    const int y = yOfTrack(track);
    const int h = trackHeight(track);
    return h >= kSliderShowHeight ? y + 5 : y + (h - kTrackButton) / 2;
}

QRect PlaylistView::arrowRect(int track) const
{
    const int depth = project_ != nullptr && track >= 0 && track < trackCount()
        ? project_->tracks()[static_cast<std::size_t>(track)].depth
        : 0;
    return {kColorBarWidth + 2 + depth * kIndent, headerTopRowY(track), kArrowWidth, kTrackButton};
}

QRect PlaylistView::trackNumberRect(int track) const
{
    const int depth = project_ != nullptr && track >= 0 && track < trackCount()
        ? project_->tracks()[static_cast<std::size_t>(track)].depth
        : 0;
    const bool hasArrow = project_ != nullptr && track >= 0 && track < trackCount()
        && project_->hasSubTracks(static_cast<std::size_t>(track));
    const int left = kColorBarWidth + 3 + depth * kIndent + (hasArrow ? kArrowWidth + 1 : 0);
    return {left, headerTopRowY(track), kTrackNumWidth, kTrackButton};
}

QRect PlaylistView::muteButtonRect(int track) const
{
    return {trackNumberRect(track).right() + 2, headerTopRowY(track), kTrackButton, kTrackButton};
}

QRect PlaylistView::soloButtonRect(int track) const
{
    return {muteButtonRect(track).right() + kTrackButtonGap + 1, headerTopRowY(track), kTrackButton,
        kTrackButton};
}

QRect PlaylistView::armButtonRect(int track) const
{
    return {soloButtonRect(track).right() + kTrackButtonGap + 1, headerTopRowY(track), kTrackButton,
        kTrackButton};
}

QRect PlaylistView::monitorButtonRect(int track) const
{
    return {
        armButtonRect(track).right() + kTrackButtonGap + 1, headerTopRowY(track), kTrackButton, kTrackButton};
}

QRect PlaylistView::trackBadgeRect(int track) const
{
    const int rightEdge = headerWidth_ - kMeterAreaWidth - 4;
    if (rightEdge - kBadgeSize <= monitorButtonRect(track).right() + 36)
        return {};
    return {rightEdge - kBadgeSize, headerTopRowY(track) + (kTrackButton - kBadgeSize) / 2, kBadgeSize,
        kBadgeSize};
}

QRect PlaylistView::trackNameRect(int track) const
{
    const int left = monitorButtonRect(track).right() + 6;
    const QRect badge = trackBadgeRect(track);
    const int right = badge.isEmpty() ? headerWidth_ - kMeterAreaWidth - 4 : badge.left() - 4;
    return {left, headerTopRowY(track) - 1, std::max(0, right - left), kTrackButton + 2};
}

QRect PlaylistView::volumeBarRect(int track) const
{
    const int h = trackHeight(track);
    if (h < kSliderShowHeight)
        return {};
    const int left = trackNumberRect(track).left();
    const int right = headerWidth_ - kMeterAreaWidth - 5;
    if (right - left < 36)
        return {};
    return {left, yOfTrack(track) + 28, right - left, kSliderHeight};
}

QRect PlaylistView::panBarRect(int track) const
{
    const int h = trackHeight(track);
    if (h < kSliderShowHeight)
        return {};
    const int left = trackNumberRect(track).left();
    const int right = headerWidth_ - kMeterAreaWidth - 5;
    if (right - left < 36)
        return {};
    return {left, yOfTrack(track) + 39, right - left, kSliderHeight};
}

QRect PlaylistView::inspectorRowRect(int track, int row) const
{
    if (row < 0 || row > 1)
        return {};
    const int h = trackHeight(track);
    const int minH = row == 0 ? kMenuRow0Height : kMenuRow1Height;
    if (h < minH)
        return {};
    const int left = trackNumberRect(track).left();
    const int right = headerWidth_ - kMeterAreaWidth - 5;
    if (right - left < 48)
        return {};
    return {left, yOfTrack(track) + 52 + row * 22, right - left, 18};
}

QRect PlaylistView::trackMeterRect(int track) const
{
    const int h = trackHeight(track);
    if (h <= 4)
        return {};
    const int w = 2 * kMeterBarWidth + kMeterBarGap + 2; // 9 px housing
    return {headerWidth_ - kMeterAreaWidth, yOfTrack(track) + 2, w, std::max(4, h - 5)};
}

int PlaylistView::resizeEdgeAt(const QPoint& pos) const
{
    if (pos.x() < 0 || pos.x() >= headerWidth_ - kHeaderDividerGrip || pos.y() < kPlaylistRulerHeight)
        return -1;
    const int track = trackAt(pos.y());
    if (track < 0 || track >= trackCount())
        return -1;
    return pos.y() >= yOfTrack(track) + trackHeight(track) - kResizeGrip ? track : -1;
}

bool PlaylistView::handleTrackHeaderClick(int track, const QPoint& pos, const QPoint& globalPos)
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
    if (armButtonRect(track).contains(pos)) {
        emit trackRecordArmToggled(t, track != armedTrack_);
        return true;
    }
    if (monitorButtonRect(track).contains(pos)) {
        emit trackMonitorToggled(t, track != monitoredTrack_);
        return true;
    }
    if (const QRect r0 = inspectorRowRect(track, 0); !r0.isEmpty() && r0.contains(pos)) {
        emit trackChannelMenuRequested(t, QPoint(viewport()->mapToGlobal(r0.bottomLeft())));
        return true;
    }
    if (const QRect r1 = inspectorRowRect(track, 1); !r1.isEmpty() && r1.contains(pos)) {
        emit trackRoutingMenuRequested(t, QPoint(viewport()->mapToGlobal(r1.bottomLeft())));
        return true;
    }
    if (trackMeterRect(track).contains(pos)) {
        if (t < meters_.size()) {
            meters_[t].clipped[0] = false;
            meters_[t].clipped[1] = false;
            meters_[t].hold[0] = 0.0;
            meters_[t].hold[1] = 0.0;
            viewport()->update(trackMeterRect(track));
        }
        return true;
    }
    (void)globalPos;
    return false;
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
            updateHeaders();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            commitTrackNameEdit();
            return true;
        }
    }
    if (watched == viewport() && playing_
        && (event->type() == QEvent::UngrabMouse || event->type() == QEvent::Hide
            || event->type() == QEvent::WindowDeactivate))
        endPlayback();
    return QAbstractScrollArea::eventFilter(watched, event);
}

void PlaylistView::endPlayback()
{
    if (!playing_)
        return;
    playing_ = false;
    updateToolCursor(viewport()->mapFromGlobal(QCursor::pos()));
    emit playbackReleased();
}

void PlaylistView::populateAddTrackActions(QMenu& menu, int afterTrack)
{
    const bool canAdd = project_ != nullptr
        && project_->tracks().size() < static_cast<std::size_t>(Project::kMaxPlaylistTracks);
    QAction* dlgAct = menu.addAction(Theme::icon(QStringLiteral("plus")), tr("Add Tracks…"), this,
        [this, afterTrack] { emit addTracksDialogRequested(afterTrack); });
    dlgAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    dlgAct->setEnabled(canAdd);

    QAction* monoAct
        = menu.addAction(Theme::icon(QStringLiteral("track-audio")), tr("Add Audio Track (mono)"), this,
            [this, afterTrack] { emit addAudioTrackRequested(afterTrack, true); });
    monoAct->setEnabled(canAdd);

    QAction* stereoAct
        = menu.addAction(Theme::icon(QStringLiteral("track-audio")), tr("Add Audio Track (stereo)"), this,
            [this, afterTrack] { emit addAudioTrackRequested(afterTrack, false); });
    stereoAct->setEnabled(canAdd);

    QAction* instAct
        = menu.addAction(Theme::icon(QStringLiteral("track-instrument")), tr("Add Instrument Track"), this,
            [this, afterTrack] { emit addInstrumentTrackRequested(afterTrack); });
    instAct->setEnabled(canAdd);
}

void PlaylistView::mousePressEvent(QMouseEvent* event)
{
    syncLayout();
    const QPoint pos = event->position().toPoint();
    const QPoint globalPos = event->globalPosition().toPoint();

    if (editingTrack_ >= 0 && inlineEditor_->isVisible()) {
        if (!inlineEditor_->geometry().contains(pos))
            commitTrackNameEdit();
    }

    if (event->button() == Qt::LeftButton && onHeaderDivider(pos)) {
        resizingHeaderWidth_ = true;
        headerResizeStartX_ = pos.x();
        headerResizeStartWidth_ = headerWidth_;
        viewport()->setCursor(Qt::SplitHCursor);
        viewport()->update(QRect(headerWidth_ - 2, 0, 4, viewport()->height()));
        return;
    }

    if (const int track = resizeEdgeAt(pos); track >= 0 && event->button() == Qt::LeftButton) {
        resizingTrack_ = track;
        resizeStartY_ = pos.y();
        resizeStartHeight_ = project_->tracks()[static_cast<std::size_t>(track)].height;
        return;
    }

    if (const int track = headerTrackAt(pos); track >= 0 && event->button() == Qt::LeftButton) {
        selectTrackByUser(track);
        const auto t = static_cast<std::size_t>(track);
        if (trackColorRect(track).contains(pos)) {
            showColorPicker(this, viewport()->mapToGlobal(QPoint(kColorBarWidth + 4, yOfTrack(track))),
                project_->tracks()[t].color,
                [this, t](std::optional<std::uint32_t> color) { emit trackColorChanged(t, color); });
            return;
        }
        if (const QRect vr = volumeBarRect(track); !vr.isEmpty() && vr.contains(pos)) {
            const double cur
                = mixer_ != nullptr ? trackMixVolume(*project_, *mixer_, t) : project_->tracks()[t].volume;
            const int curX = vr.left() + 1 + static_cast<int>(std::lround(cur * (vr.width() - 2)));
            draggingSlider_ = HeaderSlider::Volume;
            sliderTrack_ = track;
            sliderLastX_ = pos.x();
            if (!event->modifiers().testFlag(Qt::ShiftModifier) && std::abs(pos.x() - curX) > 7) {
                sliderValue_ = std::clamp(
                    static_cast<double>(pos.x() - vr.left() - 1) / std::max(1, vr.width() - 2), 0.0, 1.0);
                emit trackVolumeChanged(t, sliderValue_);
            } else {
                sliderValue_ = cur;
            }
            viewport()->setCursor(Qt::SizeHorCursor);
            viewport()->update(QRect(0, yOfTrack(track), headerWidth_, trackHeight(track)));
            showSliderTooltip(track, HeaderSlider::Volume, sliderValue_, globalPos);
            return;
        }
        if (const QRect pr = panBarRect(track); !pr.isEmpty() && pr.contains(pos)) {
            const double cur
                = mixer_ != nullptr ? trackMixPan(*project_, *mixer_, t) : project_->tracks()[t].pan;
            const int curX = pr.left() + 3
                + static_cast<int>(std::lround((cur * 0.5 + 0.5) * std::max(1, pr.width() - 6)));
            draggingSlider_ = HeaderSlider::Pan;
            sliderTrack_ = track;
            sliderLastX_ = pos.x();
            if (!event->modifiers().testFlag(Qt::ShiftModifier) && std::abs(pos.x() - curX) > 7) {
                sliderValue_ = std::clamp(
                    (static_cast<double>(pos.x() - pr.left() - 3) / std::max(1, pr.width() - 6)) * 2.0 - 1.0,
                    -1.0, 1.0);
                emit trackPanChanged(t, sliderValue_);
            } else {
                sliderValue_ = cur;
            }
            viewport()->setCursor(Qt::SizeHorCursor);
            viewport()->update(QRect(0, yOfTrack(track), headerWidth_, trackHeight(track)));
            showSliderTooltip(track, HeaderSlider::Pan, sliderValue_, globalPos);
            return;
        }
        if (handleTrackHeaderClick(track, pos, globalPos))
            return;
        // Pressing anywhere else on the track header prepares drag-to-reorder.
        if (trackCount() > 1) {
            pendingTrackDrag_ = track;
            trackDragStartPos_ = pos;
            draggingTrackOrder_ = false;
            trackDropSlot_ = track;
        }
        return;
    }

    if (const int track = headerTrackAt(pos); track >= 0 && event->button() == Qt::RightButton) {
        selectTrackByUser(track);
        const auto t = static_cast<std::size_t>(track);
        if (armButtonRect(track).contains(pos) || monitorButtonRect(track).contains(pos)) {
            emit trackChannelMenuRequested(t, globalPos);
            return;
        }
        QMenu menu(this);
        populateAddTrackActions(menu, track);
        menu.addSeparator();
        menu.addAction(tr("Rename…"), this, [this, track] { startTrackNameEdit(track); });
        addColorMenu(menu, project_->tracks()[t].color,
            [this, t](std::optional<std::uint32_t> color) { emit trackColorChanged(t, color); });
        menu.addSeparator();
        QAction* armAct = menu.addAction(
            tr("Record Arm"), this, [this, t](bool on) { emit trackRecordArmToggled(t, on); });
        armAct->setCheckable(true);
        armAct->setChecked(track == armedTrack_);
        QAction* monAct = menu.addAction(
            tr("Monitor Audio Input"), this, [this, t](bool on) { emit trackMonitorToggled(t, on); });
        monAct->setCheckable(true);
        monAct->setChecked(track == monitoredTrack_);
        menu.addSeparator();
        menu.addAction(tr("Audio Input / Source…"), this,
            [this, t, globalPos] { emit trackChannelMenuRequested(t, globalPos); });
        menu.addAction(tr("Mixer Insert Routing…"), this,
            [this, t, globalPos] { emit trackRoutingMenuRequested(t, globalPos); });
        menu.addAction(project_->tracks()[t].height < kExpandedInspectorHeight ? tr("Expand Track Controls")
                                                                               : tr("Compact Track Controls"),
            this, [this, t] {
                const int next = project_->tracks()[t].height < kExpandedInspectorHeight
                    ? kExpandedInspectorHeight
                    : PlaylistTrack::kDefaultHeight;
                emit trackHeightChanged(t, next);
            });
        menu.addSeparator();
        menu.addAction(tr("Reset Volume (0.0 dB)"), this,
            [this, t] { emit trackVolumeChanged(t, kUnityFaderPosition); });
        menu.addAction(tr("Reset Pan (Center)"), this, [this, t] { emit trackPanChanged(t, 0.0); });
        menu.addSeparator();
        menu.addAction(tr("Move to Top"), this, [this, t] { emit trackReordered(t, 0); })->setEnabled(t > 0);
        menu.addAction(tr("Move Up"), this, [this, t] { emit trackMoveRequested(t, false); })
            ->setEnabled(project_->canMoveTrackGroup(t, false));
        menu.addAction(tr("Move Down"), this, [this, t] { emit trackMoveRequested(t, true); })
            ->setEnabled(project_->canMoveTrackGroup(t, true));
        menu.addAction(tr("Move to Bottom"), this,
                [this, t] {
                    if (trackCount() > 1)
                        emit trackReordered(t, static_cast<std::size_t>(trackCount() - 1));
                })
            ->setEnabled(track + 1 < trackCount());
        menu.addSeparator();
        menu.addAction(tr("Make Sub-track"), this, [this, t] { emit trackIndentRequested(t); })
            ->setEnabled(project_->canIndentTrack(t));
        menu.addAction(tr("Move Out of Group"), this, [this, t] { emit trackOutdentRequested(t); })
            ->setEnabled(project_->tracks()[t].depth > 0);
        menu.addSeparator();
        QAction* remove
            = menu.addAction(tr("Delete “%1”").arg(QString::fromStdString(project_->tracks()[t].name)), this,
                [this, t] { emit trackRemoveRequested(t); });
        remove->setEnabled(trackCount() > 0);
        menu.exec(globalPos);
        return;
    }

    // Right-click in empty track-header space below the last track (or on the top-left TRACKS header)
    // opens the Studio One-style quick Add Track menu.
    if (project_ != nullptr && event->button() == Qt::RightButton && pos.x() >= 0 && pos.x() < headerWidth_) {
        QMenu menu(this);
        populateAddTrackActions(menu, -1);
        menu.exec(globalPos);
        return;
    }

    if (project_ == nullptr)
        return;
    const bool inTimeBar = rulerRect().contains(pos);
    if (inTimeBar && event->button() == Qt::RightButton) {
        QMenu menu(this);
        QAction* activeAct = menu.addAction(tr("Loop Active"));
        activeAct->setCheckable(true);
        activeAct->setChecked(loopActive_);
        connect(
            activeAct, &QAction::triggered, this, [this](bool checked) { emit loopActiveToggled(checked); });
        menu.addSeparator();
        QAction* setSelAct
            = menu.addAction(tr("Set Loop to Selection / Song"), this, &PlaylistView::loopSelectionOrSong);
        setSelAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
        QAction* clearAct = menu.addAction(tr("Clear Loop Range"), this, &PlaylistView::clearLoopRange);
        clearAct->setEnabled(hasLoopRange());
        menu.addSeparator();
        QAction* exportAct
            = menu.addAction(tr("Export Loop Range…"), this, [this] { emit exportLoopRangeRequested(); });
        exportAct->setEnabled(hasLoopRange());
        menu.exec(globalPos);
        return;
    }
    if (inTimeBar && event->button() == Qt::LeftButton && pos.x() >= headerWidth_
        && (pos.y() < kLoopLaneHitHeight || event->modifiers().testFlag(Qt::ControlModifier))) {
        loopPressPos_ = pos;
        loopDragMoved_ = false;
        loopOrigStart_ = loopStart_;
        loopOrigEnd_ = loopEnd_;
        const double ppt = pixelsPerTick();
        const int scrollX = horizontalScrollBar()->value();
        const double x1 = headerWidth_ + loopStart_ * ppt - scrollX;
        const double x2 = headerWidth_ + loopEnd_ * ppt - scrollX;
        const int grid = snapTicks(event->modifiers());
        if (hasLoopRange() && std::abs(pos.x() - x1) <= 6.0) {
            loopDrag_ = LoopDrag::ResizeStart;
            viewport()->setCursor(Qt::SizeHorCursor);
        } else if (hasLoopRange() && std::abs(pos.x() - x2) <= 6.0) {
            loopDrag_ = LoopDrag::ResizeEnd;
            viewport()->setCursor(Qt::SizeHorCursor);
        } else if (hasLoopRange() && pos.x() > x1 + 6.0 && pos.x() < x2 - 6.0
            && !event->modifiers().testFlag(Qt::ControlModifier)) {
            loopDrag_ = LoopDrag::Move;
            loopAnchorTick_ = static_cast<int>(std::lround(tickAt(pos.x())));
            viewport()->setCursor(Qt::SizeAllCursor);
        } else {
            loopDrag_ = LoopDrag::Draw;
            loopAnchorTick_ = std::max(0, snapNearest(tickAt(pos.x()), grid));
            viewport()->setCursor(Qt::CrossCursor);
        }
        return;
    }
    if ((tool_ == Tool::Playback || inTimeBar) && event->button() == Qt::LeftButton
        && pos.x() >= headerWidth_) {
        playing_ = true;
        viewport()->setCursor(Qt::SplitHCursor);
        lastPlaybackTick_ = std::max(0.0, tickAt(pos.x()));
        setPlayhead(lastPlaybackTick_);
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
        if (hit && hit->audio) {
            const QRectF r = clipRect(project_->audioClips()[hit->index]);
            if (pos.y() < r.top() + 16.0) {
                emit audioClipMenuRequested(hit->index, globalPos);
                return;
            }
        }
        if (hit && hit->audio)
            emit audioClipRemoved(hit->index);
        else if (hit)
            emit clipRemoved(hit->index);
        else if (pos.y() >= yOfTrack(trackCount())) {
            // Right-clicking in the empty timeline space below the last track also opens Add Track.
            QMenu menu(this);
            populateAddTrackActions(menu, -1);
            menu.exec(globalPos);
        }
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    if (hit) {
        const int hitTrack
            = hit->audio ? project_->audioClips()[hit->index].track : project_->clips()[hit->index].track;
        selectTrackByUser(hitTrack);
    } else if (const int laneTrack = trackAt(pos.y()); laneTrack >= 0 && laneTrack < trackCount()) {
        selectTrackByUser(laneTrack);
    }
    const bool ctrl = event->modifiers().testFlag(Qt::ControlModifier);
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
        if (trackCount() <= 0)
            return;
        const int track = std::clamp(trackAt(pos.y()), 0, trackCount() - 1);
        slicing_ = true;
        sliceLine_ = SliceLine{sliceTick(pos.x(), event->modifiers()), track, track};
        updateGrid();
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
            if (ctrl) {
                auto& flags = hit->audio ? selectedAudio_ : selectedClips_;
                flags[hit->index] = flags[hit->index] != 0 ? 0 : 1;
                updateGrid();
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
        updateGrid();
        return;
    case Tool::Playback:
        return;
    case Tool::Draw:
    case Tool::Paint:
        break;
    }
    if (hit) {
        if (ctrl) {
            auto& flags = hit->audio ? selectedAudio_ : selectedClips_;
            flags[hit->index] = flags[hit->index] != 0 ? 0 : 1;
            updateGrid();
            return;
        }
        if (isSelected(*hit) && selectionSize() > 1) {
            beginGroupDrag(pos);
        } else {
            clearSelection();
            (hit->audio ? selectedAudio_ : selectedClips_)[hit->index] = 1;
            beginDrag(*hit, pos);
        }
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
        painting_ = true;
        paintTrack_ = track;
        paintStart_ = start;
        paintLength_ = sourceLength();
        paintLastSlot_ = 0;
        return;
    }
    const std::size_t count = source_.audio ? project_->audioClips().size() : project_->clips().size();
    if (count > 0)
        beginDrag(ClipRef{source_.audio, count - 1}, pos);
}

void PlaylistView::mouseMoveEvent(QMouseEvent* event)
{
    syncLayout();
    const QPoint pos = event->position().toPoint();
    if (resizingHeaderWidth_) {
        setHeaderWidth(headerResizeStartWidth_ + pos.x() - headerResizeStartX_);
        return;
    }
    if (draggingSlider_ != HeaderSlider::None) {
        if (sliderTrack_ >= 0 && sliderTrack_ < trackCount()) {
            const double scale = event->modifiers().testFlag(Qt::ShiftModifier) ? 0.2 : 1.0;
            const int dx = pos.x() - sliderLastX_;
            sliderLastX_ = pos.x();
            const auto t = static_cast<std::size_t>(sliderTrack_);
            if (draggingSlider_ == HeaderSlider::Volume) {
                const int w = std::max(40, volumeBarRect(sliderTrack_).width() - 2);
                sliderValue_ = std::clamp(sliderValue_ + (dx * scale) / w, 0.0, 1.0);
                emit trackVolumeChanged(t, sliderValue_);
            } else {
                const int w = std::max(40, panBarRect(sliderTrack_).width() - 6);
                sliderValue_ = std::clamp(sliderValue_ + (dx * scale * 2.0) / w, -1.0, 1.0);
                emit trackPanChanged(t, sliderValue_);
            }
            viewport()->update(QRect(0, yOfTrack(sliderTrack_), headerWidth_, trackHeight(sliderTrack_)));
            showSliderTooltip(sliderTrack_, draggingSlider_, sliderValue_, event->globalPosition().toPoint());
        }
        return;
    }
    if (resizingTrack_ >= 0) {
        if (resizingTrack_ < trackCount())
            emit trackHeightChanged(
                static_cast<std::size_t>(resizingTrack_), resizeStartHeight_ + pos.y() - resizeStartY_);
        return;
    }
    if (pendingTrackDrag_ >= 0) {
        if (!event->buttons().testFlag(Qt::LeftButton)) {
            pendingTrackDrag_ = -1;
            draggingTrackOrder_ = false;
            trackDropSlot_ = -1;
            viewport()->unsetCursor();
            viewport()->update();
            return;
        }
        if (!draggingTrackOrder_
            && (pos - trackDragStartPos_).manhattanLength() >= QApplication::startDragDistance()) {
            draggingTrackOrder_ = true;
            viewport()->setCursor(Qt::ClosedHandCursor);
        }
        if (draggingTrackOrder_) {
            // Auto-scroll vertically when dragging near top or bottom of the track list.
            if (pos.y() < kPlaylistRulerHeight + 22)
                verticalScrollBar()->setValue(verticalScrollBar()->value() - 14);
            else if (pos.y() > viewport()->height() - 22)
                verticalScrollBar()->setValue(verticalScrollBar()->value() + 14);

            int slot = trackCount();
            for (int t = 0; t < trackCount(); ++t) {
                const int th = trackHeight(t);
                if (th <= 0)
                    continue;
                if (pos.y() < yOfTrack(t) + th / 2) {
                    slot = t;
                    break;
                }
            }
            if (slot != trackDropSlot_) {
                trackDropSlot_ = slot;
                viewport()->update();
            }
        }
        return;
    }
    if (loopDrag_ != LoopDrag::None) {
        if (!event->buttons().testFlag(Qt::LeftButton)) {
            loopDrag_ = LoopDrag::None;
            QToolTip::hideText();
            updateToolCursor(pos);
            return;
        }
        if (!loopDragMoved_ && std::abs(pos.x() - loopPressPos_.x()) < 3)
            return;
        loopDragMoved_ = true;

        constexpr int kMaxLoopTick = 1'000'000'000;
        const int grid = std::max(1, snapTicks(event->modifiers()));
        const int minSpan = std::max(kTicksPerStep, grid);
        const double rawTick = std::max(0.0, tickAt(pos.x()));
        int nextStart = loopStart_;
        int nextEnd = loopEnd_;

        switch (loopDrag_) {
        case LoopDrag::Draw: {
            const int cur = std::clamp(snapNearest(rawTick, grid), 0, kMaxLoopTick);
            nextStart = std::min(loopAnchorTick_, cur);
            nextEnd = std::max(loopAnchorTick_, cur);
            if (nextEnd - nextStart < minSpan)
                nextEnd = std::min(kMaxLoopTick, nextStart + minSpan);
            break;
        }
        case LoopDrag::ResizeStart: {
            const int cur = std::clamp(snapNearest(rawTick, grid), 0, loopOrigEnd_ - minSpan);
            nextStart = std::max(0, cur);
            nextEnd = loopOrigEnd_;
            break;
        }
        case LoopDrag::ResizeEnd: {
            const int cur = std::clamp(snapNearest(rawTick, grid), loopOrigStart_ + minSpan, kMaxLoopTick);
            nextStart = loopOrigStart_;
            nextEnd = cur;
            break;
        }
        case LoopDrag::Move: {
            const int span = std::max(kTicksPerStep, loopOrigEnd_ - loopOrigStart_);
            const int delta = snapNearest(rawTick - loopAnchorTick_, grid);
            nextStart = std::clamp(loopOrigStart_ + delta, 0, kMaxLoopTick - span);
            nextEnd = nextStart + span;
            break;
        }
        case LoopDrag::None:
            break;
        }

        if (nextStart != loopStart_ || nextEnd != loopEnd_) {
            loopStart_ = nextStart;
            loopEnd_ = nextEnd;
            updateScrollBars();
            viewport()->update();
        }
        const int startBar = loopStart_ / kTicksPerBar + 1;
        const int startBeat = (loopStart_ % kTicksPerBar) / kTicksPerBeat + 1;
        const int endBar = loopEnd_ / kTicksPerBar + 1;
        const int endBeat = (loopEnd_ % kTicksPerBar) / kTicksPerBeat + 1;
        const double bars = static_cast<double>(loopEnd_ - loopStart_) / kTicksPerBar;
        QToolTip::showText(event->globalPosition().toPoint(),
            tr("Loop: %1.%2 – %3.%4 (%5 bar%6)")
                .arg(startBar)
                .arg(startBeat)
                .arg(endBar)
                .arg(endBeat)
                .arg(QString::number(bars, 'g', 4), bars == 1.0 ? QString() : QStringLiteral("s")),
            viewport());
        return;
    }
    if (playing_ && !event->buttons().testFlag(Qt::LeftButton)) {
        endPlayback();
        return;
    }
    if (playing_) {
        const double tick = std::max(0.0, tickAt(pos.x()));
        if (std::abs(tick - lastPlaybackTick_) * pixelsPerTick() >= 3.0) {
            lastPlaybackTick_ = tick;
            setPlayhead(tick);
            emit playbackRequested(tick);
        }
        return;
    }
    if (band_) {
        band_ = QRect(bandStart_, pos).normalized().intersected(gridRect());
        updateGrid();
        return;
    }
    if (painting_) {
        paintTo(tickAt(pos.x()));
        return;
    }
    if (tool_ == Tool::Slice) {
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
            updateGrid();
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
    if (resizingHeaderWidth_) {
        resizingHeaderWidth_ = false;
        updateToolCursor(event->position().toPoint());
        viewport()->update(QRect(headerWidth_ - 2, 0, 4, viewport()->height()));
        return;
    }
    if (draggingSlider_ != HeaderSlider::None) {
        const int trk = sliderTrack_;
        draggingSlider_ = HeaderSlider::None;
        sliderTrack_ = -1;
        QToolTip::hideText();
        updateToolCursor(event->position().toPoint());
        if (trk >= 0 && trk < trackCount())
            viewport()->update(QRect(0, yOfTrack(trk), headerWidth_, trackHeight(trk)));
        return;
    }
    if (resizingTrack_ >= 0) {
        resizingTrack_ = -1;
        viewport()->unsetCursor();
        return;
    }
    if (pendingTrackDrag_ >= 0) {
        const int from = pendingTrackDrag_;
        const bool wasDragging = draggingTrackOrder_;
        const int slot = trackDropSlot_;
        pendingTrackDrag_ = -1;
        draggingTrackOrder_ = false;
        trackDropSlot_ = -1;
        viewport()->unsetCursor();
        viewport()->update();
        if (wasDragging && from >= 0 && from < trackCount() && slot >= 0 && slot <= trackCount()) {
            const int to = std::clamp(slot > from ? slot - 1 : slot, 0, trackCount() - 1);
            if (to != from)
                emit trackReordered(static_cast<std::size_t>(from), static_cast<std::size_t>(to));
        }
        return;
    }
    if (loopDrag_ != LoopDrag::None) {
        const LoopDrag finished = loopDrag_;
        const bool moved = loopDragMoved_;
        loopDrag_ = LoopDrag::None;
        loopDragMoved_ = false;
        QToolTip::hideText();
        updateToolCursor(event->position().toPoint());
        if (moved && loopEnd_ > loopStart_) {
            const bool activate = (finished == LoopDrag::Draw) ? true : loopActive_;
            if (activate)
                loopActive_ = true;
            viewport()->update(rulerRect());
            emit loopRangeChanged(loopStart_, loopEnd_, activate);
        } else if (!moved && finished == LoopDrag::Move && hasLoopRange()) {
            emit loopActiveToggled(!loopActive_);
        }
        return;
    }
    if (playing_) {
        endPlayback();
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
                zoomAround(bandStart_.x(), 1.5);
        } else {
            selectIn(band, bandAdds_);
        }
        updateGrid();
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
    refresh();
}

void PlaylistView::mouseDoubleClickEvent(QMouseEvent* event)
{
    syncLayout();
    const QPoint pos = event->position().toPoint();
    const QPoint globalPos = event->globalPosition().toPoint();

    if (event->button() == Qt::LeftButton && onHeaderDivider(pos)) {
        resizingHeaderWidth_ = false;
        setHeaderWidth(kDefaultHeaderWidth);
        return;
    }
    // Double-clicking empty space below the last track header opens the Add Tracks dialog.
    if (project_ != nullptr && event->button() == Qt::LeftButton && pos.x() >= 0 && pos.x() < headerWidth_
        && pos.y() >= std::max(kPlaylistRulerHeight, yOfTrack(trackCount()))) {
        emit addTracksDialogRequested(-1);
        return;
    }
    if (rulerRect().contains(pos) && event->button() == Qt::LeftButton) {
        if (pos.y() < kLoopLaneHitHeight && hasLoopRange()) {
            loopDrag_ = LoopDrag::None;
            if (!loopActive_)
                emit loopActiveToggled(true);
            playing_ = true;
            viewport()->setCursor(Qt::SplitHCursor);
            lastPlaybackTick_ = static_cast<double>(loopStart_);
            setPlayhead(lastPlaybackTick_);
            emit playbackRequested(lastPlaybackTick_);
            return;
        }
        playing_ = true;
        viewport()->setCursor(Qt::SplitHCursor);
        lastPlaybackTick_ = std::max(0.0, tickAt(pos.x()));
        setPlayhead(lastPlaybackTick_);
        emit playbackRequested(lastPlaybackTick_);
        return;
    }
    if (const int track = resizeEdgeAt(pos); track >= 0 && event->button() == Qt::LeftButton) {
        resizingTrack_ = -1;
        const auto t = static_cast<std::size_t>(track);
        const int cur = project_->tracks()[t].height;
        const int target
            = cur == PlaylistTrack::kDefaultHeight ? kExpandedInspectorHeight : PlaylistTrack::kDefaultHeight;
        emit trackHeightChanged(t, target);
        return;
    }
    if (const int track = headerTrackAt(pos); track >= 0 && event->button() == Qt::LeftButton) {
        selectTrackByUser(track);
        const auto t = static_cast<std::size_t>(track);
        if (trackColorRect(track).contains(pos)) {
            showColorPicker(this, viewport()->mapToGlobal(QPoint(kColorBarWidth + 4, yOfTrack(track))),
                project_->tracks()[t].color,
                [this, t](std::optional<std::uint32_t> color) { emit trackColorChanged(t, color); });
            return;
        }
        if (volumeBarRect(track).contains(pos)) {
            draggingSlider_ = HeaderSlider::None;
            sliderTrack_ = -1;
            emit trackVolumeChanged(t, kUnityFaderPosition);
            viewport()->update(QRect(0, yOfTrack(track), headerWidth_, trackHeight(track)));
            showSliderTooltip(track, HeaderSlider::Volume, kUnityFaderPosition, globalPos);
            return;
        }
        if (panBarRect(track).contains(pos)) {
            draggingSlider_ = HeaderSlider::None;
            sliderTrack_ = -1;
            emit trackPanChanged(t, 0.0);
            viewport()->update(QRect(0, yOfTrack(track), headerWidth_, trackHeight(track)));
            showSliderTooltip(track, HeaderSlider::Pan, 0.0, globalPos);
            return;
        }
        if (handleTrackHeaderClick(track, pos, globalPos))
            return;
        if (trackNameRect(track).contains(pos)) {
            startTrackNameEdit(track);
            return;
        }
    }
    if (tool_ != Tool::Draw && pos.x() >= headerWidth_) {
        mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton)
        if (const auto hit = clipAt(pos); hit && !hit->audio && project_ != nullptr) {
            const Clip& c = project_->clips()[hit->index];
            emit patternPicked(c.pattern);
            emit patternClipActivated(c.pattern, c.track);
            return;
        }
    QAbstractScrollArea::mouseDoubleClickEvent(event);
}

void PlaylistView::wheelEvent(QWheelEvent* event)
{
    const QPoint pos = event->position().toPoint();
    const bool onRuler = rulerRect().contains(pos);

    if (onRuler || event->modifiers().testFlag(Qt::ControlModifier)) {
        const int rawDelta = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->angleDelta().x();
        if (rawDelta == 0)
            return;
        const double steps = static_cast<double>(rawDelta) / 120.0;
        zoomAround(std::max(headerWidth_, pos.x()), std::pow(1.25, steps));
        event->accept();
        return;
    }

    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        const int rawDelta = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->angleDelta().x();
        if (rawDelta != 0) {
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - rawDelta);
            event->accept();
            return;
        }
    }

    QAbstractScrollArea::wheelEvent(event);
}

void PlaylistView::setSnapEnabled(bool enabled)
{
    if (snapEnabled_ == enabled)
        return;
    snapEnabled_ = enabled;
    updateGrid();
}

void PlaylistView::zoomBy(double factor)
{
    const QRect grid = gridRect();
    int anchorX = grid.left() + grid.width() / 2;
    if (playhead_ >= 0.0) {
        const int px = static_cast<int>(
            std::lround(headerWidth_ + playhead_ * pixelsPerTick() - horizontalScrollBar()->value()));
        if (px >= grid.left() && px <= grid.right())
            anchorX = px;
    }
    zoomAround(anchorX, factor);
}

void PlaylistView::quantizeSelectedClips()
{
    if (project_ == nullptr)
        return;
    syncSelection();

    // Use the explicit snap grid even if quick-snap toggle is temporarily off.
    int grid = kTicksPerStep;
    switch (snap_) {
    case Snap::Bar:
        grid = kTicksPerBar;
        break;
    case Snap::Beat:
        grid = kTicksPerBeat;
        break;
    case Snap::Step:
        grid = kTicksPerStep;
        break;
    case Snap::HalfStep:
        grid = kTicksPerStep / 2;
        break;
    case Snap::Auto: {
        grid = kTicksPerStep / 2;
        while (grid * pixelsPerTick() < 12.0 && grid < kTicksPerBar)
            grid *= 2;
        break;
    }
    }

    const bool anySelected = selectionSize() > 0;
    PlaylistClipEdit edit;
    for (std::size_t i = 0; i < project_->clips().size(); ++i) {
        if (anySelected ? (selectedClips_[i] == 0) : (project_->clips()[i].track != selectedTrack_))
            continue;
        Clip c = project_->clips()[i];
        const int snapped = std::max(0, snapNearest(c.start, grid));
        if (snapped != c.start) {
            c.start = snapped;
            edit.clips.emplace_back(i, c);
        }
    }
    for (std::size_t i = 0; i < project_->audioClips().size(); ++i) {
        if (anySelected ? (selectedAudio_[i] == 0) : (project_->audioClips()[i].track != selectedTrack_))
            continue;
        AudioClip c = project_->audioClips()[i];
        const int snapped = std::max(0, snapNearest(c.start, grid));
        if (snapped != c.start) {
            c.start = snapped;
            edit.audioClips.emplace_back(i, c);
        }
    }
    if (!edit.clips.empty() || !edit.audioClips.empty())
        emit clipsChanged(edit);
}

void PlaylistView::zoomAround(int anchorX, double factor)
{
    const double anchor = tickAt(anchorX);
    pixelsPerBar_ = std::clamp(pixelsPerBar_ * factor, kMinPixelsPerBar, kMaxPixelsPerBar);
    updateScrollBars();
    horizontalScrollBar()->setValue(static_cast<int>(anchor * pixelsPerTick()) - (anchorX - headerWidth_));
    updateGrid();
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
    updateGrid();
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
    updateGrid();
}

void PlaylistView::updateToolCursor(const QPoint& pos)
{
    if (onHeaderDivider(pos)) {
        viewport()->setCursor(Qt::SplitHCursor);
        return;
    }
    if (rulerRect().contains(pos)) {
        if (pos.y() < kLoopLaneHitHeight) {
            if (hasLoopRange()) {
                const double ppt = pixelsPerTick();
                const int scrollX = horizontalScrollBar()->value();
                const double x1 = headerWidth_ + loopStart_ * ppt - scrollX;
                const double x2 = headerWidth_ + loopEnd_ * ppt - scrollX;
                if (std::abs(pos.x() - x1) <= 6.0 || std::abs(pos.x() - x2) <= 6.0) {
                    viewport()->setCursor(Qt::SizeHorCursor);
                    return;
                }
                if (pos.x() > x1 + 6.0 && pos.x() < x2 - 6.0) {
                    viewport()->setCursor(Qt::PointingHandCursor);
                    return;
                }
            }
            viewport()->setCursor(Qt::CrossCursor);
            return;
        }
        viewport()->setCursor(Qt::PointingHandCursor);
        return;
    }
    if (resizeEdgeAt(pos) >= 0) {
        viewport()->setCursor(Qt::SizeVerCursor);
        return;
    }
    if (const int track = headerTrackAt(pos); track >= 0) {
        if (volumeBarRect(track).contains(pos) || panBarRect(track).contains(pos)) {
            viewport()->setCursor(Qt::SizeHorCursor);
            return;
        }
        const bool clickable = trackColorRect(track).contains(pos) || muteButtonRect(track).contains(pos)
            || soloButtonRect(track).contains(pos) || inspectorRowRect(track, 0).contains(pos)
            || inspectorRowRect(track, 1).contains(pos)
            || (project_->hasSubTracks(static_cast<std::size_t>(track)) && arrowRect(track).contains(pos));
        viewport()->setCursor(trackNameRect(track).contains(pos) ? Qt::IBeamCursor
                : clickable                                      ? Qt::PointingHandCursor
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
        viewport()->setCursor(pos.x() >= headerWidth_ ? Qt::SplitHCursor : Qt::ArrowCursor);
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
    updateGrid();
}

void PlaylistView::moveGroup(const QPoint& pos, int grid)
{
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
        return std::nullopt;
    if (pos.x() >= r.right() - kGrip)
        return std::pair{*hit, true};
    if (pos.x() <= r.left() + kGrip)
        return std::pair{*hit, false};
    return std::nullopt;
}

void PlaylistView::resizeClipTo(double tick, int grid)
{
    const int at = snapNearest(tick, grid);
    const bool audio = resizeClip_.audio;
    const int start = audio ? resizeStartAudio_.start : resizeStartClip_.start;
    const int offset = audio ? resizeStartAudio_.offset : resizeStartClip_.offset;
    const int length = audio ? audioLength(resizeStartAudio_) : project_->clipLength(resizeStartClip_);
    const int whole = audio ? sampleTicks(resizeStartAudio_) : 0;
    const int end = start + length;
    const int shortest = std::clamp(grid, kTicksPerStep / 2, kTicksPerStep);
    int newStart = start;
    int newLength = length;
    if (resizeRight_) {
        int newEnd = std::max(at, start + shortest);
        if (whole > 0)
            newEnd = std::min(newEnd, start + (whole - offset));
        newLength = newEnd - start;
    } else {
        newStart = std::clamp(at, std::max(0, start - offset), end - shortest);
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
    if (!snapEnabled_ || modifiers.testFlag(Qt::AltModifier))
        return 1;
    if (modifiers.testFlag(Qt::ShiftModifier))
        return kTicksPerStep;
    switch (snap_) {
    case Snap::Bar:
        return kTicksPerBar;
    case Snap::Beat:
        return kTicksPerBeat;
    case Snap::Step:
        return kTicksPerStep;
    case Snap::HalfStep:
        return kTicksPerStep / 2;
    case Snap::Auto:
        break;
    }
    constexpr double kMinSnapPixels = 12.0;
    int grid = kTicksPerStep / 2;
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
    constexpr int kShortest = kTicksPerStep / 2;
    const auto crosses = [&](int track, int start, int length) {
        return track >= top && track <= bottom && trackHeight(track) > 0 && tick - start >= kShortest
            && start + length - tick >= kShortest;
    };
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
        second.offset = clip.offset + (tick - clip.start);
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
    if (sliceLine_ && !slicing_) {
        sliceLine_.reset();
        updateGrid();
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

bool PlaylistView::hasSelectedClips()
{
    syncSelection();
    return selectionSize() > 0;
}

void PlaylistView::selectAllClips()
{
    if (project_ == nullptr)
        return;
    syncSelection();
    std::fill(selectedClips_.begin(), selectedClips_.end(), 1);
    std::fill(selectedAudio_.begin(), selectedAudio_.end(), 1);
    updateGrid();
}

void PlaylistView::deleteSelectedClips()
{
    syncSelection();
    if (selectionSize() == 0)
        return;
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
}

void PlaylistView::copySelectedClips()
{
    if (project_ == nullptr)
        return;
    syncSelection();
    if (selectionSize() == 0)
        return;

    clipboardClips_.clear();
    clipboardAudio_.clear();
    int minStart = std::numeric_limits<int>::max();
    long long maxEnd = 0;

    for (std::size_t i = 0; i < selectedClips_.size(); ++i) {
        if (selectedClips_[i] == 0)
            continue;
        const Clip& c = project_->clips()[i];
        clipboardClips_.push_back(c);
        minStart = std::min(minStart, c.start);
        maxEnd = std::max(maxEnd, static_cast<long long>(c.start) + project_->clipLength(c));
    }
    for (std::size_t i = 0; i < selectedAudio_.size(); ++i) {
        if (selectedAudio_[i] == 0)
            continue;
        const AudioClip& c = project_->audioClips()[i];
        clipboardAudio_.push_back(c);
        minStart = std::min(minStart, c.start);
        maxEnd = std::max(maxEnd, static_cast<long long>(c.start) + audioLength(c));
    }

    constexpr long long kMaxTick = std::numeric_limits<int>::max() / 2;
    clipboardMinStart_ = minStart == std::numeric_limits<int>::max() ? 0 : minStart;
    clipboardMaxEnd_ = static_cast<int>(std::clamp(
        std::max(static_cast<long long>(clipboardMinStart_) + kTicksPerStep, maxEnd), 0LL, kMaxTick));
}

void PlaylistView::cutSelectedClips()
{
    copySelectedClips();
    deleteSelectedClips();
}

void PlaylistView::pasteClips()
{
    if (project_ == nullptr || !hasClipClipboard())
        return;

    constexpr long long kMaxTick = std::numeric_limits<int>::max() / 2;
    const long long grid = std::max<long long>(1, snapTicks(Qt::NoModifier));
    const long long targetStart = playhead_ >= 0.0
        ? std::clamp<long long>(snapDown(playhead_, static_cast<int>(grid)), 0LL, kMaxTick)
        : std::min(kMaxTick, ((static_cast<long long>(clipboardMaxEnd_) + grid - 1) / grid) * grid);
    const long long delta = targetStart - clipboardMinStart_;

    std::vector<Clip> addedClips;
    std::vector<AudioClip> addedAudio;
    addedClips.reserve(clipboardClips_.size());
    addedAudio.reserve(clipboardAudio_.size());

    for (Clip c : clipboardClips_) {
        c.start = static_cast<int>(std::clamp(static_cast<long long>(c.start) + delta, 0LL, kMaxTick));
        addedClips.push_back(c);
    }
    for (AudioClip c : clipboardAudio_) {
        c.start = static_cast<int>(std::clamp(static_cast<long long>(c.start) + delta, 0LL, kMaxTick));
        addedAudio.push_back(std::move(c));
    }

    const std::size_t oldClips = project_->clips().size();
    const std::size_t oldAudio = project_->audioClips().size();
    emit clipsAddedBatch(addedClips, addedAudio);

    syncSelection();
    clearSelection();
    for (std::size_t i = oldClips; i < selectedClips_.size(); ++i)
        selectedClips_[i] = 1;
    for (std::size_t i = oldAudio; i < selectedAudio_.size(); ++i)
        selectedAudio_[i] = 1;
    updateGrid();
}

void PlaylistView::duplicateSelectedClips()
{
    if (project_ == nullptr)
        return;
    syncSelection();
    if (selectionSize() == 0)
        return;

    constexpr long long kMaxTick = std::numeric_limits<int>::max() / 2;
    int minStart = std::numeric_limits<int>::max();
    long long maxEnd = 0;
    std::vector<Clip> addedClips;
    std::vector<AudioClip> addedAudio;

    for (std::size_t i = 0; i < selectedClips_.size(); ++i) {
        if (selectedClips_[i] == 0)
            continue;
        const Clip& c = project_->clips()[i];
        addedClips.push_back(c);
        minStart = std::min(minStart, c.start);
        maxEnd = std::max(maxEnd, static_cast<long long>(c.start) + project_->clipLength(c));
    }
    for (std::size_t i = 0; i < selectedAudio_.size(); ++i) {
        if (selectedAudio_[i] == 0)
            continue;
        const AudioClip& c = project_->audioClips()[i];
        addedAudio.push_back(c);
        minStart = std::min(minStart, c.start);
        maxEnd = std::max(maxEnd, static_cast<long long>(c.start) + audioLength(c));
    }

    const long long rawSpan = std::max<long long>(kTicksPerStep, maxEnd - minStart);
    const long long grid = std::max<long long>(kTicksPerStep, snapTicks(Qt::NoModifier));
    const long long shift = ((rawSpan + grid - 1) / grid) * grid;

    for (Clip& c : addedClips)
        c.start = static_cast<int>(std::clamp(static_cast<long long>(c.start) + shift, 0LL, kMaxTick));
    for (AudioClip& c : addedAudio)
        c.start = static_cast<int>(std::clamp(static_cast<long long>(c.start) + shift, 0LL, kMaxTick));

    const std::size_t oldClips = project_->clips().size();
    const std::size_t oldAudio = project_->audioClips().size();
    emit clipsAddedBatch(addedClips, addedAudio);

    syncSelection();
    clearSelection();
    for (std::size_t i = oldClips; i < selectedClips_.size(); ++i)
        selectedClips_[i] = 1;
    for (std::size_t i = oldAudio; i < selectedAudio_.size(); ++i)
        selectedAudio_[i] = 1;
    updateGrid();
}

void PlaylistView::keyPressEvent(QKeyEvent* event)
{
    syncSelection();
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && selectionSize() > 0) {
        deleteSelectedClips();
        return;
    }
    if (event->matches(QKeySequence::SelectAll) && project_ != nullptr) {
        selectAllClips();
        return;
    }
    if (event->matches(QKeySequence::Copy) && selectionSize() > 0) {
        copySelectedClips();
        return;
    }
    if (event->matches(QKeySequence::Cut) && selectionSize() > 0) {
        cutSelectedClips();
        return;
    }
    if (event->matches(QKeySequence::Paste) && hasClipClipboard()) {
        pasteClips();
        return;
    }
    if (event->key() == Qt::Key_D && event->modifiers() == Qt::ControlModifier && selectionSize() > 0) {
        duplicateSelectedClips();
        return;
    }
    if (event->key() == Qt::Key_L && event->modifiers() == Qt::ControlModifier && project_ != nullptr) {
        loopSelectionOrSong();
        return;
    }
    if (event->key() == Qt::Key_Escape && selectionSize() > 0) {
        clearSelection();
        updateGrid();
        return;
    }
    if ((event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) && event->modifiers() == Qt::NoModifier
        && trackCount() > 0) {
        syncLayout();
        const int step = event->key() == Qt::Key_Down ? 1 : -1;
        int next = std::clamp(selectedTrack_, 0, trackCount() - 1) + step;
        while (next >= 0 && next < trackCount() && trackHeight(next) <= 0)
            next += step;
        if (next >= 0 && next < trackCount()) {
            selectTrackByUser(next);
            const int top = yOfTrack(next);
            const int bottom = top + trackHeight(next);
            const QRect grid = gridRect();
            if (top < grid.top())
                verticalScrollBar()->setValue(verticalScrollBar()->value() - (grid.top() - top));
            else if (bottom > grid.bottom())
                verticalScrollBar()->setValue(verticalScrollBar()->value() + (bottom - grid.bottom()));
        }
        event->accept();
        return;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void PlaylistView::setLoopRange(int startTicks, int endTicks)
{
    int s = std::max(0, startTicks);
    int e = std::max(0, endTicks);
    if (e <= s) {
        s = 0;
        e = 0;
    }
    if (s == loopStart_ && e == loopEnd_)
        return;
    loopStart_ = s;
    loopEnd_ = e;
    updateScrollBars();
    viewport()->update();
}

void PlaylistView::setLoopActive(bool active)
{
    if (loopActive_ == active)
        return;
    loopActive_ = active;
    viewport()->update();
}

void PlaylistView::loopSelectionOrSong()
{
    if (project_ == nullptr)
        return;
    syncSelection();
    int minStart = std::numeric_limits<int>::max();
    long long maxEnd = 0;
    const bool selectedOnly = selectionSize() > 0;

    for (std::size_t i = 0; i < project_->clips().size(); ++i) {
        if (selectedOnly && (i >= selectedClips_.size() || selectedClips_[i] == 0))
            continue;
        const Clip& c = project_->clips()[i];
        minStart = std::min(minStart, c.start);
        maxEnd = std::max(maxEnd, static_cast<long long>(c.start) + project_->clipLength(c));
    }
    for (std::size_t i = 0; i < project_->audioClips().size(); ++i) {
        if (selectedOnly && (i >= selectedAudio_.size() || selectedAudio_[i] == 0))
            continue;
        const AudioClip& c = project_->audioClips()[i];
        minStart = std::min(minStart, c.start);
        maxEnd = std::max(maxEnd, static_cast<long long>(c.start) + audioLength(c));
    }

    if (minStart == std::numeric_limits<int>::max() || maxEnd <= minStart) {
        minStart = 0;
        maxEnd = 4LL * kTicksPerBar;
    } else if (!selectedOnly) {
        minStart = 0;
        maxEnd = ((maxEnd + kTicksPerBar - 1) / kTicksPerBar) * kTicksPerBar;
    }
    const int clampedEnd = static_cast<int>(std::min<long long>(maxEnd, 1'000'000'000LL));
    setLoopRange(minStart, clampedEnd);
    loopActive_ = true;
    emit loopRangeChanged(loopStart_, loopEnd_, true);
}

void PlaylistView::clearLoopRange()
{
    if (!hasLoopRange())
        return;
    setLoopRange(0, 0);
    emit loopRangeChanged(0, 0, loopActive_);
}

} // namespace vloomdaw
