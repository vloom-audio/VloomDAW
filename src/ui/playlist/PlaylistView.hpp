// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/MixerModel.hpp"
#include "core/Project.hpp"

#include <QAbstractScrollArea>
#include <QColor>
#include <QPixmap>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class QLineEdit;

namespace vloomdaw {

/// Clips changed together (a selection moved): new values by index.
struct PlaylistClipEdit {
    std::vector<std::pair<std::size_t, Clip>> clips{};
    std::vector<std::pair<std::size_t, AudioClip>> audioClips{};
};

/// Multi-resolution peak pyramid level for fast waveform rendering at any zoom.
struct PeakMip {
    double stepSeconds = 0.0;
    std::vector<float> peaks{};
};

/// What the playlist needs to draw an audio clip.
struct ClipAudio {
    double seconds = 0.0; ///< length
    double peakSeconds = 0.01; ///< time each entry of `peaks` covers
    std::vector<float> peaks{}; ///< largest absolute sample per peakSeconds, both channels
    std::vector<PeakMip> mips{}; ///< coarser mip levels (8x, 64x, 512x) built on the loader thread
    bool loading = false; ///< decoding in the background: show placeholder until ready
};

/// The playlist: resizable Studio One-style track headers on the left (with
/// track number, mute/solo, volume & pan bars, progressive channel/insert
/// menus when stretched vertically, and live stereo VU meters), a bar ruler on
/// top, and pattern/audio clips arranged on the tracks.
class PlaylistView : public QAbstractScrollArea {
    Q_OBJECT

public:
    static constexpr int kDefaultHeaderWidth = 236;
    static constexpr int kMinHeaderWidth = 160;
    static constexpr int kMaxHeaderWidth = 420;

    explicit PlaylistView(QWidget* parent = nullptr);

    void setProject(const Project* project);
    void setMixer(const MixerModel* mixer);

    [[nodiscard]] int headerWidth() const { return headerWidth_; }
    void setHeaderWidth(int width);

    [[nodiscard]] int selectedTrack() const { return selectedTrack_; }
    void setSelectedTrack(int track);

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
    /// Clears cached waveform pixmap tiles (e.g. when a project resets).
    void clearWaveCache();
    /// Tempo in BPM: an audio clip's length in bars depends on it.
    void setTempo(double bpm);
    void refresh();
    /// Repaints only the clip grid and ruler, skipping track headers during clip drags.
    void updateGrid();
    /// Repaints only the track headers column (e.g. when mixer faders/FX change).
    void updateHeaders();
    /// Advances per-track stereo VU meters using `insertPeaks` (index 0 = Master,
    /// 1..N = Insert) and repaints only the narrow meter strip when levels move.
    void updateTrackMeters(const std::vector<std::pair<float, float>>& insertPeaks, double dt);

    enum class Tool { Draw, Paint, Delete, Mute, Slice, Select, Zoom, Playback };

    /// What placing, moving, trimming and cutting clips snap to.
    enum class Snap { Auto, Bar, Beat, Step, HalfStep };
    void setSnap(Snap snap)
    {
        snap_ = snap;
        updateGrid(); // the grid shows what clips snap to
    }
    [[nodiscard]] Snap snap() const { return snap_; }
    void setSnapEnabled(bool enabled);
    [[nodiscard]] bool snapEnabled() const { return snapEnabled_; }
    void setFollowPlayhead(bool follow) { followPlayhead_ = follow; }
    [[nodiscard]] bool followPlayhead() const { return followPlayhead_; }
    void zoomBy(double factor);
    void quantizeSelectedClips();
    void setTool(Tool tool);
    [[nodiscard]] Tool tool() const { return tool_; }

    // ---- Studio One-style Loop Bar (locators on the top timeline ruler)
    void setLoopRange(int startTicks, int endTicks);
    [[nodiscard]] int loopStartTicks() const { return loopStart_; }
    [[nodiscard]] int loopEndTicks() const { return loopEnd_; }
    [[nodiscard]] bool hasLoopRange() const { return loopEnd_ > loopStart_; }
    void setLoopActive(bool active);
    [[nodiscard]] bool loopActive() const { return loopActive_; }
    /// Sets the Loop Bar around the selected clips (or the whole song if none are selected).
    void loopSelectionOrSong();
    void clearLoopRange();

    // ---- selection & clipboard actions (Edit menu / keyboard shortcuts)
    [[nodiscard]] bool hasSelectedClips();
    [[nodiscard]] bool hasClipClipboard() const
    {
        return !clipboardClips_.empty() || !clipboardAudio_.empty();
    }
    void selectAllClips();
    void deleteSelectedClips();
    void copySelectedClips();
    void cutSelectedClips();
    void pasteClips();
    void duplicateSelectedClips();

signals:
    void clipAdded(vloomdaw::Clip clip);
    void clipChanged(std::size_t index, vloomdaw::Clip clip);
    void clipRemoved(std::size_t index);
    void clipsAddedBatch(
        const std::vector<vloomdaw::Clip>& clips, const std::vector<vloomdaw::AudioClip>& audioClips);
    void patternPicked(std::size_t pattern);
    /// Double-clicked a pattern clip on `track`: select `pattern` + `track`'s channel and open Piano Roll.
    void patternClipActivated(std::size_t pattern, int track);
    /// A clip was clicked with Draw, Paint or Select: what it is becomes the source.
    void sourcePicked(vloomdaw::PlaylistView::ClipSource source);
    /// Audio files were dropped: one audio clip per file starting at `track` and `start` ticks.
    void audioDropped(const QStringList& paths, int track, int start);
    /// Standard MIDI Files (`.mid` / `.midi`) were dropped onto `track` at `startTick`.
    void midiFilesDropped(const QStringList& paths, int track, int startTick);
    /// A `.vloom` project file was dropped onto the playlist.
    void projectFileDropped(const QString& path);
    /// Loop Bar markers changed (`endTicks <= startTicks` clears the custom loop range).
    void loopRangeChanged(int startTicks, int endTicks, bool activate);
    void loopActiveToggled(bool active);
    void exportLoopRangeRequested();
    /// A plugin from PluginsPanel was dropped onto `track` (`>= trackCount()`: new track) at `startTick`.
    void pluginDroppedOnPlaylist(std::size_t pluginIndex, int track, int startTick);
    void audioClipChanged(std::size_t index, vloomdaw::AudioClip clip);
    void audioClipRemoved(std::size_t index);
    /// The user right-clicked an audio clip's title strip: show its menu at `globalPos`.
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
    /// Move `from` directly to index `to` (dragged header or Move to Top / Bottom).
    void trackReordered(std::size_t from, std::size_t to);
    void trackHeightChanged(std::size_t track, int height);
    /// A colour (0xRRGGBB) was picked for `track`; nullopt: none.
    void trackColorChanged(std::size_t track, std::optional<std::uint32_t> color);
    void trackCollapseToggled(std::size_t track, bool collapsed);
    void trackIndentRequested(std::size_t track);
    void trackOutdentRequested(std::size_t track);
    void trackMuteToggled(std::size_t track, bool muted);
    void trackSoloToggled(std::size_t track, bool solo);
    /// Header fader (`0..1`) or pan (`-1..+1`) adjusted on `track`.
    void trackVolumeChanged(std::size_t track, double volume);
    void trackPanChanged(std::size_t track, double pan);
    /// Expandable track inspector menu rows clicked on `track`.
    void trackChannelMenuRequested(std::size_t track, QPoint globalPos);
    void trackRoutingMenuRequested(std::size_t track, QPoint globalPos);
    void trackSelected(std::size_t track);
    /// Track creation actions (`afterTrack < 0` appends at the end; `>= 0` inserts right below `afterTrack`).
    void addTracksDialogRequested(int afterTrack);
    void addAudioTrackRequested(int afterTrack, bool mono);
    void addInstrumentTrackRequested(int afterTrack);

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
    enum class HeaderSlider { None, Volume, Pan };
    enum class LoopDrag { None, Draw, Move, ResizeStart, ResizeEnd };

    struct TrackMeter {
        double level[2] = {0.0, 0.0};
        double hold[2] = {0.0, 0.0};
        double holdAge[2] = {0.0, 0.0};
        bool clipped[2] = {false, false};
    };

    struct WaveTileKey {
        const ClipAudio* info = nullptr;
        std::size_t peakCount = 0;
        int tileIndex = 0;
        int heightPx = 0;
        long long nsPerPixel = 0;
        QRgb rgba = 0;
        int dprPct = 100;
        friend bool operator==(const WaveTileKey&, const WaveTileKey&) = default;
    };
    struct WaveTileKeyHash {
        std::size_t operator()(const WaveTileKey& k) const noexcept
        {
            std::size_t h = std::hash<const void*>{}(k.info);
            auto mix = [&h](std::size_t v) { h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); };
            mix(k.peakCount);
            mix(static_cast<std::size_t>(k.tileIndex));
            mix(static_cast<std::size_t>(k.heightPx));
            mix(static_cast<std::size_t>(k.nsPerPixel));
            mix(static_cast<std::size_t>(k.rgba));
            mix(static_cast<std::size_t>(k.dprPct));
            return h;
        }
    };

    [[nodiscard]] double pixelsPerTick() const;
    [[nodiscard]] int lengthTicks() const;
    [[nodiscard]] QRect gridRect() const;
    [[nodiscard]] QRect rulerRect() const;
    /// Ends a held playback (Playback tool, time bar): playbackReleased().
    void endPlayback();
    [[nodiscard]] double tickAt(int x) const;
    [[nodiscard]] int trackAt(int y) const;
    [[nodiscard]] int trackCount() const;
    [[nodiscard]] bool onHeaderDivider(const QPoint& pos) const;
    /// The track whose header is at `pos`, or -1.
    [[nodiscard]] int headerTrackAt(const QPoint& pos) const;
    [[nodiscard]] QRect trackColorRect(int track) const;
    [[nodiscard]] int headerTopRowY(int track) const;
    [[nodiscard]] QRect trackNumberRect(int track) const;
    [[nodiscard]] QRect muteButtonRect(int track) const;
    [[nodiscard]] QRect soloButtonRect(int track) const;
    [[nodiscard]] QRect trackBadgeRect(int track) const;
    [[nodiscard]] QRect trackNameRect(int track) const;
    [[nodiscard]] QRect volumeBarRect(int track) const;
    [[nodiscard]] QRect panBarRect(int track) const;
    /// `row`: 0 = Channel/Input, 1 = Mixer Insert Output. Empty when hidden.
    [[nodiscard]] QRect inspectorRowRect(int track, int row) const;
    [[nodiscard]] QRect trackMeterRect(int track) const;
    void selectTrackByUser(int track);
    void repaintTrackRow(int track);
    void populateAddTrackActions(QMenu& menu, int afterTrack);
    /// Toggles mute, solo, collapse or opens an inspector menu when `pos` hits one.
    bool handleTrackHeaderClick(int track, const QPoint& pos, const QPoint& globalPos);
    void paintTrackHeader(QPainter& p, int track) const;
    void paintTrackMeter(QPainter& p, int track) const;
    void paintPillButton(
        QPainter& p, const QRect& r, const QString& label, bool on, const QColor& onColor) const;
    void paintInspectorBox(
        QPainter& p, const QRect& r, const QString& prefix, const QString& text, bool active) const;
    [[nodiscard]] QPixmap waveTile(const ClipAudio* info, int tileIndex, int waveH, double secondsPerPixel,
        const QColor& lineColor, qreal dpr) const;
    void showSliderTooltip(int track, HeaderSlider slider, double value, const QPoint& globalPos) const;

    [[nodiscard]] int yOfTrack(int track) const;
    /// Lane height of `track` in pixels; 0 for a hidden sub-track.
    [[nodiscard]] int trackHeight(int track) const;
    /// Rebuilds the track positions from the project.
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
    void paintAudioClip(
        QPainter& p, const AudioClip& clip, const QRect& clipArea, bool dragged, bool selected) const;
    /// Whether a clip is selected (Select tool).
    [[nodiscard]] bool isSelected(ClipRef clip) const;
    [[nodiscard]] std::size_t selectionSize() const;
    void clearSelection();
    /// Drops the selection when clips were added or removed elsewhere (indices moved).
    void syncSelection();
    /// Selects the clips touching `band` (viewport coordinates), adding when `add`.
    void selectIn(const QRect& band, bool add);
    /// Starts moving every selected clip with the pointer.
    void beginGroupDrag(const QPoint& pos);
    void moveGroup(const QPoint& pos, int grid);
    /// Paint tool: lays the current pattern on the paint track in every slot.
    void paintTo(double tick);
    /// Places a clip of the source on `track` at `start`.
    bool placeSource(int track, int start);
    /// The colour of `track` (chosen by the user), else `fallback`.
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
    [[nodiscard]] int snapTicks(Qt::KeyboardModifiers modifiers) const;
    [[nodiscard]] int sampleTicks(const AudioClip& clip) const;
    [[nodiscard]] double clipTempo(const AudioClip& clip) const
    {
        return clip.fitted() ? clip.sourceBpm : tempo_;
    }
    [[nodiscard]] std::optional<std::pair<ClipRef, bool>> edgeAt(const QPoint& pos) const;
    void resizeClipTo(double tick, int grid);
    void sliceAcross(int tick, int fromTrack, int toTrack);
    [[nodiscard]] int sliceTick(int x, Qt::KeyboardModifiers modifiers) const;
    [[nodiscard]] std::optional<std::pair<int, int>> dropTarget(const QPoint& pos) const;
    void updateScrollBars();
    void beginDrag(ClipRef clip, const QPoint& pos);
    void startTrackNameEdit(int track);
    void commitTrackNameEdit();

    const Project* project_ = nullptr;
    const MixerModel* mixer_ = nullptr;
    int selectedTrack_ = 0;
    int headerWidth_ = kDefaultHeaderWidth;
    bool resizingHeaderWidth_ = false;
    int headerResizeStartX_ = 0;
    int headerResizeStartWidth_ = kDefaultHeaderWidth;

    HeaderSlider draggingSlider_ = HeaderSlider::None;
    int sliderTrack_ = -1;
    int sliderLastX_ = 0;
    double sliderValue_ = 0.0;

    std::vector<TrackMeter> meters_;
    mutable std::unordered_map<WaveTileKey, QPixmap, WaveTileKeyHash> waveTiles_;
    mutable unsigned cachedThemeGen_ = 0;

    ClipSource source_; ///< what Draw and Paint place
    Snap snap_ = Snap::Auto;
    bool snapEnabled_ = true;
    bool followPlayhead_ = true;
    double playhead_ = -1.0;
    double pixelsPerBar_ = 72.0;
    double tempo_ = 120.0;
    std::function<const ClipAudio*(const std::string&)> audioInfo_;
    std::vector<QRectF> dropHints_; ///< where dragged audio files would land
    std::optional<std::pair<int, int>> lastDropTarget_;
    qsizetype lastDropCount_ = 0;

    bool dragging_ = false;
    ClipRef dragClip_;
    Clip dragStartClip_;
    AudioClip dragStartAudio_;
    double dragStartTick_ = 0.0;

    /// Top of each track from the first one's, in pixels, plus the bottom of the last.
    std::vector<int> tops_{0};
    int resizingTrack_ = -1; ///< whose height a drag changes
    int resizeStartY_ = 0;
    int resizeStartHeight_ = 0;
    int pendingTrackDrag_ = -1; ///< track header pressed, waiting for drag threshold
    QPoint trackDragStartPos_;
    bool draggingTrackOrder_ = false;
    int trackDropSlot_ = -1; ///< insertion slot `0..trackCount()` while dragging a track header

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
    std::vector<Clip> clipboardClips_;
    std::vector<AudioClip> clipboardAudio_;
    int clipboardMinStart_ = 0;
    int clipboardMaxEnd_ = 0;

    int loopStart_ = 0;
    int loopEnd_ = 0;
    bool loopActive_ = true;
    LoopDrag loopDrag_ = LoopDrag::None;
    QPoint loopPressPos_;
    bool loopDragMoved_ = false;
    int loopAnchorTick_ = 0;
    int loopOrigStart_ = 0;
    int loopOrigEnd_ = 0;
};

} // namespace vloomdaw
