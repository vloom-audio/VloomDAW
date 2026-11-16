// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/NoteEdits.hpp"
#include "core/Project.hpp"
#include "core/Scales.hpp"

#include <QAbstractScrollArea>
#include <QPoint>
#include <QRect>

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
/// Mouse, on the grid:
/// - Left-click an empty spot adds a note (snapped, as long as the last one)
///   and keeps dragging it into place.
/// - Drag a note to move it, its right edge to resize it. The other selected
///   notes follow. Shift+drag moves a copy.
/// - Click a note to select it; Ctrl+click adds it to the selection or takes
///   it out. Ctrl+drag from an empty spot selects the notes in a box (with
///   Shift: adds them).
/// - Right-click, or drag with the right button, deletes notes.
/// - Alt: no snap. Ctrl+wheel zooms horizontally.
///
/// In the velocity lane, drag over the stems to set how hard the notes are
/// played (the selected ones only, when some are selected). The keyboard on
/// the left plays the key under the mouse.
///
/// Keys: Delete, Ctrl+A / C / X / V / D (duplicate after the selection),
/// Ctrl+Q (quantize to the snap), Ctrl+Z / Ctrl+Y, arrows (Up / Down: a
/// semitone, with Shift an octave; Left / Right: one snap step), Escape.
class PianoRollView : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit PianoRollView(QWidget* parent = nullptr);

    void setProject(const Project* project);
    void setTarget(std::size_t pattern, std::size_t channel);
    void setKey(int root, Scale scale);
    /// Playhead in ticks from the pattern start (< 0: hidden).
    void setPlayhead(double tick);
    /// What notes snap to, in ticks (e.g. kTicksPerStep / 2 for 1/32 notes);
    /// at least kMinNoteLength. The grid shows it.
    void setSnapTicks(int ticks);
    [[nodiscard]] int snapTicks() const { return snapTicks_; }
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

signals:
    /// The notes are to be these now (a drag sends this on every move).
    void notesChanged(const std::vector<vloomdaw::Note>& notes);
    /// The edit that notesChanged() made is complete: one step to undo.
    void editFinished();
    /// A key is to sound (the keyboard, or a note being placed), until keyReleased().
    void keyPressed(int key, int velocity);
    void keyReleased(int key);
    void undoRequested();
    void redoRequested();

protected:
    bool event(QEvent* event) override;
    bool viewportEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
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
        Erase, ///< right button held: deletes what it passes over
        Velocity, ///< in the lane: sets the velocity of the stems it passes over
        Keys, ///< on the keyboard: plays the key under the mouse
    };

    [[nodiscard]] const std::vector<Note>* notes() const;
    [[nodiscard]] double pixelsPerTick() const;
    [[nodiscard]] int lengthTicks() const; ///< scrollable length
    [[nodiscard]] QRect gridRect() const; ///< right of the keyboard, between the ruler and the lane
    [[nodiscard]] QRect laneRect() const; ///< the velocity lane, right of the keyboard
    [[nodiscard]] double tickAt(int x) const;
    [[nodiscard]] int keyAt(int y) const;
    [[nodiscard]] int yOfKey(int key) const; ///< top of the row
    [[nodiscard]] QRectF noteRect(const Note& note) const;
    /// Topmost note under `pos`, and whether `pos` is on its resize edge.
    [[nodiscard]] std::optional<std::pair<std::size_t, bool>> noteAt(const QPoint& pos) const;
    void updateScrollBars();

    /// Sends `next` as the notes and repaints; the selection is kept as it is.
    void edit(const std::vector<Note>& next);
    /// One complete edit: the notes, then editFinished().
    void commit(const std::vector<Note>& next);
    void selectOnly(std::size_t index);
    void beginDrag(Drag drag, std::size_t index, const QPoint& pos);
    void endDrag();
    /// Sounds `key` instead of the one sounding (-1: none).
    void sound(int key, int velocity);
    void eraseAt(const QPoint& pos);
    void setVelocityAt(const QPoint& from, const QPoint& to);
    void nudge(int ticks, int keys);

    const Project* project_ = nullptr;
    std::size_t pattern_ = 0;
    std::size_t channel_ = 0;
    int root_ = 0;
    Scale scale_ = Scale::Major;
    double playhead_ = -1.0;
    double pixelsPerBeat_ = 64.0;
    int lastLength_ = kTicksPerStep; ///< length of the next new note
    int lastVelocity_ = kDefaultVelocity; ///< and its velocity
    int snapTicks_ = kTicksPerStep;
    bool scrolledToMiddle_ = false;

    NoteSelection selection_; ///< one flag per note
    std::vector<Note> clipboard_;
    std::size_t expectedCount_ = 0; ///< how many notes the view last knew of
    std::optional<QPoint> hover_; ///< where the mouse is over the view
    int sounding_ = -1; ///< the key keyPressed() was sent for; -1: none

    Drag drag_ = Drag::None;
    std::vector<Note> dragNotes_; ///< the notes when the drag began (Erase, Velocity: as it left them)
    NoteSelection boxBase_; ///< Box: what was selected before it
    QPoint dragStart_;
    QPoint dragLast_;
    std::size_t dragIndex_ = 0; ///< the note under the mouse when the drag began, in dragNotes_
    Note dragNote_; ///< and how it was
    double dragStartTick_ = 0.0;
    int dragStartKey_ = 0;
    bool dragChanged_ = false; ///< the drag sent notesChanged()
};

} // namespace vloomdaw
