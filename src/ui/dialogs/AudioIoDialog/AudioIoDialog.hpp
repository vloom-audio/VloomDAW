// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/AudioIoSetup.hpp"

#include <QDialog>
#include <QStringList>

#include <array>

class QComboBox;
class QLabel;
class QPushButton;
class QScrollArea;

namespace vloomdaw {

class IoInterfaceBanner;
class IoMatrixCanvas;

/// Studio Audio I/O Setup Dialog (`Inputs` & `Outputs` matrix routing):
/// - Segmented `Inputs` / `Outputs` tabs
/// - Vloom Studio Rack Audio Interface illustration + live Device selector
/// - Interactive physical channel routing matrix (`M` for Mono, `L`/`R` for Stereo)
/// - `Used` active bus selector dot, live per-bus VU meters, inline bus renaming
/// - `Add (Mono)`, `Add (Stereo)`, `Add…` (with Reset to Default), and `Remove` actions
class AudioIoDialog : public QDialog {
    Q_OBJECT

public:
    enum class Tab { Inputs = 0, Outputs = 1 };

    explicit AudioIoDialog(QWidget* parent = nullptr);

    void setConfig(const AudioIoConfig& config);
    [[nodiscard]] const AudioIoConfig& config() const { return config_; }

    void setHardwareState(const QString& outDeviceName, int outChannels, const QStringList& outDeviceList,
        int outSelectedIdx, const QString& inDeviceName, int inChannels, const QStringList& inDeviceList,
        int inSelectedCode);

    void setHardwarePeaks(const std::array<float, AudioIoConfig::kMaxHardwareChannels>& inPeaks,
        float outPeakL, float outPeakR, double dt);

    void showTab(Tab tab);

signals:
    void configChanged(const vloomdaw::AudioIoConfig& config);
    void outputDeviceSelected(int index);
    void inputDeviceSelected(int code);
    void audioSettingsRequested();

private:
    void syncUiFromTab();
    void commitConfigEdit(AudioIoConfig next);
    void addBus(bool stereo);
    void removeSelectedBus();

    AudioIoConfig config_ = defaultAudioIoConfig();
    Tab currentTab_ = Tab::Inputs;

    QString outDeviceName_;
    int outChannels_ = 2;
    QStringList outDeviceList_;
    int outSelectedIdx_ = -1;

    QString inDeviceName_;
    int inChannels_ = 2;
    QStringList inDeviceList_;
    int inSelectedCode_ = -2;

    QPushButton* inputsTabBtn_ = nullptr;
    QPushButton* outputsTabBtn_ = nullptr;
    QLabel* driverBadge_ = nullptr;
    IoInterfaceBanner* banner_ = nullptr;
    QComboBox* deviceCombo_ = nullptr;
    QScrollArea* matrixScroll_ = nullptr;
    IoMatrixCanvas* canvas_ = nullptr;
    QPushButton* addMonoBtn_ = nullptr;
    QPushButton* addStereoBtn_ = nullptr;
    QPushButton* addMenuBtn_ = nullptr;
    QPushButton* removeBtn_ = nullptr;
    std::array<double, AudioIoConfig::kMaxHardwareChannels> smoothIn_{};
    double smoothOutL_ = 0.0;
    double smoothOutR_ = 0.0;
};

} // namespace vloomdaw
