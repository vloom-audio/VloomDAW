// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/DeviceRef.hpp"
#include "core/TapTempo.hpp"
#include "ui/AppSettings.hpp"
#include "ui/dialogs/SettingsDialog/SettingsDialog.hpp"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QRect>
#include <QStringList>

#include <optional>
#include <string>
#include <vector>

class QAction;
class QCloseEvent;
class QEvent;
class QLabel;
class QSplitter;
class QStackedWidget;
class QMenu;
class QResizeEvent;
class QShowEvent;
class QTimer;

namespace vloomdaw {

class AudioController;
class ActivityBar;
class BrowserPanel;
class TransportBar;

/// The main application window. Connects UI widgets to the AudioController.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(AudioController& audio, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    QMenu* createPopupMenu() override;

private:
    void extendIntoTitleBar();
    void placeWindowButtons();
    void restoreWindowState();
    void createMenus();
    void createToolBars();
    void createCentralArea();
    [[nodiscard]] QWidget* createWorkspace();
    void showSidePanel(int index);
    void createStatusBar();

    [[nodiscard]] DeviceRef selectedDevice() const;
    /// Re-lists the output devices; returns the selected one's index (-1 = default).
    int listDevices();
    void openSelectedDevice();
    void startDevice(int index);
    void showSettings(SettingsDialog::Page page);
    void togglePlay();
    void stopAndRewind();
    void tapTempo();
    void createSettingsDialog();
    void applyStartupSettings();
    void changeAudioFormat(int sampleRate, int bufferFrames);
    void toggleFullScreen();
    void refreshFromEngine();
    void showAbout();

    AudioController& audio_;

    TransportBar* transport_ = nullptr;
    SettingsDialog* settingsDialog_ = nullptr;
    QAction* settingsAction_ = nullptr;
    ActivityBar* activityBar_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QStackedWidget* sidePanels_ = nullptr;
    BrowserPanel* browser_ = nullptr;
    AppSettings settings_;
    std::vector<std::string> deviceNames_; ///< as last listed, for dialog indices
    std::optional<double> pendingTempo_; ///< latest user tempo not yet sent
    TapTempo tapTempo_;
    QElapsedTimer tapClock_;
    QLabel* deviceStatus_ = nullptr;
    QTimer* refreshTimer_ = nullptr;
    QAction* fullScreenAction_ = nullptr;

    // macOS full screen that keeps the window buttons (see toggleFullScreen()).
    bool coversScreen_ = false;
    QRect geometryBeforeFullScreen_;
};

} // namespace vloomdaw
