// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/Project.hpp"
#include "core/Scales.hpp"

#include <QAbstractScrollArea>

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace vloomdaw {

/// Piano roll for one channel of one pattern: a keyboard on the left, a
/// bar / beat ruler on top and the notes on a grid. Rows outside the key's
/// scale are shaded darker. Reads the Project; edits are emitted as signals
/// for MainWindow to apply, which then calls refresh().
///
/// Left-click an empty spot to add a note (snapped to the grid, a 1/16 step
/// by default) and keep dragging to place it; drag a note to move it, drag
/// its right edge to resize; right-click a note to delete it. Hold Alt to
/// move without snap.
/// Ctrl+wheel zooms horizontally.
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
    void refresh();

signals:
    void noteAdded(vloomdaw::Note note);
    void noteChanged(std::size_t index, vloomdaw::Note note);
    void noteRemoved(std::size_t index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    enum class Drag { None, Move, Resize };

    [[nodiscard]] const std::vector<Note>* notes() const;
    [[nodiscard]] double pixelsPerTick() const;
    [[nodiscard]] int lengthTicks() const; ///< scrollable length
    [[nodiscard]] QRect gridRect() const; ///< viewport area right of the keyboard, below the ruler
    [[nodiscard]] double tickAt(int x) const;
    [[nodiscard]] int keyAt(int y) const;
    [[nodiscard]] int yOfKey(int key) const; ///< top of the row
    [[nodiscard]] QRectF noteRect(const Note& note) const;
    /// Topmost note under `pos`, and whether `pos` is on its resize edge.
    [[nodiscard]] std::optional<std::pair<std::size_t, bool>> noteAt(const QPoint& pos) const;
    void updateScrollBars();
    void beginDrag(std::size_t index, Drag drag, const QPoint& pos);

    const Project* project_ = nullptr;
    std::size_t pattern_ = 0;
    std::size_t channel_ = 0;
    int root_ = 0;
    Scale scale_ = Scale::Major;
    double playhead_ = -1.0;
    double pixelsPerBeat_ = 64.0;
    int lastLength_ = kTicksPerStep; ///< length of the next new note
    int snapTicks_ = kTicksPerStep;
    bool scrolledToMiddle_ = false;

    Drag drag_ = Drag::None;
    std::size_t dragIndex_ = 0;
    Note dragStartNote_;
    double dragStartTick_ = 0.0;
    int dragStartKey_ = 0;
};

} // namespace vloomdaw
