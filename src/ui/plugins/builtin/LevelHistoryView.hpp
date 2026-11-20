// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/DynamicsMeter.hpp"

#include <QString>
#include <QWidget>

#include <functional>
#include <vector>

namespace vloomdaw {

/// The scrolling level display of the built-in dynamics editors
/// (compressor, limiter, gate): the last few seconds of the input level as a
/// faint area, the output as a line (or a filled area), the gain reduction
/// hanging from the top in the accent, and a dashed marker line (threshold,
/// ceiling) with a handle and a label, dragged up or down to set it.
///
/// It draws only what it is given: push() one reading per tick, with
/// kNoLevel for a level that is not known (no audio running, an input that
/// cannot be worked out); those leave gaps.
class LevelHistoryView : public QWidget {
    Q_OBJECT

public:
    /// What it draws.
    struct Look {
        bool inputArea = true; ///< the input as a faint area
        bool outputArea = false; ///< the output filled down to the bottom
        bool outputLine = true; ///< the output's outline
        bool outputAccent = false; ///< the output in the accent (a gate: what it lets through)
        bool reduction = true; ///< the gain reduction from the top
    };

    explicit LevelHistoryView(Look look, QWidget* parent = nullptr);

    /// The levels at the top and the bottom (dB full scale) and where grid
    /// lines go.
    void setRange(double topDb, double bottomDb, std::vector<double> gridDb);
    /// Adds the newest reading at the right; the rest scroll left.
    void push(double inputDb, double outputDb, double reductionDb);
    /// Forgets the readings.
    void clear();

    /// Shows the marker at `db` with `label` ("Threshold −18.0 dB").
    void setMarker(double db, const QString& label);
    /// Where the marker's label starts (px from the left; 18 unless set): to
    /// the right of an inset that covers the left of the display.
    void setMarkerLabelX(int x);

    /// The user dragged the marker to `db` (not kept in range: the editor does that).
    std::function<void(double db)> onMarkerDragged;
    /// The user double-clicked the marker.
    std::function<void()> onMarkerReset;

    /// Readings it keeps: the widest display it can fill.
    static constexpr std::size_t kCapacity = 1024;
    /// Pixels between two readings.
    static constexpr int kStep = 4;

    [[nodiscard]] QSize minimumSizeHint() const override { return {320, 200}; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    [[nodiscard]] double pixelsPerDb() const;
    [[nodiscard]] double yOf(double db) const;
    [[nodiscard]] bool onMarker(double y) const;
    /// x of reading `index` (0: the oldest) when `count` are shown.
    [[nodiscard]] double xOf(std::size_t index, std::size_t count) const;

    Look look_;
    double topDb_ = 0.0;
    double bottomDb_ = -60.0;
    std::vector<double> gridDb_;
    LevelHistory input_{kCapacity};
    LevelHistory output_{kCapacity};
    LevelHistory reduction_{kCapacity};
    double markerDb_ = 0.0;
    QString markerLabel_;
    int markerLabelX_ = 18;
    bool dragging_ = false;
    double dragDb_ = 0.0; ///< the marker's level as the drag goes, unclamped
    double lastY_ = 0.0;
};

} // namespace vloomdaw
