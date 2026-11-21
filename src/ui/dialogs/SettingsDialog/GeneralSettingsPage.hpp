// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;

namespace vloomdaw {

class ToggleSwitch;

/// Settings → General: what VloomDAW looks like when it starts, the plugin
/// displays' animation, and updates.
/// A pure view: emits what the user picked.
class GeneralSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit GeneralSettingsPage(QWidget* parent = nullptr);

    /// 0 = bars · beats · ticks, 1 = minutes · seconds. Never emits.
    void setStartPositionMode(int mode);
    void setMetronomeOnStartup(bool on);
    void setAssociateProjectFiles(bool on);
    void setAnimatePluginDisplays(bool on);
    void setCheckForUpdates(bool on);
    void setBetaUpdates(bool on);
    /// The line under "Check now": the last result ("Up to date", an error).
    void setUpdateStatus(const QString& text);

signals:
    void startPositionModeChanged(int mode);
    void metronomeOnStartupChanged(bool on);
    void associateProjectFilesChanged(bool on);
    void animatePluginDisplaysChanged(bool on);
    void checkForUpdatesChanged(bool on);
    void betaUpdatesChanged(bool on);
    void checkForUpdatesNow();

private:
    QComboBox* positionMode_ = nullptr;
    ToggleSwitch* metronome_ = nullptr;
    ToggleSwitch* associateFiles_ = nullptr;
    ToggleSwitch* animateDisplays_ = nullptr;
    ToggleSwitch* checkUpdates_ = nullptr;
    ToggleSwitch* betaUpdates_ = nullptr;
    QPushButton* checkNow_ = nullptr;
    QLabel* updateStatus_ = nullptr;
};

} // namespace vloomdaw
