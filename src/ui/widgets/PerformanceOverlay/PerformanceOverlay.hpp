// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/FrameStats.hpp"

#include <QWidget>

namespace vloomdaw {

/// Small debug panel with UI performance figures (FPS, paint time, refresh
/// tick spacing, audio CPU), drawn over the window's corner. Ignores the
/// mouse. Each figure turns red when it is out of budget.
class PerformanceOverlay : public QWidget {
    Q_OBJECT

public:
    explicit PerformanceOverlay(QWidget* parent = nullptr);

    /// Shows `stats`, the engine's audio load (0..1) and the expected tick
    /// spacing in ms (to judge late ticks).
    void setFigures(const FrameStats::Snapshot& stats, float audioLoad, double expectedTickMs);

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    FrameStats::Snapshot stats_;
    float audioLoad_ = 0.0f;
    double expectedTickMs_ = 33.0;
    QFont font_;
};

} // namespace vloomdaw
