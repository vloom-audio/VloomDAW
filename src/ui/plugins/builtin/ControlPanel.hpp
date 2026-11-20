// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QFrame>

class QHBoxLayout;

namespace vloomdaw {

/// The floating panel of a built-in plugin's editor: 100 px tall, rounded,
/// 94 % opaque over the display, with a drop shadow. Holds a row of knobs
/// and switches (ArcKnob, PillToggle) left to right; BuiltinEditorWindow
/// keeps it centred at the bottom of the display, as wide as they need.
class ControlPanel : public QFrame {
    Q_OBJECT

public:
    explicit ControlPanel(QWidget* parent = nullptr);

    /// Adds `control` at the end of the row (centred in its height).
    void add(QWidget* control);
    /// Adds a gap of `pixels`: two groups of controls.
    void addSpacing(int pixels);
    /// Adds a thin upright rule between two groups.
    void addDivider();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QHBoxLayout* row_ = nullptr;
};

/// The 32 px bar under a built-in plugin's editor: readouts (Readout,
/// PillToggle with Layout::Inline) on the left and on the right.
class FooterBar : public QFrame {
    Q_OBJECT

public:
    explicit FooterBar(QWidget* parent = nullptr);

    /// Adds `widget` after the others on the left.
    void addLeft(QWidget* widget);
    /// Adds `widget` after the others on the right.
    void addRight(QWidget* widget);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QHBoxLayout* row_ = nullptr;
    int left_ = 0; ///< widgets before the stretch
};

} // namespace vloomdaw
