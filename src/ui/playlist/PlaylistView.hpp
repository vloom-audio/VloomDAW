// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/Project.hpp"

#include <QAbstractScrollArea>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class QLineEdit;

namespace vloomdaw {

/// Clips changed together (a selection moved): new values by index.
struct PlaylistClipEdit {
    std::vector<std::pair<std::size_t, Clip>> clips{};
    std::vector<std::pair<std::size_t, AudioClip>> audioClips{};
};

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
/// it asks for a new track. Double-click a track name to rename it inline;
/// right-click it to rename, move, delete or group the track. The mute and
/// solo buttons on a track's header mute and solo it (and its sub-tracks);
/// clips on a track that does not play are drawn faded. Drag the bottom edge
/// of a track's header to make it taller or shorter (double-click: back to
/// the default). A track can be a sub-track of the one above it ("Make
/// Sub-track"); the arrow on a track with sub-tracks hides or shows them.
///
/// Tools (setTool()) change what the left button does in the grid: Draw
/// (the above), Paint (drag along a track to lay the current pattern again
/// and again), Delete (click or drag over clips), Mute (click or drag over
/// clips to mute or unmute them), Slice (as in FL Studio: a line follows the
/// pointer; click, or drag up or down across tracks, to cut every clip the
/// line crosses at the nearest beat; Shift: step), Select (drag a box; Ctrl adds; drag a
/// selected clip to move them all), Zoom (drag a range to fill the view,
/// click to zoom in, right-click out) and Playback (hold to play from there,
/// drag to scrub). Delete removes the selection, Ctrl+A selects every clip,
/// Escape clears it. With Draw, Paint and Select, drag a clip's left or
/// right edge to trim or stretch it (beat grid; Shift: step): a pattern clip
/// repeats its pattern when longer, an audio clip reaches no further than its
/// sample, at either end.
class PlaylistView : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit PlaylistView(QWidget* parent = nullptr);

    void setProject(const Project* project);
    /// What Draw and Paint place: a pattern clip of pattern `index`, or an
    /// audio clip of channel `index`'s sample.
    struct ClipSource {
        bool audio = false;
        std::size_t index = 0;
        friend bool operator==(const ClipSource&, const ClipSource&) = default;
    };
    void setSource(ClipSource source) { source_ = source; }
    [[nodiscard]] ClipSource source() const { return source_; }
    /// The pattern a click places (the source becomes that pattern).
    void setCurrentPattern(std::size_t pattern);
    /// Playhead in ticks from the song start (< 0: hidden).
    void setPlayhead(double tick);
    /// Looks up an audio clip's length and waveform by path (null: unknown,
    /// drawn one bar long).
    void setAudioInfo(std::function<const ClipAudio*(const std::string& path)> info);
    /// Tempo in BPM: an audio clip's length in bars depends on it.
    void setTempo(double bpm);
    void refresh();

    enum class Tool { Draw, Paint, Delete, Mute, Slice, Select, Zoom, Playback };

    /// What placing, moving, trimming and cutting clips snap to. Auto: the
    /// finest grid line far enough apart at the zoom to aim at (a step when
    /// zoomed in, up to a bar when zoomed out). Shift snaps to steps whatever
    /// this is. A step (a 16th note) is the finest: the engine places clips on
    /// steps.
    enum class Snap { Auto, Bar, Beat, Step };
    void setSnap(Snap snap) { snap_ = snap; }
    [[nodiscard]] Snap snap() const { return snap_; }
    void setTool(Tool tool);
    [[nodiscard]] Tool tool() const { return tool_; }

signals:
    void clipAdded(vloomdaw::Clip clip);
    void clipChanged(std::size_t index, vloomdaw::Clip clip);
    void clipRemoved(std::size_t index);
    void patternPicked(std::size_t pattern);
    /// A clip was clicked with Draw, Paint or Select: what it is (its
    /// pattern or its channel's sample) becomes the source, as in FL Studio.
    void sourcePicked(vloomdaw::PlaylistView::ClipSource source);
    /// A WAV file was dropped: an audio clip at `track` (== the track count:
    /// below the last one) and `start` ticks.
    void audioDropped(const QString& path, int track, int start);
    void audioClipChanged(std::size_t index, vloomdaw::AudioClip clip);
    void audioClipRemoved(std::size_t index);
    /// The user right-clicked an audio clip's title strip: show its menu at
    /// `globalPos` (fit to tempo).
    void audioClipMenuRequested(std::size_t index, QPoint globalPos);
    /// An audio clip was made (the second part of a cut one).
    void audioClipAdded(vloomdaw::AudioClip clip);
    /// Several clips changed at once (a selection moved).
    void clipsChanged(const vloomdaw::PlaylistClipEdit& edit);
    /// Several clips were deleted (the selection): pattern and audio clip indices.
    void clipsRemoved(const std::vector<std::size_t>& clips, const std::vector<std::size_t>& audioClips);
    /// Playback tool: play from `tick` (pressed, or dragged to scrub).
    void playbackRequested(double tick);
    /// Playback tool: the button was released; stop.
    void playbackReleased();
    void trackRenameRequested(std::size_t track);
    void trackNameEdited(std::size_t track, const QString& name);
    void trackRemoveRequested(std::size_t track);
    /// Move `track` with its sub-tracks past its neighbour in its group.
    void trackMoveRequested(std::size_t track, bool down);
    void trackHeightChanged(std::size_t track, int height);
    /// A colour (0xRRGGBB) was picked for `track`; nullopt: none.
    void trackColorChanged(std::size_t track, std::optional<std::uint32_t> color);
    void trackCollapseToggled(std::size_t track, bool collapsed);
    void trackIndentRequested(std::size_t track);
    void trackOutdentRequested(std::size_t track);
    void trackMuteToggled(std::size_t track, bool muted);
    void trackSoloToggled(std::size_t track, bool solo);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
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
    [[nodiscard]] QRect trackNameRect(int track) const;
    [[nodiscard]] QRect muteButtonRect(int track) const;
    [[nodiscard]] QRect soloButtonRect(int track) const;
    /// Toggles mute or solo when `pos` is on one of `track`'s buttons;
    /// whether it was.
    bool toggleTrackButton(int track, const QPoint& pos);
    void paintTrackButton(
        QPainter& p, const QRect& r, const QString& icon, bool on, const QColor& onColor) const;
    [[nodiscard]] int yOfTrack(int track) const;
    /// Lane height of `track` in pixels; 0 for a hidden sub-track.
    [[nodiscard]] int trackHeight(int track) const;
    /// Rebuilds the track positions from the project: call before using
    /// yOfTrack(), trackAt() or trackHeight() in an event.
    void syncLayout();
    /// The track whose header's bottom edge is at `pos` (to resize it), or -1.
    [[nodiscard]] int resizeEdgeAt(const QPoint& pos) const;
    /// The collapse arrow of a track with sub-tracks.
    [[nodiscard]] QRect arrowRect(int track) const;
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
    void paintAudioClip(QPainter& p, const AudioClip& clip, bool dragged, bool selected) const;
    /// Whether a clip is selected (Select tool).
    [[nodiscard]] bool isSelected(ClipRef clip) const;
    [[nodiscard]] std::size_t selectionSize() const;
    void clearSelection();
    /// Drops the selection when clips were added or removed elsewhere (indices moved).
    void syncSelection();
    /// Selects the clips touching `band` (viewport coordinates), adding to the
    /// selection when `add`.
    void selectIn(const QRect& band, bool add);
    /// Starts moving every selected clip with the pointer.
    void beginGroupDrag(const QPoint& pos);
    void moveGroup(const QPoint& pos, int grid);
    /// Paint tool: lays the current pattern on the paint track in every slot
    /// from the last one painted to the one under `tick`.
    void paintTo(double tick);
    /// Places a clip of the source on `track` at `start`; whether there was
    /// one to place (an audio source needs a channel with a sample).
    bool placeSource(int track, int start);
    /// The colour of `track` (chosen by the user), else `fallback`: clips
    /// on a coloured track take its colour.
    [[nodiscard]] QColor trackColorOr(int track, const QColor& fallback) const;
    /// How long a clip of the source is, in ticks.
    [[nodiscard]] int sourceLength() const;
    /// Mute tool: gives the clip at `pos` the mute state being painted.
    void muteAt(const QPoint& pos);
    /// Zooms horizontally by `factor`, keeping the tick under `anchorX` in place.
    void zoomAround(int anchorX, double factor);
    /// Zooms so ticks `from`..`to` fill the grid.
    void zoomToRange(double from, double to);
    void updateToolCursor(const QPoint& pos);
    /// The snap grid in ticks for an edit made with `modifiers` held.
    [[nodiscard]] int snapTicks(Qt::KeyboardModifiers modifiers) const;
    /// The whole sample of an audio clip in ticks at the tempo; 0 when unknown.
    [[nodiscard]] int sampleTicks(const AudioClip& clip) const;
    /// The tempo a clip's sample is counted at: its own when fitted to the
    /// song's tempo, else the song's.
    [[nodiscard]] double clipTempo(const AudioClip& clip) const
    {
        return clip.fitted() ? clip.sourceBpm : tempo_;
    }
    /// A clip's edge under `pos`, to trim or stretch it: the clip, and
    /// whether it is the right edge.
    [[nodiscard]] std::optional<std::pair<ClipRef, bool>> edgeAt(const QPoint& pos) const;
    void resizeClipTo(double tick, int grid);
    /// Slice tool: cuts every clip on tracks `fromTrack`..`toTrack` that
    /// `tick` falls inside in two there.
    void sliceAcross(int tick, int fromTrack, int toTrack);
    /// Slice tool: the tick the line is at for the pointer at `x`.
    [[nodiscard]] int sliceTick(int x, Qt::KeyboardModifiers modifiers) const;
    /// Where a drop at `pos` would land: track (may be the track count) and
    /// start tick; nullopt outside the grid.
    [[nodiscard]] std::optional<std::pair<int, int>> dropTarget(const QPoint& pos) const;
    void updateScrollBars();
    void beginDrag(ClipRef clip, const QPoint& pos);
    void startTrackNameEdit(int track);
    void commitTrackNameEdit();

    const Project* project_ = nullptr;
    ClipSource source_; ///< what Draw and Paint place
    Snap snap_ = Snap::Auto;
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

    /// Top of each track from the first one's, in pixels, plus the bottom
    /// of the last (hidden sub-tracks take no room).
    std::vector<int> tops_{0};
    int resizingTrack_ = -1; ///< whose height a drag changes
    int resizeStartY_ = 0;
    int resizeStartHeight_ = 0;

    Tool tool_ = Tool::Draw;
    std::vector<char> selectedClips_; ///< by index into Project::clips()
    std::vector<char> selectedAudio_; ///< by index into Project::audioClips()
    std::optional<QRect> band_; ///< the Select / Zoom tool's box, while dragged
    QPoint bandStart_;
    bool bandAdds_ = false; ///< Ctrl was held: add to the selection
    bool groupDragging_ = false;
    std::vector<std::pair<std::size_t, Clip>> groupStartClips_;
    std::vector<std::pair<std::size_t, AudioClip>> groupStartAudio_;
    int groupStartTrack_ = 0;
    bool painting_ = false; ///< Paint tool drag
    int paintTrack_ = 0;
    int paintStart_ = 0; ///< tick of slot 0
    int paintLength_ = kTicksPerBar; ///< the pattern's length: one slot
    long long paintLastSlot_ = 0;
    bool deleting_ = false; ///< Delete tool drag
    bool muting_ = false; ///< Mute tool drag
    bool muteState_ = false; ///< what the Mute tool drag gives clips
    bool playing_ = false; ///< Playback tool held
    double lastPlaybackTick_ = -1.0;
    /// Slice tool: where the cut line is (tick, first and last track) while
    /// the pointer is over the grid or dragging; none otherwise.
    struct SliceLine {
        int tick = 0;
        int fromTrack = 0;
        int toTrack = 0;
    };
    std::optional<SliceLine> sliceLine_;
    bool slicing_ = false; ///< the button is down: the line stretches across tracks
    bool resizingClip_ = false; ///< dragging a clip's edge
    ClipRef resizeClip_;
    bool resizeRight_ = false;
    Clip resizeStartClip_;
    AudioClip resizeStartAudio_;

    QLineEdit* inlineEditor_ = nullptr;
    int editingTrack_ = -1;
};

} // namespace vloomdaw
