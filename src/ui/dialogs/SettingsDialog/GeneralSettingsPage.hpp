// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QWidget>

class QComboBox;

namespace vloomdaw {

class ToggleSwitch;

/// Settings → General: what VloomDAW looks like when it starts.
/// A pure view: emits what the user picked.
class GeneralSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit GeneralSettingsPage(QWidget* parent = nullptr);

    /// 0 = bars · beats · ticks, 1 = minutes · seconds. Never emits.
    void setStartPositionMode(int mode);
    void setMetronomeOnStartup(bool on);

signals:
    void startPositionModeChanged(int mode);
    void metronomeOnStartupChanged(bool on);

private:
    QComboBox* positionMode_ = nullptr;
    ToggleSwitch* metronome_ = nullptr;
};

} // namespace vloomdaw
