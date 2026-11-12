// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "audio/ChannelRackAudio.hpp"
#include "core/DeviceRef.hpp"
#include "core/DeviceWatch.hpp"
#include "core/FrameStats.hpp"
#include "core/MixerModel.hpp"
#include "core/Project.hpp"
#include "core/TapTempo.hpp"
#include "ui/AppSettings.hpp"
#include "ui/dialogs/SettingsDialog/SettingsDialog.hpp"
#include "ui/plugins/PluginStore.hpp"
#include "ui/widgets/ViewButtons/ViewButtons.hpp"

#include <vloom/engine/io/AudioDevice.hpp>

#include <QElapsedTimer>
#include <QMainWindow>
#include <QRect>
#include <QStringList>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
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
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QTimer;
class QToolButton;

namespace vloomdaw {

class DeviceScanner;

class AudioController;
class ActivityBar;
class BrowserPanel;
class ChannelRackView;
class MixerPanel;
class DetachButton;
class FloatingPanel;
class PanelFrame;
class PerformanceOverlay;
class PianoRollView;
class PlaylistView;
class PluginManagerDialog;
class PluginsPanel;
class TransportBar;
class WindowButtons;

/// The main application window. Connects UI widgets to the AudioController.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(AudioController& audio, QWidget* parent = nullptr);
    /// Deletes the detached windows (they have no parent).
    ~MainWindow() override;

protected:
    bool event(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    QMenu* createPopupMenu() override;
#if defined(Q_OS_WIN)
    void paintEvent(QPaintEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif

private:
    void extendIntoTitleBar();
    /// True where a press should move the window: empty tool bar and menu bar space.
    [[nodiscard]] bool isCaptionAt(QPoint pos) const;
    void placeWindowButtons();
    void restoreWindowState();
    void createMenus();
    void createToolBars();
    void createCentralArea();
    /// Workspace pages, in QStackedWidget order.
    enum class WorkspacePage { Empty, ChannelRack, PianoRoll, Playlist };

    [[nodiscard]] QWidget* createWorkspace();
    [[nodiscard]] QWidget* createEmptyState();
    void showWorkspace(WorkspacePage page);
    /// Shows `page`, or the empty state when it is already showing.
    void toggleWorkspace(WorkspacePage page);
    void selectPattern(std::size_t pattern);
    void selectChannel(std::size_t channel);
    void updatePanelContexts();
    /// Re-reads the project into every editor (after a structural change).
    void refreshEditors();
    void setPerformanceOverlayVisible(bool visible);
    void placePerformanceOverlay();
    void showSidePanel(int index);
    /// Opens a main window from the view buttons (only the browser exists yet).
    void showView(ViewButtons::View view);
    [[nodiscard]] QWidget* createMixer();
    void setMixerVisible(bool visible);
    [[nodiscard]] bool isMixerOpen() const;
    [[nodiscard]] bool isMixerDetached() const;
    /// Moves the mixer into its own window (true) or back under the workspace.
    void setMixerDetached(bool detached);
    /// A workspace editor (piano roll, playlist) that can live in its own window.
    struct DetachablePage {
        WorkspacePage page = WorkspacePage::Empty;
        PanelFrame* frame = nullptr;
        DetachButton* button = nullptr;
        QWidget* placeholder = nullptr; ///< in the workspace while detached
        FloatingPanel* window = nullptr; ///< created on first detach
        QString title; ///< window title
        QString settingsId; ///< "playlist": keys mainWindow/playlistDetached, ...
    };
    [[nodiscard]] DetachablePage* detachable(WorkspacePage page);
    [[nodiscard]] const DetachablePage* detachable(WorkspacePage page) const;
    void makeDetachable(DetachablePage& d, WorkspacePage page, PanelFrame* frame, const QString& title,
        const QString& name, const QString& settingsId);
    [[nodiscard]] bool isPageDetached(WorkspacePage page) const;
    /// Moves an editor into its own window (shown when `showWindow`) or back
    /// into the workspace.
    void setPageDetached(WorkspacePage page, bool detached, bool showWindow = true);
    /// Lights the view buttons of the editors that are open (docked or not).
    void updateViewLights();
    void setFloatingVisible(FloatingPanel* window, bool visible);
    [[nodiscard]] FloatingPanel* makeFloatingPanel(
        const QString& title, const QString& settingsKey, std::function<void()> onClosed);
    /// Applies the mixer model to the strips (solo greying) and the engine.
    void updateMixer();
    /// Queues master volume knob x master track (gain, mute, pan) for the engine.
    void updateMasterOutput();
    void createStatusBar();

    [[nodiscard]] DeviceRef selectedDevice() const;
    /// Re-lists the output devices; returns the selected one's index (-1 = default).
    int listDevices();
    void openSelectedDevice();
    /// Opens output device `index` of deviceNames_ (-1 = system default);
    /// `fallback`: standing in for a lost device. Returns whether it runs.
    bool startDevice(int index, bool fallback = false);
    /// Follows DeviceWatch: falls back to the default when the device is
    /// lost, back to the user's device when it returns.
    void watchDevice();
    /// Opens output device `index` of deviceNames_ and remembers it.
    void selectOutputDevice(int index);
    void showSettings(SettingsDialog::Page page);
    /// Sends the channel rack of the current pattern to the engine and
    /// reports samples that could not be loaded.
    void syncChannelRack();
    /// Gives `channel` the WAV file at `path` (channel == count: a new channel).
    void assignSample(std::size_t channel, const QString& path);
    /// Opens the Plugin Manager, where the user scans for plugins.
    void showPluginManager();
    /// Starts over: stops, returns to the start and resets the tempo.
    void newProject();
    void togglePlay();
    void stopAndRewind();
    void tapTempo();
    void createSettingsDialog();
    void applyStartupSettings();
    /// Shows settings_.theme at once in everything painted in code (cheap).
    void previewTheme();
    /// Applies settings_.theme to the whole app and saves it.
    void applyTheme();
    void changeAudioFormat(int sampleRate, int bufferFrames);
    void toggleFullScreen();
    void refreshFromEngine();
    /// Matches the refresh timer to the screen's refresh rate.
    void updateRefreshRate();
    void showAbout();

    AudioController& audio_;
    MixerModel mixer_;
    Project project_;
    ChannelRackAudio rackAudio_; ///< the project's channel rack, as the engine plays it
    std::size_t currentPattern_ = 0;
    std::size_t currentChannel_ = 0;

    TransportBar* transport_ = nullptr;
    SettingsDialog* settingsDialog_ = nullptr;
    QAction* settingsAction_ = nullptr;
    ActivityBar* activityBar_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QStackedWidget* sidePanels_ = nullptr;
    BrowserPanel* browser_ = nullptr;
    PluginsPanel* pluginsPanel_ = nullptr;
    PluginManagerDialog* pluginManager_ = nullptr; ///< created on first use
    PluginStore pluginStore_;
    AppSettings settings_;
    DeviceWatch deviceWatch_;
    DeviceScanner* deviceScanner_ = nullptr; ///< lists devices off the UI thread for deviceWatch_
    std::unique_ptr<vloom::DeviceListWatcher> deviceListWatcher_; ///< the OS says when devices change
    std::uint64_t scannedGeneration_ = 0; ///< deviceListWatcher_ generation at the last scan
    bool preferredPresent_ = false; ///< the user's device was in the last scan
    QElapsedTimer deviceClock_; ///< time base for deviceWatch_
    std::vector<std::string> deviceNames_; ///< as last listed, for dialog indices
    std::optional<double> pendingTempo_; ///< latest user tempo not yet sent
    std::optional<float> pendingGain_; ///< latest master gain not yet sent
    std::optional<float> pendingPan_; ///< latest master pan not yet sent
    QSplitter* workspaceSplitter_ = nullptr; ///< workspace above, mixer below
    MixerPanel* mixerPanel_ = nullptr;
    QStackedWidget* workspace_ = nullptr;
    ChannelRackView* channelRack_ = nullptr;
    PianoRollView* pianoRoll_ = nullptr;
    PlaylistView* playlist_ = nullptr;
    PanelFrame* channelRackFrame_ = nullptr;
    PanelFrame* pianoRollFrame_ = nullptr;
    PanelFrame* playlistFrame_ = nullptr;
    std::array<DetachablePage, 2> detachable_; ///< piano roll, playlist
    FloatingPanel* mixerWindow_ = nullptr;
    int floatingWindowsCreated_ = 0; ///< to cascade first-time windows
    PerformanceOverlay* performanceOverlay_ = nullptr; ///< debug figures (View menu)
    QAction* performanceAction_ = nullptr;
    FrameStats frameStats_;
    QElapsedTimer frameClock_;
    int refreshIntervalMs_ = 16; ///< see updateRefreshRate()
    double lastTickAt_ = -1.0; ///< frameClock_ seconds of the last refresh
    double nextCpuAt_ = 0.0; ///< when the slow readouts update next
    double nextMemoryAt_ = 0.0;
    double nextOverlayAt_ = 0.0;
    TapTempo tapTempo_;
    QElapsedTimer tapClock_;
    QToolButton* deviceStatus_ = nullptr; ///< device readout, click to switch
    QTimer* refreshTimer_ = nullptr;
    QTimer* themeApplyTimer_ = nullptr; ///< coalesces theme edits (knob drags)
    QAction* fullScreenAction_ = nullptr;
    WindowButtons* windowButtons_ = nullptr; ///< Windows only: replaces the title bar's

    // macOS full screen that keeps the window buttons (see toggleFullScreen()).
    bool coversScreen_ = false;
    QRect geometryBeforeFullScreen_;
};

} // namespace vloomdaw
