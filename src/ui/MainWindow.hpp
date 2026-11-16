// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "audio/ChannelRackAudio.hpp"
#include "audio/PluginHost.hpp"
#include "core/DeviceRef.hpp"
#include "core/DeviceWatch.hpp"
#include "core/FrameStats.hpp"
#include "core/MixerModel.hpp"
#include "core/NoteHistory.hpp"
#include "core/Project.hpp"
#include "core/ProjectFile.hpp"
#include "core/TapTempo.hpp"
#include "core/UndoHistory.hpp"
#include "ui/AppSettings.hpp"
#include "ui/dialogs/SettingsDialog/SettingsDialog.hpp"
#include "ui/plugins/PluginStore.hpp"
#include "ui/plugins/PluginThumbnails.hpp"
#include "ui/widgets/ViewButtons/ViewButtons.hpp"

#include <vloom/engine/io/AudioDevice.hpp>

#include <QElapsedTimer>
#include <QList>
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
class QProgressDialog;
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
struct AddTracksSpec;
struct ClipAudio;
class PluginManagerDialog;
class ProjectLoadDialog;
class StemSeparator;
struct StemFile;
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
    /// Initializes an empty project at `tempo` BPM (used when launching from
    /// the Start Window before `MainWindow` is shown).
    void startEmptyProject(double tempo);
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
    void addMixerRoleChannel(MixerRole role);
    void addBusForMixerTrack(std::size_t track);
    void addFxSendForMixerTrack(std::size_t track);
    void openMixerInstrumentEditor(std::size_t track);
    void showMixerInstrumentMenu(std::size_t track, const QPoint& globalPos);
    void showMixerSourceMenu(std::size_t track, const QPoint& globalPos);
    void showMixerOutputMenu(std::size_t track, const QPoint& globalPos);
    void showMixerAddSendMenu(std::size_t track, const QPoint& globalPos);
    void showMixerSendMenu(std::size_t track, std::size_t sendSlot, const QPoint& globalPos);
    void copyMixerChannelSettings(std::size_t track);
    void pasteMixerChannelSettings(std::size_t track);
    void removeMixerInsert(std::size_t track);
    void renameMixerInsert(std::size_t track);
    void addPlaylistTrack();
    void showAddTracksDialog(int afterTrack = -1);
    void applyAddTracksSpec(const AddTracksSpec& spec, int afterTrack);
    void addQuickAudioTrack(int afterTrack, bool mono);
    void addQuickInstrumentTrack(int afterTrack);
    void removePlaylistTrack(std::size_t track);
    void renamePlaylistTrack(std::size_t track);
    void showTrackChannelMenu(std::size_t track, const QPoint& globalPos);
    void showTrackRoutingMenu(std::size_t track, const QPoint& globalPos);
    void dropAudioFiles(const QStringList& paths, int track, int start);
    void dropMidiFiles(const QStringList& paths, int track, int startTick);
    void handlePluginDroppedOnPlaylist(std::size_t pluginIndex, int track, int startTick);
    void movePlaylistTrackTo(std::size_t from, std::size_t to);
    void moveMixerInsertTo(std::size_t from, std::size_t to);
    /// Loads (decoding if needed) the sample at `path` on a background thread,
    /// then calls `then` on this thread; the UI stays responsive meanwhile.
    /// Calls `onFailed` (if set) and reports in the status bar when loading fails.
    void loadSampleThen(const QString& path, std::function<void()> then, std::function<void()> onFailed = {});
    /// Starts the loader thread (loaderThread_, loader_) if it is not running.
    void startLoader();
    /// Audio clip `index`'s menu (fit to tempo) at `globalPos`.
    void showAudioClipMenu(std::size_t index, const QPoint& globalPos);
    /// Splits audio clip `index`'s sample into stems with the model in use
    /// (asking for one in the model store when none is), then places them.
    void extractStems(std::size_t index);
    /// Puts the stems on new tracks under `clip`'s, as clips like it, each
    /// through a channel of its own, and mutes `clip`. Loads them first
    /// (`next`: the one to load now).
    void placeStems(const AudioClip& clip, const QList<StemFile>& stems, int next = 0);
    /// Opens the model store. True when a model is in use afterwards.
    bool showStemModels();
    /// Settings → Stem Extractor shows what is downloaded and chosen now.
    void refreshStemSettings();
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
    /// Coalesces rapid clip/channel edits (mouse drags) into ~40 Hz engine updates.
    void scheduleChannelRackSync();
    void flushChannelRackSync();
    /// Whether the Edit menu's commands go to the piano roll: it is the editor in front.
    [[nodiscard]] bool pianoRollActive() const;
    /// Takes the newest piano roll edit back, or (`redo`) makes it again, and shows where.
    void stepNoteHistory(bool redo);
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
    /// Fills the piano roll's channel switcher box and highlights `currentChannel_`.
    void rebuildPianoRollChannels();
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
    /// Replaces the effect at `slot` on mixer insert `track` with `plugin`.
    void replacePluginInInsert(std::size_t plugin, std::size_t track, std::size_t slot);
    /// Moves an effect from slot `from` to slot `to` on mixer insert `track`.
    void moveEffectInInsert(std::size_t track, std::size_t from, std::size_t to);
    /// Asks which insert plugin `plugin` goes on.
    void chooseInsertForPlugin(std::size_t plugin);
    /// Loads plugin `plugin`, which must be of kind `wanted`; says why not in
    /// the status bar otherwise.
    std::optional<PluginSlot> loadPlugin(std::size_t plugin, vloom::PluginInfo::Kind wanted);
    /// The insert's effects: open, bypass, remove.
    void showEffectsMenu(std::size_t track, const QPoint& globalPos);
    /// Context menu for a specific FX slot on an insert.
    void showEffectSlotContextMenu(std::size_t track, std::size_t slot, const QPoint& globalPos);
    /// Menu of effect plugins to load into slot `slot` of insert `track`.
    void showEffectChooserMenu(std::size_t track, std::size_t slot, const QPoint& globalPos);
    /// Menu of instrument plugins to load into `channel` on the channel rack.
    void showInstrumentChooserMenu(std::size_t channel, const QPoint& globalPos);
    /// Handles a plugin dropped onto slot `slot` of insert `track`.
    void handlePluginDroppedOnSlot(std::size_t track, std::size_t slot, std::size_t plugin);
    /// Opens (or raises) the editor of loaded plugin `handle`.
    void openPluginEditor(std::uint64_t handle, const QString& title);
    /// Builds the Studio One-style header context for an open plugin editor.
    [[nodiscard]] PluginEditorContext buildPluginEditorContext(std::uint64_t handle);
    /// Refreshes header bar state (slot name, bypass/power, favorite) on open plugin editors.
    void refreshOpenPluginEditors();
    /// Returns mixer_ (or a copy with all effects bypassed when global FX bypass is active).
    [[nodiscard]] MixerModel effectiveMixerModel() const;
    /// Returns the master output gain including transport Vol, Master fader/mute, Mute & Dim.
    [[nodiscard]] float effectiveMasterGain() const;
    /// Bypasses effect `slot` of insert `track` or turns it back on, and shows
    /// it in the mixer and in the effect's open editor.
    void setEffectBypassed(std::size_t track, std::size_t slot, bool bypassed);
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
    /// Closes the current project (unloading plugins and audio after asking to
    /// save unsaved changes) and returns to the standalone Start Hub.
    void closeProject();
    /// Persists main window geometry, splitters, sidebar and detached window flags.
    void saveWindowStateToSettings();
    /// Starts over without asking: an empty project, mixer and song settings.
    void resetProject();

    // ---- undo / redo & Edit menu commands
    [[nodiscard]] UndoSnapshot captureUndoSnapshot() const;
    void recordUndo(const QString& label, std::string coalesceKey = {});
    void applyUndoSnapshot(UndoSnapshot snapshot);
    void performUndo();
    void performRedo();
    void performCut();
    void performCopy();
    void performPaste();
    void performDuplicate();
    void performDelete();
    void performSelectAll();
    void performQuantize();
    [[nodiscard]] bool isPianoRollActive() const;
    void updateUndoActions();

    // ---- project files (the File menu)
    void openProject();
    /// Saves to the project's file, or asks for one; false if not saved.
    bool saveProject();
    bool saveProjectAs();
    bool writeProject(const QString& path);
    /// Adds audio files as new channels.
    void importAudioFiles();
    /// Imports Standard MIDI Files (`.mid` / `.midi`), creating one track,
    /// empty MIDI channel (no auto-loaded instrument), pattern, clip, and mixer
    /// insert per MIDI track.
    void importMidiFiles();
    std::size_t importMidiFile(
        const QString& path, int startTrack = -1, int startTick = 0, bool recordUndoStep = true);
    /// Renders the song (or the current pattern, or the Loop Bar range) to an audio file.
    void exportSong(bool forceLoopRange = false);
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
    /// saved settings and reporting progress to `progress` (if non-null).
    /// Returns `false` if cancelled via `progress`, leaving the current project intact.
    bool applyProject(ProjectData incoming, QStringList& problems, ProjectLoadDialog* progress = nullptr);
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
    Project project_{0};
    PluginHost pluginHost_; ///< the plugins the project uses, loaded
    ChannelRackAudio rackAudio_; ///< the project's channel rack, as the engine plays it
    /// By plugin handle: a PluginEditorWindow, or for a plugin without an
    /// editor of its own an EqEditorWindow (the built-in equalizer), a
    /// SynthEditorWindow (the built-in synthesizer) or a ParameterEditorWindow.
    std::map<std::uint64_t, QPointer<QWidget>> pluginEditors_;
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
    using LoadCallbacks = std::pair<std::function<void()>, std::function<void()>>;
    std::map<std::string, std::vector<LoadCallbacks>> pendingLoads_;
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
    double songLoopStart_ = -1.0; ///< the loop start last sent to the engine (< 0: never sent)
    double songLoopEnd_ = -1.0; ///< the loop end last sent to the engine (0: off; < 0: never sent)
    int loopStartTicks_ = 0; ///< custom Loop Bar start in ticks (`loopEndTicks_ > loopStartTicks_`: set)
    int loopEndTicks_ = 0; ///< custom Loop Bar end in ticks (0: none)
    bool songMode_ = false; ///< the engine plays the playlist instead of looping the pattern
    bool allEffectsBypassed_ = false; ///< global console FX bypass (preserves per-slot bypass)
    std::uint64_t projectGeneration_
        = 0; ///< guards async sample-load callbacks across project switches/undos
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
    QAction* closeProjectAction_ = nullptr;
    QAction* settingsAction_ = nullptr;
    ActivityBar* activityBar_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QStackedWidget* sidePanels_ = nullptr;
    BrowserPanel* browser_ = nullptr;
    SamplePreviewer* previewer_ = nullptr; ///< plays the browser's files
    PluginsPanel* pluginsPanel_ = nullptr;
    PluginManagerDialog* pluginManager_ = nullptr; ///< created on first use
    StemSeparator* stemSeparator_ = nullptr; ///< created on first use
    QProgressDialog* stemProgress_ = nullptr; ///< while stems are being extracted
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
    QComboBox* pianoRollChannel_ = nullptr; ///< active channel switcher in the piano roll header
    QString pianoRollToolHint_; ///< active MIDI tool hint for the piano roll header
    QString pianoRollSelectionText_; ///< live selected-note count & pitch range summary
    NoteHistory noteHistory_; ///< undo and redo of the piano roll's edits
    /// The notes before the piano roll edit under way (a drag sends many changes: one step to undo).
    std::optional<std::vector<Note>> noteEditBefore_;
    NoteHistory::Target noteEditTarget_; ///< and whose notes they are
    std::shared_ptr<vloom::PluginInstance> previewInstrument_; ///< sounding a key of the piano roll
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
    QTimer* rackSyncTimer_ = nullptr; ///< coalesces rapid playlist clip/slider drags
    QTimer* themeApplyTimer_ = nullptr; ///< coalesces theme edits (knob drags)
    std::vector<std::pair<float, float>>
        insertPeaks_; ///< reused per refresh tick for mixer & playlist meters
    QAction* fullScreenAction_ = nullptr;
    WindowButtons* windowButtons_ = nullptr; ///< Windows only: replaces the title bar's

    // macOS full screen that keeps the window buttons (see toggleFullScreen()).
    bool coversScreen_ = false;
    QRect geometryBeforeFullScreen_;

    // Playback scrubbing state
    bool scrubbingPlayback_ = false;
    bool wasPlayingBeforeScrub_ = false;
    bool scrubPlaySent_ = false; ///< the engine took this scrub's play
    bool scrubStopPending_ = false; ///< a scrub's stop the engine has not acted on yet
    bool scrubStopSent_ = false; ///< the engine took that stop

    // Undo / redo history and shared window actions
    struct CopiedChannelSettings {
        double fader = 0.8;
        double pan = 0.0;
        bool mono = false;
        std::vector<PluginSlot> effects{};
        std::vector<std::vector<std::uint8_t>> effectStates{};
        std::vector<MixerSend> sends{};
    };
    std::optional<CopiedChannelSettings> copiedChannelSettings_;
    UndoHistory undoHistory_;
    bool applyingUndoRedo_ = false;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* cutAction_ = nullptr;
    QAction* copyAction_ = nullptr;
    QAction* pasteAction_ = nullptr;
    QAction* duplicateAction_ = nullptr;
    QAction* deleteAction_ = nullptr;
    QAction* selectAllAction_ = nullptr;
    std::vector<QAction*> sharedWindowActions_;
};

} // namespace vloomdaw
