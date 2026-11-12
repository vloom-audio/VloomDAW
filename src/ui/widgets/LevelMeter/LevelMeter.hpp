// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QWidget>

namespace vloomdaw {

/// Vertical stereo peak meter for a mixer strip: two bars on a dB scale
/// (-60 .. +6 dB) that fall back smoothly, with a peak-hold line and a red
/// clip light above 0 dB. Colours come from Theme.
///
/// Feed it from a UI timer with addPeaks(); the meter does its own fall-off
/// in tick(), which the owner calls once per timer tick.
class LevelMeter : public QWidget {
    Q_OBJECT

public:
    explicit LevelMeter(QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override { return {14, 160}; }
    [[nodiscard]] QSize minimumSizeHint() const override { return {10, 60}; }

public slots:
    /// Linear peaks since the last tick (0 = silence, 1 = 0 dBFS).
    void addPeaks(float left, float right);
    /// Advances the fall-off by `seconds`; repaints only when something moved.
    void tick(double seconds);
    /// Clears the bars, holds and clip light (click on the meter does too).
    void reset();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    struct Channel {
        double level = 0.0; ///< shown bar, 0..1 of the scale
        double hold = 0.0; ///< peak-hold line, 0..1 of the scale
        double holdAge = 0.0; ///< seconds since the hold was set
        bool clipped = false;
    };
    void feed(Channel& channel, float peak);

    Channel channels_[2];
};

} // namespace vloomdaw
