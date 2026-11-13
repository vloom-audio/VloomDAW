// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/Project.hpp"

#include <QAbstractScrollArea>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace vloomdaw {

/// What the playlist needs to draw an audio clip.
struct ClipAudio {
    double seconds = 0.0; ///< length
    double peakSeconds = 0.01; ///< time each entry of `peaks` covers
    std::vector<float> peaks; ///< largest absolute sample per peakSeconds, both channels
};

/// The playlist: track names on the left, a bar ruler on top and pattern
/// clips arranged on the tracks. Reads the Project; edits are emitted as
/// signals for MainWindow to apply, which then calls refresh().
///
/// Left-click an empty spot to place the current pattern there (snapped to
/// the bar) and keep dragging to position it; drag a clip to move it (Shift:
/// snap to beats); right-click a clip to delete it; double-click a clip to
/// make its pattern current. Ctrl+wheel zooms horizontally. Drop a WAV file
/// (from the browser or the desktop) on a track for an audio clip; audio
/// clips move and delete like pattern clips. Dropped below the last track,
/// it asks for a new track. Double-click a
/// track name to rename it; right-click it to rename or delete the track.
class PlaylistView : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit PlaylistView(QWidget* parent = nullptr);

    void setProject(const Project* project);
    /// The pattern a click places.
    void setCurrentPattern(std::size_t pattern);
    /// Playhead in ticks from the song start (< 0: hidden).
    void setPlayhead(double tick);
    /// Looks up an audio clip's length and waveform by path (null: unknown,
    /// drawn one bar long).
    void setAudioInfo(std::function<const ClipAudio*(const std::string& path)> info);
    /// Tempo in BPM: an audio clip's length in bars depends on it.
    void setTempo(double bpm);
    void refresh();

signals:
    void clipAdded(vloomdaw::Clip clip);
    void clipChanged(std::size_t index, vloomdaw::Clip clip);
    void clipRemoved(std::size_t index);
    void patternPicked(std::size_t pattern);
    /// A WAV file was dropped: an audio clip at `track` (== the track count:
    /// below the last one) and `start` ticks.
    void audioDropped(const QString& path, int track, int start);
    void audioClipChanged(std::size_t index, vloomdaw::AudioClip clip);
    void audioClipRemoved(std::size_t index);
    void trackRenameRequested(std::size_t track);
    void trackRemoveRequested(std::size_t track);
    void trackMoveRequested(std::size_t from, std::size_t to);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool viewportEvent(QEvent* event) override;

private:
    [[nodiscard]] double pixelsPerTick() const;
    [[nodiscard]] int lengthTicks() const;
    [[nodiscard]] QRect gridRect() const;
    [[nodiscard]] double tickAt(int x) const;
    [[nodiscard]] int trackAt(int y) const;
    [[nodiscard]] int trackCount() const;
    /// The track whose name is at `pos`, or -1.
    [[nodiscard]] int headerTrackAt(const QPoint& pos) const;
    [[nodiscard]] int yOfTrack(int track) const;
    /// A clip in the playlist: a pattern clip or an audio clip, by index.
    struct ClipRef {
        bool audio = false;
        std::size_t index = 0;
    };
    [[nodiscard]] QRectF clipRect(const Clip& clip) const;
    [[nodiscard]] QRectF clipRect(const AudioClip& clip) const;
    [[nodiscard]] int audioLength(const AudioClip& clip) const; ///< ticks
    [[nodiscard]] const ClipAudio* audioOf(const AudioClip& clip) const; ///< its channel's sample
    [[nodiscard]] std::optional<ClipRef> clipAt(const QPoint& pos) const;
    void paintAudioClip(QPainter& p, const AudioClip& clip, bool dragged) const;
    /// Where a drop at `pos` would land: track (may be the track count) and
    /// start tick; nullopt outside the grid.
    [[nodiscard]] std::optional<std::pair<int, int>> dropTarget(const QPoint& pos) const;
    void updateScrollBars();
    void beginDrag(ClipRef clip, const QPoint& pos);

    const Project* project_ = nullptr;
    std::size_t currentPattern_ = 0;
    double playhead_ = -1.0;
    double pixelsPerBar_ = 72.0;
    double tempo_ = 120.0;
    std::function<const ClipAudio*(const std::string&)> audioInfo_;
    std::optional<QRectF> dropHint_; ///< where a dragged file would land

    bool dragging_ = false;
    ClipRef dragClip_;
    Clip dragStartClip_;
    AudioClip dragStartAudio_;
    double dragStartTick_ = 0.0;
    int dragStartTrack_ = 0;
};

} // namespace vloomdaw
