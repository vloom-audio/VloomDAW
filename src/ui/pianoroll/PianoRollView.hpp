// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/NoteEdits.hpp"
#include "core/Project.hpp"
#include "core/Scales.hpp"

#include <QAbstractScrollArea>
#include <QPoint>
#include <QRect>
#include <QString>

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace vloomdaw {

/// Piano roll for one channel of one pattern: a keyboard on the left, a
/// bar / beat ruler on top, the notes on a grid and their velocities in a
/// lane along the bottom. Rows outside the key's scale are shaded darker.
/// Reads the Project; an edit is emitted as the whole new list of notes for
/// MainWindow to apply, which then calls refresh().
///
/// Supports Studio One-style MIDI editing tools (`Select`, `Draw`, `Paint`,
/// `Slice`, `Delete`, `Playback`), marquee box-selection (`Select` tool or
/// `Ctrl+drag`), pitch transposition (`transpose()`, drag, `Up/Down`, `Alt+Wheel`),
/// Legato, Scale Snap, Velocity scaling, Audition toggle, and Ruler scrubbing.
class PianoRollView : public QAbstractScrollArea {
    Q_OBJECT

public:
    enum class Tool { Select, Draw, Paint, Slice, Delete, Playback };
    enum class LaneMode { Velocity, PitchBend, Controller, Pressure };

    explicit PianoRollView(QWidget* parent = nullptr);

    void setProject(const Project* project);
    void setTarget(std::size_t pattern, std::size_t channel);
    void setKey(int root, Scale scale);
    void setLaneMode(LaneMode mode, int cc = 1);
    [[nodiscard]] LaneMode laneMode() const { return laneMode_; }
    [[nodiscard]] int laneController() const { return laneCc_; }
    /// Playhead in ticks from the pattern start (< 0: hidden).
    void setPlayhead(double tick);
    /// What notes snap to, in ticks (e.g. kTicksPerStep / 2 for 1/32 notes);
    /// at least kMinNoteLength. The grid shows it.
    void setSnapTicks(int ticks);
    [[nodiscard]] int snapTicks() const { return snapTicks_; }
    void setSnapEnabled(bool enabled);
    [[nodiscard]] bool snapEnabled() const { return snapEnabled_; }
    void setAuditionEnabled(bool enabled) { auditionEnabled_ = enabled; }
    [[nodiscard]] bool auditionEnabled() const { return auditionEnabled_; }
    void setFollowPlayhead(bool follow) { followPlayhead_ = follow; }
    [[nodiscard]] bool followPlayhead() const { return followPlayhead_; }
    void setTool(Tool tool);
    [[nodiscard]] Tool tool() const { return tool_; }
    void zoomBy(double factor);

    /// The project's notes changed: repaint. A change the view did not make
    /// itself that adds or removes notes (an undo, a new project) drops the selection.
    void refresh();

    [[nodiscard]] bool hasSelection() const { return anySelected(selection_); }
    [[nodiscard]] bool canPaste() const { return !clipboard_.empty(); }

public slots:
    void selectAll();
    void clearSelection();
    void copySelection();
    void cutSelection();
    /// Pastes where the mouse points on the grid (snapped), else at the first
    /// bar line in view; the pasted notes are selected.
    void paste();
    /// Copies the selection right after itself; the copies are selected.
    void duplicateSelection();
    void deleteSelection();
    /// Moves the starts of the selected notes (all notes, when none is) to the snap grid.
    void quantize();
    /// Transposes selected notes (or all notes when none are selected) by `semitones`.
    void transpose(int semitones);
    /// Extends selected notes (or all notes) legato up to the next note onset.
    void applyLegato();
    /// Snaps selected notes (or all notes) to the active `(root_, scale_)`.
    void snapToScale();
    /// Scales velocity of selected notes (or all notes) by `factor`.
    void scaleVelocity(double factor);

signals:
    /// The notes are to be these now (a drag sends this on every move).
    void notesChanged(const std::vector<vloomdaw::Note>& notes);
    /// The edit that notesChanged() made is complete: one step to undo.
    void editFinished();
    /// MIDI control curve points (pitch bend / CC / channel pressure) changed during a lane drag.
    void controlsChanged(const std::vector<vloomdaw::ControlPoint>& controls);
    /// The edit that controlsChanged() made is complete: one step to undo.
    void controlEditFinished();
    /// A key is to sound (the keyboard, or a note being placed), until keyReleased().
    void keyPressed(int key, int velocity);
    void keyReleased(int key);
    void undoRequested();
    void redoRequested();
    /// Playback tool or ruler scrub: play from `tick` (in pattern ticks).
    void playbackRequested(double tick);
    void playbackReleased();
    /// Emitted whenever the note selection changes (`count` selected notes + pitch range summary).
    void selectionSummaryChanged(int count, const QString& summary);

protected:
    bool event(QEvent* event) override;
    bool viewportEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    enum class Drag {
        None,
        Move,
        Resize,
        Box, ///< selecting the notes in a rectangle
        Erase, ///< right button or Delete tool held: deletes what it passes over
        Paint, ///< Paint tool held: lays consecutive notes at each grid step
        Slice, ///< Slice tool held: cuts notes crossed by the slice line
        Scrub, ///< Playback tool or Ruler held: scrubs transport position
        Velocity, ///< in the lane: sets the velocity of the stems it passes over
        ControlDraw, ///< in the lane: draws/updates MIDI control curve points
        ControlErase, ///< in the lane: erases MIDI control curve points
        Keys, ///< on the keyboard: plays the key under the mouse
    };

    struct SliceLine {
        int tick = 0;
        int fromKey = 60;
        int toKey = 60;
    };

    [[nodiscard]] const std::vector<Note>* notes() const;
    [[nodiscard]] const std::vector<ControlPoint>* controls() const;
    [[nodiscard]] bool matchesActiveLane(const ControlPoint& pt) const;
    [[nodiscard]] QString activeLaneShortLabel() const;
    [[nodiscard]] double pixelsPerTick() const;
    [[nodiscard]] int lengthTicks() const; ///< scrollable length
    [[nodiscard]] QRect rulerRect() const;
    [[nodiscard]] QRect gridRect() const; ///< right of the keyboard, between the ruler and the lane
    [[nodiscard]] QRect laneRect() const; ///< the velocity/control lane, right of the keyboard
    [[nodiscard]] QRect laneCornerRect() const; ///< bottom-left lane selector area under the keyboard
    [[nodiscard]] double tickAt(int x) const;
    [[nodiscard]] int keyAt(int y) const;
    [[nodiscard]] int yOfKey(int key) const; ///< top of the row
    [[nodiscard]] QRectF noteRect(const Note& note) const;
    [[nodiscard]] int effectiveGrid(Qt::KeyboardModifiers mods) const;
    /// Topmost note under `pos`, and whether `pos` is on its resize edge.
    [[nodiscard]] std::optional<std::pair<std::size_t, bool>> noteAt(const QPoint& pos) const;
    void updateScrollBars();
    void scrollToChannelNotes();
    void emitSelectionSummary();
    void updateHoverCursor(const QPoint& pos);

    /// Sends `next` as the notes and repaints; the selection is kept as it is.
    void edit(const std::vector<Note>& next);
    /// One complete edit: the notes, then editFinished().
    void commit(const std::vector<Note>& next);
    void selectOnly(std::size_t index);
    void insertNoteAndBeginMove(const QPoint& pos);
    void paintNoteAt(const QPoint& pos, int grid);
    void beginDrag(Drag drag, std::size_t index, const QPoint& pos);
    void endDrag();
    /// Sounds `key` instead of the one sounding (-1: none).
    void sound(int key, int velocity);
    void eraseAt(const QPoint& pos);
    void setVelocityAt(const QPoint& from, const QPoint& to);
    void drawControlAt(const QPoint& from, const QPoint& to, Qt::KeyboardModifiers mods);
    void eraseControlAt(const QPoint& from, const QPoint& to);
    void openLaneMenu(const QPoint& globalPos);
    void nudge(int ticks, int keys);

    const Project* project_ = nullptr;
    std::size_t pattern_ = 0;
    std::size_t channel_ = 0;
    int root_ = 0;
    Scale scale_ = Scale::Major;
    Tool tool_ = Tool::Select;
    LaneMode laneMode_ = LaneMode::Velocity;
    int laneCc_ = 1;
    double playhead_ = -1.0;
    double pixelsPerBeat_ = 64.0;
    int lastLength_ = kTicksPerStep; ///< length of the next new note
    int lastVelocity_ = kDefaultVelocity; ///< and its velocity
    int snapTicks_ = kTicksPerStep;
    bool snapEnabled_ = true;
    bool auditionEnabled_ = true;
    bool followPlayhead_ = true;
    bool scrolledToMiddle_ = false;

    NoteSelection selection_; ///< one flag per note
    std::vector<Note> clipboard_;
    std::size_t expectedCount_ = 0; ///< how many notes the view last knew of
    std::optional<QPoint> hover_; ///< where the mouse is over the view
    int sounding_ = -1; ///< the key keyPressed() was sent for; -1: none

    Drag drag_ = Drag::None;
    std::vector<Note> dragNotes_; ///< the notes when the drag began (Erase, Velocity: as it left them)
    std::vector<ControlPoint> dragControls_; ///< control points while dragging a control lane
    bool dragControlChanged_ = false;
    NoteSelection boxBase_; ///< Box: what was selected before it
    std::optional<SliceLine> sliceLine_;
    int paintLastSlot_ = -1;
    double lastScrubTick_ = -1.0;
    QPoint dragStart_;
    QPoint dragLast_;
    std::size_t dragIndex_ = 0; ///< the note under the mouse when the drag began, in dragNotes_
    Note dragNote_; ///< and how it was
    double dragStartTick_ = 0.0;
    int dragStartKey_ = 0;
    bool dragChanged_ = false; ///< the drag sent notesChanged()
};

} // namespace vloomdaw
