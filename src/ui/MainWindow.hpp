// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "audio/ChannelRackAudio.hpp"
#include "audio/PluginHost.hpp"
#include "core/DeviceRef.hpp"
#include "core/DeviceWatch.hpp"
#include "core/FrameStats.hpp"
#include "core/MixerModel.hpp"
#include "core/Project.hpp"
#include "core/ProjectFile.hpp"
#include "core/TapTempo.hpp"
#include "ui/AppSettings.hpp"
#include "ui/dialogs/SettingsDialog/SettingsDialog.hpp"
#include "ui/plugins/PluginStore.hpp"
#include "ui/widgets/ViewButtons/ViewButtons.hpp"

#include <vloom/engine/io/AudioDevice.hpp>

#include <QElapsedTimer>
#include <QMainWindow>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QStringList>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QAction;
class QCloseEvent;
class QEvent;
class QComboBox;
class QLabel;
class QSplitter;
class QStackedWidget;
class QMenu;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QThread;
class QTimer;
class QToolButton;

namespace vloomdaw {

class DeviceScanner;

class AudioController;
class ActivityBar;
class BrowserPanel;
class SamplePreviewer;
class ChannelRackView;
class MixerPanel;
class DetachButton;
class FloatingPanel;
class PanelFrame;
class PerformanceOverlay;
class PianoRollView;
class PlaylistView;
struct ClipAudio;
class PluginManagerDialog;
class PluginEditorWindow;
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

    /// Opens the project at `path`, without asking to save the current one
    /// (File > Open and Open Recent ask first; so does the command line's
    /// caller, which has nothing open yet).
    void openProjectFile(const QString& path);
    /// The start window (File > Start Window, and at startup): a new
    /// project, or a recent one.
    void showLauncher();

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

    // ---- patterns (the Patterns menu and the pattern selector)
    /// The selector shows the project's pattern names and the current one.
    void syncPatternSelector();
    /// After patterns were added, removed or reordered: editors, selector and
    /// engine follow, with `select` current.
    void patternsChanged(std::size_t select);
    /// False (and a status message) when `more` patterns would pass the limit.
    bool roomForPatterns(std::size_t more);
    void findEmptyPattern(bool fromStart, bool askName);
    void renameCurrentPattern();
    void transposeCurrentPattern();
    void insertPattern();
    void cloneCurrentPattern();
    void deleteCurrentPattern();
    void moveCurrentPattern(int by);
    void splitCurrentPatternByChannel();
    /// Adds (+1) or removes (-1) a bar of step pads in the current pattern.
    void changeBars(int bars);
    void addMixerInsert();
    void removeMixerInsert(std::size_t track);
    void renameMixerInsert(std::size_t track);
    void addPlaylistTrack();
    void removePlaylistTrack(std::size_t track);
    void renamePlaylistTrack(std::size_t track);
    void dropAudio(const QString& path, int track, int start);
    /// Loads (decoding if needed) the sample at `path` on a background thread,
    /// then calls `then` on this thread; the UI stays responsive meanwhile.
    /// Reports a file that cannot be loaded instead of calling `then`.
    void loadSampleThen(const QString& path, std::function<void()> then);
    /// Starts the loader thread (loaderThread_, loader_) if it is not running.
    void startLoader();
    /// Audio clip `index`'s menu (fit to tempo) at `globalPos`.
    void showAudioClipMenu(std::size_t index, const QPoint& globalPos);
    /// Fits audio clip `index` to the song's tempo with `fit` (or stops),
    /// detecting its sample's tempo first when it has none.
    void setAudioClipFit(std::size_t index, AudioClip::Fit fit);
    /// Gives audio clip `index` the tempo `bpm` (0: none), keeping its
    /// offset and length on the same part of the sample.
    void setAudioClipTempo(std::size_t index, double bpm);
    /// Measures the tempo of the sample at `path` on the loader thread (once
    /// per file), then calls `then` with it (0: no steady beat) on this thread.
    void detectTempoThen(const std::string& path, std::function<void(double bpm)> then);
    /// For ChannelRackAudio: the stretched copy of `path` at `ratio`, or null
    /// while it is being made (once the tempo has settled).
    [[nodiscard]] std::shared_ptr<const vloom::SampleBuffer> stretchedSample(
        const std::string& path, double ratio);
    /// Makes the stretched copies asked for meanwhile, on the loader thread.
    void makeStretchedSamples();
    [[nodiscard]] const ClipAudio* clipAudio(const std::string& path);
    /// The channel rack's insert menu follows the mixer.
    void syncInsertNames();
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
    /// Ticks where the song ends: the end of its last clip, rounded up to a
    /// bar; 0 for an empty playlist. Audio clips count at the current tempo.
    [[nodiscard]] int songEndTicks();
    /// Song mode loops the song: at its end playback jumps back to the start,
    /// as in FL Studio. Sent from refreshFromEngine() when it changes, so a
    /// tempo drag does not flood the engine's queue.
    void applySongLoop();
    /// Fills the playlist's clip source box (patterns, then the channels with
    /// a sample) and shows the playlist's source in it.
    void rebuildClipSources();
    /// Gives `channel` the WAV file at `path` (channel == count: a new channel).
    void assignSample(std::size_t channel, const QString& path);
    /// Opens the Plugin Manager, where the user scans for plugins.
    void showPluginManager();

    // ---- hosted plugins
    /// Loads plugin `plugin` (an index into the plugin store) as the
    /// instrument of `channel` (channel == count: a new channel), and opens
    /// its editor.
    void addPluginToRack(std::size_t plugin, std::size_t channel);
    /// Loads plugin `plugin` as the last effect of mixer insert `track`, and
    /// opens its editor.
    void addPluginToInsert(std::size_t plugin, std::size_t track);
    /// Asks which insert plugin `plugin` goes on.
    void chooseInsertForPlugin(std::size_t plugin);
    /// Loads plugin `plugin`, which must be of kind `wanted`; says why not in
    /// the status bar otherwise.
    std::optional<PluginSlot> loadPlugin(std::size_t plugin, vloom::PluginInfo::Kind wanted);
    /// The insert's effects: open, bypass, remove.
    void showEffectsMenu(std::size_t track, const QPoint& globalPos);
    /// Opens (or raises) the editor of loaded plugin `handle`.
    void openPluginEditor(std::uint64_t handle, const QString& title);
    /// Drops the plugins the project no longer uses (the engine was sent
    /// states without them), closing their editors.
    void releaseUnusedPlugins();
    /// The handles of every plugin the project uses.
    [[nodiscard]] std::vector<std::uint64_t> pluginsInUse() const;
    void syncEffectCounts();
    /// Runs the plugins' main-thread work while any are loaded.
    void updatePluginIdle();
    /// Starts over (asking to save unsaved changes first).
    void newProject();
    /// Starts over without asking: an empty project, mixer and song settings.
    void resetProject();

    // ---- project files (the File menu)
    void openProject();
    /// Saves to the project's file, or asks for one; false if not saved.
    bool saveProject();
    bool saveProjectAs();
    bool writeProject(const QString& path);
    /// Adds audio files as new channels.
    void importAudioFiles();
    /// Renders the song (or the current pattern, when the playlist is
    /// empty) to a WAV file.
    void exportSong();
    void rebuildRecentMenu();
    /// Asks to save unsaved changes; false if the user cancelled.
    bool maybeSaveChanges();
    /// The project as a file holds it; plugin states only when asked (they
    /// are not cheap, and not compared to tell whether the project changed).
    [[nodiscard]] ProjectData projectData(bool withPluginStates) const;
    [[nodiscard]] std::string projectSnapshot() const;
    /// Whether the project differs from what was saved. Cheap enough for the
    /// title's "*": plugin settings count through the editors' edit reports.
    [[nodiscard]] bool isProjectModified() const;
    /// A fingerprint of every plugin's settings (their saved states): what
    /// maybeSaveChanges() compares, so an edit no editor reported still asks.
    [[nodiscard]] std::uint64_t pluginFingerprint() const;
    void markProjectSaved();
    void updateWindowTitle();
    /// Replaces the project, mixer and song settings with `incoming`, loading the
    /// plugins it names (only ones found by the plugin scan) with their
    /// saved settings. What could not be restored is added to `problems`.
    void applyProject(ProjectData incoming, QStringList& problems);
    /// Matches the mixer panel's strips to mixer_ (count, names, values).
    void syncMixerPanel();
    /// Whether `slot` is a plugin the user's scan found (a project file may
    /// name any library; only scanned ones are loaded).
    [[nodiscard]] bool isScannedPlugin(const PluginSlot& slot) const;
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
    PluginHost pluginHost_; ///< the plugins the project uses, loaded
    ChannelRackAudio rackAudio_; ///< the project's channel rack, as the engine plays it
    std::map<std::uint64_t, QPointer<PluginEditorWindow>> pluginEditors_; ///< by plugin handle
    QTimer* pluginIdleTimer_ = nullptr; ///< runs the plugins' main-thread work (PluginHost::idle())
    /// Instruments loaded while no device ran: activated at the requested
    /// rate and not sent to the engine yet (the rack is published only with
    /// a device), so Engine::prepare() does not see them. startDevice()
    /// activates them at the device's real rate.
    std::vector<std::uint64_t> unpublishedInstruments_;
    /// Loads dropped samples (decoding, resampling) off the UI thread, one
    /// at a time; loader_ lives on it.
    QThread* loaderThread_ = nullptr;
    QObject* loader_ = nullptr;
    int loading_ = 0; ///< loads under way, for the status bar
    QComboBox* clipSource_ = nullptr; ///< what the playlist's Draw and Paint place
    /// Fit to tempo: tempi measured per sample path, stretched copies by
    /// (path, ratio x 10000), and the copies asked for, being made, and used
    /// by the last sync.
    std::map<std::string, double> detectedTempo_;
    using StretchKey = std::pair<std::string, long long>;
    std::map<StretchKey, std::shared_ptr<const vloom::SampleBuffer>> stretched_;
    std::map<StretchKey, bool> stretchWanted_; ///< value: being made
    std::map<StretchKey, bool> stretchUsed_;
    QTimer* stretchTimer_ = nullptr; ///< waits for the tempo to settle
    double fitTempo_ = 0.0; ///< the song tempo the fitted clips were last sent with
    double songLoopEnd_ = -1.0; ///< the loop end last sent to the engine (0: off; < 0: never sent)
    bool songMode_ = false; ///< the engine plays the playlist instead of looping the pattern
    /// Length and waveform of the playlist's audio files, by path.
    std::map<std::string, std::shared_ptr<const ClipAudio>> clipAudio_;
    std::size_t currentPattern_ = 0;
    std::size_t currentChannel_ = 0;
    std::filesystem::path projectPath_; ///< the project's file; empty: never saved
    std::string savedSnapshot_; ///< projectSnapshot() as last saved or opened
    std::uint64_t savedPluginPrint_ = 0; ///< pluginFingerprint() then
    bool pluginsEdited_ = false; ///< an editor reported an edit since
    QMenu* recentMenu_ = nullptr;
    QTimer* modifiedTimer_ = nullptr; ///< updates the "*" in the window title

    TransportBar* transport_ = nullptr;
    SettingsDialog* settingsDialog_ = nullptr;
    QAction* settingsAction_ = nullptr;
    ActivityBar* activityBar_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QStackedWidget* sidePanels_ = nullptr;
    BrowserPanel* browser_ = nullptr;
    SamplePreviewer* previewer_ = nullptr; ///< plays the browser's files
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
