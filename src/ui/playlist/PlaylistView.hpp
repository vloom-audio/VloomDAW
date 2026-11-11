// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/Project.hpp"

#include <QAbstractScrollArea>

#include <cstddef>
#include <optional>

namespace vloomdaw {

/// The playlist: track names on the left, a bar ruler on top and pattern
/// clips arranged on the tracks. Reads the Project; edits are emitted as
/// signals for MainWindow to apply, which then calls refresh().
///
/// Left-click an empty spot to place the current pattern there (snapped to
/// the bar) and keep dragging to position it; drag a clip to move it (Shift:
/// snap to beats); right-click a clip to delete it; double-click a clip to
/// make its pattern current. Ctrl+wheel zooms horizontally.
class PlaylistView : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit PlaylistView(QWidget* parent = nullptr);

    void setProject(const Project* project);
    /// The pattern a click places.
    void setCurrentPattern(std::size_t pattern);
    /// Playhead in ticks from the song start (< 0: hidden).
    void setPlayhead(double tick);
    void refresh();

signals:
    void clipAdded(vloomdaw::Clip clip);
    void clipChanged(std::size_t index, vloomdaw::Clip clip);
    void clipRemoved(std::size_t index);
    void patternPicked(std::size_t pattern);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    [[nodiscard]] double pixelsPerTick() const;
    [[nodiscard]] int lengthTicks() const;
    [[nodiscard]] QRect gridRect() const;
    [[nodiscard]] double tickAt(int x) const;
    [[nodiscard]] int trackAt(int y) const;
    [[nodiscard]] int yOfTrack(int track) const;
    [[nodiscard]] QRectF clipRect(const Clip& clip) const;
    [[nodiscard]] std::optional<std::size_t> clipAt(const QPoint& pos) const;
    void updateScrollBars();
    void beginDrag(std::size_t index, const QPoint& pos);

    const Project* project_ = nullptr;
    std::size_t currentPattern_ = 0;
    double playhead_ = -1.0;
    double pixelsPerBar_ = 72.0;

    bool dragging_ = false;
    std::size_t dragIndex_ = 0;
    Clip dragStartClip_;
    double dragStartTick_ = 0.0;
    int dragStartTrack_ = 0;
};

} // namespace vloomdaw
