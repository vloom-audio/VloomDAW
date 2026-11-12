// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QString>
#include <QWidget>

#include <functional>

namespace vloomdaw {

/// A small rotary knob with a caption underneath, like FL Studio's master
/// volume and pitch knobs.
///
/// Drag up / down to turn it (Shift: fine), scroll, or double-click to return
/// to the default. When the range spans zero the value arc grows from the
/// centre (bipolar, e.g. pitch); otherwise from the left. Colours come from
/// Theme.
class Knob : public QWidget {
    Q_OBJECT

public:
    Knob(const QString& caption, double minimum, double maximum, double defaultValue,
        QWidget* parent = nullptr);

    [[nodiscard]] double value() const { return value_; }
    /// How a value reads in the tool tip, e.g. "100% (0.0 dB)".
    void setValueText(std::function<QString(double)> format);
    /// What the knob does, shown above the value in the tool tip.
    void setDescription(const QString& text);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override { return sizeHint(); }

public slots:
    /// Sets the value (clamped) without emitting valueChanged().
    void setValue(double value);

signals:
    /// The user turned the knob.
    void valueChanged(double value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void changeByUser(double value);
    void updateToolTip();
    [[nodiscard]] double fraction() const; ///< value as 0..1 of the range

    QString caption_;
    QString description_;
    double minimum_;
    double maximum_;
    double default_;
    double value_;
    std::function<QString(double)> format_;
    bool dragging_ = false;
    double dragStartY_ = 0.0;
    double dragStartValue_ = 0.0;
};

} // namespace vloomdaw
