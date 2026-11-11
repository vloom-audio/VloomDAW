// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QWidget>

namespace vloomdaw {

/// Vertical mixer fader. Its value is a position 0..1 (see faderToGain());
/// the tool tip shows it in dB.
///
/// Drag the cap or anywhere on the track (Shift: fine), scroll, or
/// double-click to return to unity. Colours come from Theme.
class Fader : public QWidget {
    Q_OBJECT

public:
    explicit Fader(QWidget* parent = nullptr);

    [[nodiscard]] double value() const { return value_; }

    [[nodiscard]] QSize sizeHint() const override { return {22, 160}; }
    [[nodiscard]] QSize minimumSizeHint() const override { return {18, 60}; }

public slots:
    /// Sets the position (clamped) without emitting valueChanged().
    void setValue(double position);

signals:
    void valueChanged(double position);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void changeByUser(double position);
    void updateToolTip();
    [[nodiscard]] int travel() const; ///< pixels the cap centre can move
    [[nodiscard]] int capCentreY() const;

    double value_;
    bool dragging_ = false;
    double dragStartY_ = 0.0;
    double dragStartValue_ = 0.0;
};

} // namespace vloomdaw
