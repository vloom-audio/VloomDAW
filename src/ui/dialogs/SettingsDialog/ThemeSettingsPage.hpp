// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/ThemePresets.hpp"

#include <QList>
#include <QWidget>

class QAbstractButton;
class QButtonGroup;
class QLabel;

namespace vloomdaw {

class Knob;

/// Settings → Theme, a small theme editor:
///  - Presets: miniatures of the app in each palette;
///  - Adjustments: hue, saturation, brightness, contrast and text knobs that
///    tune the chosen preset;
///  - Colours: one swatch per role (selection, playing, record, LCD, text) to
///    pick any colour, plus quick accent picks;
///  - a warning when the tuning makes text hard to read.
/// A pure view: it edits a ThemeChoice and emits it; the caller applies it.
class ThemeSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit ThemeSettingsPage(QWidget* parent = nullptr);

    /// Shows `choice`. Never emits.
    void setChoice(const ThemeChoice& choice);
    [[nodiscard]] const ThemeChoice& choice() const { return choice_; }

signals:
    /// Any edit; knob drags emit continuously.
    void choiceChanged(const vloomdaw::ThemeChoice& choice);

private:
    void edit(); ///< refresh, then emit
    void refresh(); ///< marks, swatches and the warning follow choice_
    void pickRoleColor(int role);

    ThemeChoice choice_;
    QButtonGroup* presets_ = nullptr;
    QList<QAbstractButton*> presetTiles_; ///< in palettes() order
    QList<QAbstractButton*> accentSwatches_; ///< in accents() order
    QList<QAbstractButton*> roleSwatches_; ///< selection, playing, record, LCD, text
    Knob* hue_ = nullptr;
    Knob* saturation_ = nullptr;
    Knob* brightness_ = nullptr;
    Knob* contrast_ = nullptr;
    Knob* text_ = nullptr;
    QLabel* warning_ = nullptr;
};

} // namespace vloomdaw
