// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/MainWindow.hpp"

#include "audio/AudioController.hpp"
#include "audio/MixerAudio.hpp"
#include "audio/TempoDetector.hpp"
#include "audio/TimeStretch.hpp"
#include "core/BrowserFilter.hpp"
#include "core/Decibels.hpp"
#include "core/DeviceRef.hpp"
#include "core/MidiFile.hpp"
#include "core/Names.hpp"
#include "core/NoteNames.hpp"
#include "core/PluginMetadata.hpp"
#include "core/PositionFormat.hpp"
#include "core/ProcessMemory.hpp"
#include "core/Scales.hpp"
#include "core/StemModels.hpp"
#include "core/TrackRouting.hpp"
#include "ui/AudioDecoder.hpp"
#include "ui/DeviceScanner.hpp"
#include "ui/FilePaths.hpp"
#include "ui/RecentProjects.hpp"
#include "ui/Theme.hpp"
#include "ui/browser/BrowserPanel.hpp"
#include "ui/browser/SamplePreviewer.hpp"
#include "ui/browser/SampleWaveform.hpp"
#include "ui/channelrack/ChannelRackView.hpp"
#include "ui/dialogs/AboutDialog/AboutDialog.hpp"
#include "ui/dialogs/AddTracksDialog/AddTracksDialog.hpp"
#include "ui/dialogs/ExportDialog.hpp"
#include "ui/dialogs/ProjectLoadDialog/ProjectLoadDialog.hpp"
#include "ui/dialogs/SettingsDialog/AudioSettingsPage.hpp"
#include "ui/dialogs/SettingsDialog/GeneralSettingsPage.hpp"
#include "ui/dialogs/SettingsDialog/SettingsDialog.hpp"
#include "ui/dialogs/SettingsDialog/StemSettingsPage.hpp"
#include "ui/dialogs/SettingsDialog/ThemeSettingsPage.hpp"
#include "ui/launcher/LauncherDialog.hpp"
#include "ui/mixer/MixerPanel.hpp"
#include "ui/pianoroll/PianoRollView.hpp"
#include "ui/playlist/PlaylistView.hpp"
#include "ui/plugins/EqEditorWindow.hpp"
#include "ui/plugins/ParameterEditorWindow.hpp"
#include "ui/plugins/PluginEditorWindow.hpp"
#include "ui/plugins/PluginManagerDialog.hpp"
#include "ui/plugins/PluginPickerPopup.hpp"
#include "ui/plugins/PluginsPanel.hpp"
#include "ui/plugins/SynthEditorWindow.hpp"
#include "ui/stems/StemModelStore.hpp"
#include "ui/stems/StemSeparator.hpp"
#include "ui/widgets/ActivityBar/ActivityBar.hpp"
#include "ui/widgets/DetachButton/DetachButton.hpp"
#include "ui/widgets/FloatingPanel/FloatingPanel.hpp"
#include "ui/widgets/KeyDisplay/KeyDisplay.hpp"
#include "ui/widgets/LevelMeter/LevelMeter.hpp"
#include "ui/widgets/PanelFrame/PanelFrame.hpp"
#include "ui/widgets/PatternSelector/PatternSelector.hpp"
#include "ui/widgets/PerformanceMeter/PerformanceMeter.hpp"
#include "ui/widgets/PerformanceOverlay/PerformanceOverlay.hpp"
#include "ui/widgets/ToolGroup/ToolGroup.hpp"
#include "ui/widgets/TransportBar/TransportBar.hpp"

#if defined(Q_OS_MACOS)
#include "ui/MacWindow.hpp"
#endif
#if defined(Q_OS_WIN)
#include "ui/WinWindow.hpp"
#include "ui/widgets/WindowButtons/WindowButtons.hpp"
#endif

#include <vloom/engine/core/Version.hpp>
#include <vloom/engine/render/SongExport.hpp>
#include <vloom/engine/sample/SampleBuffer.hpp>
#include <vloom/engine/sample/SampleLoader.hpp>

#include <QAbstractItemView>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QProgressDialog>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace vloomdaw {

namespace {

constexpr QLatin1StringView kProjectFolderKey{"project/lastFolder"};

/// Where file dialogs open: the last folder used, else the user's Music folder.
QString projectFolder()
{
    const QString last = QSettings().value(kProjectFolderKey).toString();
    if (isLocalPath(last) && QDir(last).exists())
        return last;
    return QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
}

void rememberFolder(const QString& file)
{
    if (!isLocalPath(file))
        return;
    const QString folder = QFileInfo(file).absolutePath();
    if (isLocalPath(folder))
        QSettings().setValue(kProjectFolderKey, folder);
}

/// A combo box that refills itself just before its list opens.
class ClipSourceBox : public QComboBox {
public:
    ClipSourceBox(std::function<void()> refill, QWidget* parent)
        : QComboBox(parent)
        , refill_(std::move(refill))
    {
    }

    void showPopup() override
    {
        if (refill_)
            refill_();
        QComboBox::showPopup();
    }

private:
    std::function<void()> refill_;
};

} // namespace

namespace {
constexpr qreal kMinRefreshHz = 30.0; ///< UI refresh follows the screen,
constexpr qreal kMaxRefreshHz = 240.0; ///< within these bounds (144 Hz screens included)

// QSettings keys for the window layout.
constexpr QLatin1StringView kGeometryKey{"mainWindow/geometry"};
constexpr QLatin1StringView kStateKey{"mainWindow/state"};
constexpr QLatin1StringView kSplitterKey{"mainWindow/sidebarSplitter"};
constexpr QLatin1StringView kMixerSplitterKey{"mainWindow/mixerSplitter"};
constexpr QLatin1StringView kMixerOpenKey{"mainWindow/mixerOpen"};
constexpr QLatin1StringView kMixerDetachedKey{"mainWindow/mixerDetached"};
constexpr QLatin1StringView kMixerWindowKey{"detached/mixerGeometry"};
constexpr QLatin1StringView kPerformanceOverlayKey{"debug/performanceOverlay"};
constexpr QLatin1StringView kSidebarPageKey{"mainWindow/sidebarPage"};
constexpr QLatin1StringView kPlaylistHeaderWidthKey{"playlist/headerWidth"};

constexpr int kDefaultSidebarWidth = 280;
constexpr int kThemeApplyMs = 200; ///< full restyle once theme edits pause this long
constexpr int kDefaultMixerHeight = 408;
constexpr std::size_t kMixerInserts = 0;
constexpr double kDefaultTempo = 120.0; ///< BPM of a new project

/// Volume knob percent to engine gain. Squared, so the knob feels even to
/// the ear: 100 % = unity, 50 % = -12 dB, 125 % = +3.9 dB.
float volumeToGain(double percent)
{
    const double x = percent / 100.0;
    return static_cast<float>(x * x);
}

#if defined(Q_OS_MACOS) && QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
// macOS window buttons: distance from the left edge, and the room kept free
// for them at the start of the tool bar.
constexpr int kWindowButtonsInset = 14;
constexpr int kWindowButtonsWidth = 80;
#endif
} // namespace

MainWindow::MainWindow(AudioController& audio, QWidget* parent)
    : QMainWindow(parent)
    , audio_(audio)
    , mixer_(kMixerInserts)
{
    setWindowTitle(QStringLiteral("VloomDAW"));
    // Audio clips fitted to the tempo by stretching: their copies, made once
    // the tempo has settled.
    stretchTimer_ = new QTimer(this);
    stretchTimer_->setSingleShot(true);
    stretchTimer_->setInterval(300);
    connect(stretchTimer_, &QTimer::timeout, this, &MainWindow::makeStretchedSamples);

    rackSyncTimer_ = new QTimer(this);
    rackSyncTimer_->setSingleShot(true);
    rackSyncTimer_->setInterval(25);
    connect(rackSyncTimer_, &QTimer::timeout, this, &MainWindow::syncChannelRack);
    rackAudio_.setStretchProvider(
        [this](const std::string& path, double ratio) { return stretchedSample(path, ratio); });
    // Hosted plugins: instruments of channels, effects of inserts.
    rackAudio_.setPluginResolver(pluginHost_.resolver());
    pluginIdleTimer_ = new QTimer(this);
    connect(pluginIdleTimer_, &QTimer::timeout, this, [this] {
        const std::vector<std::uint64_t> failed = pluginHost_.idle();
        for (const std::uint64_t handle : failed) {
            if (const auto ed = pluginEditors_.find(handle); ed != pluginEditors_.end()) {
                if (!ed->second.isNull())
                    delete ed->second.data();
                pluginEditors_.erase(ed);
            }
            if (const auto inst = pluginHost_.instance(handle); inst != nullptr) {
                const QString name = QString::fromStdString(inst->processor().info().name);
                statusBar()->showMessage(
                    tr("Plugin “%1” stopped responding (its bridge process ended); its last settings were "
                       "preserved.")
                        .arg(name),
                    10000);
            }
        }
        // Plugins change their latency while they run (oversampling, linear
        // phase): the mixer's delay compensation follows.
        audio_.engine().updatePluginLatency();
        // About 60 times a second while an editor is open (ADR 0009 of
        // vloom-engine), else 30: plugins' timers and edits.
        const bool editing = std::any_of(pluginEditors_.begin(), pluginEditors_.end(),
            [](const auto& entry) { return !entry.second.isNull(); });
        const int interval = editing ? 16 : 33;
        if (pluginIdleTimer_->interval() != interval)
            pluginIdleTimer_->setInterval(interval);
    });
    // MP3, FLAC, Ogg and AIFF reach the engine as WAV copies decoded once.
    rackAudio_.setSourceResolver([this](const std::string& path, double sampleRate, std::string* error) {
        const QString file = QString::fromStdString(path);
        if (needsDecoding(file)) {
            statusBar()->showMessage(tr("Decoding %1…").arg(QFileInfo(file).fileName()));
            statusBar()->repaint(); // shown while the decoding blocks
        }
        QString why;
        const QString wav = playableWav(file, sampleRate, &why);
        if (needsDecoding(file))
            statusBar()->clearMessage();
        if (wav.isEmpty() && error != nullptr)
            *error = why.toStdString();
        return wav.toStdString();
    });
    setMinimumSize(800, 480);
    resize(1280, 720);

    settings_ = AppSettings::load();

    createToolBars();
    createSettingsDialog();
    createMenus();
    createCentralArea();
    createStatusBar();
    syncMixerPanel();
    syncInsertNames();
    channelRack_->refresh();
    extendIntoTitleBar();
    restoreWindowState();
    applyStartupSettings();
    openSelectedDevice();
    updateMixer();

    frameClock_.start();
    performanceOverlay_ = new PerformanceOverlay(this);
    performanceOverlay_->hide();
    setPerformanceOverlayVisible(QSettings().value(kPerformanceOverlayKey, false).toBool());

    refreshTimer_ = new QTimer(this);
    // Precise: the default coarse timer rounds to the 15.6 ms Windows tick,
    // so a 33 ms timer fired every 31-47 ms and the playhead stuttered.
    refreshTimer_->setTimerType(Qt::PreciseTimer);
    connect(refreshTimer_, &QTimer::timeout, this, &MainWindow::refreshFromEngine);
    updateRefreshRate();
    if (windowHandle() != nullptr)
        connect(windowHandle(), &QWindow::screenChanged, this, &MainWindow::updateRefreshRate);
}

MainWindow::~MainWindow()
{
    // Plugin editors close before their plugins and this window's members go.
    for (auto& [handle, editor] : pluginEditors_)
        delete editor.data();
    // A load under way finishes first; its result, queued to this window,
    // is dropped with it.
    if (loaderThread_ != nullptr) {
        loaderThread_->quit();
        loaderThread_->wait();
    }
    // Detached windows are top-level and unparented; a docked panel is back
    // under this window, so only what is still hosted goes with them.
    for (DetachablePage& d : detachable_)
        delete d.window;
    delete mixerWindow_;
}

// ---------------------------------------------------------------- construction

void MainWindow::createMenus()
{
    auto* fileMenu = menuBar()->addMenu(tr("&File"));

    // Explicit keys, not QKeySequence::SaveAs etc.: several standard keys are
    // empty or media keys on Windows. Qt::CTRL is Cmd on macOS.
    const auto addFileAction = [&](const QString& text, const QKeySequence& shortcut, auto&& handler) {
        auto* action = fileMenu->addAction(text, this, handler);
        action->setShortcut(shortcut);
        sharedWindowActions_.push_back(action);
        return action;
    };
    addFileAction(tr("&New Project"), QKeySequence(Qt::CTRL | Qt::Key_N), [this] { newProject(); });
    fileMenu->addAction(tr("S&tart Hub…"), this, [this] { showLauncher(); });
    addFileAction(tr("&Open…"), QKeySequence(Qt::CTRL | Qt::Key_O), [this] { openProject(); });

    recentMenu_ = fileMenu->addMenu(tr("Open &Recent"));
    connect(recentMenu_, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
    rebuildRecentMenu();

    fileMenu->addSeparator();
    addFileAction(tr("&Save"), QKeySequence(Qt::CTRL | Qt::Key_S), [this] { saveProject(); });
    addFileAction(
        tr("Save &As…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), [this] { saveProjectAs(); });
    closeProjectAction_
        = addFileAction(tr("&Close Project"), QKeySequence::Close, [this] { closeProject(); });
    closeProjectAction_->setIcon(Theme::icon(QStringLiteral("home")));
    closeProjectAction_->setIconVisibleInMenu(false);
    closeProjectAction_->setToolTip(
        tr("Close Project (%1)").arg(closeProjectAction_->shortcut().toString(QKeySequence::NativeText)));

    fileMenu->addSeparator();
    addFileAction(tr("&Import Audio…"), QKeySequence(Qt::CTRL | Qt::Key_I), [this] { importAudioFiles(); });
    addFileAction(
        tr("Import &MIDI…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M), [this] { importMidiFiles(); });
    addFileAction(tr("&Export…"), QKeySequence(Qt::CTRL | Qt::Key_E), [this] { exportSong(); });

    // The title's "*" follows unsaved changes; the snapshot is taken once
    // the window is built.
    modifiedTimer_ = new QTimer(this);
    modifiedTimer_->setInterval(1000);
    connect(modifiedTimer_, &QTimer::timeout, this, &MainWindow::updateWindowTitle);
    modifiedTimer_->start();
    QTimer::singleShot(0, this, [this] { markProjectSaved(); });

    fileMenu->addSeparator();
    // close(), not QApplication::quit(): closeEvent() saves the window state
    // and restores the menu bar / Dock; closing the last window then quits.
#if defined(Q_OS_WIN)
    // QKeySequence::Quit is the "Exit" media key on Windows; Windows apps
    // name this Exit and close with Alt+F4.
    auto* quit = fileMenu->addAction(tr("E&xit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence(Qt::ALT | Qt::Key_F4));
#else
    auto* quit = fileMenu->addAction(tr("&Quit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);
#endif

    // Edit menu: real Undo/Redo and active editor selection/clipboard commands.
    // Focused QLineEdit controls delegate text editing keys to the text field first.
    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    const auto addEditAction
        = [&](const QString& text, const QList<QKeySequence>& shortcuts, auto&& handler) {
              auto* action = editMenu->addAction(text, this, handler);
              action->setShortcuts(shortcuts);
              sharedWindowActions_.push_back(action);
              return action;
          };
    undoAction_ = addEditAction(tr("&Undo"), {QKeySequence(Qt::CTRL | Qt::Key_Z)}, [this] { performUndo(); });
#if defined(Q_OS_WIN)
    // Windows apps also redo with Ctrl+Y.
    const QList<QKeySequence> redoKeys{
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), QKeySequence(Qt::CTRL | Qt::Key_Y)};
#else
    const QList<QKeySequence> redoKeys{QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)};
#endif
    redoAction_ = addEditAction(tr("&Redo"), redoKeys, [this] { performRedo(); });
    editMenu->addSeparator();
    cutAction_ = addEditAction(tr("Cu&t"), {QKeySequence(Qt::CTRL | Qt::Key_X)}, [this] { performCut(); });
    copyAction_ = addEditAction(tr("&Copy"), {QKeySequence(Qt::CTRL | Qt::Key_C)}, [this] { performCopy(); });
    pasteAction_
        = addEditAction(tr("&Paste"), {QKeySequence(Qt::CTRL | Qt::Key_V)}, [this] { performPaste(); });
    duplicateAction_ = addEditAction(
        tr("&Duplicate"), {QKeySequence(Qt::CTRL | Qt::Key_D)}, [this] { performDuplicate(); });
    deleteAction_ = addEditAction(tr("De&lete"),
        {QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Backspace)}, [this] { performDelete(); });
    editMenu->addSeparator();
    selectAllAction_ = addEditAction(
        tr("Select &All"), {QKeySequence(Qt::CTRL | Qt::Key_A)}, [this] { performSelectAll(); });
    addEditAction(tr("&Quantize"), {QKeySequence(Qt::CTRL | Qt::Key_Q)}, [this] { performQuantize(); });
    editMenu->addSeparator();
    addEditAction(
        tr("Add &Tracks…"), {QKeySequence(Qt::CTRL | Qt::Key_T)}, [this] { showAddTracksDialog(-1); });
    connect(editMenu, &QMenu::aboutToShow, this, &MainWindow::updateUndoActions);
    updateUndoActions();

    const auto notYetIn = [this](const QString& feature, const QString& when) {
        return [this, feature, when] {
            statusBar()->showMessage(tr("%1 is not available yet: it arrives %2.").arg(feature, when), 5000);
        };
    };

    // Patterns, as in FL Studio.
    auto* patternsMenu = menuBar()->addMenu(tr("&Patterns"));
    const auto addPatternAction = [&](const QString& text, const QKeySequence& shortcut, auto&& handler) {
        auto* action = patternsMenu->addAction(text, this, handler);
        if (!shortcut.isEmpty()) {
            action->setShortcut(shortcut);
            sharedWindowActions_.push_back(action);
        }
        return action;
    };
    addPatternAction(tr("Find &first empty…"), QKeySequence(Qt::SHIFT | Qt::Key_F4),
        [this] { findEmptyPattern(true, true); });
    addPatternAction(
        tr("Find &next empty…"), QKeySequence(Qt::Key_F4), [this] { findEmptyPattern(false, true); });
    addPatternAction(tr("Find next empty (no naming)"), QKeySequence(Qt::CTRL | Qt::Key_F4),
        [this] { findEmptyPattern(false, false); });
    patternsMenu->addSeparator();
    addPatternAction(tr("&Rename…"), QKeySequence(Qt::Key_F2), [this] { renameCurrentPattern(); });
    const QString withColours = tr("with pattern colours in milestone M3");
    addPatternAction(tr("Change &color…"), {}, notYetIn(tr("Pattern colours"), withColours));
    addPatternAction(tr("Random color"), {}, notYetIn(tr("Pattern colours"), withColours));
    addPatternAction(tr("&Transpose…"), {}, [this] { transposeCurrentPattern(); });
    patternsMenu->addSeparator();
    addPatternAction(
        tr("&Insert one"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Insert), [this] { insertPattern(); });
    addPatternAction(
        tr("&Clone"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), [this] { cloneCurrentPattern(); });
    addPatternAction(tr("&Delete"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Delete),
        [this] { deleteCurrentPattern(); });
    patternsMenu->addSeparator();
    addPatternAction(
        tr("Move &up"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Up), [this] { moveCurrentPattern(-1); });
    addPatternAction(tr("Move do&wn"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Down),
        [this] { moveCurrentPattern(1); });
    patternsMenu->addSeparator();
    addPatternAction(tr("&Split by channel"), {}, [this] { splitCurrentPatternByChannel(); });
    addPatternAction(tr("Render as audio clip…"), {},
        notYetIn(tr("Rendering a pattern"), tr("with WAV export in milestone M3")));

    // No Transport menu: the tool bar buttons carry those shortcuts. Go to
    // Start has no button, so it is added to the window to keep Home working.
    addAction(transport_->rewindAction());

    // On macOS the preferences role moves this into the app menu.
    auto* optionsMenu = menuBar()->addMenu(tr("&Options"));
    settingsAction_ = optionsMenu->addAction(Theme::icon(QStringLiteral("settings")), tr("Settings…"), this,
        [this] { showSettings(SettingsDialog::Page::General); });
    // Not QKeySequence::Preferences: on Windows that is the "Settings" media
    // key, not Ctrl+,. Qt::CTRL is Cmd on macOS, so this is Cmd+, there.
    settingsAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    settingsAction_->setMenuRole(QAction::PreferencesRole);
    settingsAction_->setToolTip(
        tr("Settings (%1)").arg(settingsAction_->shortcut().toString(QKeySequence::NativeText)));
    settingsAction_->setIconVisibleInMenu(false);
    optionsMenu->addAction(
        tr("Audio Settings…"), this, [this] { showSettings(SettingsDialog::Page::Audio); });
    optionsMenu->addAction(tr("Theme…"), this, [this] { showSettings(SettingsDialog::Page::Theme); });
    optionsMenu->addSeparator();
    optionsMenu->addAction(tr("Plugin Manager…"), this, &MainWindow::showPluginManager);
    optionsMenu->addAction(tr("Stem Models…"), this, [this] { (void)showStemModels(); });
    optionsMenu->addAction(
        tr("Stem Extractor Settings…"), this, [this] { showSettings(SettingsDialog::Page::Stems); });

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    fullScreenAction_ = viewMenu->addAction(tr("Enter Full Screen"), this, &MainWindow::toggleFullScreen);
    fullScreenAction_->setShortcut(QKeySequence::FullScreen);

    viewMenu->addSeparator();
    performanceAction_ = viewMenu->addAction(tr("Show Performance Overlay"));
    performanceAction_->setCheckable(true);
    performanceAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
    connect(performanceAction_, &QAction::triggered, this, &MainWindow::setPerformanceOverlayVisible);

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About VloomDAW"), this, &MainWindow::showAbout);
}

void MainWindow::createToolBars()
{
    transport_ = new TransportBar(this);
    addToolBar(transport_);

    connect(transport_, &TransportBar::playToggled, this, &MainWindow::togglePlay);
    connect(transport_, &TransportBar::stopRequested, this, &MainWindow::stopAndRewind);
    connect(transport_, &TransportBar::tapTempoRequested, this, &MainWindow::tapTempo);
    connect(transport_, &TransportBar::rewindRequested, this, [this] { audio_.engine().locate(0.0); });
    connect(transport_, &TransportBar::stepBackwardRequested, this, [this] {
        auto& engine = audio_.engine();
        const double barTicks = static_cast<double>(engine.beatsPerBar() * kTicksPerBeat);
        const double target = std::max(0.0, std::floor((engine.positionTicks() - 1.0) / barTicks) * barTicks);
        (void)engine.locate(target);
    });
    connect(transport_, &TransportBar::stepForwardRequested, this, [this] {
        auto& engine = audio_.engine();
        const double barTicks = static_cast<double>(engine.beatsPerBar() * kTicksPerBeat);
        const double target = (std::floor(engine.positionTicks() / barTicks) + 1.0) * barTicks;
        (void)engine.locate(target);
    });
    connect(transport_, &TransportBar::goToEndRequested, this, [this] {
        const double end = songMode_ ? static_cast<double>(std::max(0, songEndTicks()))
                                     : static_cast<double>(project_.patternLength(currentPattern_));
        (void)audio_.engine().locate(end);
    });
    connect(transport_, &TransportBar::loopToggled, this, [this](bool loop) {
        songLoopStart_ = -1.0;
        songLoopEnd_ = -1.0;
        if (playlist_ != nullptr)
            playlist_->setLoopActive(loop);
        if (audio_.isRunning())
            applySongLoop();
        statusBar()->showMessage(loop ? tr("Cycle loop enabled") : tr("Cycle loop disabled"), 3000);
    });
    // A BPM drag fires on every mouse move; keep only the latest value and let
    // refreshFromEngine() send it, so the engine's command queue never floods.
    connect(transport_, &TransportBar::tempoChanged, this, [this](double bpm) {
        pendingTempo_ = bpm;
        playlist_->setTempo(bpm); // audio clips' length in bars
    });
    // The engine cannot record yet (no audio input): keep the armed state in
    // the button and say so, so the button is not silently dead.
    connect(transport_, &TransportBar::recordArmedChanged, this, [this](bool armed) {
        statusBar()->showMessage(
            armed ? tr("Recording armed. Audio input is not available yet.") : tr("Recording disarmed"),
            4000);
    });
    connect(transport_, &TransportBar::metronomeToggled, this,
        [this](bool on) { audio_.engine().setMetronomeEnabled(on); });

    // Like the tempo, the volume is sent once per tick (a knob drag fires on
    // every mouse move) and only while a device drains the queue.
    connect(transport_, &TransportBar::masterVolumeChanged, this, [this] { updateMasterOutput(); });
    connect(transport_, &TransportBar::masterMuteToggled, this, [this](bool muted) {
        updateMasterOutput();
        statusBar()->showMessage(muted ? tr("Master output muted") : tr("Master output unmuted"), 3000);
    });
    connect(transport_, &TransportBar::masterDimToggled, this, [this](bool dimmed) {
        updateMasterOutput();
        statusBar()->showMessage(
            dimmed ? tr("Master monitor dimmed (-15 dB)") : tr("Master monitor normal (0 dB)"), 3000);
    });
    connect(transport_, &TransportBar::masterPitchChanged, this, [this](double semitones) {
        statusBar()->showMessage(
            tr("Master pitch %1 semitones: it applies to instruments, which arrive in milestone M2.")
                .arg(QString::number(semitones, 'f', 1)),
            4000);
    });
    connect(transport_, &TransportBar::songModeChanged, this, [this](bool song) {
        songMode_ = song;
        songLoopStart_ = -1.0;
        songLoopEnd_ = -1.0;
        syncChannelRack();
        if (audio_.isRunning())
            applySongLoop();
        statusBar()->showMessage(song ? tr("Song mode: Play runs through the playlist.")
                                      : tr("Pattern mode: Play loops the current pattern."),
            4000);
    });

    connect(transport_->viewButtons(), &ViewButtons::viewRequested, this, &MainWindow::showView);

    // Transport and view keys (Space, F5-F9, …) also work while a detached
    // mixer or playlist window has the focus, and while their buttons sit in
    // the transport's » drop-down (Qt ignores shortcuts of actions whose
    // widgets are all hidden; the main window is always shown).
    for (QAction* action : transport_->findChildren<QAction*>()) {
        action->setShortcutContext(Qt::ApplicationShortcut);
        addAction(action);
    }
    // The key is shown only for now; the piano roll will highlight its scale.
    const auto announceKey = [this] {
        const KeyDisplay* key = transport_->keyDisplay();
        const std::string_view root = noteName(key->rootNote());
        const std::string_view scale = scaleInfo(key->scale()).name;
        statusBar()->showMessage(
            tr("Key: %1 %2 (highlighted in the piano roll)")
                .arg(QString::fromLatin1(root.data(), static_cast<qsizetype>(root.size())),
                    QString::fromLatin1(scale.data(), static_cast<qsizetype>(scale.size()))),
            4000);
        pianoRoll_->setKey(key->rootNote(), key->scale());
    };
    connect(transport_->keyDisplay(), &KeyDisplay::rootNoteChanged, this, announceKey);
    connect(transport_->keyDisplay(), &KeyDisplay::scaleChanged, this, announceKey);

    // Patterns: the selector shows the project's patterns; edits go through
    // the same functions as the Patterns menu.
    PatternSelector* patterns = transport_->patternSelector();
    connect(patterns, &PatternSelector::patternPicked, this,
        [this](int index) { selectPattern(static_cast<std::size_t>(index)); });
    connect(patterns, &PatternSelector::addRequested, this, [this] {
        if (roomForPatterns(1)) {
            recordUndo(tr("Add Pattern"));
            patternsChanged(project_.addPattern());
        }
    });
    connect(patterns, &PatternSelector::deleteRequested, this, &MainWindow::deleteCurrentPattern);
}

void MainWindow::selectPattern(std::size_t pattern)
{
    currentPattern_ = std::min(pattern, project_.patterns().size() - 1);
    syncPatternSelector();
    channelRack_->setPattern(currentPattern_);
    pianoRoll_->setTarget(currentPattern_, currentChannel_);
    playlist_->setCurrentPattern(currentPattern_);
    rebuildClipSources();
    updatePanelContexts();
    syncChannelRack(); // the engine plays the current pattern
}

void MainWindow::changeBars(int bars)
{
    const int steps = project_.stepCount(currentPattern_);
    // Whole bars: a count that is not one rounds to the bar it is in.
    const int target = bars > 0 ? (steps / kStepsPerBar + bars) * kStepsPerBar
                                : ((steps - 1) / kStepsPerBar + bars + 1) * kStepsPerBar;
    if (bars < 0 && (steps <= kStepsPerBar || target < kStepsPerBar)) {
        statusBar()->showMessage(tr("A pattern has at least one bar of steps."), 4000);
        return;
    }
    if (target > Project::kMaxSteps) {
        statusBar()->showMessage(tr("A pattern can have up to %1 steps.").arg(Project::kMaxSteps), 4000);
        return;
    }
    if (target < steps && project_.hasStepsFrom(currentPattern_, target)
        && QMessageBox::question(
               this, tr("Remove Bar"), tr("The last bar has steps that are on. Remove it anyway?"))
            != QMessageBox::Yes)
        return;
    recordUndo(bars > 0 ? tr("Add Bar") : tr("Remove Bar"));
    project_.setStepCount(currentPattern_, target);
    patternsChanged(currentPattern_); // rack width, clip lengths, the engine's loop
    const QString name = QString::fromStdString(project_.patterns()[currentPattern_].name);
    const int barCount = (target + kStepsPerBar - 1) / kStepsPerBar;
    statusBar()->showMessage(barCount == 1 ? tr("%1: %2 steps, 1 bar").arg(name).arg(target)
                                           : tr("%1: %2 steps, %3 bars").arg(name).arg(target).arg(barCount),
        3000);
}

void MainWindow::syncInsertNames()
{
    QStringList inserts{tr("Master")};
    for (std::size_t i = 1; i < mixer_.size(); ++i) {
        const MixerTrack& t = mixer_.track(i);
        // A named insert keeps its number in front: channels are routed by it.
        inserts << (t.named ? tr("%1 · %2").arg(i).arg(QString::fromStdString(t.name))
                            : tr("Insert %1").arg(i));
    }
    channelRack_->setInsertNames(inserts);
    channelRack_->update();
}

void MainWindow::addMixerInsert()
{
    if (mixer_.insertCount() >= MixerModel::kMaxInserts) {
        statusBar()->showMessage(tr("The mixer has up to %1 inserts.").arg(MixerModel::kMaxInserts), 4000);
        return;
    }
    recordUndo(tr("Add Insert"));
    const auto addedIns = mixer_.addInsert();
    if (!addedIns)
        return;

    // Pair 1:1 with the first unlinked playlist track (e.g. initial Track 1),
    // or create a matching playlist track when all existing tracks are already bound.
    std::optional<std::size_t> targetTrack;
    for (std::size_t t = 0; t < project_.tracks().size(); ++t) {
        if (trackMixerInsert(project_, mixer_, t) < 0) {
            targetTrack = t;
            break;
        }
    }
    if (!targetTrack) {
        if (const auto addedTrk = project_.addTrack()) {
            project_.setTrackColor(*addedTrk, trackPaletteColor(*addedTrk));
            targetTrack = *addedTrk;
        }
    }
    if (targetTrack)
        pairTrackWithInsert(project_, mixer_, *targetTrack, *addedIns);

    syncMixerPanel();
    syncInsertNames();
    playlist_->refresh();
    mixerPanel_->setSelectedTrack(*addedIns);
    if (targetTrack)
        playlist_->setSelectedTrack(static_cast<int>(*targetTrack));
    updateMixer();
}

void MainWindow::addMixerRoleChannel(MixerRole role)
{
    if (mixer_.insertCount() >= MixerModel::kMaxInserts) {
        statusBar()->showMessage(tr("The mixer has up to %1 channels.").arg(MixerModel::kMaxInserts), 4000);
        return;
    }
    const QString undoLabel = role == MixerRole::Bus ? tr("Add Bus Channel")
        : role == MixerRole::Fx                      ? tr("Add FX Channel")
        : role == MixerRole::Aux                     ? tr("Add Aux Channel")
                                                     : tr("Add Insert");
    recordUndo(undoLabel);
    const auto added = mixer_.addChannel(role);
    if (!added)
        return;

    syncMixerPanel();
    syncInsertNames();
    mixerPanel_->setSelectedTrack(*added);
    updateMixer();
    setMixerVisible(true);

    const QString chTitle = QString::fromStdString(mixer_.track(*added).name);
    if (role == MixerRole::Fx) {
        statusBar()->showMessage(
            tr("Created %1 — add a Reverb / Delay in Inserts and feed it via Sends.").arg(chTitle), 5000);
    } else if (role == MixerRole::Bus) {
        statusBar()->showMessage(
            tr("Created %1 — route channels' Output to %1 to submix them.").arg(chTitle), 5000);
    } else {
        statusBar()->showMessage(tr("Created %1.").arg(chTitle), 3500);
    }
}

void MainWindow::addBusForMixerTrack(std::size_t track)
{
    if (track == MixerModel::kMaster || track >= mixer_.size())
        return;
    if (mixer_.insertCount() >= MixerModel::kMaxInserts) {
        statusBar()->showMessage(tr("The mixer has up to %1 channels.").arg(MixerModel::kMaxInserts), 4000);
        return;
    }
    recordUndo(tr("Add Bus for Channel"));
    const auto busIdx = mixer_.addChannel(MixerRole::Bus);
    if (!busIdx)
        return;
    (void)mixer_.setOutput(track, static_cast<int>(*busIdx));

    syncMixerPanel();
    syncInsertNames();
    if (playlist_ != nullptr)
        playlist_->updateHeaders();
    mixerPanel_->setSelectedTrack(*busIdx);
    updateMixer();
    statusBar()->showMessage(tr("Routed %1 → %2")
                                 .arg(QString::fromStdString(mixer_.track(track).name),
                                     QString::fromStdString(mixer_.track(*busIdx).name)),
        4000);
}

void MainWindow::addFxSendForMixerTrack(std::size_t track)
{
    if (track == MixerModel::kMaster || track >= mixer_.size())
        return;
    if (mixer_.insertCount() >= MixerModel::kMaxInserts) {
        statusBar()->showMessage(tr("The mixer has up to %1 channels.").arg(MixerModel::kMaxInserts), 4000);
        return;
    }
    recordUndo(tr("Add FX Send"));
    const auto fxIdx = mixer_.addChannel(MixerRole::Fx);
    if (!fxIdx)
        return;
    (void)mixer_.addSend(track, static_cast<int>(*fxIdx), 0.8, false);

    syncMixerPanel();
    syncInsertNames();
    mixerPanel_->setSelectedTrack(*fxIdx);
    updateMixer();
    statusBar()->showMessage(tr("Added Send from %1 → %2")
                                 .arg(QString::fromStdString(mixer_.track(track).name),
                                     QString::fromStdString(mixer_.track(*fxIdx).name)),
        4000);
    // Offer effect picker immediately on the new FX channel's Slot 1!
    showEffectChooserMenu(*fxIdx, 0, QCursor::pos());
}

void MainWindow::openMixerInstrumentEditor(std::size_t track)
{
    if (track == MixerModel::kMaster || track >= mixer_.size())
        return;
    const auto targetIns = static_cast<int>(track);
    std::optional<std::size_t> instCh;
    std::optional<std::size_t> fallbackCh;
    for (std::size_t c = 0; c < project_.channels().size(); ++c) {
        if (project_.channels()[c].insert != targetIns)
            continue;
        if (!fallbackCh)
            fallbackCh = c;
        if (project_.channels()[c].instrument.has_value()) {
            instCh = c;
            break;
        }
    }
    if (instCh) {
        selectChannel(*instCh);
        const Channel& ch = project_.channels()[*instCh];
        if (ch.instrument && ch.instrument->instance != 0) {
            openPluginEditor(ch.instrument->instance, QString::fromStdString(ch.instrument->name));
            return;
        }
    }
    if (fallbackCh) {
        selectChannel(*fallbackCh);
        showInstrumentChooserMenu(*fallbackCh, QCursor::pos());
    }
}

void MainWindow::showMixerInstrumentMenu(std::size_t track, const QPoint& globalPos)
{
    if (track == MixerModel::kMaster || track >= mixer_.size())
        return;
    const auto targetIns = static_cast<int>(track);
    std::optional<std::size_t> instCh;
    for (std::size_t c = 0; c < project_.channels().size(); ++c) {
        if (project_.channels()[c].insert == targetIns && project_.channels()[c].instrument.has_value()) {
            instCh = c;
            break;
        }
    }
    if (!instCh)
        instCh = insertPrimaryChannel(project_, track);
    if (!instCh)
        return;

    const Channel& ch = project_.channels()[*instCh];
    QMenu menu(this);
    if (ch.instrument && ch.instrument->instance != 0) {
        menu.addAction(Theme::icon(QStringLiteral("track-instrument")),
            tr("Open “%1” Editor…").arg(QString::fromStdString(ch.instrument->name)), this,
            [this, handle = ch.instrument->instance, title = QString::fromStdString(ch.instrument->name)] {
                openPluginEditor(handle, title);
            });
    }
    menu.addAction(tr("Replace Instrument…"), this,
        [this, chIdx = *instCh, globalPos] { showInstrumentChooserMenu(chIdx, globalPos); });
    menu.addSeparator();
    menu.addAction(tr("Open in Piano Roll"), this, [this, chIdx = *instCh] {
        selectChannel(chIdx);
        showWorkspace(WorkspacePage::PianoRoll);
    });
    menu.addAction(tr("Select in Channel Rack"), this, [this, chIdx = *instCh] {
        selectChannel(chIdx);
        showWorkspace(WorkspacePage::ChannelRack);
    });
    menu.exec(globalPos);
}

void MainWindow::showMixerSourceMenu(std::size_t track, const QPoint& globalPos)
{
    if (track == MixerModel::kMaster || track >= mixer_.size())
        return;
    QMenu menu(this);
    const auto ch = insertPrimaryChannel(project_, track);
    if (ch) {
        const QString chName = QString::fromStdString(project_.channels()[*ch].name);
        menu.addAction(tr("Select “%1” in Channel Rack").arg(chName), this, [this, chIdx = *ch] {
            selectChannel(chIdx);
            showWorkspace(WorkspacePage::ChannelRack);
        });
        if (!project_.channels()[*ch].samplePath.empty()) {
            const QString file
                = QFileInfo(QString::fromStdString(project_.channels()[*ch].samplePath)).fileName();
            menu.addAction(tr("Audio File: %1").arg(file))->setEnabled(false);
        } else {
            menu.addAction(Theme::icon(QStringLiteral("track-instrument")),
                project_.channels()[*ch].instrument ? tr("Replace Instrument Plug-in…")
                                                    : tr("Load Instrument Plug-in…"),
                this, [this, chIdx = *ch, globalPos] { showInstrumentChooserMenu(chIdx, globalPos); });
        }
        menu.addSeparator();
    }

    // Show upstream mixer channels feeding this strip (when used as a Bus / FX / Aux)
    const int thisIns = static_cast<int>(track);
    QStringList feeders;
    for (std::size_t i = 1; i < mixer_.size(); ++i) {
        if (i == track)
            continue;
        if (mixer_.track(i).output == thisIns) {
            feeders << tr("%1 (Output)").arg(QString::fromStdString(mixer_.track(i).name));
        } else {
            for (const MixerSend& s : mixer_.track(i).sends) {
                if (s.target == thisIns)
                    feeders << tr("%1 (Send)").arg(QString::fromStdString(mixer_.track(i).name));
            }
        }
    }
    if (!feeders.isEmpty()) {
        menu.addAction(tr("Fed by: %1").arg(feeders.join(QStringLiteral(", "))))->setEnabled(false);
        menu.addSeparator();
    }

    QMenu* roleMenu = menu.addMenu(tr("Channel Mode"));
    const MixerRole curRole = mixer_.track(track).role;
    const auto addRoleOpt = [&](const QString& label, MixerRole r) {
        QAction* act = roleMenu->addAction(label, this, [this, track, r] {
            recordUndo(tr("Change Channel Mode"));
            mixer_.setRole(track, r);
            syncMixerPanel();
            syncInsertNames();
        });
        act->setCheckable(true);
        act->setChecked(curRole == r);
    };
    addRoleOpt(tr("Standard Track Insert"), MixerRole::Standard);
    addRoleOpt(tr("Bus Channel"), MixerRole::Bus);
    addRoleOpt(tr("FX Return Channel"), MixerRole::Fx);
    addRoleOpt(tr("Aux Channel"), MixerRole::Aux);

    menu.exec(globalPos);
}

void MainWindow::showMixerOutputMenu(std::size_t track, const QPoint& globalPos)
{
    if (track == MixerModel::kMaster || track >= mixer_.size())
        return;

    const int curOut = mixer_.track(track).output;
    QMenu menu(this);

    const auto applyOut = [this, track](int dst) {
        if (!mixer_.canRouteOutput(track, dst)) {
            statusBar()->showMessage(tr("Cannot route there: that would create a feedback loop."), 4000);
            return;
        }
        recordUndo(tr("Route Channel Output"));
        mixer_.setOutput(track, dst);
        syncMixerPanel();
        if (playlist_ != nullptr)
            playlist_->updateHeaders();
        updateMixer();
    };

    QAction* mainAct = menu.addAction(tr("Main (Master Out)"), this, [applyOut] { applyOut(0); });
    mainAct->setCheckable(true);
    mainAct->setChecked(curOut == 0);

    bool addedBusHeader = false;
    QMenu* otherInsertsMenu = nullptr;
    for (std::size_t i = 1; i < mixer_.size(); ++i) {
        if (i == track)
            continue;
        const MixerTrack& mt = mixer_.track(i);
        const bool can = mixer_.canRouteOutput(track, static_cast<int>(i));
        const QString roleTag = mt.role == MixerRole::Bus ? tr(" [Bus]")
            : mt.role == MixerRole::Aux                   ? tr(" [Aux]")
            : mt.role == MixerRole::Fx                    ? tr(" [FX]")
                                                          : QString();
        const QString title = QStringLiteral("%1%2").arg(QString::fromStdString(mt.name), roleTag);

        if (mt.role != MixerRole::Standard) {
            if (!addedBusHeader) {
                menu.addSeparator();
                addedBusHeader = true;
            }
            QAction* act = menu.addAction(title, this, [applyOut, i] { applyOut(static_cast<int>(i)); });
            act->setCheckable(true);
            act->setChecked(curOut == static_cast<int>(i));
            act->setEnabled(can);
        } else {
            if (otherInsertsMenu == nullptr)
                otherInsertsMenu = new QMenu(tr("Other Channels"), &menu);
            QAction* act
                = otherInsertsMenu->addAction(title, this, [applyOut, i] { applyOut(static_cast<int>(i)); });
            act->setCheckable(true);
            act->setChecked(curOut == static_cast<int>(i));
            act->setEnabled(can);
        }
    }
    if (otherInsertsMenu != nullptr) {
        menu.addSeparator();
        menu.addMenu(otherInsertsMenu);
    }

    if (mixer_.insertCount() < MixerModel::kMaxInserts) {
        menu.addSeparator();
        menu.addAction(Theme::icon(QStringLiteral("plus")), tr("Route to New Bus Channel"), this,
            [this, track] { addBusForMixerTrack(track); });
    }

    menu.exec(globalPos);
}

void MainWindow::showMixerAddSendMenu(std::size_t track, const QPoint& globalPos)
{
    if (track == MixerModel::kMaster || track >= mixer_.size())
        return;

    QMenu menu(this);
    if (mixer_.track(track).sends.size() >= MixerModel::kMaxSends) {
        menu.addAction(tr("Maximum %1 sends reached").arg(MixerModel::kMaxSends))->setEnabled(false);
        menu.exec(globalPos);
        return;
    }

    const auto addTargetSend = [this, track](int dst) {
        if (!mixer_.canAddSend(track, dst))
            return;
        recordUndo(tr("Add Send"));
        mixer_.addSend(track, dst, 0.8, false);
        syncMixerPanel();
        updateMixer();
    };

    bool anyPreferred = false;
    QMenu* otherMenu = nullptr;
    for (std::size_t i = 1; i < mixer_.size(); ++i) {
        if (i == track || !mixer_.canAddSend(track, static_cast<int>(i)))
            continue;
        const MixerTrack& mt = mixer_.track(i);
        if (mt.role != MixerRole::Standard) {
            anyPreferred = true;
            menu.addAction(QString::fromStdString(mt.name), this,
                [addTargetSend, i] { addTargetSend(static_cast<int>(i)); });
        } else {
            if (otherMenu == nullptr)
                otherMenu = new QMenu(tr("Send to Other Channel"), &menu);
            otherMenu->addAction(QString::fromStdString(mt.name), this,
                [addTargetSend, i] { addTargetSend(static_cast<int>(i)); });
        }
    }
    if (anyPreferred && otherMenu != nullptr)
        menu.addSeparator();
    if (otherMenu != nullptr)
        menu.addMenu(otherMenu);

    if (mixer_.insertCount() < MixerModel::kMaxInserts) {
        if (!menu.isEmpty())
            menu.addSeparator();
        menu.addAction(Theme::icon(QStringLiteral("plus")), tr("Add FX Channel && Send…"), this,
            [this, track] { addFxSendForMixerTrack(track); });
        menu.addAction(
            Theme::icon(QStringLiteral("plus")), tr("Add Bus Channel && Send"), this, [this, track] {
                recordUndo(tr("Add Bus Send"));
                if (const auto busIdx = mixer_.addChannel(MixerRole::Bus)) {
                    mixer_.addSend(track, static_cast<int>(*busIdx), 0.8, false);
                    syncMixerPanel();
                    mixerPanel_->setSelectedTrack(*busIdx);
                    updateMixer();
                }
            });
    }

    menu.exec(globalPos);
}

void MainWindow::showMixerSendMenu(std::size_t track, std::size_t sendSlot, const QPoint& globalPos)
{
    if (track == MixerModel::kMaster || track >= mixer_.size()
        || sendSlot >= mixer_.track(track).sends.size()) {
        return;
    }
    const MixerSend send = mixer_.track(track).sends[sendSlot];
    const QString dstName = (send.target > 0 && static_cast<std::size_t>(send.target) < mixer_.size())
        ? QString::fromStdString(mixer_.track(static_cast<std::size_t>(send.target)).name)
        : tr("Channel %1").arg(send.target);

    QMenu menu(this);
    menu.addAction(
            tr("Send → %1 (%2)").arg(dstName, QString::fromStdString(formatDb(faderToGain(send.level)))))
        ->setEnabled(false);
    menu.addSeparator();

    QAction* preAct = menu.addAction(tr("Pre-Fader"), this, [this, track, sendSlot](bool pre) {
        recordUndo(tr("Toggle Send Pre-Fader"));
        mixer_.setSendPreFader(track, sendSlot, pre);
        syncMixerPanel();
        updateMixer();
    });
    preAct->setCheckable(true);
    preAct->setChecked(send.preFader);

    QAction* muteAct = menu.addAction(tr("Mute Send"), this, [this, track, sendSlot](bool muted) {
        recordUndo(tr("Mute Send"));
        mixer_.setSendMuted(track, sendSlot, muted);
        syncMixerPanel();
        updateMixer();
    });
    muteAct->setCheckable(true);
    muteAct->setChecked(send.muted);

    menu.addAction(tr("Reset Level (0.0 dB)"), this, [this, track, sendSlot] {
        recordUndo(tr("Reset Send Level"));
        mixer_.setSendLevel(track, sendSlot, kUnityFaderPosition);
        syncMixerPanel();
        updateMixer();
    });

    if (send.target > 0 && static_cast<std::size_t>(send.target) < mixerPanel_->trackCount()) {
        menu.addAction(tr("Select “%1” in Mixer").arg(dstName), this,
            [this, dst = static_cast<std::size_t>(send.target)] { mixerPanel_->setSelectedTrack(dst); });
    }

    menu.addSeparator();
    menu.addAction(tr("Remove Send"), this, [this, track, sendSlot] {
        recordUndo(tr("Remove Send"));
        mixer_.removeSend(track, sendSlot);
        syncMixerPanel();
        updateMixer();
    });
    menu.exec(globalPos);
}

void MainWindow::copyMixerChannelSettings(std::size_t track)
{
    if (track >= mixer_.size())
        return;
    const MixerTrack& mt = mixer_.track(track);
    CopiedChannelSettings copied;
    copied.fader = mt.fader;
    copied.pan = mt.pan;
    copied.mono = mt.mono;
    copied.effects = mt.effects;
    copied.sends = mt.sends;
    for (const PluginSlot& slot : mt.effects)
        copied.effectStates.push_back(pluginHost_.saveState(slot.instance, slot.savedState));
    copiedChannelSettings_ = std::move(copied);
    if (mixerPanel_ != nullptr)
        mixerPanel_->setCanPasteChannelSettings(true);
    statusBar()->showMessage(tr("Copied settings from %1.").arg(QString::fromStdString(mt.name)), 3000);
}

void MainWindow::pasteMixerChannelSettings(std::size_t track)
{
    if (!copiedChannelSettings_.has_value() || track >= mixer_.size())
        return;
    recordUndo(tr("Paste Channel Settings"));
    const CopiedChannelSettings& src = *copiedChannelSettings_;
    mixer_.setFader(track, src.fader);
    mixer_.setPan(track, src.pan);
    mixer_.setMono(track, src.mono);
    if (track != MixerModel::kMaster) {
        if (const auto trk = insertPrimaryTrack(project_, mixer_, track)) {
            project_.setTrackVolume(*trk, src.fader);
            project_.setTrackPan(*trk, src.pan);
        }
    }

    while (mixer_.removeEffect(track, 0)) { }
    const double rate = audio_.sampleRate() > 0
        ? audio_.sampleRate()
        : (audio_.requestedSampleRate() > 0 ? static_cast<double>(audio_.requestedSampleRate()) : 48000.0);
    for (std::size_t e = 0; e < src.effects.size(); ++e) {
        PluginSlot clone = src.effects[e];
        clone.instance = 0;
        const std::span<const std::uint8_t> st = e < src.effectStates.size()
            ? std::span<const std::uint8_t>(src.effectStates[e])
            : std::span<const std::uint8_t>();
        if (isScannedPlugin(clone)) {
            const PluginHost::Loaded loaded = pluginHost_.loadWithState(
                clone.format, pathFromUtf8(clone.path), clone.id, clone.name, rate, st);
            if (loaded.handle != 0)
                clone.instance = loaded.handle;
        }
        mixer_.addEffect(track, std::move(clone));
    }

    if (track != MixerModel::kMaster) {
        while (mixer_.removeSend(track, 0)) { }
        for (const MixerSend& s : src.sends) {
            if (mixer_.addSend(track, s.target, s.level, s.preFader))
                mixer_.setSendMuted(track, mixer_.track(track).sends.size() - 1, s.muted);
        }
    }

    syncMixerPanel();
    syncEffectCounts();
    updateMixer();
    releaseUnusedPlugins();
    if (playlist_ != nullptr)
        playlist_->updateHeaders();
    statusBar()->showMessage(
        tr("Pasted channel settings to %1.").arg(QString::fromStdString(mixer_.track(track).name)), 3000);
}

void MainWindow::renameMixerInsert(std::size_t track)
{
    if (track == MixerModel::kMaster || track >= mixer_.size() || mixerPanel_ == nullptr)
        return;
    mixerPanel_->startTrackRename(track);
}

void MainWindow::addPlaylistTrack()
{
    if (project_.tracks().size() >= static_cast<std::size_t>(Project::kMaxPlaylistTracks)) {
        statusBar()->showMessage(
            tr("The playlist has up to %1 tracks.").arg(Project::kMaxPlaylistTracks), 4000);
        return;
    }
    recordUndo(tr("Add Track"));

    // Ensure any earlier unlinked tracks (e.g. initial Track 1) get their 1:1 Mixer Insert first
    // so Track N and Insert N stay aligned.
    for (std::size_t t = 0; t < project_.tracks().size(); ++t) {
        if (trackMixerInsert(project_, mixer_, t) < 0 && mixer_.insertCount() < MixerModel::kMaxInserts) {
            if (const auto ins = mixer_.addInsert())
                pairTrackWithInsert(project_, mixer_, t, *ins);
        }
    }

    const auto added = project_.addTrack();
    if (!added)
        return;
    project_.setTrackColor(*added, trackPaletteColor(*added));
    if (mixer_.insertCount() < MixerModel::kMaxInserts) {
        if (const auto ins = mixer_.addInsert())
            pairTrackWithInsert(project_, mixer_, *added, *ins);
    }

    syncMixerPanel();
    syncInsertNames();
    playlist_->refresh();
    playlist_->setSelectedTrack(static_cast<int>(*added));
    const int linkedIns = trackMixerInsert(project_, mixer_, *added);
    if (linkedIns >= 0 && static_cast<std::size_t>(linkedIns) < mixerPanel_->trackCount())
        mixerPanel_->setSelectedTrack(static_cast<std::size_t>(linkedIns));
    playlist_->verticalScrollBar()->setValue(playlist_->verticalScrollBar()->maximum());
    updateMixer();
}

void MainWindow::showAddTracksDialog(int afterTrack)
{
    if (project_.tracks().size() >= static_cast<std::size_t>(Project::kMaxPlaylistTracks)) {
        statusBar()->showMessage(
            tr("The playlist has up to %1 tracks.").arg(Project::kMaxPlaylistTracks), 4000);
        return;
    }
    AddTracksDialog dlg(project_, mixer_, pluginStore_, AddTracksSpec::Kind::Audio, this);
    if (dlg.exec() == QDialog::Accepted)
        applyAddTracksSpec(dlg.spec(), afterTrack);
}

void MainWindow::addQuickAudioTrack(int afterTrack, bool mono)
{
    AddTracksSpec spec;
    spec.kind = AddTracksSpec::Kind::Audio;
    spec.mono = mono;
    spec.count = 1;
    spec.autoColor = true;
    spec.audioOutputInsert = -1;
    const std::size_t num = project_.tracks().size() + 1;
    spec.baseName = mono ? tr("Audio %1 (M)").arg(num) : tr("Audio %1").arg(num);
    applyAddTracksSpec(spec, afterTrack);
}

void MainWindow::addQuickInstrumentTrack(int afterTrack)
{
    AddTracksSpec spec;
    spec.kind = AddTracksSpec::Kind::Instrument;
    spec.count = 1;
    spec.autoColor = true;
    spec.openInstrumentEditor = true;
    for (std::size_t i = 0; i < pluginStore_.plugins.size(); ++i) {
        const PluginInfo& info = pluginStore_.plugins[i];
        if (info.enabled && info.format == PluginFormat::Builtin && pluginMainGroup(info) == "Instrument") {
            spec.newInstrumentPlugin = i;
            break;
        }
    }
    const std::size_t num = project_.tracks().size() + 1;
    spec.baseName = spec.newInstrumentPlugin.has_value()
        ? tr("%1 %2")
              .arg(QString::fromStdString(pluginStore_.plugins[*spec.newInstrumentPlugin].name))
              .arg(num)
        : tr("Instrument %1").arg(num);
    applyAddTracksSpec(spec, afterTrack);
}

void MainWindow::applyAddTracksSpec(const AddTracksSpec& spec, int afterTrack)
{
    const std::size_t room = project_.tracks().size() < static_cast<std::size_t>(Project::kMaxPlaylistTracks)
        ? static_cast<std::size_t>(Project::kMaxPlaylistTracks) - project_.tracks().size()
        : 0;
    if (room == 0) {
        statusBar()->showMessage(
            tr("The playlist has up to %1 tracks.").arg(Project::kMaxPlaylistTracks), 4000);
        return;
    }

    const bool createFolder = spec.packFolder && room >= 2;
    const std::size_t trackBudget = createFolder ? (room - 1) : room;
    const std::size_t actualCount
        = std::clamp<std::size_t>(static_cast<std::size_t>(std::max(1, spec.count)), 1, trackBudget);

    recordUndo(
        spec.kind == AddTracksSpec::Kind::Instrument ? tr("Add Instrument Track") : tr("Add Audio Track"));

    // Ensure any earlier unlinked tracks get their 1:1 Mixer Insert first so
    // Insert numbers stay aligned with existing tracks.
    for (std::size_t t = 0; t < project_.tracks().size(); ++t) {
        if (trackMixerInsert(project_, mixer_, t) < 0 && mixer_.insertCount() < MixerModel::kMaxInserts) {
            if (const auto ins = mixer_.addInsert())
                pairTrackWithInsert(project_, mixer_, t, *ins);
        }
    }

    std::size_t insertPos = project_.tracks().size();
    if (afterTrack >= 0 && static_cast<std::size_t>(afterTrack) < project_.tracks().size())
        insertPos = project_.subTreeEnd(static_cast<std::size_t>(afterTrack));

    const QString fallbackBase = spec.kind == AddTracksSpec::Kind::Instrument
        ? tr("Instrument")
        : (spec.mono ? tr("Audio (M)") : tr("Audio"));
    const QString cleanBase = spec.baseName.trimmed().isEmpty() ? fallbackBase : spec.baseName.trimmed();

    if (createFolder) {
        if (const auto folderAdded = project_.addTrack()) {
            const std::size_t folderIdx = project_.moveTrack(*folderAdded, insertPos);
            const std::uint32_t folderCol = spec.autoColor ? trackPaletteColor(folderIdx) : spec.customColor;
            project_.setTrackName(folderIdx, cleanBase.toStdString());
            project_.setTrackColor(folderIdx, folderCol);
            insertPos = folderIdx + 1;
        }
    }

    const auto findBuiltinIndex = [this](std::string_view builtinId) -> std::optional<std::size_t> {
        for (std::size_t i = 0; i < pluginStore_.plugins.size(); ++i) {
            const PluginInfo& info = pluginStore_.plugins[i];
            if (info.format == PluginFormat::Builtin && pathToUtf8(info.path) == builtinId)
                return i;
        }
        return std::nullopt;
    };

    const auto attachFxPreset = [&](std::size_t insertIdx) {
        if (insertIdx == 0 || insertIdx >= mixer_.size() || spec.fxPreset == AddTracksSpec::FxPreset::None)
            return;
        const auto addOneBuiltin = [&](std::string_view id) {
            if (mixer_.track(insertIdx).effects.size() >= MixerModel::kMaxEffects)
                return;
            const auto plugIdx = findBuiltinIndex(id);
            if (!plugIdx)
                return;
            if (const auto slot = loadPlugin(*plugIdx, vloom::PluginInfo::Kind::Effect))
                mixer_.addEffect(insertIdx, *slot);
        };
        switch (spec.fxPreset) {
        case AddTracksSpec::FxPreset::None:
            break;
        case AddTracksSpec::FxPreset::BuiltinEq:
            addOneBuiltin("vloom.eq");
            break;
        case AddTracksSpec::FxPreset::BuiltinGain:
            addOneBuiltin("vloom.gain");
            break;
        case AddTracksSpec::FxPreset::BuiltinEqAndGain:
            addOneBuiltin("vloom.eq");
            addOneBuiltin("vloom.gain");
            break;
        }
    };

    // Collect existing non-sample channels for "Existing Instrument" + Ascending mode.
    std::vector<std::size_t> existingInstChannels;
    if (spec.kind == AddTracksSpec::Kind::Instrument && spec.useExistingInstrument) {
        for (std::size_t c = 0; c < project_.channels().size(); ++c) {
            const Channel& ch = project_.channels()[c];
            if (ch.samplePath.empty() || ch.instrument.has_value())
                existingInstChannels.push_back(c);
        }
    }

    int firstCreatedTrack = -1;
    std::optional<std::size_t> firstChannelToSelect;
    std::uint64_t editorHandleToOpen = 0;
    QString editorTitleToOpen;

    for (std::size_t i = 0; i < actualCount; ++i) {
        const auto added = project_.addTrack();
        if (!added)
            break;
        const std::size_t trackIdx = project_.moveTrack(*added, insertPos);
        insertPos = trackIdx + 1;
        if (firstCreatedTrack < 0)
            firstCreatedTrack = static_cast<int>(trackIdx);

        if (createFolder)
            (void)project_.indentTrack(trackIdx);

        const QString itemName = (actualCount == 1 && !createFolder)
            ? cleanBase
            : QStringLiteral("%1 %2").arg(cleanBase).arg(i + 1);
        const std::uint32_t col = spec.autoColor ? trackPaletteColor(trackIdx + i) : spec.customColor;

        project_.setTrackName(trackIdx, itemName.toStdString());
        project_.setTrackColor(trackIdx, col);

        if (spec.kind == AddTracksSpec::Kind::Audio) {
            const std::size_t ch = project_.addChannel(itemName.toStdString());
            project_.setChannelColor(ch, col);
            if (!firstChannelToSelect)
                firstChannelToSelect = ch;

            int targetInsert = 0;
            if (spec.audioOutputInsert == 0) {
                targetInsert = 0;
            } else if (spec.audioOutputInsert > 0) {
                const int desired
                    = spec.audioOutputInsert + (spec.audioOutputAscending ? static_cast<int>(i) : 0);
                while (static_cast<int>(mixer_.insertCount()) < desired
                    && mixer_.insertCount() < MixerModel::kMaxInserts) {
                    (void)mixer_.addInsert();
                }
                targetInsert = std::min(desired, static_cast<int>(mixer_.insertCount()));
            } else if (mixer_.insertCount() < MixerModel::kMaxInserts) {
                if (const auto newIns = mixer_.addInsert())
                    targetInsert = static_cast<int>(*newIns);
            }

            project_.setChannelInsert(ch, targetInsert);
            project_.setTrackInsert(trackIdx, targetInsert);
            if (targetInsert > 0 && spec.audioOutputInsert < 0) {
                pairTrackWithInsert(project_, mixer_, trackIdx, static_cast<std::size_t>(targetInsert));
            }
            if (targetInsert > 0) {
                if (spec.mono)
                    mixer_.setMono(static_cast<std::size_t>(targetInsert), true);
                attachFxPreset(static_cast<std::size_t>(targetInsert));
            }
        } else {
            // Instrument track
            std::size_t ch = 0;
            if (spec.useExistingInstrument && spec.existingChannel.has_value()
                && *spec.existingChannel < project_.channels().size()) {
                ch = *spec.existingChannel;
                if (spec.instrumentAscending && !existingInstChannels.empty()) {
                    const auto it = std::find(
                        existingInstChannels.begin(), existingInstChannels.end(), *spec.existingChannel);
                    const std::size_t startOffset = it != existingInstChannels.end()
                        ? static_cast<std::size_t>(it - existingInstChannels.begin())
                        : 0;
                    ch = existingInstChannels[(startOffset + i) % existingInstChannels.size()];
                }
                int ins = project_.channels()[ch].insert;
                if (ins == 0 && mixer_.insertCount() < MixerModel::kMaxInserts)
                    ins = static_cast<int>(linkChannelToInsert(project_, mixer_, ch));
                project_.setTrackInsert(trackIdx, ins);
            } else {
                ch = project_.addChannel(itemName.toStdString());
                project_.setChannelColor(ch, col);

                int targetInsert = 0;
                if (mixer_.insertCount() < MixerModel::kMaxInserts) {
                    if (const auto newIns = mixer_.addInsert())
                        targetInsert = static_cast<int>(*newIns);
                }
                project_.setChannelInsert(ch, targetInsert);
                project_.setTrackInsert(trackIdx, targetInsert);
                if (targetInsert > 0)
                    pairTrackWithInsert(project_, mixer_, trackIdx, static_cast<std::size_t>(targetInsert));

                if (spec.newInstrumentPlugin.has_value()) {
                    if (const auto slot
                        = loadPlugin(*spec.newInstrumentPlugin, vloom::PluginInfo::Kind::Instrument)) {
                        project_.setChannelInstrument(ch, *slot);
                        if (audio_.sampleRate() <= 0)
                            unpublishedInstruments_.push_back(slot->instance);
                        if (spec.openInstrumentEditor && editorHandleToOpen == 0) {
                            editorHandleToOpen = slot->instance;
                            editorTitleToOpen = itemName;
                        }
                    }
                }
            }

            if (!firstChannelToSelect)
                firstChannelToSelect = ch;
        }
    }

    syncMixerPanel();
    syncInsertNames();
    channelRack_->refresh();
    playlist_->refresh();
    rebuildClipSources();
    if (firstChannelToSelect)
        selectChannel(*firstChannelToSelect);
    if (firstCreatedTrack >= 0) {
        playlist_->setSelectedTrack(firstCreatedTrack);
        const int linkedIns = trackMixerInsert(project_, mixer_, static_cast<std::size_t>(firstCreatedTrack));
        if (linkedIns >= 0 && static_cast<std::size_t>(linkedIns) < mixerPanel_->trackCount())
            mixerPanel_->setSelectedTrack(static_cast<std::size_t>(linkedIns));
    }
    updateMixer();
    syncChannelRack();
    syncEffectCounts();

    if (!isPageDetached(WorkspacePage::Playlist)
        && workspace_->currentIndex() != static_cast<int>(WorkspacePage::Playlist)) {
        showWorkspace(WorkspacePage::Playlist);
    }

    if (editorHandleToOpen != 0)
        openPluginEditor(editorHandleToOpen, editorTitleToOpen);

    statusBar()->showMessage(actualCount == 1
            ? tr("Added track “%1”.").arg(cleanBase)
            : tr("Added %n tracks.", nullptr, static_cast<int>(actualCount)),
        3000);
}

void MainWindow::removePlaylistTrack(std::size_t track)
{
    if (track >= project_.tracks().size())
        return;
    const QString name = QString::fromStdString(project_.tracks()[track].name);
    const std::size_t clips = project_.clipsOnTrack(track);
    if (clips > 0
        && QMessageBox::question(this, tr("Delete Track"),
               (clips == 1 ? tr("“%1” has 1 clip.").arg(name) : tr("“%1” has %2 clips.").arg(name).arg(clips))
                   + QLatin1Char(' ') + tr("Delete the track and its clips?"))
            != QMessageBox::Yes)
        return;

    recordUndo(tr("Delete Track"));
    const int ownedIns = trackMixerInsert(project_, mixer_, track);
    const auto primaryCh = trackPrimaryChannel(project_, track);
    project_.removeTrack(track);

    const auto isChannelStillUsed = [this](std::size_t chIdx) {
        for (const AudioClip& ac : project_.audioClips()) {
            if (ac.channel == chIdx)
                return true;
        }
        for (const Clip& pc : project_.clips()) {
            if (pc.pattern < project_.patterns().size()
                && chIdx < project_.patterns()[pc.pattern].channels.size()) {
                const ChannelPattern& cp = project_.patterns()[pc.pattern].channels[chIdx];
                if (!cp.notes.empty()
                    || std::any_of(cp.steps.begin(), cp.steps.end(), [](bool on) { return on; }))
                    return true;
            }
        }
        return false;
    };

    // If `ownedIns` was dedicated exclusively to the deleted track, remove that mixer strip
    // and release any dedicated instrument/sample on its exclusive channel.
    if (ownedIns > 0 && static_cast<std::size_t>(ownedIns) < mixer_.size()
        && !insertPrimaryTrack(project_, mixer_, static_cast<std::size_t>(ownedIns)).has_value()) {
        bool sharedByLiveClip = false;
        for (std::size_t c = 0; c < project_.channels().size(); ++c) {
            if (project_.channels()[c].insert == ownedIns && isChannelStillUsed(c)) {
                sharedByLiveClip = true;
                break;
            }
        }
        if (!sharedByLiveClip) {
            for (std::size_t c = 0; c < project_.channels().size(); ++c) {
                if (project_.channels()[c].insert == ownedIns) {
                    project_.setChannelInstrument(c, std::nullopt);
                    project_.setChannelSample(c, {});
                }
            }
            project_.removeInsert(ownedIns);
            mixer_.removeInsert(static_cast<std::size_t>(ownedIns));
        }
    } else if (primaryCh && *primaryCh >= 4 && !isChannelStillUsed(*primaryCh)) {
        project_.setChannelInstrument(*primaryCh, std::nullopt);
        project_.setChannelSample(*primaryCh, {});
    }

    syncMixerPanel();
    syncInsertNames();
    channelRack_->refresh();
    rebuildClipSources();
    playlist_->refresh();
    updateMixer();
    syncChannelRack();
    releaseUnusedPlugins();
    syncEffectCounts();
    statusBar()->showMessage(tr("“%1” deleted.").arg(name), 3000);
}

namespace {

std::shared_ptr<ClipAudio> makeClipAudio(const vloom::SampleBuffer& sample)
{
    auto info = std::make_shared<ClipAudio>();
    if (sample.sampleRate() <= 0.0)
        return info;
    info->seconds = static_cast<double>(sample.frames()) / sample.sampleRate();
    const auto perPeak = std::max<std::int64_t>(1, static_cast<std::int64_t>(sample.sampleRate() / 200.0));
    info->peakSeconds = static_cast<double>(perPeak) / sample.sampleRate();
    info->peaks.reserve(static_cast<std::size_t>(sample.frames() / perPeak + 1));
    for (std::int64_t from = 0; from < sample.frames(); from += perPeak) {
        const std::int64_t to = std::min(sample.frames(), from + perPeak);
        float peak = 0.0f;
        for (int ch = 0; ch < sample.channels(); ++ch) {
            const float* samples = sample.channel(ch);
            for (std::int64_t i = from; i < to; ++i)
                peak = std::max(peak, std::abs(samples[i]));
        }
        info->peaks.push_back(peak);
    }

    // Build 3 coarser peak mipmap levels (8x, 64x, 512x) on the worker thread
    // so zoomed-out waveform tiles render in O(1) per column.
    const std::vector<float>* prev = &info->peaks;
    double prevStep = info->peakSeconds;
    for (int level = 0; level < 3 && prev->size() >= 16; ++level) {
        PeakMip mip;
        mip.stepSeconds = prevStep * 8.0;
        mip.peaks.reserve((prev->size() + 7) / 8);
        for (std::size_t i = 0; i < prev->size(); i += 8) {
            const std::size_t end = std::min(prev->size(), i + 8);
            float p = 0.0f;
            for (std::size_t k = i; k < end; ++k)
                p = std::max(p, (*prev)[k]);
            mip.peaks.push_back(p);
        }
        info->mips.push_back(std::move(mip));
        prev = &info->mips.back().peaks;
        prevStep = info->mips.back().stepSeconds;
    }
    return info;
}

} // namespace

const ClipAudio* MainWindow::clipAudio(const std::string& path)
{
    if (const auto found = clipAudio_.find(path); found != clipAudio_.end())
        return found->second.get();
    if (const auto sample = rackAudio_.cachedSample(path); sample && sample->sampleRate() > 0.0)
        return clipAudio_.emplace(path, makeClipAudio(*sample)).first->second.get();
    if (rackAudio_.isLoading(path)) {
        static const ClipAudio kLoading{0.0, 0.01, {}, {}, true};
        return &kLoading;
    }
    static const ClipAudio kUnknown{};
    return &kUnknown;
}

void MainWindow::loadSampleThen(
    const QString& path, std::function<void()> then, std::function<void()> onFailed)
{
    const std::string file = QDir::cleanPath(path).toStdString();
    const double audioRate = audio_.sampleRate();
    const double decodeRate = audioRate > 0.0
        ? audioRate
        : (audio_.requestedSampleRate() > 0 ? static_cast<double>(audio_.requestedSampleRate()) : 48000.0);
    if (audioRate > 0.0 && rackAudio_.hasSample(file, audioRate)) {
        if (!clipAudio_.contains(file))
            if (const auto cached = rackAudio_.cachedSample(file))
                clipAudio_[file] = makeClipAudio(*cached);
        if (then)
            then();
        return;
    }

    if (const auto inflight = pendingLoads_.find(file); inflight != pendingLoads_.end()) {
        inflight->second.emplace_back(std::move(then), std::move(onFailed));
        return;
    }

    pendingLoads_[file].emplace_back(std::move(then), std::move(onFailed));
    rackAudio_.setLoading(file, true);
    auto loadingClip = std::make_shared<ClipAudio>();
    loadingClip->loading = true;
    clipAudio_[file] = std::move(loadingClip);

    startLoader();
    ++loading_;
    const QString name = QFileInfo(path).fileName();
    statusBar()->showMessage(
        loading_ > 1 ? tr("Loading %1 (%2 files)…").arg(name).arg(loading_) : tr("Loading %1…").arg(name));
    QMetaObject::invokeMethod(
        loader_,
        [this, file, decodeRate, name] {
            // Loader thread: decode (MP3 and others), resample and extract waveform peaks.
            std::string error;
            std::shared_ptr<const vloom::SampleBuffer> sample;
            std::shared_ptr<ClipAudio> clipInfo;
            try {
                QString why;
                const QString wav = playableWav(QString::fromStdString(file), decodeRate, &why);
                if (wav.isEmpty())
                    error = why.toStdString();
                else
                    sample = vloom::loadSample(wav.toStdString(), decodeRate, &error);
                if (sample != nullptr)
                    clipInfo = makeClipAudio(*sample);
            } catch (const std::exception&) {
                error = "not enough memory to load it";
            }
            QMetaObject::invokeMethod(
                this,
                [this, file, decodeRate, name, sample, clipInfo, error] {
                    auto callbacks = std::move(pendingLoads_[file]);
                    pendingLoads_.erase(file);
                    --loading_;
                    if (loading_ > 0)
                        statusBar()->showMessage(tr("Loading audio (%1 remaining)…").arg(loading_));
                    else
                        statusBar()->clearMessage();

                    if (sample == nullptr) {
                        rackAudio_.setLoading(file, false);
                        clipAudio_.erase(file);
                        statusBar()->showMessage(
                            tr("Could not load %1: %2").arg(name, QString::fromStdString(error)), 8000);
                        for (auto& [okCb, failCb] : callbacks)
                            if (failCb)
                                failCb();
                        return;
                    }

                    if (clipInfo != nullptr)
                        clipAudio_[file] = clipInfo;
                    if (audio_.sampleRate() > 0.0 && audio_.sampleRate() == decodeRate)
                        rackAudio_.store(file, sample, decodeRate);
                    else
                        rackAudio_.setLoading(file, false);

                    for (auto& [okCb, failCb] : callbacks)
                        if (okCb)
                            okCb();
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void MainWindow::startLoader()
{
    if (loaderThread_ != nullptr)
        return;
    loaderThread_ = new QThread(this);
    loaderThread_->setObjectName(QStringLiteral("SampleLoader"));
    loader_ = new QObject;
    loader_->moveToThread(loaderThread_);
    connect(loaderThread_, &QThread::finished, loader_, &QObject::deleteLater);
    loaderThread_->start();
}

void MainWindow::detectTempoThen(const std::string& path, std::function<void(double bpm)> then)
{
    if (const auto found = detectedTempo_.find(path); found != detectedTempo_.end()) {
        then(found->second);
        return;
    }
    const auto sample = rackAudio_.sample(path); // loaded already: the clip plays it
    if (sample == nullptr) {
        then(0.0);
        return;
    }
    startLoader();
    statusBar()->showMessage(
        tr("Detecting the tempo of %1…").arg(QFileInfo(QString::fromStdString(path)).fileName()));
    QMetaObject::invokeMethod(
        loader_,
        [this, path, sample, then = std::move(then)] {
            const double bpm = detectTempo(*sample).bpm; // loader thread
            QMetaObject::invokeMethod(
                this,
                [this, path, bpm, then] {
                    statusBar()->clearMessage();
                    detectedTempo_[path] = bpm;
                    then(bpm);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

std::shared_ptr<const vloom::SampleBuffer> MainWindow::stretchedSample(const std::string& path, double ratio)
{
    const StretchKey key{path, std::llround(ratio * 10000.0)};
    stretchUsed_[key] = true;
    if (const auto found = stretched_.find(key); found != stretched_.end())
        return found->second;
    // Made once the tempo has settled: a tempo drag must not start a stretch
    // per step. The clip plays resampled meanwhile.
    stretchWanted_.emplace(key, false);
    stretchTimer_->start();
    return nullptr;
}

void MainWindow::makeStretchedSamples()
{
    for (auto& [key, making] : stretchWanted_) {
        if (making || !stretchUsed_.contains(key))
            continue; // under way, or no clip needs it any more
        const auto sample = rackAudio_.sample(key.first);
        if (sample == nullptr)
            continue;
        making = true;
        startLoader();
        ++loading_;
        statusBar()->showMessage(
            tr("Stretching %1 to the tempo…").arg(QFileInfo(QString::fromStdString(key.first)).fileName()));
        QMetaObject::invokeMethod(
            loader_,
            [this, key, sample] {
                auto stretched
                    = timeStretch(*sample, static_cast<double>(key.second) / 10000.0); // loader thread
                QMetaObject::invokeMethod(
                    this,
                    [this, key, stretched] {
                        if (--loading_ == 0)
                            statusBar()->clearMessage();
                        stretchWanted_.erase(key);
                        if (stretched == nullptr)
                            return; // too short or out of range: it stays resampled
                        stretched_[key] = stretched;
                        syncChannelRack(); // the clip now plays its stretched copy
                    },
                    Qt::QueuedConnection);
            },
            Qt::QueuedConnection);
    }
    std::erase_if(stretchWanted_,
        [this](const auto& entry) { return !entry.second && !stretchUsed_.contains(entry.first); });
}

void MainWindow::showAudioClipMenu(std::size_t index, const QPoint& globalPos)
{
    if (index >= project_.audioClips().size())
        return;
    const AudioClip clip = project_.audioClips()[index];
    QMenu menu(this);
    QMenu* fit = menu.addMenu(tr("Fit to Tempo"));
    auto* modes = new QActionGroup(fit);
    const auto mode = [&](const QString& text, AudioClip::Fit value) {
        QAction* action = fit->addAction(text, this, [this, index, value] { setAudioClipFit(index, value); });
        action->setCheckable(true);
        action->setChecked(clip.fit == value);
        modes->addAction(action);
    };
    mode(tr("Off"), AudioClip::Fit::Off);
    mode(tr("Stretch (keep the pitch)"), AudioClip::Fit::Stretch);
    mode(tr("Resample (the pitch follows)"), AudioClip::Fit::Resample);
    menu.addSeparator();
    menu.addAction(clip.sourceBpm > 0.0
                ? tr("Tempo of the sample: %1 BPM").arg(QString::number(clip.sourceBpm, 'f', 2))
                : tr("Tempo of the sample: not known yet"))
        ->setEnabled(false);
    menu.addAction(tr("Detect Tempo"), this, [this, index] {
        if (index >= project_.audioClips().size())
            return;
        const std::size_t channel = project_.audioClips()[index].channel;
        if (channel >= project_.channels().size())
            return;
        const std::string path = project_.channels()[channel].samplePath;
        detectedTempo_.erase(path); // measure again
        detectTempoThen(path, [this, index, channel](double bpm) {
            if (index >= project_.audioClips().size() || project_.audioClips()[index].channel != channel)
                return; // the clip changed meanwhile
            if (bpm <= 0.0) {
                statusBar()->showMessage(tr("No steady beat found: set the tempo by hand."), 6000);
                return;
            }
            setAudioClipTempo(index, bpm);
            statusBar()->showMessage(
                tr("Tempo of the sample: %1 BPM").arg(QString::number(bpm, 'f', 2)), 4000);
        });
    });
    menu.addAction(tr("Set Tempo…"), this, [this, index] {
        if (index >= project_.audioClips().size())
            return;
        bool ok = false;
        const double current = project_.audioClips()[index].sourceBpm;
        const double bpm = QInputDialog::getDouble(this, tr("Tempo of the Sample"), tr("BPM:"),
            current > 0.0 ? current : transport_->tempo(), Project::kMinSourceBpm, Project::kMaxSourceBpm, 2,
            &ok);
        if (ok)
            setAudioClipTempo(index, bpm);
    });
    menu.addSeparator();
    QAction* stems = menu.addAction(tr("Stem Extractor…"), this, [this, index] { extractStems(index); });
    stems->setEnabled(stemSeparator_ == nullptr || !stemSeparator_->isRunning());
    menu.exec(globalPos);
}

bool MainWindow::showStemModels()
{
    StemModelStore store(settings_.stemModel, settings_.stemUseGpu, this);
    connect(&store, &StemModelStore::useGpuChanged, this, [this](bool on) {
        settings_.stemUseGpu = on;
        settings_.save();
    });
    const auto keep = [this](const QString& id) {
        settings_.stemModel = id;
        settings_.save();
    };
    connect(&store, &StemModelStore::chosenChanged, this, keep);
    store.exec();
    if (store.chosen() != settings_.stemModel)
        keep(store.chosen()); // the saved one was not on this computer any more
    refreshStemSettings(); // what is downloaded and in use may have changed
    return !settings_.stemModel.isEmpty();
}

void MainWindow::extractStems(std::size_t index)
{
    if (index >= project_.audioClips().size())
        return;
    const AudioClip clip = project_.audioClips()[index];
    if (clip.channel >= project_.channels().size() || project_.channels()[clip.channel].samplePath.empty())
        return;
    if (stemSeparator_ != nullptr && stemSeparator_->isRunning()) {
        statusBar()->showMessage(tr("The stem extractor is still working on another clip."), 5000);
        return;
    }
    if (StemSeparator::program().isEmpty() || !stemSeparationSupported()) {
        QMessageBox::information(this, tr("Stem Extractor"),
            StemSeparator::program().isEmpty()
                ? tr("The stem separator is missing from this installation.")
                : tr("This processor is too old for the stem extractor: it needs AVX2 (2013 and later)."));
        return;
    }
    const auto modelInUse = [this]() -> const StemModel* {
        const StemModel* model = findStemModel(settings_.stemModel.toStdString());
        return model != nullptr && stemModelInstalled(*model, toPath(StemSeparator::modelFolder())) ? model
                                                                                                    : nullptr;
    };
    const StemModel* model = modelInUse();
    if (model == nullptr) {
        // None yet: the store, where the user picks and downloads one.
        if (!showStemModels())
            return;
        model = modelInUse();
        if (model == nullptr)
            return;
    }
    // A model from before the faster engine came: say so once, since nothing
    // else tells the user why it takes minutes.
    const bool fasterExists = std::ranges::any_of(
        stemModels(), [](const StemModel& other) { return other.engine == StemModel::Engine::Onnx; });
    if (model->engine == StemModel::Engine::Demucs && fasterExists && !settings_.stemFasterOffered) {
        settings_.stemFasterOffered = true;
        settings_.save();
        QMessageBox ask(QMessageBox::Question, tr("Stem Extractor"),
            tr("%1 runs on the processor only and takes several times as long as the audio lasts.\n\n"
               "The model store has faster ones; \"HT Demucs\" also runs on the graphics card.")
                .arg(QString::fromLatin1(model->name.data(), static_cast<qsizetype>(model->name.size()))),
            QMessageBox::NoButton, this);
        QAbstractButton* pick = ask.addButton(tr("Choose a Faster Model…"), QMessageBox::AcceptRole);
        ask.addButton(tr("Continue with This One"), QMessageBox::RejectRole);
        ask.exec();
        if (ask.clickedButton() == pick) {
            (void)showStemModels();
            model = modelInUse();
            if (model == nullptr)
                return;
        }
    }

    const QFileInfo source(QString::fromStdString(project_.channels()[clip.channel].samplePath));
    StemJob job;
    job.source = source.absoluteFilePath();
    job.baseName = source.completeBaseName();
    job.modelId = QString::fromLatin1(model->id.data(), static_cast<qsizetype>(model->id.size()));
    job.modelFolder = StemSeparator::modelFolder();
    job.useGpu = settings_.stemUseGpu;
    // demucs.cpp has its own, fixed overlap. Stems made with another overlap
    // are another set: they get their own folder, so neither is taken for the other.
    const bool overlaps = model->engine == StemModel::Engine::Onnx;
    job.overlapPercent = overlaps ? settings_.stemOverlap : 25;
    const QString set = job.overlapPercent == 25
        ? job.modelId
        : QStringLiteral("%1-overlap%2").arg(job.modelId).arg(job.overlapPercent);
    // Next to the file, so the stems travel with it; in the app's own folder
    // when that place cannot be written to (a read-only sample pack).
    const QString beside = QStringLiteral("%1/%2 - stems/%3").arg(source.absolutePath(), job.baseName, set);
    const QByteArray pathHash
        = QCryptographicHash::hash(job.source.toUtf8(), QCryptographicHash::Sha256).toHex().left(12);
    job.outputFolder = QDir().mkpath(beside) && QFileInfo(beside).isWritable()
        ? beside
        : QStringLiteral("%1/stems/%2-%3/%4")
              .arg(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation), job.baseName,
                  QString::fromLatin1(pathHash), set);

    // Extracted before with this model, and the file has not changed since: use those.
    QList<StemFile> ready;
    for (const std::string_view name : model->stems) {
        const QString stem = QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()));
        const QFileInfo file(StemSeparator::stemPath(job, stem));
        if (file.isFile() && file.size() > 0 && file.lastModified() >= source.lastModified())
            ready.push_back({stem, file.absoluteFilePath()});
    }
    if (ready.size() == static_cast<qsizetype>(model->stems.size())) {
        placeStems(clip, ready);
        return;
    }

    if (stemSeparator_ == nullptr)
        stemSeparator_ = new StemSeparator(this);
    const auto closeProgress = [this] {
        if (stemProgress_ != nullptr) {
            stemProgress_->deleteLater();
            stemProgress_ = nullptr;
        }
    };
    stemSeparator_->disconnect(this, nullptr); // the last job's finished / failed
    connect(stemSeparator_, &StemSeparator::progress, this, [this](double fraction, const QString& stage) {
        if (stemProgress_ == nullptr)
            return;
        stemProgress_->setValue(static_cast<int>(fraction * 1000.0));
        if (!stage.isEmpty())
            stemProgress_->setLabelText(
                stemProgress_->property("what").toString() + QLatin1Char('\n') + stage);
    });
    connect(stemSeparator_, &StemSeparator::finished, this,
        [this, clip, closeProgress](const QList<StemFile>& stems) {
            closeProgress();
            placeStems(clip, stems);
        });
    connect(stemSeparator_, &StemSeparator::deviceChanged, this, [this](bool, const QString& note) {
        if (!note.isEmpty())
            statusBar()->showMessage(note, 8000);
    });
    connect(stemSeparator_, &StemSeparator::failed, this, [this, closeProgress](const QString& reason) {
        closeProgress();
        QMessageBox::warning(this, tr("Stem Extractor"), reason);
    });

    const QString what
        = tr("Extracting stems from %1 with %2.\nYou can keep working meanwhile.")
              .arg(source.fileName(),
                  QString::fromLatin1(model->name.data(), static_cast<qsizetype>(model->name.size())));
    stemProgress_ = new QProgressDialog(what, tr("Cancel"), 0, 1000, this);
    stemProgress_->setProperty("what", what);
    stemProgress_->setWindowTitle(tr("Stem Extractor"));
    stemProgress_->setWindowModality(Qt::NonModal);
    stemProgress_->setAutoClose(false);
    stemProgress_->setAutoReset(false);
    stemProgress_->setMinimumDuration(0);
    stemProgress_->setMinimumWidth(420);
    connect(stemProgress_, &QProgressDialog::canceled, this, [this, closeProgress] {
        if (stemSeparator_ != nullptr)
            stemSeparator_->cancel();
        closeProgress();
        statusBar()->showMessage(tr("Stem extraction cancelled."), 4000);
    });
    stemProgress_->show();
    stemSeparator_->start(job);
}

void MainWindow::placeStems(const AudioClip& clip, const QList<StemFile>& stems, int next)
{
    if (next < stems.size()) {
        // One after another: each is decoded for the engine off this thread.
        const std::uint64_t gen = projectGeneration_;
        loadSampleThen(stems[next].path, [this, gen, clip, stems, next] {
            if (projectGeneration_ == gen)
                placeStems(clip, stems, next + 1);
        });
        return;
    }
    recordUndo(tr("Extract Stems"));
    int placed = 0;
    for (const StemFile& stem : stems) {
        const std::string file = QDir::cleanPath(stem.path).toStdString();
        if (clipAudio(file)->seconds <= 0.0) {
            clipAudio_.erase(file);
            continue;
        }
        std::size_t channel = 0;
        if (const auto existing = project_.channelWithSample(file)) {
            channel = *existing;
        } else {
            channel = project_.addChannel(QFileInfo(stem.path).completeBaseName().toStdString());
            project_.setChannelSample(channel, file);
            project_.setChannelColor(channel, trackPaletteColor(channel));
        }
        if (project_.channels()[channel].insert == 0)
            linkChannelToInsert(project_, mixer_, channel);
        // A track of its own, right under the clip's (and the stems before it).
        QString title = stem.stem;
        if (!title.isEmpty())
            title[0] = title[0].toUpper();
        int track = std::max(0, static_cast<int>(project_.tracks().size()) - 1);
        if (const auto added = project_.addTrack()) {
            const int desired = std::max(0, clip.track + 1 + placed);
            track = static_cast<int>(project_.moveTrack(*added, static_cast<std::size_t>(desired)));
            const auto trkIdx = static_cast<std::size_t>(track);
            project_.setTrackName(trkIdx, title.toStdString());
            project_.setTrackColor(trkIdx, project_.channels()[channel].color);
            project_.setTrackInsert(trkIdx, project_.channels()[channel].insert);
        }
        AudioClip copy = clip; // the same part of the song, fitted the same way
        copy.channel = channel;
        copy.track = track;
        copy.muted = false;
        project_.addAudioClip(copy);
        ++placed;
    }
    if (placed == 0) {
        statusBar()->showMessage(tr("The stems could not be loaded."), 6000);
        return;
    }
    // The clip itself goes quiet, unless the user wants it kept: its stems
    // play in its place. (Tracks added above did not move it: they went in
    // below it.)
    bool mutedClip = false;
    for (std::size_t i = 0; settings_.stemMuteOriginal && i < project_.audioClips().size() && !mutedClip; ++i)
        if (project_.audioClips()[i] == clip) {
            AudioClip muted = clip;
            muted.muted = true;
            project_.setAudioClip(i, muted);
            mutedClip = true;
        }
    syncMixerPanel();
    syncInsertNames();
    channelRack_->refresh();
    playlist_->refresh();
    rebuildClipSources();
    updateMixer();
    syncChannelRack();
    // Not muted: the user keeps it playing, or it was moved or changed
    // while its stems were made.
    statusBar()->showMessage(mutedClip
            ? tr("%n stem(s) placed under the clip; the clip itself is muted.", nullptr, placed)
            : tr("%n stem(s) placed where the clip was; mute the clip to hear them alone.", nullptr, placed),
        8000);
}

void MainWindow::setAudioClipTempo(std::size_t index, double bpm)
{
    if (index >= project_.audioClips().size())
        return;
    recordUndo(tr("Clip Tempo"));
    AudioClip clip = project_.audioClips()[index];
    // Offset and length count in the tempo the sample is read at: when that
    // changes, they scale so they cover the same part of the sample.
    const double before = clip.fitted() ? clip.sourceBpm : transport_->tempo();
    clip.sourceBpm = bpm;
    const double after = clip.fitted() ? clip.sourceBpm : transport_->tempo();
    const auto rescale = [&](int ticks) { return static_cast<int>(std::lround(ticks * after / before)); };
    clip.offset = rescale(clip.offset);
    clip.length = rescale(clip.length);
    project_.setAudioClip(index, clip);
    playlist_->refresh();
    if (songMode_)
        syncChannelRack();
}

void MainWindow::setAudioClipFit(std::size_t index, AudioClip::Fit fit)
{
    if (index >= project_.audioClips().size())
        return;
    const AudioClip clip = project_.audioClips()[index];
    const auto apply = [this, index](AudioClip::Fit mode, bool record) {
        if (index >= project_.audioClips().size())
            return;
        if (record)
            recordUndo(tr("Fit Clip"));
        AudioClip changed = project_.audioClips()[index];
        const double before = changed.fitted() ? changed.sourceBpm : transport_->tempo();
        changed.fit = mode;
        const double after = changed.fitted() ? changed.sourceBpm : transport_->tempo();
        const auto rescale = [&](int ticks) { return static_cast<int>(std::lround(ticks * after / before)); };
        changed.offset = rescale(changed.offset);
        changed.length = rescale(changed.length);
        project_.setAudioClip(index, changed);
        playlist_->refresh();
        if (songMode_)
            syncChannelRack();
    };
    if (fit == AudioClip::Fit::Off || clip.sourceBpm > 0.0 || clip.channel >= project_.channels().size()) {
        apply(fit, true);
        return;
    }
    // No tempo yet: measure it first.
    const std::size_t channel = clip.channel;
    detectTempoThen(project_.channels()[channel].samplePath, [this, index, channel, fit, apply](double bpm) {
        if (index >= project_.audioClips().size() || project_.audioClips()[index].channel != channel)
            return;
        if (bpm <= 0.0) {
            statusBar()->showMessage(
                tr("No steady beat found: set the tempo with Set Tempo…, then fit the clip."), 8000);
            return;
        }
        recordUndo(tr("Fit Clip"));
        AudioClip withTempo = project_.audioClips()[index];
        withTempo.sourceBpm = bpm; // not fitted yet: offset and length keep their meaning
        project_.setAudioClip(index, withTempo);
        apply(fit, false);
        statusBar()->showMessage(tr("Fitted from %1 BPM").arg(QString::number(bpm, 'f', 2)), 4000);
    });
}

void MainWindow::dropAudioFiles(const QStringList& paths, int track, int start)
{
    QStringList localPaths;
    localPaths.reserve(paths.size());
    for (const QString& p : paths) {
        if (isLocalPath(p))
            localPaths.push_back(p);
    }
    if (localPaths.isEmpty())
        return;

    recordUndo(tr("Drop Audio"));
    const std::uint64_t gen = projectGeneration_;
    const std::vector<int> tracks
        = project_.ensureDropTracks(track, static_cast<std::size_t>(localPaths.size()));
    std::size_t lastChannel = 0;

    for (qsizetype i = 0; i < localPaths.size(); ++i) {
        const QString& path = localPaths[i];
        const std::string file = QDir::cleanPath(path).toStdString();
        const std::string stemName = QFileInfo(path).completeBaseName().toStdString();
        const int targetTrack = tracks[static_cast<std::size_t>(i)];
        const auto trackIdx = static_cast<std::size_t>(targetTrack);

        // Bind 1:1 with the track and its mixer insert (reusing pre-created/named
        // inserts without leaving orphaned strips), then place the clip immediately.
        const std::size_t channel = bindAudioToTrack(project_, mixer_, trackIdx, file, stemName);
        lastChannel = channel;
        project_.addAudioClip(AudioClip{channel, targetTrack, start});

        loadSampleThen(
            path,
            [this, gen] {
                if (projectGeneration_ != gen)
                    return;
                playlist_->refresh();
                if (songMode_)
                    syncChannelRack();
            },
            [this, gen, channel, file] {
                if (projectGeneration_ != gen)
                    return;
                if (channel >= project_.channels().size() || project_.channels()[channel].samplePath != file)
                    return;
                for (std::size_t c = project_.audioClips().size(); c-- > 0;)
                    if (project_.audioClips()[c].channel == channel)
                        project_.removeAudioClip(c);
                playlist_->refresh();
                if (songMode_)
                    syncChannelRack();
            });
    }

    syncMixerPanel();
    syncInsertNames();
    channelRack_->refresh();
    playlist_->setSource(PlaylistView::ClipSource{true, lastChannel});
    rebuildClipSources();
    selectChannel(lastChannel);
    if (!tracks.empty()) {
        playlist_->setSelectedTrack(tracks.front());
        const int ins = trackMixerInsert(project_, mixer_, static_cast<std::size_t>(tracks.front()));
        if (ins >= 0 && static_cast<std::size_t>(ins) < mixerPanel_->trackCount())
            mixerPanel_->setSelectedTrack(static_cast<std::size_t>(ins));
    }
    playlist_->refresh();
    updateMixer();

    if (!songMode_) {
        // The playlist only plays in song mode: switch, or Play would stay silent.
        transport_->songModeAction()->setChecked(true); // syncs through songModeChanged
        statusBar()->showMessage(
            tr("Song mode on: Play runs through the playlist (L for pattern mode)."), 5000);
    } else {
        syncChannelRack();
    }
}

void MainWindow::handlePluginDroppedOnPlaylist(std::size_t pluginIndex, int track, int startTick)
{
    if (pluginIndex >= pluginStore_.plugins.size())
        return;
    const PluginInfo& info = pluginStore_.plugins[pluginIndex];
    const bool isInstrument = pluginMainGroup(info) == "Instrument";

    if (!isInstrument) {
        // Effect dropped onto the Playlist: attach it to the target track's Mixer Insert
        // (creating a 1:1 insert or FX channel if needed).
        if (track >= 0 && static_cast<std::size_t>(track) < project_.tracks().size()) {
            const auto trackIdx = static_cast<std::size_t>(track);
            int ins = trackMixerInsert(project_, mixer_, trackIdx);
            if (ins <= 0) {
                if (mixer_.insertCount() >= MixerModel::kMaxInserts) {
                    statusBar()->showMessage(
                        tr("The mixer has up to %1 channels.").arg(MixerModel::kMaxInserts), 4000);
                    return;
                }
                recordUndo(tr("Add Effect to Track"));
                ins = routeTrackToInsert(
                    project_, mixer_, trackIdx, static_cast<int>(mixer_.insertCount() + 1), true);
                syncMixerPanel();
                syncInsertNames();
            }
            if (ins > 0)
                addPluginToInsert(pluginIndex, static_cast<std::size_t>(ins));
        } else if (mixer_.insertCount() < MixerModel::kMaxInserts) {
            addMixerRoleChannel(MixerRole::Fx);
            if (mixerPanel_ != nullptr && mixerPanel_->selectedTrack() > 0)
                addPluginToInsert(pluginIndex, mixerPanel_->selectedTrack());
        }
        return;
    }

    if ((track < 0 || static_cast<std::size_t>(track) >= project_.tracks().size())
        && project_.tracks().size() >= static_cast<std::size_t>(Project::kMaxPlaylistTracks)) {
        statusBar()->showMessage(
            tr("The playlist has up to %1 tracks.").arg(Project::kMaxPlaylistTracks), 4000);
        return;
    }

    const auto slot = loadPlugin(pluginIndex, vloom::PluginInfo::Kind::Instrument);
    if (!slot)
        return;

    recordUndo(tr("Drop Instrument"));
    if (audio_.sampleRate() <= 0)
        unpublishedInstruments_.push_back(slot->instance);

    // Keep earlier unlinked tracks aligned 1:1 with Mixer Inserts first.
    for (std::size_t t = 0; t < project_.tracks().size(); ++t) {
        if (trackMixerInsert(project_, mixer_, t) < 0 && mixer_.insertCount() < MixerModel::kMaxInserts) {
            if (const auto ins = mixer_.addInsert())
                pairTrackWithInsert(project_, mixer_, t, *ins);
        }
    }

    std::size_t trackIdx = 0;
    std::size_t ch = 0;
    int targetIns = 0;

    if (track < 0 || static_cast<std::size_t>(track) >= project_.tracks().size()) {
        const auto addedTrk = project_.addTrack();
        if (!addedTrk)
            return;
        trackIdx = *addedTrk;
        const std::uint32_t col = trackPaletteColor(trackIdx);
        project_.setTrackName(trackIdx, slot->name);
        project_.setTrackColor(trackIdx, col);

        ch = project_.addChannel(slot->name);
        project_.setChannelColor(ch, col);
        project_.setChannelInstrument(ch, *slot);

        if (mixer_.insertCount() < MixerModel::kMaxInserts) {
            if (const auto newIns = mixer_.addInsert())
                targetIns = static_cast<int>(*newIns);
        }
        project_.setChannelInsert(ch, targetIns);
        project_.setTrackInsert(trackIdx, targetIns);
        if (targetIns > 0)
            pairTrackWithInsert(project_, mixer_, trackIdx, static_cast<std::size_t>(targetIns));
    } else {
        trackIdx = static_cast<std::size_t>(track);
        const PlaylistTrack& pt = project_.tracks()[trackIdx];
        targetIns = trackMixerInsert(project_, mixer_, trackIdx);
        if (targetIns <= 0 && mixer_.insertCount() < MixerModel::kMaxInserts) {
            if (const auto newIns = mixer_.addInsert()) {
                targetIns = static_cast<int>(*newIns);
                pairTrackWithInsert(project_, mixer_, trackIdx, *newIns);
            }
        }

        // Reuse an existing dedicated non-sample channel on this track/insert if present.
        std::optional<std::size_t> reusableCh;
        if (const auto prim = trackPrimaryChannel(project_, trackIdx);
            prim && project_.channels()[*prim].samplePath.empty()) {
            reusableCh = *prim;
        } else if (targetIns > 0) {
            for (std::size_t c = 4; c < project_.channels().size(); ++c) {
                if (project_.channels()[c].insert == targetIns && project_.channels()[c].samplePath.empty()) {
                    reusableCh = c;
                    break;
                }
            }
        }

        std::string prevInstName;
        if (reusableCh) {
            ch = *reusableCh;
            if (project_.channels()[ch].instrument.has_value())
                prevInstName = project_.channels()[ch].instrument->name;
            project_.setChannelName(ch, slot->name);
            project_.setChannelInstrument(ch, *slot);
        } else {
            ch = project_.addChannel(slot->name);
            const std::uint32_t col = pt.color ? *pt.color : trackPaletteColor(trackIdx);
            project_.setChannelColor(ch, col);
            project_.setChannelInstrument(ch, *slot);
        }

        if (targetIns > 0) {
            project_.setChannelInsert(ch, targetIns);
            project_.setTrackInsert(trackIdx, targetIns);
        }
        if (!pt.color)
            syncTrackColor(project_, mixer_, trackIdx, trackPaletteColor(trackIdx));
        if (!pt.named || (!prevInstName.empty() && pt.name == prevInstName))
            syncTrackName(project_, mixer_, trackIdx, slot->name);
        else if (targetIns > 0)
            pairTrackWithInsert(project_, mixer_, trackIdx, static_cast<std::size_t>(targetIns));
    }

    // Ensure a dedicated clean pattern clip is placed on the track so MIDI notes play immediately.
    const int clipStart = std::max(0, startTick);
    bool hasClipOnTrack = false;
    for (const Clip& existing : project_.clips()) {
        if (existing.track == static_cast<int>(trackIdx)) {
            hasClipOnTrack = true;
            break;
        }
    }
    std::size_t targetPattern = currentPattern_;
    if (!hasClipOnTrack) {
        const bool curUsedElsewhere = !project_.isPatternEmpty(currentPattern_)
            || std::any_of(project_.clips().begin(), project_.clips().end(), [&](const Clip& c) {
                   return c.pattern == currentPattern_ && c.track != static_cast<int>(trackIdx);
               });
        if (curUsedElsewhere) {
            if (const auto emptyPat = project_.findEmptyPattern(0))
                targetPattern = *emptyPat;
            else if (project_.patterns().size() < Project::kMaxPatterns)
                targetPattern = project_.addPattern();
        }
        project_.addClip(Clip{targetPattern, static_cast<int>(trackIdx), clipStart});
    }

    currentPattern_ = std::min(targetPattern, project_.patterns().size() - 1);
    syncMixerPanel();
    syncInsertNames();
    syncPatternSelector();
    channelRack_->refresh();
    pianoRoll_->refresh();
    playlist_->refresh();
    selectPattern(currentPattern_);
    selectChannel(ch);
    playlist_->setSelectedTrack(static_cast<int>(trackIdx));
    if (targetIns > 0 && static_cast<std::size_t>(targetIns) < mixerPanel_->trackCount())
        mixerPanel_->setSelectedTrack(static_cast<std::size_t>(targetIns));
    updateMixer();

    if (!songMode_)
        transport_->songModeAction()->setChecked(true);
    else
        syncChannelRack();

    releaseUnusedPlugins();
    openPluginEditor(slot->instance, QString::fromStdString(slot->name));
}

void MainWindow::renamePlaylistTrack(std::size_t track)
{
    if (track >= project_.tracks().size())
        return;
    const PlaylistTrack& t = project_.tracks()[track];
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Track %1").arg(track + 1),
        tr("Name (leave empty for “Track %1”):").arg(track + 1), QLineEdit::Normal,
        t.named ? QString::fromStdString(t.name) : QString(), &ok);
    if (!ok)
        return;
    recordUndo(tr("Rename Track"));
    syncTrackName(project_, mixer_, track, name.toStdString());
    syncMixerPanel();
    syncInsertNames();
    playlist_->refresh();
}

void MainWindow::showTrackChannelMenu(std::size_t track, const QPoint& globalPos)
{
    if (track >= project_.tracks().size())
        return;
    QMenu menu(this);
    const auto primary = trackPrimaryChannel(project_, track);
    if (primary) {
        const Channel& ch = project_.channels()[*primary];
        const QString chName = QString::fromStdString(ch.name);
        menu.addAction(tr("Select “%1” in Channel Rack").arg(chName), this, [this, chIdx = *primary] {
            selectChannel(chIdx);
            showWorkspace(WorkspacePage::ChannelRack);
        });
        menu.addAction(tr("Open “%1” in Piano Roll").arg(chName), this, [this, chIdx = *primary] {
            selectChannel(chIdx);
            showWorkspace(WorkspacePage::PianoRoll);
        });
        if (ch.instrument && pluginHost_.instance(ch.instrument->instance) != nullptr) {
            menu.addAction(tr("Open Instrument Editor…"), this,
                [this, handle = ch.instrument->instance, chName] { openPluginEditor(handle, chName); });
        }
        if (ch.samplePath.empty()) {
            menu.addAction(Theme::icon(QStringLiteral("track-instrument")),
                ch.instrument ? tr("Replace Instrument Plug-in…") : tr("Load Instrument Plug-in…"), this,
                [this, chIdx = *primary, globalPos] { showInstrumentChooserMenu(chIdx, globalPos); });
        }
        menu.addSeparator();
    }

    QMenu* placeMenu = menu.addMenu(tr("Place Channel on Track"));
    for (std::size_t c = 0; c < project_.channels().size(); ++c) {
        const Channel& ch = project_.channels()[c];
        QAction* act = placeMenu->addAction(QString::fromStdString(ch.name), this, [this, track, c] {
            recordUndo(tr("Place Channel"));
            const int trackIns = project_.tracks()[track].insert;
            if (trackIns > 0 && project_.channels()[c].insert == 0)
                project_.setChannelInsert(c, trackIns);
            const Channel& targetCh = project_.channels()[c];
            if (!targetCh.samplePath.empty()) {
                project_.addAudioClip(AudioClip{c, static_cast<int>(track), 0});
                playlist_->setSource(PlaylistView::ClipSource{true, c});
                rebuildClipSources();
            } else {
                const std::size_t pat = currentPattern_;
                if (project_.isPatternEmpty(pat))
                    project_.setStep(pat, c, 0, true);
                project_.addClip(Clip{pat, static_cast<int>(track), 0});
            }
            selectChannel(c);
            playlist_->refresh();
            if (songMode_)
                syncChannelRack();
        });
        act->setCheckable(true);
        act->setChecked(primary == c);
    }

    menu.addAction(tr("Import Audio to This Track…"), this, [this, track] {
        const QStringList files
            = QFileDialog::getOpenFileNames(this, tr("Import Audio to Track"), projectFolder(),
                tr("Audio files (*.wav *.mp3 *.flac *.ogg *.aif *.aiff *.m4a *.opus);;All files (*)"));
        if (!files.isEmpty()) {
            rememberFolder(files.front());
            dropAudioFiles(files, static_cast<int>(track), 0);
        }
    });
    menu.exec(globalPos);
}

void MainWindow::showTrackRoutingMenu(std::size_t track, const QPoint& globalPos)
{
    if (track >= project_.tracks().size())
        return;

    const int currentIns = trackMixerInsert(project_, mixer_, track);
    QMenu menu(this);

    const auto applyRoute = [this, track](int targetInsert) {
        recordUndo(tr("Route Track"));
        routeTrackToInsert(project_, mixer_, track, targetInsert, true);
        syncMixerPanel();
        syncInsertNames();
        channelRack_->refresh();
        playlist_->updateHeaders();
        updateMixer();
        syncChannelRack();
    };

    QAction* masterAct = menu.addAction(tr("Master (Direct)"), this, [applyRoute] { applyRoute(0); });
    masterAct->setCheckable(true);
    masterAct->setChecked(currentIns == 0);
    menu.addSeparator();

    for (std::size_t i = 1; i < mixer_.size(); ++i) {
        const MixerTrack& mt = mixer_.track(i);
        const QString label = mt.named ? tr("Insert %1 · %2").arg(i).arg(QString::fromStdString(mt.name))
                                       : tr("Insert %1").arg(i);
        QAction* act = menu.addAction(label, this, [applyRoute, i] { applyRoute(static_cast<int>(i)); });
        act->setCheckable(true);
        act->setChecked(currentIns == static_cast<int>(i));
    }

    menu.addSeparator();
    if (mixer_.insertCount() < MixerModel::kMaxInserts) {
        menu.addAction(tr("+ New Dedicated Insert"), this, [this, track] {
            recordUndo(tr("New Insert"));
            auto ch = trackPrimaryChannel(project_, track);
            if (!ch) {
                const PlaylistTrack& pt = project_.tracks()[track];
                const std::size_t added = project_.addChannel(pt.name);
                project_.setChannelColor(added, pt.color ? pt.color : trackPaletteColor(track));
                ch = added;
            }
            if (const auto addedIns = mixer_.addInsert()) {
                project_.setChannelInsert(*ch, static_cast<int>(*addedIns));
                pairTrackWithInsert(project_, mixer_, track, *addedIns);
            }
            syncMixerPanel();
            syncInsertNames();
            channelRack_->refresh();
            playlist_->updateHeaders();
            updateMixer();
            syncChannelRack();
        });
    }
    menu.addAction(tr("Open Mixer"), this, [this] { setMixerVisible(true); });
    menu.exec(globalPos);
}

void MainWindow::removeMixerInsert(std::size_t track)
{
    if (track == MixerModel::kMaster || track >= mixer_.size())
        return;
    const auto insert = static_cast<int>(track);
    const QString stripName = QString::fromStdString(mixer_.track(track).name);

    std::vector<std::size_t> pairedTracks;
    std::size_t totalClips = 0;
    for (std::size_t t = 0; t < project_.tracks().size(); ++t) {
        if (trackMixerInsert(project_, mixer_, t) == insert) {
            pairedTracks.push_back(t);
            totalClips += project_.clipsOnTrack(t);
        }
    }

    std::size_t upstreamFeeders = 0;
    for (std::size_t i = 1; i < mixer_.size(); ++i) {
        if (i != track && mixer_.track(i).output == insert)
            ++upstreamFeeders;
    }

    if (totalClips > 0 || upstreamFeeders > 0) {
        QString prompt;
        if (!pairedTracks.empty() && totalClips > 0) {
            prompt = tr("Delete “%1” and its %n playlist track(s) (%2 clip(s))?", "",
                static_cast<int>(pairedTracks.size()))
                         .arg(stripName)
                         .arg(totalClips);
        } else {
            prompt = tr("%n channel(s) route into “%1”. Delete it? They will return to Main.", "",
                static_cast<int>(upstreamFeeders))
                         .arg(stripName);
        }
        if (QMessageBox::question(this, tr("Delete Channel"), prompt) != QMessageBox::Yes)
            return;
    }

    recordUndo(tr("Delete Channel"));

    // Remove all paired Playlist tracks (highest index down to lowest so indices stay valid).
    for (auto it = pairedTracks.rbegin(); it != pairedTracks.rend(); ++it) {
        if (*it < project_.tracks().size())
            project_.removeTrack(*it);
    }

    // Release any instrument/sample on channels that belonged exclusively to this deleted channel/track.
    const auto isChannelStillUsed = [this](std::size_t chIdx) {
        for (const AudioClip& ac : project_.audioClips()) {
            if (ac.channel == chIdx)
                return true;
        }
        for (const Clip& pc : project_.clips()) {
            if (pc.pattern < project_.patterns().size()
                && chIdx < project_.patterns()[pc.pattern].channels.size()) {
                const ChannelPattern& cp = project_.patterns()[pc.pattern].channels[chIdx];
                if (!cp.notes.empty()
                    || std::any_of(cp.steps.begin(), cp.steps.end(), [](bool on) { return on; })) {
                    return true;
                }
            }
        }
        return false;
    };
    for (std::size_t c = 0; c < project_.channels().size(); ++c) {
        if (project_.channels()[c].insert == insert && !isChannelStillUsed(c)) {
            project_.setChannelInstrument(c, std::nullopt);
            project_.setChannelSample(c, {});
        }
    }

    project_.removeInsert(insert);
    mixer_.removeInsert(track);

    syncMixerPanel();
    syncInsertNames();
    channelRack_->refresh();
    rebuildClipSources();
    playlist_->refresh();
    updateMixer();
    syncChannelRack();
    releaseUnusedPlugins();
    syncEffectCounts();
    statusBar()->showMessage(tr("“%1” deleted.").arg(stripName), 3000);
}

void MainWindow::selectChannel(std::size_t channel)
{
    currentChannel_ = std::min(channel, project_.channels().size() - 1);
    channelRack_->setSelectedChannel(currentChannel_);
    pianoRoll_->setTarget(currentPattern_, currentChannel_);
    if (mixerPanel_ != nullptr && currentChannel_ < project_.channels().size()) {
        const int ins = project_.channels()[currentChannel_].insert;
        if (ins >= 0 && static_cast<std::size_t>(ins) < mixerPanel_->trackCount())
            mixerPanel_->setSelectedTrack(static_cast<std::size_t>(ins));
    }
    updatePanelContexts();
}

void MainWindow::updatePanelContexts()
{
    const QString pattern = QString::fromStdString(project_.patterns()[currentPattern_].name);
    const QString channel = QString::fromStdString(project_.channels()[currentChannel_].name);
    channelRackFrame_->setContext(pattern);
    rebuildPianoRollChannels();
    QString prContext = QStringLiteral("%1  ·  %2").arg(pattern, channel);
    if (!pianoRollSelectionText_.isEmpty())
        prContext += QStringLiteral("   [%1]").arg(pianoRollSelectionText_);
    else if (!pianoRollToolHint_.isEmpty())
        prContext += QStringLiteral("   ·   %1").arg(pianoRollToolHint_);
    pianoRollFrame_->setContext(prContext);
}

void MainWindow::syncPatternSelector()
{
    QStringList names;
    for (const Pattern& pattern : project_.patterns())
        names << QString::fromStdString(pattern.name);
    transport_->patternSelector()->setPatterns(names, static_cast<int>(currentPattern_));
}

void MainWindow::patternsChanged(std::size_t select)
{
    currentPattern_ = std::min(select, project_.patterns().size() - 1);
    channelRack_->refresh();
    pianoRoll_->refresh();
    playlist_->refresh();
    selectPattern(currentPattern_); // selector, editors' target, engine
}

bool MainWindow::roomForPatterns(std::size_t more)
{
    if (project_.patterns().size() + more <= Project::kMaxPatterns)
        return true;
    statusBar()->showMessage(tr("A project can have up to %1 patterns.").arg(Project::kMaxPatterns), 5000);
    return false;
}

void MainWindow::findEmptyPattern(bool fromStart, bool askName)
{
    std::optional<std::size_t> found = project_.findEmptyPattern(fromStart ? 0 : currentPattern_ + 1);
    if (!found) {
        if (!roomForPatterns(1))
            return;
        recordUndo(tr("Add Pattern"));
        found = project_.addPattern();
    }
    patternsChanged(*found);
    if (askName)
        renameCurrentPattern();
}

void MainWindow::renameCurrentPattern()
{
    const QString current = QString::fromStdString(project_.patterns()[currentPattern_].name);
    bool ok = false;
    const QString name
        = QInputDialog::getText(this, tr("Rename Pattern"), tr("Name:"), QLineEdit::Normal, current, &ok)
              .trimmed();
    if (!ok || name.isEmpty() || name == current)
        return;
    recordUndo(tr("Rename Pattern"));
    project_.setPatternName(currentPattern_, name.left(64).toStdString());
    patternsChanged(currentPattern_); // the selector, panel titles and playlist clips show it
}

void MainWindow::transposeCurrentPattern()
{
    if (project_.patterns()[currentPattern_].channels.empty())
        return;
    bool ok = false;
    const int semitones = QInputDialog::getInt(this, tr("Transpose Pattern"),
        tr("Semitones (every note of \u201C%1\u201D):")
            .arg(QString::fromStdString(project_.patterns()[currentPattern_].name)),
        0, -48, 48, 1, &ok);
    if (!ok || semitones == 0)
        return;
    recordUndo(tr("Transpose Pattern"));
    project_.transposePattern(currentPattern_, semitones);
    patternsChanged(currentPattern_);
}

void MainWindow::insertPattern()
{
    if (roomForPatterns(1)) {
        recordUndo(tr("Insert Pattern"));
        patternsChanged(project_.insertPattern(currentPattern_ + 1));
    }
}

void MainWindow::cloneCurrentPattern()
{
    if (!roomForPatterns(1))
        return;
    recordUndo(tr("Clone Pattern"));
    if (const auto copy = project_.clonePattern(currentPattern_))
        patternsChanged(*copy);
}

void MainWindow::deleteCurrentPattern()
{
    if (project_.patterns().size() <= 1)
        return;
    const QString name = QString::fromStdString(project_.patterns()[currentPattern_].name);
    if (!project_.isPatternEmpty(currentPattern_)) {
        const auto clips = std::count_if(project_.clips().begin(), project_.clips().end(),
            [this](const Clip& c) { return c.pattern == currentPattern_; });
        const QString detail = clips > 0
            ? tr("Its steps, notes and %n playlist clip(s) are removed.", "", static_cast<int>(clips))
            : tr("Its steps and notes are removed.");
        if (QMessageBox::question(this, tr("Delete Pattern"),
                tr("Delete \u201C%1\u201D?").arg(name) + QStringLiteral("\n\n") + detail)
            != QMessageBox::Yes)
            return;
    }
    recordUndo(tr("Delete Pattern"));
    project_.removePattern(currentPattern_);
    patternsChanged(currentPattern_ > 0 ? currentPattern_ - 1 : 0);
}

void MainWindow::moveCurrentPattern(int by)
{
    const auto count = static_cast<long long>(project_.patterns().size());
    const long long to = static_cast<long long>(currentPattern_) + by;
    if (to < 0 || to >= count)
        return;
    recordUndo(tr("Move Pattern"));
    patternsChanged(project_.movePattern(currentPattern_, static_cast<std::size_t>(to)));
}

void MainWindow::splitCurrentPatternByChannel()
{
    std::size_t used = 0;
    for (const ChannelPattern& cp : project_.patterns()[currentPattern_].channels)
        if (!cp.notes.empty() || std::find(cp.steps.begin(), cp.steps.end(), true) != cp.steps.end())
            ++used;
    if (used == 0) {
        statusBar()->showMessage(tr("This pattern is empty: nothing to split."), 4000);
        return;
    }
    if (!roomForPatterns(used))
        return;
    recordUndo(tr("Split Pattern"));
    const auto parts = project_.splitPatternByChannel(currentPattern_);
    patternsChanged(currentPattern_);
    statusBar()->showMessage(
        tr("Split into %n pattern(s), one per channel.", "", static_cast<int>(parts.size())), 4000);
}

void MainWindow::refreshEditors()
{
    currentPattern_ = std::min(currentPattern_, project_.patterns().size() - 1);
    currentChannel_ = std::min(currentChannel_, project_.channels().size() - 1);
    channelRack_->refresh();
    pianoRoll_->refresh();
    playlist_->refresh();
    selectPattern(currentPattern_);
}

void MainWindow::showWorkspace(WorkspacePage page)
{
    // A detached editor lives in its own window; its workspace page is only
    // a placeholder, so show the empty state instead.
    if (isPageDetached(page))
        page = WorkspacePage::Empty;
    workspace_->setCurrentIndex(static_cast<int>(page));
    updateViewLights();
}

void MainWindow::updateViewLights()
{
    const auto current = static_cast<WorkspacePage>(workspace_->currentIndex());
    const auto open = [this, current](WorkspacePage page) {
        const DetachablePage* d = detachable(page);
        if (d != nullptr && isPageDetached(page))
            return d->window->isVisible();
        return current == page;
    };
    auto* views = transport_->viewButtons();
    views->setViewOpen(ViewButtons::View::ChannelRack, open(WorkspacePage::ChannelRack));
    views->setViewOpen(ViewButtons::View::PianoRoll, open(WorkspacePage::PianoRoll));
    views->setViewOpen(ViewButtons::View::Playlist, open(WorkspacePage::Playlist));
}

void MainWindow::toggleWorkspace(WorkspacePage page)
{
    if (DetachablePage* d = detachable(page); d != nullptr && isPageDetached(page)) {
        setFloatingVisible(d->window, !d->window->isVisible());
        updateViewLights();
        return;
    }
    const bool showing = workspace_->currentIndex() == static_cast<int>(page);
    showWorkspace(showing ? WorkspacePage::Empty : page);
}

// ---------------------------------------------------------------- detaching

void MainWindow::setFloatingVisible(FloatingPanel* window, bool visible)
{
    window->setVisible(visible);
    if (visible) {
        window->raise();
        window->activateWindow();
    }
}

FloatingPanel* MainWindow::makeFloatingPanel(
    const QString& title, const QString& settingsKey, std::function<void()> onClosed)
{
    auto* window = new FloatingPanel(title, settingsKey);
    window->setWindowIcon(windowIcon());
    // Share File/Edit/Pattern window shortcuts so Ctrl+Z, Ctrl+Y, Delete,
    // Ctrl+A/C/X/V/D, Ctrl+S etc. work when a detached panel is active.
    window->addActions(QList<QAction*>(sharedWindowActions_.begin(), sharedWindowActions_.end()));
    if (!window->hasSavedGeometry()) {
        // First time: over the main window, each new one a step down-right.
        const int step = 32 * floatingWindowsCreated_++;
        window->move(geometry().topLeft() + QPoint(120 + step, 140 + step));
    }
    connect(window, &FloatingPanel::closed, this, std::move(onClosed));
    return window;
}

bool MainWindow::isMixerDetached() const
{
    return mixerWindow_ != nullptr && mixerWindow_->content() == mixerPanel_;
}

bool MainWindow::isMixerOpen() const
{
    return isMixerDetached() ? mixerWindow_->isVisible() : !mixerPanel_->isHidden();
}

void MainWindow::setMixerDetached(bool detached)
{
    if (detached == isMixerDetached())
        return;
    const bool open = isMixerOpen();
    if (detached) {
        if (mixerWindow_ == nullptr)
            mixerWindow_ = makeFloatingPanel(tr("Mixer"), QString(kMixerWindowKey),
                [this] { transport_->viewButtons()->setViewOpen(ViewButtons::View::Mixer, false); });
        // Takes it out of the splitter; the window buttons join its header.
        mixerWindow_->host(mixerPanel_, [this](QWidget* buttons) { mixerPanel_->addHeaderWidget(buttons); });
    } else {
        QWidget* panel = mixerWindow_->release();
        workspaceSplitter_->insertWidget(1, panel);
        workspaceSplitter_->setSizes(
            {std::max(200, workspaceSplitter_->height() - kDefaultMixerHeight), kDefaultMixerHeight});
    }
    mixerPanel_->setDetached(detached);
    setMixerVisible(open || detached); // detaching shows the window
}

MainWindow::DetachablePage* MainWindow::detachable(WorkspacePage page)
{
    for (DetachablePage& d : detachable_)
        if (d.page == page && d.frame != nullptr)
            return &d;
    return nullptr;
}

const MainWindow::DetachablePage* MainWindow::detachable(WorkspacePage page) const
{
    for (const DetachablePage& d : detachable_)
        if (d.page == page && d.frame != nullptr)
            return &d;
    return nullptr;
}

void MainWindow::makeDetachable(DetachablePage& d, WorkspacePage page, PanelFrame* frame,
    const QString& title, const QString& name, const QString& settingsId)
{
    d.page = page;
    d.frame = frame;
    d.title = title;
    d.settingsId = settingsId;

    d.button = new DetachButton(name);
    frame->addHeaderWidget(d.button);
    connect(d.button, &QToolButton::clicked, this,
        [this, page] { setPageDetached(page, !isPageDetached(page)); });

    // Stands in the workspace while the editor is in its own window, so the
    // page numbering stays put.
    d.placeholder = new QWidget(this);
    d.placeholder->setObjectName(QStringLiteral("emptyState"));
    auto* layout = new QVBoxLayout(d.placeholder);
    layout->addStretch();
    auto* text = new QLabel(tr("The %1 is in its own window.").arg(name), d.placeholder);
    text->setObjectName(QStringLiteral("emptyHint"));
    text->setAlignment(Qt::AlignCenter);
    layout->addWidget(text);
    auto* dockBack = new QPushButton(tr("Dock it back"), d.placeholder);
    connect(dockBack, &QPushButton::clicked, this, [this, page] { setPageDetached(page, false); });
    layout->addWidget(dockBack, 0, Qt::AlignHCenter);
    layout->addStretch();
    d.placeholder->hide();
}

bool MainWindow::pianoRollActive() const
{
    // Docked, it shows only as the workspace's page; detached, its window is in front.
    return pianoRoll_->isVisible() && pianoRoll_->window()->isActiveWindow();
}

void MainWindow::stepNoteHistory(bool redo)
{
    const bool any = redo ? noteHistory_.canRedo() : noteHistory_.canUndo();
    const auto target = redo ? noteHistory_.redo(project_) : noteHistory_.undo(project_);
    if (!target) {
        statusBar()->showMessage(!any
                ? (redo ? tr("Nothing to redo.") : tr("Nothing to undo."))
                : tr("The notes were changed elsewhere since: the undo history was cleared."),
            4000);
        return;
    }
    // Show the notes it changed.
    if (target->pattern != currentPattern_)
        selectPattern(target->pattern);
    if (target->channel != currentChannel_)
        selectChannel(target->channel);
    pianoRoll_->refresh();
    playlist_->refresh();
    syncChannelRack();
}

bool MainWindow::isPageDetached(WorkspacePage page) const
{
    const DetachablePage* d = detachable(page);
    return d != nullptr && d->window != nullptr && d->window->content() == d->frame;
}

void MainWindow::setPageDetached(WorkspacePage page, bool detached, bool showWindow)
{
    DetachablePage* d = detachable(page);
    if (d == nullptr || detached == isPageDetached(page))
        return;
    const int index = static_cast<int>(page);
    if (detached) {
        const bool wasShowing = workspace_->currentIndex() == index;
        if (d->window == nullptr)
            d->window = makeFloatingPanel(d->title, QStringLiteral("detached/%1Geometry").arg(d->settingsId),
                [this] { updateViewLights(); });
        workspace_->removeWidget(d->frame);
        workspace_->insertWidget(index, d->placeholder);
        PanelFrame* frame = d->frame;
        d->window->host(frame, [frame](QWidget* buttons) { frame->addHeaderWidget(buttons); });
        d->button->setDetached(true);
        setFloatingVisible(d->window, showWindow);
        if (wasShowing)
            workspace_->setCurrentIndex(static_cast<int>(WorkspacePage::Empty));
    } else {
        QWidget* frame = d->window->release();
        workspace_->removeWidget(d->placeholder);
        d->placeholder->setParent(this);
        d->placeholder->hide();
        workspace_->insertWidget(index, frame);
        d->button->setDetached(false);
        workspace_->setCurrentIndex(index);
    }
    updateViewLights();
}

void MainWindow::showView(ViewButtons::View view)
{
    switch (view) {
    case ViewButtons::View::Browser: {
        // Same as clicking the browser icon on the activity bar.
        const int page = activityBar_->current() == 0 ? -1 : 0;
        activityBar_->setCurrent(page);
        showSidePanel(page);
        return;
    }
    case ViewButtons::View::ChannelRack:
        toggleWorkspace(WorkspacePage::ChannelRack);
        return;
    case ViewButtons::View::PianoRoll:
        toggleWorkspace(WorkspacePage::PianoRoll);
        return;
    case ViewButtons::View::Playlist:
        toggleWorkspace(WorkspacePage::Playlist);
        return;
    case ViewButtons::View::Mixer:
        setMixerVisible(!isMixerOpen());
        return;
    }
}

void MainWindow::setMixerVisible(bool visible)
{
    if (isMixerDetached()) {
        mixerPanel_->show();
        setFloatingVisible(mixerWindow_, visible);
    } else {
        mixerPanel_->setVisible(visible);
    }
    transport_->viewButtons()->setViewOpen(ViewButtons::View::Mixer, visible);
}

QWidget* MainWindow::createMixer()
{
    mixerPanel_ = new MixerPanel(mixer_.size() - 1, this);
    mixerPanel_->setMinimumHeight(396);
    connect(mixerPanel_, &MixerPanel::closeRequested, this, [this] { setMixerVisible(false); });
    connect(
        mixerPanel_, &MixerPanel::detachRequested, this, [this] { setMixerDetached(!isMixerDetached()); });

    connect(mixerPanel_, &MixerPanel::faderChanged, this, [this](std::size_t track, double position) {
        recordUndo(tr("Mixer Fader"), "mixer.fader." + std::to_string(track));
        mixer_.setFader(track, position);
        if (track != MixerModel::kMaster) {
            if (const auto trk = insertPrimaryTrack(project_, mixer_, track))
                project_.setTrackVolume(*trk, position);
        }
        updateMixer();
        if (playlist_ != nullptr)
            playlist_->updateHeaders();
    });
    connect(mixerPanel_, &MixerPanel::panChanged, this, [this](std::size_t track, double pan) {
        recordUndo(tr("Mixer Pan"), "mixer.pan." + std::to_string(track));
        mixer_.setPan(track, pan);
        if (track != MixerModel::kMaster) {
            if (const auto trk = insertPrimaryTrack(project_, mixer_, track))
                project_.setTrackPan(*trk, pan);
        }
        updateMixer();
        if (playlist_ != nullptr)
            playlist_->updateHeaders();
    });
    connect(mixerPanel_, &MixerPanel::muteToggled, this, [this](std::size_t track, bool muted) {
        recordUndo(tr("Mute Track"));
        if (track == MixerModel::kMaster)
            mixer_.setMuted(track, muted);
        else
            syncInsertMute(project_, mixer_, track, muted);
        updateMixer();
        if (playlist_ != nullptr)
            playlist_->viewport()->update();
        if (songMode_)
            syncChannelRack();
    });
    connect(mixerPanel_, &MixerPanel::addInsertRequested, this, &MainWindow::addMixerInsert);
    connect(mixerPanel_, &MixerPanel::addChannelRoleRequested, this, &MainWindow::addMixerRoleChannel);
    connect(mixerPanel_, &MixerPanel::addBusForTrackRequested, this, &MainWindow::addBusForMixerTrack);
    connect(mixerPanel_, &MixerPanel::addFxSendForTrackRequested, this, &MainWindow::addFxSendForMixerTrack);
    connect(mixerPanel_, &MixerPanel::addTracksDialogRequested, this, [this] { showAddTracksDialog(-1); });
    connect(
        mixerPanel_, &MixerPanel::addAudioTrackRequested, this, [this] { addQuickAudioTrack(-1, false); });
    connect(
        mixerPanel_, &MixerPanel::addInstrumentTrackRequested, this, [this] { addQuickInstrumentTrack(-1); });
    connect(
        mixerPanel_, &MixerPanel::copyChannelSettingsRequested, this, &MainWindow::copyMixerChannelSettings);
    connect(mixerPanel_, &MixerPanel::pasteChannelSettingsRequested, this,
        &MainWindow::pasteMixerChannelSettings);
    connect(mixerPanel_, &MixerPanel::instrumentClicked, this, &MainWindow::openMixerInstrumentEditor);
    connect(mixerPanel_, &MixerPanel::instrumentMenuRequested, this, &MainWindow::showMixerInstrumentMenu);
    connect(mixerPanel_, &MixerPanel::sourceClicked, this, &MainWindow::showMixerSourceMenu);
    connect(mixerPanel_, &MixerPanel::outputRoutingRequested, this, &MainWindow::showMixerOutputMenu);
    connect(mixerPanel_, &MixerPanel::addSendMenuRequested, this, &MainWindow::showMixerAddSendMenu);
    connect(mixerPanel_, &MixerPanel::sendLevelChanged, this,
        [this](std::size_t track, std::size_t slot, double level) {
            recordUndo(tr("Send Level"), "mixer.send." + std::to_string(track) + "." + std::to_string(slot));
            mixer_.setSendLevel(track, slot, level);
            updateMixer();
        });
    connect(mixerPanel_, &MixerPanel::sendMuteToggled, this,
        [this](std::size_t track, std::size_t slot, bool muted) {
            recordUndo(tr("Mute Send"));
            mixer_.setSendMuted(track, slot, muted);
            syncMixerPanel();
            updateMixer();
        });
    connect(mixerPanel_, &MixerPanel::sendPreFaderToggled, this,
        [this](std::size_t track, std::size_t slot, bool preFader) {
            recordUndo(tr("Send Pre-Fader"));
            mixer_.setSendPreFader(track, slot, preFader);
            syncMixerPanel();
            updateMixer();
        });
    connect(mixerPanel_, &MixerPanel::sendMenuRequested, this, &MainWindow::showMixerSendMenu);
    connect(mixerPanel_, &MixerPanel::removeInsertRequested, this, &MainWindow::removeMixerInsert);
    connect(mixerPanel_, &MixerPanel::renameRequested, this, &MainWindow::renameMixerInsert);
    connect(mixerPanel_, &MixerPanel::trackNameEdited, this, [this](std::size_t track, const QString& name) {
        if (track == MixerModel::kMaster || track >= mixer_.size())
            return;
        recordUndo(tr("Rename Track"));
        syncInsertName(project_, mixer_, track, name.toStdString());
        syncMixerPanel();
        syncInsertNames();
        if (playlist_ != nullptr)
            playlist_->refresh();
    });
    connect(mixerPanel_, &MixerPanel::monoToggled, this, [this](std::size_t track, bool mono) {
        if (track >= mixer_.size())
            return;
        recordUndo(tr("Toggle Mono"));
        mixer_.setMono(track, mono);
        syncMixerPanel();
        updateMixer();
        if (playlist_ != nullptr)
            playlist_->updateHeaders();
    });
    connect(mixerPanel_, &MixerPanel::colorChanged, this,
        [this](std::size_t track, std::optional<std::uint32_t> color) {
            if (track >= mixer_.size())
                return;
            recordUndo(tr("Track Color"));
            syncInsertColor(project_, mixer_, track, color);
            syncMixerPanel();
            channelRack_->update();
            pianoRoll_->viewport()->update();
            if (playlist_ != nullptr)
                playlist_->viewport()->update();
        });
    connect(mixerPanel_, &MixerPanel::moveInsertRequested, this, &MainWindow::moveMixerInsertTo);
    connect(mixerPanel_, &MixerPanel::effectsMenuRequested, this, &MainWindow::showEffectsMenu);
    connect(mixerPanel_, &MixerPanel::effectBypassToggled, this,
        [this](std::size_t track, std::size_t slot, bool bypassed) {
            setEffectBypassed(track, slot, bypassed);
        });
    connect(mixerPanel_, &MixerPanel::effectClicked, this, [this](std::size_t track, std::size_t slot) {
        if (track >= mixer_.size())
            return;
        if (slot < mixer_.track(track).effects.size()) {
            const auto& effect = mixer_.track(track).effects[slot];
            openPluginEditor(effect.instance,
                tr("%1 - %2").arg(
                    QString::fromStdString(effect.name), QString::fromStdString(mixer_.track(track).name)));
        } else {
            showEffectChooserMenu(track, slot, QCursor::pos());
        }
    });
    connect(mixerPanel_, &MixerPanel::effectMenuRequested, this, &MainWindow::showEffectSlotContextMenu);
    connect(mixerPanel_, &MixerPanel::pluginDroppedOnSlot, this, &MainWindow::handlePluginDroppedOnSlot);
    connect(mixerPanel_, &MixerPanel::pluginDropped, this, [this](std::size_t track, std::size_t plugin) {
        if (plugin < pluginStore_.plugins.size()
            && pluginMainGroup(pluginStore_.plugins[plugin]) == "Instrument" && track > 0
            && track < mixer_.size()) {
            if (const auto paired = insertPrimaryTrack(project_, mixer_, track)) {
                handlePluginDroppedOnPlaylist(plugin, static_cast<int>(*paired), 0);
                return;
            }
            handlePluginDroppedOnPlaylist(plugin, static_cast<int>(project_.tracks().size()), 0);
            return;
        }
        addPluginToInsert(plugin, track);
    });
    connect(mixerPanel_, &MixerPanel::soloToggled, this, [this](std::size_t track, bool soloed) {
        recordUndo(tr("Solo Track"));
        syncInsertSolo(project_, mixer_, track, soloed);
        syncMixerPanel();
        updateMixer();
        if (playlist_ != nullptr)
            playlist_->viewport()->update();
        if (songMode_)
            syncChannelRack();
    });
    connect(mixerPanel_, &MixerPanel::trackSelected, this, [this](std::size_t mixerTrack) {
        if (const auto ch = insertPrimaryChannel(project_, mixerTrack))
            selectChannel(*ch);
        mixerPanel_->setSelectedTrack(mixerTrack);
        if (playlist_ != nullptr) {
            if (const auto trk = insertPrimaryTrack(project_, mixer_, mixerTrack))
                playlist_->setSelectedTrack(static_cast<int>(*trk));
        }
    });
    connect(mixerPanel_, &MixerPanel::clearAllSoloRequested, this, [this] {
        bool anySolo = mixer_.anySoloed();
        for (const PlaylistTrack& t : project_.tracks())
            anySolo = anySolo || t.solo;
        if (!anySolo)
            return;
        recordUndo(tr("Clear Solo"));
        for (std::size_t i = 1; i < mixer_.size(); ++i)
            if (mixer_.track(i).soloed)
                syncInsertSolo(project_, mixer_, i, false);
        for (std::size_t t = 0; t < project_.tracks().size(); ++t)
            if (project_.tracks()[t].solo)
                syncTrackSolo(project_, mixer_, t, false);
        syncMixerPanel();
        updateMixer();
        if (playlist_ != nullptr)
            playlist_->viewport()->update();
        if (songMode_)
            syncChannelRack();
        statusBar()->showMessage(tr("Cleared all soloed tracks"), 3000);
    });
    connect(mixerPanel_, &MixerPanel::clearAllMuteRequested, this, [this] {
        bool anyMuted = mixer_.track(MixerModel::kMaster).muted;
        for (std::size_t i = 1; i < mixer_.size() && !anyMuted; ++i)
            anyMuted = mixer_.track(i).muted;
        for (const PlaylistTrack& t : project_.tracks())
            anyMuted = anyMuted || t.muted;
        if (!anyMuted)
            return;
        recordUndo(tr("Clear Mute"));
        mixer_.setMuted(MixerModel::kMaster, false);
        for (std::size_t i = 1; i < mixer_.size(); ++i)
            if (mixer_.track(i).muted)
                syncInsertMute(project_, mixer_, i, false);
        for (std::size_t t = 0; t < project_.tracks().size(); ++t)
            if (project_.tracks()[t].muted)
                syncTrackMute(project_, mixer_, t, false);
        syncMixerPanel();
        updateMixer();
        if (playlist_ != nullptr)
            playlist_->viewport()->update();
        if (songMode_)
            syncChannelRack();
        statusBar()->showMessage(tr("Cleared all muted tracks"), 3000);
    });
    connect(mixerPanel_, &MixerPanel::bypassAllEffectsToggled, this, [this](bool bypassed) {
        allEffectsBypassed_ = bypassed;
        updateMixer();
        refreshOpenPluginEditors();
        statusBar()->showMessage(
            bypassed ? tr("All mixer effects bypassed") : tr("Mixer effects active"), 3000);
    });
    syncEffectCounts();
    mixerPanel_->hide();
    return mixerPanel_;
}

MixerModel MainWindow::effectiveMixerModel() const
{
    if (!allEffectsBypassed_)
        return mixer_;
    MixerModel copy = mixer_;
    for (std::size_t t = 0; t < copy.size(); ++t)
        for (std::size_t s = 0; s < copy.track(t).effects.size(); ++s)
            copy.setEffectBypassed(t, s, true);
    return copy;
}

float MainWindow::effectiveMasterGain() const
{
    if (transport_ != nullptr && transport_->masterMuted())
        return 0.0f;
    constexpr double kDimLinear = 0.17782794100389228; // -15 dB
    double gain = volumeToGain(transport_ != nullptr ? transport_->masterVolume() : 100.0)
        * mixer_.effectiveGain(MixerModel::kMaster);
    if (transport_ != nullptr && transport_->masterDimmed())
        gain *= kDimLinear;
    return static_cast<float>(gain);
}

void MainWindow::updateMixer()
{
    // Grey out channels silenced by solo (respecting Bus & FX solo-safe propagation).
    const bool solo = mixer_.anySoloed();
    for (std::size_t i = 1; i < mixer_.size(); ++i)
        mixerPanel_->setSilenced(i, solo && !mixer_.isTrackAudible(i));
    updateMasterOutput();
    const MixerModel effective = effectiveMixerModel();
    audio_.engine().setMixerState(engineMixerState(effective, pluginHost_.resolver()));
}

void MainWindow::updateMasterOutput()
{
    // The engine has one output stage: the transport's master volume/mute/dim and
    // the mixer's master track (fader, mute, pan) both feed it.
    pendingGain_ = effectiveMasterGain();
    pendingPan_ = static_cast<float>(mixer_.track(MixerModel::kMaster).pan);
}

void MainWindow::createSettingsDialog()
{
    settingsDialog_ = new SettingsDialog(this);

    // Every change applies now and is saved at once.
    auto* audio = settingsDialog_->audioPage();
    audio->setFormat(settings_.sampleRate, settings_.bufferFrames);
    // `index` is into the list the page shows, which is deviceNames_.
    connect(audio, &AudioSettingsPage::deviceSelected, this, &MainWindow::selectOutputDevice);
    connect(audio, &AudioSettingsPage::formatChanged, this, &MainWindow::changeAudioFormat);

    // Theme, in two steps so knobs stay responsive:
    //  1. at once, every edit: new colours for everything painted in code
    //     (LCDs, meters, editors, knobs, icons) - under a millisecond;
    //  2. once the edits pause for kThemeApplyMs: the full restyle that the
    //     style-sheet surfaces (panels, buttons) need - about 150 ms.
    themeApplyTimer_ = new QTimer(this);
    themeApplyTimer_->setSingleShot(true);
    themeApplyTimer_->setInterval(kThemeApplyMs);
    connect(themeApplyTimer_, &QTimer::timeout, this, &MainWindow::applyTheme);
    ThemeSettingsPage* theme = settingsDialog_->themePage();
    theme->setChoice(settings_.theme);
    connect(theme, &ThemeSettingsPage::choiceChanged, this, [this](const ThemeChoice& choice) {
        settings_.theme = choice;
        previewTheme();
        themeApplyTimer_->start(); // restarts: applies once the edits pause
    });

    auto* general = settingsDialog_->generalPage();
    general->setStartPositionMode(static_cast<int>(settings_.startPositionMode));
    general->setMetronomeOnStartup(settings_.metronomeOnStartup);
    general->setAssociateProjectFiles(settings_.associateProjectFiles);
    connect(general, &GeneralSettingsPage::startPositionModeChanged, this, [this](int mode) {
        settings_.startPositionMode
            = mode == 1 ? AppSettings::PositionMode::Time : AppSettings::PositionMode::BarsBeats;
        settings_.save();
    });
    connect(general, &GeneralSettingsPage::metronomeOnStartupChanged, this, [this](bool on) {
        settings_.metronomeOnStartup = on;
        settings_.save();
    });
    connect(general, &GeneralSettingsPage::associateProjectFilesChanged, this, [this](bool on) {
        settings_.associateProjectFiles = on;
        settings_.save();
#if defined(Q_OS_WIN)
        setWindowsProjectFileAssociation(QCoreApplication::applicationFilePath(), on);
#endif
    });

    StemSettingsPage* stems = settingsDialog_->stemPage();
    refreshStemSettings();
    connect(stems, &StemSettingsPage::modelChosen, this, [this](const QString& id) {
        settings_.stemModel = id;
        settings_.save();
    });
    connect(stems, &StemSettingsPage::useGpuChanged, this, [this](bool on) {
        settings_.stemUseGpu = on;
        settings_.save();
    });
    connect(stems, &StemSettingsPage::overlapChanged, this, [this](int percent) {
        settings_.stemOverlap = percent;
        settings_.save();
    });
    connect(stems, &StemSettingsPage::muteOriginalChanged, this, [this](bool on) {
        settings_.stemMuteOriginal = on;
        settings_.save();
    });
    connect(stems, &StemSettingsPage::storeRequested, this, [this] { (void)showStemModels(); });
}

void MainWindow::refreshStemSettings()
{
    if (settingsDialog_ == nullptr)
        return;
    QStringList installed;
    const std::filesystem::path folder = toPath(StemSeparator::modelFolder());
    for (const StemModel& model : stemModels())
        if (stemModelInstalled(model, folder))
            installed << QString::fromLatin1(model.id.data(), static_cast<qsizetype>(model.id.size()));
    StemSettingsPage* page = settingsDialog_->stemPage();
    page->setModels(installed, settings_.stemModel);
    page->setUseGpu(settings_.stemUseGpu);
    page->setOverlap(settings_.stemOverlap);
    page->setMuteOriginal(settings_.stemMuteOriginal);
}

void MainWindow::previewTheme()
{
    Theme::select(settings_.theme);
    // Custom-painted widgets and themed icons read the colours when they
    // paint: schedule a repaint of every window, the detached ones too.
    for (QWidget* window : QApplication::topLevelWidgets())
        window->update();
}

void MainWindow::applyTheme()
{
    settings_.save();
    Theme::select(settings_.theme);
    Theme::apply(*qApp); // restyles the style-sheet surfaces
    for (QWidget* window : QApplication::topLevelWidgets())
        window->update();
}

void MainWindow::applyStartupSettings()
{
    audio_.setFormat(settings_.sampleRate, settings_.bufferFrames);
    transport_->metronomeAction()->setChecked(settings_.metronomeOnStartup);
    transport_->setPositionShowsTime(settings_.startPositionMode == AppSettings::PositionMode::Time);
}

void MainWindow::createCentralArea()
{
    // [activity bar][ side panel | workspace ]; the splitter lets the user
    // resize the side panel, the activity bar opens and closes it.
    auto* root = new QWidget(this);
    auto* rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    activityBar_ = new ActivityBar(root);
    rootLayout->addWidget(activityBar_);

    splitter_ = new QSplitter(Qt::Horizontal, root);
    splitter_->setObjectName(QStringLiteral("sidebarSplitter"));
    splitter_->setChildrenCollapsible(false);
    splitter_->setHandleWidth(5);
    rootLayout->addWidget(splitter_, 1);

    sidePanels_ = new QStackedWidget(splitter_);
    sidePanels_->setMinimumWidth(190);
    sidePanels_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    splitter_->addWidget(sidePanels_);

    // Pages: the activity bar index equals the stack index.
    browser_ = new BrowserPanel(sidePanels_);
    browser_->setFolders(settings_.browserFolders, settings_.browserFolder);
    connect(browser_, &BrowserPanel::foldersChanged, this,
        [this](const QStringList& folders, const QString& current) {
            settings_.browserFolders = folders;
            settings_.browserFolder = current;
            settings_.save();
        });
    sidePanels_->addWidget(browser_);

    previewer_ = new SamplePreviewer(audio_.engine(), this);
    previewer_->setSampleRate(audio_.sampleRate());
    browser_->setAutoPreview(settings_.browserAutoPreview);
    connect(browser_, &BrowserPanel::autoPreviewChanged, this, [this](bool on) {
        settings_.browserAutoPreview = on;
        settings_.save();
    });
    connect(browser_, &BrowserPanel::previewRequested, previewer_, &SamplePreviewer::play);
    connect(browser_, &BrowserPanel::previewToggled, this, [this](const QString& path) {
        if (previewer_->playingPath() == path)
            previewer_->stop();
        else
            previewer_->play(path);
    });
    connect(browser_, &BrowserPanel::previewStopRequested, previewer_, &SamplePreviewer::stop);
    connect(browser_, &BrowserPanel::fileShown, previewer_, &SamplePreviewer::show);
    connect(previewer_, &SamplePreviewer::failed, this, [this](const QString& path, const QString& error) {
        browser_->waveform()->setFailed(path, error);
        statusBar()->showMessage(tr("Cannot preview %1: %2").arg(QFileInfo(path).fileName(), error), 6000);
    });
    // The preview strip under the browser: waveform, play / stop.
    connect(previewer_, &SamplePreviewer::loaded, browser_->waveform(), &SampleWaveform::setWaveform);
    connect(browser_->waveform(), &SampleWaveform::playRequested, previewer_, &SamplePreviewer::play);
    connect(browser_->waveform(), &SampleWaveform::stopRequested, previewer_, &SamplePreviewer::stop);
    activityBar_->addPage(Theme::icon(QStringLiteral("folder")), tr("Browser"));

    // Plugins: only what the user's last scan found; nothing scans on its own.
    pluginStore_ = PluginStore::load();
    pluginStore_.setBuiltins(builtinPlugins()); // the effects that come with the app
    pluginsPanel_ = new PluginsPanel(sidePanels_);
    pluginsPanel_->setStore(pluginStore_);
    connect(pluginsPanel_, &PluginsPanel::managerRequested, this, &MainWindow::showPluginManager);
    connect(pluginsPanel_, &PluginsPanel::userCategoryChosen, this,
        [this](std::size_t index, const QString& category, const QString& type) {
            if (index >= pluginStore_.plugins.size())
                return;
            PluginInfo& plugin = pluginStore_.plugins[index];
            plugin.userCategory = category.toStdString();
            plugin.userInstrumentType
                = category == QLatin1String("Instrument") ? type.toStdString() : std::string();
            pluginStore_.save();
            pluginsPanel_->setStore(pluginStore_);
            if (pluginManager_ != nullptr)
                pluginManager_->setStore(pluginStore_.scanned()); // its next scan starts from this
        });
    connect(pluginsPanel_, &PluginsPanel::favoriteToggled, this, [this](std::size_t index, bool favorite) {
        if (index >= pluginStore_.plugins.size())
            return;
        pluginStore_.plugins[index].favorite = favorite;
        pluginStore_.save();
        pluginsPanel_->setStore(pluginStore_);
        if (pluginManager_ != nullptr)
            pluginManager_->setStore(pluginStore_.scanned());
    });
    connect(pluginsPanel_, &PluginsPanel::addToRackRequested, this,
        [this](std::size_t plugin) { addPluginToRack(plugin, project_.channels().size()); });
    connect(pluginsPanel_, &PluginsPanel::addToMixerRequested, this, &MainWindow::chooseInsertForPlugin);
    sidePanels_->addWidget(pluginsPanel_);
    activityBar_->addPage(Theme::icon(QStringLiteral("plug")), tr("Plugins"));

    activityBar_->addBottomAction(closeProjectAction_);
    activityBar_->addBottomAction(settingsAction_);
    connect(activityBar_, &ActivityBar::currentChanged, this, &MainWindow::showSidePanel);

    // Workspace above, mixer below (F9); the user drags the split.
    workspaceSplitter_ = new QSplitter(Qt::Vertical, splitter_);
    workspaceSplitter_->setObjectName(QStringLiteral("workspaceSplitter"));
    workspaceSplitter_->setChildrenCollapsible(false);
    workspaceSplitter_->setHandleWidth(4);
    workspaceSplitter_->setMinimumWidth(360);
    workspaceSplitter_->addWidget(createWorkspace());
    workspaceSplitter_->addWidget(createMixer());
    workspaceSplitter_->setStretchFactor(0, 1);
    workspaceSplitter_->setStretchFactor(1, 0);
    workspaceSplitter_->setSizes({400, kDefaultMixerHeight});
    splitter_->addWidget(workspaceSplitter_);
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 1);
    splitter_->setSizes({kDefaultSidebarWidth, 1000});

    setCentralWidget(root);
}

void MainWindow::showSidePanel(int index)
{
    sidePanels_->setVisible(index >= 0);
    if (index >= 0)
        sidePanels_->setCurrentIndex(index);
    transport_->viewButtons()->setViewOpen(ViewButtons::View::Browser, index == 0);
}

QWidget* MainWindow::createWorkspace()
{
    // One editor at a time, chosen with the view buttons (F5 / F6 / F7);
    // the order matches WorkspacePage.
    workspace_ = new QStackedWidget(this);
    workspace_->addWidget(createEmptyState());

    // Channel rack.
    channelRack_ = new ChannelRackView;
    channelRack_->setProject(&project_);
    auto* rackScroll = new QScrollArea;
    rackScroll->setObjectName(QStringLiteral("channelRackScroll"));
    rackScroll->setWidget(channelRack_);
    rackScroll->setFrameShape(QFrame::NoFrame);
    channelRackFrame_ = new PanelFrame(tr("CHANNEL RACK"), rackScroll, workspace_);
    auto* addChannel = new QToolButton;
    addChannel->setObjectName(QStringLiteral("panelButton"));
    addChannel->setIcon(Theme::icon(QStringLiteral("plus")));
    addChannel->setText(tr("Channel"));
    addChannel->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    addChannel->setToolTip(tr("Add a channel"));
    addChannel->setFocusPolicy(Qt::NoFocus);
    const auto headerButton = [](const QString& icon, const QString& text, const QString& tip) {
        auto* button = new QToolButton;
        button->setObjectName(QStringLiteral("panelButton"));
        button->setIcon(Theme::icon(icon));
        button->setText(text);
        button->setToolButtonStyle(text.isEmpty() ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
        button->setProperty("iconOnly", text.isEmpty()); // even padding (Theme)
        button->setToolTip(tip);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    };
    auto* removeBar = headerButton(
        QStringLiteral("minus"), QString(), tr("Remove the last bar of steps from this pattern"));
    auto* addBar = headerButton(QStringLiteral("plus"), tr("Bar"),
        tr("Add a bar: %1 step pads more for every channel in this pattern").arg(kStepsPerBar));
    connect(removeBar, &QToolButton::clicked, this, [this] { changeBars(-1); });
    connect(addBar, &QToolButton::clicked, this, [this] { changeBars(1); });
    // "+ Bar" and "−" together, like the playlist's "+ Track" and "−".
    channelRackFrame_->addHeaderWidget(makePanelButtonGroup({addBar, removeBar}));
    channelRackFrame_->addHeaderWidget(addChannel);
    workspace_->addWidget(channelRackFrame_);
    connect(addChannel, &QToolButton::clicked, this, [this] {
        recordUndo(tr("Add Channel"));
        const auto index
            = project_.addChannel(tr("Channel %1").arg(project_.channels().size() + 1).toStdString());
        project_.setChannelColor(index, trackPaletteColor(index));
        linkChannelToInsert(project_, mixer_, index);
        syncMixerPanel();
        syncInsertNames();
        channelRack_->refresh();
        selectChannel(index);
        updateMixer();
        syncChannelRack();
    });
    connect(channelRack_, &ChannelRackView::stepSet, this, [this](std::size_t channel, int step, bool on) {
        if (project_.step(currentPattern_, channel, step) == on)
            return;
        recordUndo(tr("Edit Steps"), "steps." + std::to_string(currentPattern_));
        project_.setStep(currentPattern_, channel, step, on);
        channelRack_->update();
        playlist_->viewport()->update();
        syncChannelRack();
    });
    connect(channelRack_, &ChannelRackView::channelColorChanged, this,
        [this](std::size_t channel, std::optional<std::uint32_t> color) {
            recordUndo(tr("Channel Color"));
            project_.setChannelColor(channel, color);
            if (channel < project_.channels().size()) {
                const int ins = project_.channels()[channel].insert;
                if (ins > 0 && static_cast<std::size_t>(ins) < mixer_.size()) {
                    const bool shared = std::any_of(
                        project_.channels().begin(), project_.channels().end(), [&](const Channel& c) {
                            return &c != &project_.channels()[channel] && c.insert == ins;
                        });
                    if (!shared)
                        syncInsertColor(project_, mixer_, static_cast<std::size_t>(ins), color);
                }
            }
            syncMixerPanel();
            channelRack_->update();
            pianoRoll_->viewport()->update(); // its notes take the channel's colour
            if (playlist_ != nullptr)
                playlist_->viewport()->update();
        });
    connect(channelRack_, &ChannelRackView::muteToggled, this, [this](std::size_t channel) {
        recordUndo(tr("Mute Channel"));
        project_.setChannelMuted(channel, !project_.channels()[channel].muted);
        channelRack_->update();
        syncChannelRack();
        refreshOpenPluginEditors();
    });
    connect(channelRack_, &ChannelRackView::volumeChanged, this, [this](std::size_t channel, double volume) {
        recordUndo(tr("Channel Volume"), "ch.vol." + std::to_string(channel));
        project_.setChannelVolume(channel, volume);
        channelRack_->update();
        if (playlist_ != nullptr)
            playlist_->updateHeaders();
        scheduleChannelRackSync();
    });
    connect(channelRack_, &ChannelRackView::panChanged, this, [this](std::size_t channel, double pan) {
        recordUndo(tr("Channel Pan"), "ch.pan." + std::to_string(channel));
        project_.setChannelPan(channel, pan);
        channelRack_->update();
        if (playlist_ != nullptr)
            playlist_->updateHeaders();
        scheduleChannelRackSync();
    });
    connect(channelRack_, &ChannelRackView::insertChanged, this, [this](std::size_t channel, int insert) {
        recordUndo(tr("Channel Output"));
        project_.setChannelInsert(channel, insert);
        syncMixerPanel();
        channelRack_->update();
        if (playlist_ != nullptr)
            playlist_->updateHeaders();
        syncChannelRack();
        refreshOpenPluginEditors();
    });
    syncInsertNames();
    // Long files take a while to decode and resample: off the UI thread.
    connect(channelRack_, &ChannelRackView::sampleDropped, this,
        [this](std::size_t channel, const QStringList& paths) {
            const std::uint64_t gen = projectGeneration_;
            if (paths.size() == 1) {
                const QString& path = paths.front();
                if (!isLocalPath(path))
                    return;
                loadSampleThen(path, [this, gen, channel, path] {
                    if (projectGeneration_ == gen)
                        assignSample(channel, path);
                });
                return;
            }
            for (const QString& path : paths) {
                if (!isLocalPath(path))
                    continue;
                loadSampleThen(path, [this, gen, path] {
                    if (projectGeneration_ == gen)
                        assignSample(project_.channels().size(), path);
                });
            }
        });
    connect(channelRack_, &ChannelRackView::channelSelected, this, &MainWindow::selectChannel);
    connect(channelRack_, &ChannelRackView::addBarRequested, this, [this] { changeBars(1); });
    connect(channelRack_, &ChannelRackView::pluginDropped, this,
        [this](std::size_t channel, std::size_t plugin) { addPluginToRack(plugin, channel); });
    connect(channelRack_, &ChannelRackView::channelOpened, this, [this](std::size_t channel) {
        selectChannel(channel);
        // A plugin channel opens its plugin's editor, as in FL Studio.
        if (channel < project_.channels().size()) {
            const Channel& c = project_.channels()[channel];
            // Its own editor, or the one the app has for a built-in.
            const auto instance = c.instrument ? pluginHost_.instance(c.instrument->instance) : nullptr;
            if (instance != nullptr
                && (instance->processor().hasEditor() || !instance->processor().parameters().empty())) {
                openPluginEditor(c.instrument->instance, QString::fromStdString(c.name));
                return;
            }
        }
        showWorkspace(WorkspacePage::PianoRoll);
    });

    // Piano roll.
    pianoRoll_ = new PianoRollView;
    pianoRoll_->setProject(&project_);
    pianoRollFrame_ = new PanelFrame(tr("PIANO ROLL"), pianoRoll_, workspace_);
    workspace_->addWidget(pianoRollFrame_);
    {
        // Active MIDI channel switcher + 1-click Instrument plugin editor / picker.
        pianoRollChannel_ = new ClipSourceBox([this] { rebuildPianoRollChannels(); }, pianoRollFrame_);
        pianoRollChannel_->setObjectName(QStringLiteral("pianoRollChannel"));
        pianoRollChannel_->setToolTip(tr("Active channel edited in the Piano Roll"));
        pianoRollChannel_->setAccessibleName(tr("Piano Roll Channel"));
        pianoRollChannel_->setFocusPolicy(Qt::NoFocus);
        pianoRollChannel_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        pianoRollChannel_->setMinimumContentsLength(8);
        pianoRollChannel_->setMaximumWidth(200);
        if (QAbstractItemView* popupView = pianoRollChannel_->view())
            popupView->setMinimumWidth(260);
        connect(pianoRollChannel_, &QComboBox::activated, this, [this](int row) {
            const QVariant v = pianoRollChannel_->itemData(row);
            if (v.isValid())
                selectChannel(static_cast<std::size_t>(v.toULongLong()));
        });
        pianoRollFrame_->addHeaderWidget(pianoRollChannel_);

        auto* instBtn = headerButton(QStringLiteral("plug"), tr("Inst"),
            tr("Open this channel's Instrument editor (or load an Instrument plug-in; right-click to "
               "replace)"));
        instBtn->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(instBtn, &QToolButton::clicked, this, [this, instBtn] {
            if (currentChannel_ >= project_.channels().size())
                return;
            const Channel& c = project_.channels()[currentChannel_];
            const auto instance = c.instrument ? pluginHost_.instance(c.instrument->instance) : nullptr;
            if (instance != nullptr
                && (instance->processor().hasEditor() || !instance->processor().parameters().empty())) {
                openPluginEditor(c.instrument->instance, QString::fromStdString(c.name));
                return;
            }
            showInstrumentChooserMenu(currentChannel_, instBtn->mapToGlobal(QPoint(0, instBtn->height())));
        });
        connect(instBtn, &QWidget::customContextMenuRequested, this, [this, instBtn](const QPoint& pos) {
            if (currentChannel_ < project_.channels().size())
                showInstrumentChooserMenu(currentChannel_, instBtn->mapToGlobal(pos));
        });
        pianoRollFrame_->addHeaderWidget(instBtn);

        // Studio One-style MIDI editing tools (Select is default so plain left-drag marquee-selects notes).
        struct PianoToolInfo {
            PianoRollView::Tool tool;
            const char* icon;
            QString name;
            QKeySequence key;
            QString hint;
        };
        const PianoToolInfo prTools[] = {
            {PianoRollView::Tool::Select, "tool-select", tr("Select"), QKeySequence(Qt::Key_E),
                tr("Select: drag a box to select notes, drag up/down to adjust pitch (double-click "
                   "inserts)")},
            {PianoRollView::Tool::Draw, "tool-draw", tr("Draw"), QKeySequence(Qt::Key_P),
                tr("Draw: click/drag to place notes, drag a note to move or trim (Ctrl+drag box-selects)")},
            {PianoRollView::Tool::Paint, "tool-paint", tr("Paint"), QKeySequence(Qt::Key_B),
                tr("Paint: drag horizontally to lay repeated notes at each snap step")},
            {PianoRollView::Tool::Slice, "tool-slice", tr("Slice"), QKeySequence(Qt::Key_C),
                tr("Slice: click or drag across notes to split them at the cut line (Alt: free)")},
            {PianoRollView::Tool::Delete, "tool-delete", tr("Delete"), QKeySequence(Qt::Key_D),
                tr("Delete: click or drag over notes to erase them")},
            {PianoRollView::Tool::Playback, "tool-playback", tr("Playback"), QKeySequence(Qt::Key_Y),
                tr("Playback: hold to audition from the pointer, drag to scrub")},
        };
        auto* prGroup = new QActionGroup(pianoRollFrame_);
        QList<QToolButton*> prToolButtons;
        for (const PianoToolInfo& info : prTools) {
            auto* action
                = new QAction(Theme::icon(QString::fromLatin1(info.icon)), info.name, pianoRollFrame_);
            action->setCheckable(true);
            action->setChecked(info.tool == PianoRollView::Tool::Select);
            action->setShortcut(info.key);
            action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            action->setToolTip(
                QStringLiteral("%1 (%2)").arg(info.name, info.key.toString(QKeySequence::NativeText)));
            pianoRollFrame_->addAction(action);
            prGroup->addAction(action);
            auto* btn = new QToolButton;
            btn->setDefaultAction(action);
            btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
            btn->setFocusPolicy(Qt::NoFocus);
            prToolButtons.append(btn);
            connect(action, &QAction::triggered, this, [this, tool = info.tool, hint = info.hint] {
                pianoRoll_->setTool(tool);
                pianoRollToolHint_ = hint;
                updatePanelContexts();
            });
        }
        pianoRollToolHint_ = prTools[0].hint;
        pianoRollFrame_->addHeaderWidget(makePanelButtonGroup(prToolButtons));

        // Pitch / Transpose & MIDI Action buttons (acts on selected notes, or all notes if none selected).
        const auto textSegBtn = [this](const QString& label, const QString& tip) {
            auto* b = new QToolButton(pianoRollFrame_);
            b->setText(label);
            b->setFocusPolicy(Qt::NoFocus);
            b->setToolTip(tip);
            return b;
        };
        auto* octDownBtn = textSegBtn(
            tr("-Oct"), tr("Transpose -1 octave (-12 semitones; Shift+Down or Alt+Shift+Wheel)"));
        auto* semiDownBtn
            = textSegBtn(tr("-1"), tr("Transpose -1 semitone (Down arrow, drag vertically, or Alt+Wheel)"));
        auto* semiUpBtn
            = textSegBtn(tr("+1"), tr("Transpose +1 semitone (Up arrow, drag vertically, or Alt+Wheel)"));
        auto* octUpBtn
            = textSegBtn(tr("+Oct"), tr("Transpose +1 octave (+12 semitones; Shift+Up or Alt+Shift+Wheel)"));
        auto* actionBtn = textSegBtn(
            tr("Action ▾"), tr("MIDI note actions: Transpose, Quantize, Legato, Snap to Scale, Velocity"));
        connect(octDownBtn, &QToolButton::clicked, this, [this] { pianoRoll_->transpose(-12); });
        connect(semiDownBtn, &QToolButton::clicked, this, [this] { pianoRoll_->transpose(-1); });
        connect(semiUpBtn, &QToolButton::clicked, this, [this] { pianoRoll_->transpose(1); });
        connect(octUpBtn, &QToolButton::clicked, this, [this] { pianoRoll_->transpose(12); });
        connect(actionBtn, &QToolButton::clicked, this, [this, actionBtn] {
            QMenu menu(this);
            menu.addAction(tr("Transpose Custom Semitones…"), this, [this] {
                bool ok = false;
                const int st = QInputDialog::getInt(
                    this, tr("Transpose Notes"), tr("Semitones (-48 to +48):"), 0, -48, 48, 1, &ok);
                if (ok && st != 0)
                    pianoRoll_->transpose(st);
            });
            menu.addAction(tr("Transpose +1 Octave (+12 st)"), this, [this] { pianoRoll_->transpose(12); });
            menu.addAction(tr("Transpose -1 Octave (-12 st)"), this, [this] { pianoRoll_->transpose(-12); });
            menu.addSeparator();
            menu.addAction(tr("Quantize Note Starts\tCtrl+Q"), this, [this] { pianoRoll_->quantize(); });
            menu.addAction(tr("Legato (Extend to Next Note)"), this, [this] { pianoRoll_->applyLegato(); });
            menu.addAction(tr("Snap Pitch to Scale"), this, [this] { pianoRoll_->snapToScale(); });
            menu.addSeparator();
            menu.addAction(tr("Velocity +15%"), this, [this] { pianoRoll_->scaleVelocity(1.15); });
            menu.addAction(tr("Velocity -15%"), this, [this] { pianoRoll_->scaleVelocity(0.85); });
            menu.addSeparator();
            menu.addAction(tr("Select All\tCtrl+A"), this, [this] { pianoRoll_->selectAll(); });
            menu.addAction(tr("Duplicate\tCtrl+D"), this, [this] { pianoRoll_->duplicateSelection(); });
            menu.addAction(tr("Delete Selected\tDel"), this, [this] { pianoRoll_->deleteSelection(); });
            menu.exec(actionBtn->mapToGlobal(QPoint(0, actionBtn->height())));
        });
        pianoRollFrame_->addHeaderWidget(
            makePanelButtonGroup({octDownBtn, semiDownBtn, semiUpBtn, octUpBtn, actionBtn}));

        // What notes snap to while placed, moved or resized (Alt: no snap).
        auto* snap = new QComboBox(pianoRollFrame_);
        snap->setObjectName(QStringLiteral("pianoRollSnap"));
        snap->setFocusPolicy(Qt::NoFocus);
        snap->setToolTip(tr("Snap: what notes line up to when placed, moved or resized (Alt: no snap)"));
        snap->setAccessibleName(tr("Snap"));
        snap->addItem(tr("Snap: Beat"), kTicksPerBeat);
        snap->addItem(tr("Snap: 1/8"), kTicksPerStep * 2);
        snap->addItem(tr("Snap: Step"), kTicksPerStep);
        snap->addItem(tr("Snap: 1/32"), kTicksPerStep / 2);
        snap->addItem(tr("Snap: 1/64"), kTicksPerStep / 4);
        snap->setCurrentIndex(snap->findData(kTicksPerStep));
        connect(snap, &QComboBox::activated, this,
            [this, snap](int row) { pianoRoll_->setSnapTicks(snap->itemData(row).toInt()); });
        pianoRollFrame_->addHeaderWidget(snap);

        auto* snapToggle
            = textSegBtn(tr("Snap"), tr("Toggle grid snap on/off (Alt holds temporary free move)"));
        snapToggle->setCheckable(true);
        snapToggle->setChecked(true);
        connect(snapToggle, &QToolButton::toggled, this, [this](bool on) { pianoRoll_->setSnapEnabled(on); });

        auto* quantizeBtn
            = textSegBtn(tr("Q"), tr("Quantize selected notes (or all notes) to the snap grid (Ctrl+Q)"));
        connect(quantizeBtn, &QToolButton::clicked, this, [this] { pianoRoll_->quantize(); });

        auto* auditionBtn = headerButton(QStringLiteral("speaker"), QString(),
            tr("Audition Notes: preview pitch sound when clicking, drawing, or dragging notes"));
        auditionBtn->setCheckable(true);
        auditionBtn->setChecked(true);
        connect(auditionBtn, &QToolButton::toggled, this,
            [this](bool on) { pianoRoll_->setAuditionEnabled(on); });

        auto* followBtn = headerButton(
            QStringLiteral("follow-playhead"), QString(), tr("Follow playhead during playback"));
        followBtn->setCheckable(true);
        followBtn->setChecked(true);
        connect(
            followBtn, &QToolButton::toggled, this, [this](bool on) { pianoRoll_->setFollowPlayhead(on); });

        pianoRollFrame_->addHeaderWidget(
            makePanelButtonGroup({snapToggle, quantizeBtn, auditionBtn, followBtn}));

        auto* zoomOutBtn = headerButton(
            QStringLiteral("minus"), QString(), tr("Zoom out horizontally (or Ctrl+Wheel on the grid)"));
        auto* zoomInBtn = headerButton(
            QStringLiteral("plus"), QString(), tr("Zoom in horizontally (or Ctrl+Wheel on the grid)"));
        connect(zoomOutBtn, &QToolButton::clicked, this, [this] { pianoRoll_->zoomBy(1.0 / 1.3); });
        connect(zoomInBtn, &QToolButton::clicked, this, [this] { pianoRoll_->zoomBy(1.3); });
        pianoRollFrame_->addHeaderWidget(makePanelButtonGroup({zoomOutBtn, zoomInBtn}));
    }
    makeDetachable(detachable_[0], WorkspacePage::PianoRoll, pianoRollFrame_, tr("Piano roll"),
        tr("piano roll"), QStringLiteral("pianoRoll"));
    connect(pianoRoll_, &PianoRollView::selectionSummaryChanged, this,
        [this](int /*count*/, const QString& summary) {
            pianoRollSelectionText_ = summary;
            updatePanelContexts();
        });
    connect(pianoRoll_, &PianoRollView::playbackRequested, this, [this](double tick) {
        double targetTick = std::max(0.0, tick);
        if (songMode_) {
            bool foundClip = false;
            for (const Clip& clip : project_.clips()) {
                if (clip.pattern == currentPattern_ && !clip.muted) {
                    targetTick = std::max(0.0, static_cast<double>(clip.start) + tick - clip.offset);
                    foundClip = true;
                    break;
                }
            }
            if (!foundClip)
                transport_->songModeAction()->setChecked(false);
        }
        auto& engine = audio_.engine();
        if (!scrubbingPlayback_) {
            scrubbingPlayback_ = true;
            wasPlayingBeforeScrub_ = engine.isPlaying() && !scrubStopPending_;
            scrubStopPending_ = false;
            scrubPlaySent_ = false;
        }
        (void)engine.locate(targetTick);
        if (!wasPlayingBeforeScrub_ && !scrubPlaySent_)
            scrubPlaySent_ = engine.play();
    });
    connect(pianoRoll_, &PianoRollView::playbackReleased, this, [this] {
        if (scrubbingPlayback_ && !wasPlayingBeforeScrub_) {
            scrubStopPending_ = true;
            scrubStopSent_ = audio_.engine().stop();
        }
        scrubbingPlayback_ = false;
        wasPlayingBeforeScrub_ = false;
        scrubPlaySent_ = false;
    });
    // An edit arrives as the whole list of notes; a drag sends one on every
    // move. The notes before the first one are kept, so the edit is one step
    // to undo once it is finished.
    connect(pianoRoll_, &PianoRollView::notesChanged, this, [this](const std::vector<Note>& notes) {
        const NoteHistory::Target target{currentPattern_, currentChannel_};
        const auto* now = project_.notes(target.pattern, target.channel);
        if (now == nullptr)
            return;
        if (!noteEditBefore_ || noteEditTarget_ != target) {
            recordUndo(tr("Piano Roll Notes"));
            noteEditBefore_ = *now;
            noteEditTarget_ = target;
        }
        project_.setNotes(target.pattern, target.channel, notes);
        playlist_->viewport()->update(); // clip lengths follow the pattern
        scheduleChannelRackSync();
    });
    connect(pianoRoll_, &PianoRollView::editFinished, this, [this] {
        if (!noteEditBefore_)
            return;
        if (const auto* now = project_.notes(noteEditTarget_.pattern, noteEditTarget_.channel))
            noteHistory_.record(noteEditTarget_, std::move(*noteEditBefore_), *now);
        noteEditBefore_.reset();
        undoHistory_.breakCoalescing();
        playlist_->refresh();
        syncChannelRack(); // the engine plays the notes
    });
    connect(pianoRoll_, &PianoRollView::undoRequested, this, [this] { performUndo(); });
    connect(pianoRoll_, &PianoRollView::redoRequested, this, [this] { performRedo(); });
    // A key of the piano roll sounds through the channel's instrument plugin.
    connect(pianoRoll_, &PianoRollView::keyPressed, this, [this](int key, int velocity) {
        if (currentChannel_ >= project_.channels().size())
            return;
        const Channel& channel = project_.channels()[currentChannel_];
        auto instance = channel.instrument ? pluginHost_.instance(channel.instrument->instance) : nullptr;
        if (instance != nullptr && instance->playNote(key, static_cast<float>(velocity) / 128.0f))
            previewInstrument_ = std::move(instance);
    });
    connect(pianoRoll_, &PianoRollView::keyReleased, this, [this](int key) {
        if (previewInstrument_ != nullptr)
            (void)previewInstrument_->stopNote(key);
        previewInstrument_.reset();
    });

    // Playlist.
    playlist_ = new PlaylistView;
    playlist_->setProject(&project_);
    playlist_->setMixer(&mixer_);
    playlistFrame_ = new PanelFrame(tr("PLAYLIST"), playlist_, workspace_);
    playlistFrame_->setContext(tr("Click to place the chosen clip"));
    workspace_->addWidget(playlistFrame_);
    auto* removeTrack = headerButton(QStringLiteral("minus"), QString(), tr("Delete the last track"));
    auto* addTrack = headerButton(
        QStringLiteral("plus"), tr("Track"), tr("Add a track (right-click for Audio / Instrument options)"));
    addTrack->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(removeTrack, &QToolButton::clicked, this, [this] {
        if (!project_.tracks().empty())
            removePlaylistTrack(project_.tracks().size() - 1);
    });
    connect(addTrack, &QToolButton::clicked, this, &MainWindow::addPlaylistTrack);
    connect(addTrack, &QWidget::customContextMenuRequested, this, [this, addTrack](const QPoint& pos) {
        QMenu menu(this);
        QAction* dlgAct = menu.addAction(Theme::icon(QStringLiteral("plus")), tr("Add Tracks…"), this,
            [this] { showAddTracksDialog(-1); });
        dlgAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
        menu.addAction(Theme::icon(QStringLiteral("track-audio")), tr("Add Audio Track (mono)"), this,
            [this] { addQuickAudioTrack(-1, true); });
        menu.addAction(Theme::icon(QStringLiteral("track-audio")), tr("Add Audio Track (stereo)"), this,
            [this] { addQuickAudioTrack(-1, false); });
        menu.addAction(Theme::icon(QStringLiteral("track-instrument")), tr("Add Instrument Track"), this,
            [this] { addQuickInstrumentTrack(-1); });
        menu.exec(addTrack->mapToGlobal(pos));
    });
    // Tools: what the left button does in the grid. Their keys work while the
    // playlist has the focus (T is tap tempo, so Mute is M).
    {
        struct ToolInfo {
            PlaylistView::Tool tool;
            const char* icon;
            QString name;
            QKeySequence key;
            QString context;
        };
        const ToolInfo tools[] = {
            {PlaylistView::Tool::Draw, "tool-draw", tr("Draw"), QKeySequence(Qt::Key_P),
                tr("Draw: click to place the clip chosen on the left, drag a clip to move it or its edge to "
                   "trim "
                   "it")},
            {PlaylistView::Tool::Paint, "tool-paint", tr("Paint"), QKeySequence(Qt::Key_B),
                tr("Paint: drag along a track to lay the chosen clip again and again")},
            {PlaylistView::Tool::Delete, "tool-delete", tr("Delete"), QKeySequence(Qt::Key_D),
                tr("Delete: click or drag over clips to delete them")},
            {PlaylistView::Tool::Mute, "tool-mute", tr("Mute"), QKeySequence(Qt::Key_M),
                tr("Mute: click or drag over clips to mute or unmute them")},
            {PlaylistView::Tool::Slice, "tool-slice", tr("Slice"), QKeySequence(Qt::Key_C),
                tr("Slice: click, or drag across tracks, to cut every clip the line crosses (Shift: step; "
                   "Alt: free)")},
            {PlaylistView::Tool::Select, "tool-select", tr("Select"), QKeySequence(Qt::Key_E),
                tr("Select: drag a box (Ctrl adds), drag the selection to move it, Delete removes it")},
            {PlaylistView::Tool::Zoom, "tool-zoom", tr("Zoom"), QKeySequence(Qt::Key_Z),
                tr("Zoom: drag a range to fill the view, click to zoom in, right-click to zoom out")},
            {PlaylistView::Tool::Playback, "tool-playback", tr("Playback"), QKeySequence(Qt::Key_Y),
                tr("Playback: hold to play from there, drag to scrub")},
        };
        auto* group = new QActionGroup(playlistFrame_);
        QList<QAction*> actions;
        for (const ToolInfo& info : tools) {
            auto* action
                = new QAction(Theme::icon(QString::fromLatin1(info.icon)), info.name, playlistFrame_);
            action->setCheckable(true);
            action->setChecked(info.tool == PlaylistView::Tool::Draw);
            action->setShortcut(info.key);
            action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            action->setToolTip(
                QStringLiteral("%1 (%2)").arg(info.name, info.key.toString(QKeySequence::NativeText)));
            playlistFrame_->addAction(action); // the key works while the playlist has the focus
            group->addAction(action);
            actions.append(action);
            connect(action, &QAction::triggered, this, [this, tool = info.tool, context = info.context] {
                playlist_->setTool(tool);
                playlistFrame_->setContext(context);
            });
        }
        // What Draw and Paint place: a pattern or a channel's audio clip. The
        // list is filled afresh each time it opens, so it is never stale.
        clipSource_ = new ClipSourceBox([this] { rebuildClipSources(); }, playlistFrame_);
        clipSource_->setObjectName(QStringLiteral("clipSource"));
        clipSource_->setToolTip(
            tr("What Draw and Paint place: a pattern, or a channel's sample as an audio clip"));
        clipSource_->setAccessibleName(tr("Clip to place"));
        clipSource_->setFocusPolicy(Qt::NoFocus);
        clipSource_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        clipSource_->setMinimumContentsLength(10);
        clipSource_->setMaximumWidth(220);
        if (QAbstractItemView* popupView = clipSource_->view())
            popupView->setMinimumWidth(280);
        connect(clipSource_, &QComboBox::activated, this, [this](int row) {
            const QVariant value = clipSource_->itemData(row);
            if (!value.isValid())
                return;
            const auto index = static_cast<std::size_t>(value.toULongLong() & 0xffffffffULL);
            const bool audio = (value.toULongLong() >> 32) != 0;
            if (audio)
                playlist_->setSource(PlaylistView::ClipSource{true, index});
            else
                selectPattern(index); // as in FL Studio: picking a pattern here makes it current
        });
        playlistFrame_->addHeaderWidget(clipSource_);

        // What clips snap to while placed, moved, trimmed or cut.
        auto* snap = new QComboBox(playlistFrame_);
        snap->setObjectName(QStringLiteral("playlistSnap"));
        snap->setFocusPolicy(Qt::NoFocus);
        snap->setToolTip(
            tr("Snap: what clips line up to when placed, moved, trimmed or cut (Shift: steps; Alt: free)"));
        snap->setAccessibleName(tr("Snap"));
        snap->addItem(tr("Snap: Auto"), static_cast<int>(PlaylistView::Snap::Auto));
        snap->addItem(tr("Snap: Bar"), static_cast<int>(PlaylistView::Snap::Bar));
        snap->addItem(tr("Snap: Beat"), static_cast<int>(PlaylistView::Snap::Beat));
        snap->addItem(tr("Snap: Step"), static_cast<int>(PlaylistView::Snap::Step));
        snap->addItem(tr("Snap: 1/32"), static_cast<int>(PlaylistView::Snap::HalfStep));
        connect(snap, &QComboBox::activated, this, [this, snap](int row) {
            playlist_->setSnap(static_cast<PlaylistView::Snap>(snap->itemData(row).toInt()));
        });
        playlistFrame_->addHeaderWidget(snap);

        auto* snapToggle = new QToolButton(playlistFrame_);
        snapToggle->setText(tr("Snap"));
        snapToggle->setCheckable(true);
        snapToggle->setChecked(true);
        snapToggle->setFocusPolicy(Qt::NoFocus);
        snapToggle->setToolTip(tr("Toggle grid snap on/off (Alt holds temporary free move)"));
        connect(snapToggle, &QToolButton::toggled, this, [this](bool on) { playlist_->setSnapEnabled(on); });

        auto* quantizeBtn = new QToolButton(playlistFrame_);
        quantizeBtn->setText(tr("Q"));
        quantizeBtn->setFocusPolicy(Qt::NoFocus);
        quantizeBtn->setToolTip(tr("Quantize selected clips (or all clips) to the snap grid"));
        connect(quantizeBtn, &QToolButton::clicked, this, [this] { playlist_->quantizeSelectedClips(); });

        auto* loopBtn = headerButton(QStringLiteral("loop"), QString(),
            tr("Set Loop Bar around selection/song (Ctrl+L); right-click for Loop / Mixdown options"));
        loopBtn->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(loopBtn, &QToolButton::clicked, this, [this] { playlist_->loopSelectionOrSong(); });
        connect(loopBtn, &QWidget::customContextMenuRequested, this, [this, loopBtn](const QPoint& pos) {
            QMenu menu(this);
            QAction* activeAct = menu.addAction(tr("Loop Active"));
            activeAct->setCheckable(true);
            activeAct->setChecked(transport_->loopEnabled());
            connect(activeAct, &QAction::triggered, this,
                [this](bool checked) { transport_->setLoopEnabled(checked); });
            menu.addSeparator();
            QAction* setAct = menu.addAction(
                tr("Set Loop to Selection / Song"), playlist_, &PlaylistView::loopSelectionOrSong);
            setAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
            menu.addAction(tr("Clear Loop Range"), playlist_, &PlaylistView::clearLoopRange)
                ->setEnabled(playlist_->hasLoopRange());
            menu.addSeparator();
            menu.addAction(tr("Export Loop Range…"), this, [this] { exportSong(true); })
                ->setEnabled(playlist_->hasLoopRange());
            menu.exec(loopBtn->mapToGlobal(pos));
        });

        auto* followBtn = headerButton(
            QStringLiteral("follow-playhead"), QString(), tr("Follow playhead during playback"));
        followBtn->setCheckable(true);
        followBtn->setChecked(true);
        connect(
            followBtn, &QToolButton::toggled, this, [this](bool on) { playlist_->setFollowPlayhead(on); });

        playlistFrame_->addHeaderWidget(makePanelButtonGroup({snapToggle, quantizeBtn, loopBtn, followBtn}));
        rebuildClipSources();

        // The tools side by side in one box; the tool in use is lit.
        QList<QToolButton*> toolButtons;
        for (QAction* action : actions) {
            auto* button = new QToolButton;
            button->setDefaultAction(action);
            button->setToolButtonStyle(Qt::ToolButtonIconOnly);
            button->setFocusPolicy(Qt::NoFocus);
            toolButtons.append(button);
        }
        playlistFrame_->addHeaderWidget(makePanelButtonGroup(toolButtons));

        auto* zoomOutBtn = headerButton(QStringLiteral("minus"), QString(),
            tr("Zoom out horizontally (or scroll mouse wheel on the timeline ruler)"));
        auto* zoomInBtn = headerButton(QStringLiteral("plus"), QString(),
            tr("Zoom in horizontally (or scroll mouse wheel on the timeline ruler)"));
        connect(zoomOutBtn, &QToolButton::clicked, this, [this] { playlist_->zoomBy(1.0 / 1.3); });
        connect(zoomInBtn, &QToolButton::clicked, this, [this] { playlist_->zoomBy(1.3); });
        playlistFrame_->addHeaderWidget(makePanelButtonGroup({zoomOutBtn, zoomInBtn}));

        playlistFrame_->setContext(tools[0].context);
    }
    // "+ Track" and "−" side by side in a box of their own.
    playlistFrame_->addHeaderWidget(makePanelButtonGroup({addTrack, removeTrack}));
    connect(playlist_, &PlaylistView::trackRenameRequested, this, &MainWindow::renamePlaylistTrack);
    connect(playlist_, &PlaylistView::trackNameEdited, this, [this](std::size_t track, const QString& name) {
        if (track >= project_.tracks().size())
            return;
        recordUndo(tr("Rename Track"));
        syncTrackName(project_, mixer_, track, name.toStdString());
        syncMixerPanel();
        syncInsertNames();
        playlist_->refresh();
    });
    connect(playlist_, &PlaylistView::trackRemoveRequested, this, &MainWindow::removePlaylistTrack);
    connect(playlist_, &PlaylistView::trackColorChanged, this,
        [this](std::size_t track, std::optional<std::uint32_t> color) {
            recordUndo(tr("Track Color"));
            syncTrackColor(project_, mixer_, track, color);
            syncMixerPanel();
            channelRack_->update();
            pianoRoll_->viewport()->update();
            playlist_->viewport()->update();
        });
    connect(playlist_, &PlaylistView::trackHeightChanged, this, [this](std::size_t track, int height) {
        recordUndo(tr("Resize Track"), "trk.h." + std::to_string(track));
        project_.setTrackHeight(track, height);
        playlist_->refresh();
    });
    connect(playlist_, &PlaylistView::trackCollapseToggled, this, [this](std::size_t track, bool collapsed) {
        recordUndo(tr("Fold Track"));
        project_.setTrackCollapsed(track, collapsed);
        playlist_->refresh();
    });
    connect(playlist_, &PlaylistView::trackVolumeChanged, this, [this](std::size_t track, double volume) {
        recordUndo(tr("Track Volume"), "trk.vol." + std::to_string(track));
        setTrackMixVolume(project_, mixer_, track, volume);
        const int ins = trackMixerInsert(project_, mixer_, track);
        if (ins > 0 && static_cast<std::size_t>(ins) < mixer_.size()
            && static_cast<std::size_t>(ins) < mixerPanel_->trackCount()) {
            const auto i = static_cast<std::size_t>(ins);
            const MixerTrack& mt = mixer_.track(i);
            mixerPanel_->setTrackValues(i, mt.fader, mt.pan, mt.muted, mt.soloed);
            updateMixer();
        }
        channelRack_->update();
        scheduleChannelRackSync();
    });
    connect(playlist_, &PlaylistView::trackPanChanged, this, [this](std::size_t track, double pan) {
        recordUndo(tr("Track Pan"), "trk.pan." + std::to_string(track));
        setTrackMixPan(project_, mixer_, track, pan);
        const int ins = trackMixerInsert(project_, mixer_, track);
        if (ins > 0 && static_cast<std::size_t>(ins) < mixer_.size()
            && static_cast<std::size_t>(ins) < mixerPanel_->trackCount()) {
            const auto i = static_cast<std::size_t>(ins);
            const MixerTrack& mt = mixer_.track(i);
            mixerPanel_->setTrackValues(i, mt.fader, mt.pan, mt.muted, mt.soloed);
            updateMixer();
        }
        channelRack_->update();
        scheduleChannelRackSync();
    });
    connect(playlist_, &PlaylistView::trackChannelMenuRequested, this, &MainWindow::showTrackChannelMenu);
    connect(playlist_, &PlaylistView::trackRoutingMenuRequested, this, &MainWindow::showTrackRoutingMenu);
    connect(playlist_, &PlaylistView::addTracksDialogRequested, this, &MainWindow::showAddTracksDialog);
    connect(playlist_, &PlaylistView::addAudioTrackRequested, this, &MainWindow::addQuickAudioTrack);
    connect(
        playlist_, &PlaylistView::addInstrumentTrackRequested, this, &MainWindow::addQuickInstrumentTrack);
    connect(playlist_, &PlaylistView::trackSelected, this, [this](std::size_t track) {
        if (const auto ch = trackPrimaryChannel(project_, track))
            selectChannel(*ch);
        if (mixerPanel_ != nullptr) {
            const int ins = trackMixerInsert(project_, mixer_, track);
            mixerPanel_->setSelectedTrack(
                ins >= 0 && static_cast<std::size_t>(ins) < mixerPanel_->trackCount()
                    ? static_cast<std::size_t>(ins)
                    : MixerModel::kMaster);
        }
    });
    // Settings ids match the keys of the first release that had a detachable playlist.
    makeDetachable(detachable_[1], WorkspacePage::Playlist, playlistFrame_, tr("Playlist"), tr("playlist"),
        QStringLiteral("playlist"));
    // Song mode plays what the playlist holds: every edit goes to the engine.
    const auto songEdited = [this] {
        if (songMode_)
            syncChannelRack();
    };
    // Moving or regrouping tracks can change which ones play (mute and solo
    // count for a whole group).
    connect(playlist_, &PlaylistView::trackReordered, this, &MainWindow::movePlaylistTrackTo);
    connect(
        playlist_, &PlaylistView::trackMoveRequested, this, [this, songEdited](std::size_t track, bool down) {
            if (track >= project_.tracks().size())
                return;
            if (project_.tracks()[track].depth == 0 && !project_.hasSubTracks(track)) {
                if (!down && track > 0 && project_.tracks()[track - 1].depth == 0
                    && !project_.hasSubTracks(track - 1)) {
                    movePlaylistTrackTo(track, track - 1);
                    return;
                }
                if (down && track + 1 < project_.tracks().size() && project_.tracks()[track + 1].depth == 0
                    && !project_.hasSubTracks(track + 1)) {
                    movePlaylistTrackTo(track, track + 1);
                    return;
                }
            }
            recordUndo(tr("Move Track"));
            if (project_.moveTrackGroup(track, down)) {
                playlist_->refresh();
                songEdited();
            }
        });
    connect(playlist_, &PlaylistView::trackIndentRequested, this, [this, songEdited](std::size_t track) {
        if (!project_.canIndentTrack(track))
            return;
        recordUndo(tr("Group Track"));
        if (project_.indentTrack(track)) {
            playlist_->refresh();
            songEdited();
        }
    });
    connect(playlist_, &PlaylistView::trackOutdentRequested, this, [this, songEdited](std::size_t track) {
        if (track >= project_.tracks().size() || project_.tracks()[track].depth == 0)
            return;
        recordUndo(tr("Ungroup Track"));
        if (project_.outdentTrack(track)) {
            playlist_->refresh();
            songEdited();
        }
    });
    connect(
        playlist_, &PlaylistView::trackMuteToggled, this, [this, songEdited](std::size_t track, bool muted) {
            recordUndo(tr("Mute Track"));
            syncTrackMute(project_, mixer_, track, muted);
            syncMixerPanel();
            updateMixer();
            playlist_->viewport()->update();
            songEdited();
        });
    connect(
        playlist_, &PlaylistView::trackSoloToggled, this, [this, songEdited](std::size_t track, bool solo) {
            recordUndo(tr("Solo Track"));
            syncTrackSolo(project_, mixer_, track, solo);
            syncMixerPanel();
            updateMixer();
            playlist_->viewport()->update();
            songEdited();
        });
    connect(playlist_, &PlaylistView::clipAdded, this, [this, songEdited](Clip clip) {
        recordUndo(tr("Add Clip"));
        project_.addClip(clip);
        playlist_->viewport()->update();
        songEdited();
    });
    connect(playlist_, &PlaylistView::clipsAddedBatch, this,
        [this, songEdited](const std::vector<Clip>& clips, const std::vector<AudioClip>& audioClips) {
            if (clips.empty() && audioClips.empty())
                return;
            recordUndo(tr("Paste Clips"));
            for (const Clip& c : clips)
                project_.addClip(c);
            for (const AudioClip& ac : audioClips)
                project_.addAudioClip(ac);
            playlist_->refresh();
            songEdited();
        });
    connect(playlist_, &PlaylistView::clipChanged, this, [this](std::size_t index, Clip clip) {
        if (index < project_.clips().size() && project_.clips()[index] == clip)
            return; // the drag has not reached the next grid line
        recordUndo(tr("Edit Clip"), "clip.edit." + std::to_string(index));
        project_.setClip(index, clip);
        playlist_->updateGrid();
        if (songMode_)
            scheduleChannelRackSync();
    });
    connect(playlist_, &PlaylistView::clipsChanged, this, [this](const PlaylistClipEdit& edit) {
        bool anyChanged = false;
        for (const auto& [index, clip] : edit.clips) {
            if (index < project_.clips().size() && !(project_.clips()[index] == clip)) {
                anyChanged = true;
                break;
            }
        }
        if (!anyChanged) {
            for (const auto& [index, clip] : edit.audioClips) {
                if (index < project_.audioClips().size() && !(project_.audioClips()[index] == clip)) {
                    anyChanged = true;
                    break;
                }
            }
        }
        if (!anyChanged)
            return;
        recordUndo(tr("Move Clips"), "clips.edit");
        for (const auto& [index, clip] : edit.clips) {
            if (index < project_.clips().size())
                project_.setClip(index, clip);
        }
        for (const auto& [index, clip] : edit.audioClips) {
            if (index < project_.audioClips().size())
                project_.setAudioClip(index, clip);
        }
        playlist_->updateGrid();
        if (songMode_)
            scheduleChannelRackSync();
    });
    connect(playlist_, &PlaylistView::clipsRemoved, this,
        [this, songEdited](const std::vector<std::size_t>& clips, const std::vector<std::size_t>& audio) {
            if (clips.empty() && audio.empty())
                return;
            recordUndo(tr("Delete Clips"));
            // From the last: removing a clip moves the ones after it down.
            for (auto it = clips.rbegin(); it != clips.rend(); ++it)
                project_.removeClip(*it);
            for (auto it = audio.rbegin(); it != audio.rend(); ++it)
                project_.removeAudioClip(*it);
            playlist_->refresh();
            songEdited();
        });
    // Playback tool & Time Bar scrub: song mode, play from the pointer while the button is held.
    connect(playlist_, &PlaylistView::playbackRequested, this, [this](double tick) {
        if (!songMode_)
            transport_->songModeAction()->setChecked(true); // syncs through songModeChanged
        auto& engine = audio_.engine();
        if (!scrubbingPlayback_) {
            scrubbingPlayback_ = true;
            // isPlaying() follows the audio thread: right after a scrub (a
            // double-click) it still says "playing" while that scrub's stop
            // is on its way.
            wasPlayingBeforeScrub_ = engine.isPlaying() && !scrubStopPending_;
            scrubStopPending_ = false;
            scrubPlaySent_ = false;
        }
        // A full command queue drops these; the next move sends them again.
        (void)engine.locate(tick);
        if (!wasPlayingBeforeScrub_ && !scrubPlaySent_)
            scrubPlaySent_ = engine.play();
    });
    connect(playlist_, &PlaylistView::playbackReleased, this, [this] {
        if (scrubbingPlayback_ && !wasPlayingBeforeScrub_) {
            // refreshFromEngine() sends the stop again if the queue was full.
            scrubStopPending_ = true;
            scrubStopSent_ = audio_.engine().stop();
        }
        scrubbingPlayback_ = false;
        wasPlayingBeforeScrub_ = false;
        scrubPlaySent_ = false;
    });
    connect(playlist_, &PlaylistView::clipRemoved, this, [this, songEdited](std::size_t index) {
        recordUndo(tr("Delete Clip"));
        project_.removeClip(index);
        playlist_->refresh();
        songEdited();
    });
    playlist_->setTempo(transport_->tempo());
    playlist_->setAudioInfo([this](const std::string& path) { return clipAudio(path); });
    connect(playlist_, &PlaylistView::sourcePicked, this, [this](PlaylistView::ClipSource source) {
        if (source.audio)
            rebuildClipSources(); // the box shows the playlist's new source
        else
            selectPattern(source.index); // its pattern becomes current, as in FL Studio
    });
    connect(playlist_, &PlaylistView::audioDropped, this, &MainWindow::dropAudioFiles);
    connect(playlist_, &PlaylistView::midiFilesDropped, this, &MainWindow::dropMidiFiles);
    connect(playlist_, &PlaylistView::projectFileDropped, this, [this](const QString& path) {
        if (maybeSaveChanges())
            openProjectFile(path);
    });
    connect(playlist_, &PlaylistView::loopRangeChanged, this,
        [this](int startTicks, int endTicks, bool activate) {
            const int normStart = endTicks > startTicks ? std::max(0, startTicks) : 0;
            const int normEnd = endTicks > startTicks ? endTicks : 0;
            if (normStart != loopStartTicks_ || normEnd != loopEndTicks_) {
                recordUndo(tr("Loop Range"), "song.loop");
                loopStartTicks_ = normStart;
                loopEndTicks_ = normEnd;
            }
            if (activate && !transport_->loopEnabled())
                transport_->setLoopEnabled(true);
            if (activate && normEnd > normStart && !songMode_)
                transport_->songModeAction()->setChecked(true);
            songLoopStart_ = -1.0;
            songLoopEnd_ = -1.0;
            if (audio_.isRunning())
                applySongLoop();
            if (loopEndTicks_ > loopStartTicks_) {
                const int b1 = loopStartTicks_ / kTicksPerBar + 1;
                const int b2 = loopEndTicks_ / kTicksPerBar + 1;
                statusBar()->showMessage(tr("Loop range: Bar %1 – Bar %2").arg(b1).arg(b2), 3000);
            } else {
                statusBar()->showMessage(tr("Loop range cleared"), 2500);
            }
        });
    connect(playlist_, &PlaylistView::loopActiveToggled, this,
        [this](bool active) { transport_->setLoopEnabled(active); });
    connect(playlist_, &PlaylistView::exportLoopRangeRequested, this, [this] { exportSong(true); });
    playlist_->setLoopActive(transport_->loopEnabled());
    connect(
        playlist_, &PlaylistView::pluginDroppedOnPlaylist, this, &MainWindow::handlePluginDroppedOnPlaylist);
    connect(playlist_, &PlaylistView::audioClipChanged, this, [this](std::size_t index, AudioClip clip) {
        if (index < project_.audioClips().size() && project_.audioClips()[index] == clip)
            return;
        recordUndo(tr("Edit Audio Clip"), "aclip.edit." + std::to_string(index));
        project_.setAudioClip(index, std::move(clip));
        playlist_->updateGrid();
        if (songMode_)
            scheduleChannelRackSync();
    });
    connect(playlist_, &PlaylistView::audioClipMenuRequested, this, &MainWindow::showAudioClipMenu);
    connect(playlist_, &PlaylistView::audioClipAdded, this, [this, songEdited](AudioClip clip) {
        recordUndo(tr("Slice Clip"));
        project_.addAudioClip(std::move(clip));
        playlist_->refresh();
        songEdited();
    });
    connect(playlist_, &PlaylistView::audioClipRemoved, this, [this, songEdited](std::size_t index) {
        recordUndo(tr("Delete Audio Clip"));
        project_.removeAudioClip(index);
        playlist_->refresh();
        songEdited();
    });
    connect(playlist_, &PlaylistView::patternPicked, this, &MainWindow::selectPattern);
    connect(playlist_, &PlaylistView::patternClipActivated, this, [this](std::size_t pattern, int track) {
        selectPattern(pattern);
        std::optional<std::size_t> chosenCh;
        if (track >= 0 && static_cast<std::size_t>(track) < project_.tracks().size())
            chosenCh = trackPrimaryChannel(project_, static_cast<std::size_t>(track));
        if (chosenCh && *chosenCh < project_.channels().size()) {
            selectChannel(*chosenCh);
        } else if (pattern < project_.patterns().size()) {
            const auto& chs = project_.patterns()[pattern].channels;
            for (std::size_t c = 0; c < chs.size(); ++c) {
                if (!chs[c].notes.empty()) {
                    selectChannel(c);
                    break;
                }
            }
        }
        showWorkspace(WorkspacePage::PianoRoll);
    });

    selectChannel(0);
    selectPattern(0);
    return workspace_;
}

QWidget* MainWindow::createEmptyState()
{
    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("emptyState"));
    auto* layout = new QVBoxLayout(central);
    layout->addStretch();

    auto* title = new QLabel(QStringLiteral("VloomDAW"), central);
    title->setObjectName(QStringLiteral("emptyTitle"));
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    const QString playKey = transport_->playAction()->shortcut().toString(QKeySequence::NativeText);
    auto* hint
        = new QLabel(tr("Press %1 to start the metronome.\n"
                        "Open the playlist (F5), channel rack (F6) or piano roll (F7) to write a song.")
                         .arg(playKey),
            central);
    hint->setObjectName(QStringLiteral("emptyHint"));
    hint->setAlignment(Qt::AlignCenter);
    layout->addWidget(hint);

    layout->addStretch();
    return central;
}

void MainWindow::createStatusBar()
{
    // The device readout doubles as a quick output switcher: click it for
    // the list of devices, rebuilt each time so unplugged ones drop out.
    auto* menu = new QMenu(this);
    connect(menu, &QMenu::aboutToShow, this, [this, menu] {
        menu->clear();
        const int current = listDevices();
        auto* group = new QActionGroup(menu);
        for (int i = 0; i < static_cast<int>(deviceNames_.size()); ++i) {
            QAction* action
                = menu->addAction(QString::fromStdString(deviceNames_[static_cast<std::size_t>(i)]));
            action->setCheckable(true);
            action->setChecked(i == current);
            group->addAction(action);
            // Queued: the menu is cleared on its next show, never while this
            // action's own signal is running.
            connect(action, &QAction::triggered, this,
                [this, i] { QTimer::singleShot(0, this, [this, i] { selectOutputDevice(i); }); });
        }
        if (deviceNames_.empty())
            menu->addAction(tr("No output devices found"))->setEnabled(false);
        menu->addSeparator();
        menu->addAction(tr("Audio Settings…"), this, [this] { showSettings(SettingsDialog::Page::Audio); });
    });

    deviceStatus_ = new QToolButton(this);
    deviceStatus_->setObjectName(QStringLiteral("deviceStatus"));
    deviceStatus_->setMenu(menu);
    deviceStatus_->setPopupMode(QToolButton::InstantPopup);
    deviceStatus_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    deviceStatus_->setFocusPolicy(Qt::NoFocus);
    deviceStatus_->setToolTip(tr("Audio output: click to switch device"));
    statusBar()->addPermanentWidget(deviceStatus_);
}

void MainWindow::selectOutputDevice(int index)
{
    // `index` is into deviceNames_ as last listed.
    const DeviceRef ref = deviceRefAt(deviceNames_, index);
    settings_.outputDevice = QString::fromStdString(ref.name);
    settings_.outputDeviceOccurrence = ref.occurrence;
    settings_.save();
    openSelectedDevice();
}

void MainWindow::extendIntoTitleBar()
{
#if defined(Q_OS_MACOS) && QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    // Draw the tool bars in the title bar row, like most macOS creative apps,
    // instead of a separate strip that only shows the window title.
    setWindowFlag(Qt::ExpandedClientAreaHint);
    setWindowFlag(Qt::NoTitleBarBackgroundHint);
    setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
    // macOS would still draw the title text over the tool bar. The app name
    // stays visible in the menu bar and the Dock.
    setWindowTitle(QString());

    transport_->setLeadingSpace(kWindowButtonsWidth);

    // Native full screen hides the window buttons; toggleFullScreen() covers
    // the screen without it, and the green button zooms instead.
    disableMacNativeFullScreen(this);

    // With no title bar left, empty tool bar space moves the window.
    for (auto* bar : findChildren<QToolBar*>())
        bar->installEventFilter(this);
#elif defined(Q_OS_WIN)
    // No title bar: the menu bar row carries the window buttons, and empty
    // menu bar / tool bar space moves the window (see isCaptionAt()).
    // Resizing, Aero Snap and the taskbar keep working natively.
    removeWindowsTitleBar(this);

    // The app name the title bar used to show. It ignores the mouse, so the
    // press reaches the menu bar and moves the window.
    auto* appName = new QLabel(QStringLiteral("VLOOM"), menuBar());
    appName->setObjectName(QStringLiteral("appName"));
    appName->setAttribute(Qt::WA_TransparentForMouseEvents);
    menuBar()->setCornerWidget(appName, Qt::TopLeftCorner);

    windowButtons_ = new WindowButtons(menuBar());
    menuBar()->setCornerWidget(windowButtons_, Qt::TopRightCorner);
    connect(windowButtons_, &WindowButtons::minimizeRequested, this, &QWidget::showMinimized);
    connect(windowButtons_, &WindowButtons::maximizeToggled, this,
        [this] { isMaximized() ? showNormal() : showMaximized(); });
    connect(windowButtons_, &WindowButtons::closeRequested, this, &QWidget::close);
#endif
}

bool MainWindow::isCaptionAt(QPoint pos) const
{
    const QWidget* hit = childAt(pos);
    if (hit == nullptr)
        return false;
    if (hit == menuBar())
        return menuBar()->actionAt(menuBar()->mapFrom(this, pos)) == nullptr;
    // Spacers on the tool bar ignore the mouse, so childAt() returns the bar.
    return qobject_cast<const QToolBar*>(hit) != nullptr;
}

#if defined(Q_OS_WIN)
bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    if (eventType == "windows_generic_MSG"
        && handleWindowsTitleBarMessage(
            this, message, result, [this](QPoint pos) { return isCaptionAt(pos); }))
        return true;
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

void MainWindow::placeWindowButtons()
{
#if defined(Q_OS_MACOS) && QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    placeMacWindowButtons(this, transport_->height(), kWindowButtonsInset);
    // AppKit may lay the title bar out again after this event; repeat once
    // the event loop is idle so our position wins.
    QTimer::singleShot(
        0, this, [this] { placeMacWindowButtons(this, transport_->height(), kWindowButtonsInset); });
#elif defined(Q_OS_WIN)
    windowButtons_->setMaximized(isMaximized());
    // A 1 px outline (painted in paintEvent()) keeps the window's edge
    // visible over dark windows, where the DWM shadow barely shows.
    const int border = (isMaximized() || isFullScreen()) ? 0 : 1;
    setContentsMargins(border, border, border, border);
#endif
}

#if defined(Q_OS_WIN)
void MainWindow::paintEvent(QPaintEvent* event)
{
    QMainWindow::paintEvent(event);
    if (contentsMargins().left() == 0)
        return;
    QPainter painter(this);
    painter.setPen(Theme::colors().border);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}
#endif

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    updateRefreshRate(); // the screen is known for sure once shown
    placeWindowButtons();
    placePerformanceOverlay();
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    placeWindowButtons();
    placePerformanceOverlay();
}

bool MainWindow::event(QEvent* event)
{
    if (event->type() == QEvent::FileOpen) {
        auto* openEvt = static_cast<QFileOpenEvent*>(event);
        const QString file = openEvt->file();
        if (isLocalPath(file)
            && (file.endsWith(QStringLiteral(".vloom"), Qt::CaseInsensitive)
                || isMidiFile(QFileInfo(file).fileName().toStdString()))) {
            if (maybeSaveChanges())
                openProjectFile(file);
            return true;
        }
    }
    // A frame: Qt sends UpdateRequest to the top-level widget, which then
    // paints everything dirty. Time it for the performance overlay.
    if (event->type() == QEvent::UpdateRequest && performanceOverlay_ != nullptr
        && !performanceOverlay_->isHidden()) {
        const qint64 start = frameClock_.nsecsElapsed();
        const bool handled = QMainWindow::event(event);
        const qint64 end = frameClock_.nsecsElapsed();
        frameStats_.addFrame(static_cast<double>(end) / 1e9, static_cast<double>(end - start) / 1e6);
        return handled;
    }
    return QMainWindow::event(event);
}

void MainWindow::setPerformanceOverlayVisible(bool visible)
{
    performanceOverlay_->setVisible(visible);
    performanceAction_->setChecked(visible);
    QSettings().setValue(kPerformanceOverlayKey, visible);
    placePerformanceOverlay();
}

void MainWindow::placePerformanceOverlay()
{
    if (performanceOverlay_ == nullptr || performanceOverlay_->isHidden())
        return;
    // Top-right corner of the area under the tool bars.
    const QRect area = centralWidget() != nullptr ? centralWidget()->geometry() : rect();
    // Below the panel headers, so it does not hide their buttons.
    performanceOverlay_->move(area.right() - performanceOverlay_->width() - 24, area.top() + 44);
    performanceOverlay_->raise();
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange || event->type() == QEvent::ActivationChange)
        placeWindowButtons();
}

QMenu* MainWindow::createPopupMenu()
{
    // No right-click menu for hiding tool bars: they are not optional panels.
    return nullptr;
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{

    if (qobject_cast<QToolBar*>(watched) != nullptr) {
        // While covering the screen the window must not move or maximize:
        // leaving full screen would then restore the wrong geometry.
        if (coversScreen_
            && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick))
            return true;
        if (event->type() == QEvent::MouseButtonPress
            && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton && windowHandle() != nullptr) {
            windowHandle()->startSystemMove();
            return true;
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            isMaximized() ? showNormal() : showMaximized();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::restoreWindowState()
{
    const QSettings settings;
    restoreGeometry(settings.value(kGeometryKey).toByteArray());
    restoreState(settings.value(kStateKey).toByteArray());
    splitter_->restoreState(settings.value(kSplitterKey).toByteArray());
    workspaceSplitter_->restoreState(settings.value(kMixerSplitterKey).toByteArray());
    // Ensure the mixer has enough height to cleanly show all FX slots and the fader without overlapping.
    if (workspaceSplitter_->sizes().size() >= 2 && mixerPanel_ != nullptr) {
        const int minH = mixerPanel_->minimumHeight();
        if (workspaceSplitter_->sizes()[1] < minH) {
            const int total = workspaceSplitter_->sizes()[0] + workspaceSplitter_->sizes()[1];
            workspaceSplitter_->setSizes({std::max(100, total - minH), minH});
        }
    }
    setMixerVisible(settings.value(kMixerOpenKey, false).toBool());
    if (settings.value(kMixerDetachedKey, false).toBool())
        setMixerDetached(true);
    for (const DetachablePage& d : detachable_)
        if (settings.value(QStringLiteral("mainWindow/%1Detached").arg(d.settingsId), false).toBool())
            setPageDetached(d.page, true,
                settings.value(QStringLiteral("mainWindow/%1WindowOpen").arg(d.settingsId), false).toBool());
    const int page = settings.value(kSidebarPageKey, 0).toInt();
    activityBar_->setCurrent(page);
    showSidePanel(activityBar_->current());
    if (playlist_ != nullptr) {
        playlist_->setHeaderWidth(
            settings.value(kPlaylistHeaderWidthKey, PlaylistView::kDefaultHeaderWidth).toInt());
    }
    // The tool bars hold the transport and the window buttons' space; never
    // come back with them hidden, whatever an older saved state says.
    for (auto* bar : findChildren<QToolBar*>())
        bar->show();
}

void MainWindow::saveWindowStateToSettings()
{
    QSettings settings;
    settings.setValue(kGeometryKey, saveGeometry());
    settings.setValue(kStateKey, saveState());
    settings.setValue(kSplitterKey, splitter_->saveState());
    settings.setValue(kMixerSplitterKey, workspaceSplitter_->saveState());
    settings.setValue(kMixerOpenKey, isMixerOpen());
    settings.setValue(kMixerDetachedKey, isMixerDetached());
    for (const DetachablePage& d : detachable_) {
        const bool detached = isPageDetached(d.page);
        settings.setValue(QStringLiteral("mainWindow/%1Detached").arg(d.settingsId), detached);
        settings.setValue(
            QStringLiteral("mainWindow/%1WindowOpen").arg(d.settingsId), detached && d.window->isVisible());
    }
    settings.setValue(kSidebarPageKey, activityBar_->current());
    if (playlist_ != nullptr)
        settings.setValue(kPlaylistHeaderWidthKey, playlist_->headerWidth());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!maybeSaveChanges()) {
        event->ignore();
        return;
    }

    // A visible Settings window would otherwise keep the app running.
    settingsDialog_->close();

    // Save the normal window size and give the menu bar and Dock back.
    if (coversScreen_)
        toggleFullScreen();

    saveWindowStateToSettings();

    // Detached windows are separate top-level windows: close them with the
    // main window, or the app would keep running.
    for (const DetachablePage& d : detachable_)
        if (d.window != nullptr)
            d.window->hide();
    if (mixerWindow_ != nullptr)
        mixerWindow_->hide();
    QMainWindow::closeEvent(event);
}

// ---------------------------------------------------------------- behaviour

DeviceRef MainWindow::selectedDevice() const
{
    return {settings_.outputDevice.toStdString(), settings_.outputDeviceOccurrence};
}

int MainWindow::listDevices()
{
    // Indices are only valid for this listing: devices come and go.
    const auto devices = audio_.outputDevices();
    deviceNames_ = devices.names;

    int index = findDevice(deviceNames_, selectedDevice());
    if (index < 0)
        index = devices.defaultIndex; // not saved, or unplugged

    QStringList names;
    for (const auto& name : deviceNames_)
        names << QString::fromStdString(name);
    settingsDialog_->audioPage()->setDevices(names, index);
    return index;
}

void MainWindow::openSelectedDevice() { startDevice(listDevices()); }

void MainWindow::showSettings(SettingsDialog::Page page)
{
    listDevices(); // show what is plugged in now
    refreshStemSettings();
    settingsDialog_->showPage(page);
}

void MainWindow::showPluginManager()
{
    if (pluginManager_ == nullptr) {
        // Kept for the session (hidden on close) so a running scan survives
        // closing the window.
        pluginManager_ = new PluginManagerDialog(this);
        pluginManager_->setStore(pluginStore_.scanned()); // it manages files; built-ins have none
        connect(pluginManager_, &PluginManagerDialog::storeChanged, this, [this](const PluginStore& store) {
            pluginStore_ = store;
            pluginStore_.setBuiltins(builtinPlugins());
            pluginStore_.save();
            pluginsPanel_->setStore(pluginStore_);
        });
    }
    pluginManager_->show();
    pluginManager_->raise();
    pluginManager_->activateWindow();
}

void MainWindow::changeAudioFormat(int sampleRate, int bufferFrames)
{
    settings_.sampleRate = sampleRate;
    settings_.bufferFrames = bufferFrames;
    settings_.save();
    audio_.setFormat(sampleRate, bufferFrames);
    openSelectedDevice(); // reopen with the new format
}

bool MainWindow::startDevice(int index, bool fallback)
{
    if (!deviceClock_.isValid())
        deviceClock_.start();
    deviceWatch_.setPreferredIsDefault(selectedDevice().isDefault());
    std::string error;
    if (!audio_.openDevice(index, &error)) {
        deviceWatch_.openFailed(deviceClock_.elapsed());
        deviceStatus_->setText(tr("No audio output"));
        settingsDialog_->audioPage()->setStatus(
            tr("Not running: %1").arg(QString::fromStdString(error)), false);
        statusBar()->showMessage(tr("Audio error: %1").arg(QString::fromStdString(error)), 8000);
        return false;
    }
    deviceWatch_.opened(!fallback, deviceClock_.elapsed());

    // Instruments loaded before any device ran: at the device's rate before
    // the rack first sends them to the engine (below).
    for (const std::uint64_t handle : unpublishedInstruments_)
        if (pluginHost_.sampleRate(handle) != audio_.sampleRate())
            pluginHost_.reactivate(handle, audio_.sampleRate());
    unpublishedInstruments_.clear();

    // Re-send UI state so the engine matches what the user sees.
    pendingTempo_.reset();
    songLoopStart_ = -1.0;
    songLoopEnd_ = -1.0; // sent again on the next tick
    audio_.engine().setTempo(transport_->tempo());
    audio_.engine().setMetronomeEnabled(transport_->metronomeEnabled());
    updateMasterOutput();
    audio_.engine().setMasterGain(*pendingGain_);
    audio_.engine().setMasterPan(*pendingPan_);
    pendingGain_.reset();
    pendingPan_.reset();
    const MixerModel effective = effectiveMixerModel();
    audio_.engine().setMixerState(engineMixerState(effective, pluginHost_.resolver()));

    syncChannelRack(); // samples at the device's rate
    previewer_->setSampleRate(audio_.sampleRate());

    const QString format = tr("%1 Hz  ·  %2 samples").arg(audio_.sampleRate()).arg(audio_.bufferFrames());
    deviceStatus_->setText(
        QStringLiteral("%1  ·  %2").arg(QString::fromStdString(audio_.deviceName()), format));

    // Say so when the device could not run at the requested rate.
    QString status = tr("Running at %1 Hz").arg(audio_.sampleRate());
    if (audio_.sampleRate() != audio_.requestedSampleRate())
        status += tr(" (device does not support %1 Hz)").arg(audio_.requestedSampleRate());
    settingsDialog_->audioPage()->setStatus(status, true);
    return true;
}

void MainWindow::watchDevice()
{
    if (!deviceClock_.isValid())
        return; // no device opened yet
    if (deviceScanner_ == nullptr) {
        deviceScanner_ = new DeviceScanner(this);
        connect(deviceScanner_, &DeviceScanner::scanned, this,
            [this] { preferredPresent_ = findDevice(deviceScanner_->names(), selectedDevice()) >= 0; });
        deviceListWatcher_ = std::make_unique<vloom::DeviceListWatcher>();
    }
    // Listing devices takes up to ~200 ms and disturbs the UI even from
    // another thread, so it happens only when the OS reports a change
    // (where it cannot, every poll, still off the UI thread). Each poll
    // answers from the last scan: a returning device is noticed one poll
    // (2 s) after the OS reports it.
    const auto action = deviceWatch_.tick(audio_.isRunning(), deviceClock_.elapsed(), [this] {
        const std::uint64_t generation = deviceListWatcher_->generation();
        if (!deviceListWatcher_->isSupported() || generation != scannedGeneration_) {
            scannedGeneration_ = generation;
            deviceScanner_->scan();
        }
        return preferredPresent_;
    });
    if (action == DeviceWatch::Action::None)
        return;
    preferredPresent_ = false; // the next wait for the user's device starts from a fresh scan

    // The transport keeps its position and play state across a reopen, so
    // the song carries on through the new device.
    const QString previous = QString::fromStdString(audio_.deviceName());
    if (action == DeviceWatch::Action::OpenDefault) {
        audio_.closeDevice();
        listDevices();
        if (!startDevice(-1, true))
            return;
        const QString now = QString::fromStdString(audio_.deviceName());
        statusBar()->showMessage(previous.isEmpty()
                ? tr("Audio output: %1").arg(now)
                : tr("\u201C%1\u201D was disconnected: playing through \u201C%2\u201D").arg(previous, now),
            10000);
    } else if (startDevice(listDevices())) {
        statusBar()->showMessage(
            tr("\u201C%1\u201D is back").arg(QString::fromStdString(audio_.deviceName())), 6000);
    }
}

void MainWindow::rebuildClipSources()
{
    if (clipSource_ == nullptr)
        return;
    const QSignalBlocker block(clipSource_);
    clipSource_->clear();
    // Item data: the index, with bit 32 set for an audio clip.
    const auto key = [](bool audio, std::size_t index) {
        return QVariant::fromValue<qulonglong>(
            (audio ? (1ULL << 32) : 0ULL) | static_cast<qulonglong>(index));
    };
    const auto swatch = [](const QColor& color) {
        QPixmap pixmap(12, 12);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(1, 1, 10, 10), 2, 2);
        return QIcon(pixmap);
    };
    for (std::size_t i = 0; i < project_.patterns().size(); ++i)
        clipSource_->addItem(swatch(Theme::patternColor(i)),
            QString::fromStdString(project_.patterns()[i].name), key(false, i));
    const QIcon audioIcon = Theme::icon(QStringLiteral("browser-file"));
    bool separated = false;
    for (std::size_t i = 0; i < project_.channels().size(); ++i) {
        const Channel& channel = project_.channels()[i];
        if (channel.samplePath.empty())
            continue;
        if (!separated) {
            clipSource_->insertSeparator(clipSource_->count());
            separated = true;
        }
        clipSource_->addItem(
            audioIcon, tr("%1 (audio)").arg(QString::fromStdString(channel.name)), key(true, i));
    }
    const PlaylistView::ClipSource source = playlist_->source();
    const int row = clipSource_->findData(key(source.audio, source.index));
    clipSource_->setCurrentIndex(std::max(0, row));
}

void MainWindow::rebuildPianoRollChannels()
{
    if (pianoRollChannel_ == nullptr)
        return;
    const QSignalBlocker block(pianoRollChannel_);
    pianoRollChannel_->clear();
    const auto swatch = [](const QColor& color) {
        QPixmap pixmap(12, 12);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(1, 1, 10, 10), 2, 2);
        return QIcon(pixmap);
    };
    for (std::size_t i = 0; i < project_.channels().size(); ++i) {
        const Channel& ch = project_.channels()[i];
        const QColor col = ch.color ? QColor::fromRgb(*ch.color) : Theme::colors().accent;
        QString label = QString::fromStdString(ch.name);
        if (ch.instrument && !ch.instrument->name.empty() && ch.instrument->name != ch.name)
            label += QStringLiteral(" · %1").arg(QString::fromStdString(ch.instrument->name));
        pianoRollChannel_->addItem(swatch(col), label, QVariant::fromValue<qulonglong>(i));
    }
    if (currentChannel_ < project_.channels().size())
        pianoRollChannel_->setCurrentIndex(static_cast<int>(currentChannel_));
}

int MainWindow::songEndTicks()
{
    long long end = 0;
    for (const Clip& clip : project_.clips())
        end = std::max<long long>(end, static_cast<long long>(clip.start) + project_.clipLength(clip));
    for (const AudioClip& clip : project_.audioClips()) {
        if (clip.channel >= project_.channels().size())
            continue;
        // As the playlist draws it: the part of the sample the clip plays.
        const ClipAudio* info = clipAudio(project_.channels()[clip.channel].samplePath);
        // A clip fitted to the tempo lasts its own beats, whatever the song's tempo.
        const double tempo = clip.fitted() ? clip.sourceBpm : transport_->tempo();
        const double whole = info->seconds * tempo / 60.0 * kTicksPerBeat;
        double length = whole > 0.0 ? whole - clip.offset : kTicksPerBar;
        if (clip.length > 0)
            length = whole > 0.0 ? std::min<double>(clip.length, length) : clip.length;
        end = std::max<long long>(end, clip.start + static_cast<long long>(std::ceil(std::max(0.0, length))));
    }
    const long long bars = (end + kTicksPerBar - 1) / kTicksPerBar;
    return static_cast<int>(std::min<long long>(bars * kTicksPerBar, 1'000'000'000LL));
}

void MainWindow::applySongLoop()
{
    // Pattern mode loops the pattern itself; an empty song (or loop turned off) clears the song loop.
    const bool loopOn = transport_ == nullptr || transport_->loopEnabled();
    double start = 0.0;
    double end = 0.0;
    if (songMode_ && loopOn) {
        if (loopEndTicks_ > loopStartTicks_) {
            start = static_cast<double>(loopStartTicks_);
            end = static_cast<double>(loopEndTicks_);
        } else {
            end = static_cast<double>(songEndTicks());
        }
    }
    if (start == songLoopStart_ && end == songLoopEnd_)
        return;
    auto& engine = audio_.engine();
    if ((end > start ? engine.setLoop(start, end) : engine.clearLoop())) {
        songLoopStart_ = start;
        songLoopEnd_ = end; // else the queue was full: tried again next tick
    }
}

void MainWindow::scheduleChannelRackSync()
{
    if (rackSyncTimer_ != nullptr)
        rackSyncTimer_->start();
}

void MainWindow::flushChannelRackSync()
{
    if (rackSyncTimer_ != nullptr && rackSyncTimer_->isActive()) {
        rackSyncTimer_->stop();
        syncChannelRack();
    }
}

void MainWindow::syncChannelRack()
{
    if (rackSyncTimer_ != nullptr)
        rackSyncTimer_->stop();
    rackAudio_.setTempo(transport_->tempo());
    fitTempo_ = transport_->tempo();
    stretchUsed_.clear(); // filled again by the stretch provider during the sync
    // Stretched copies no clip uses any more go, once there are several.
    const auto pruneStretched = [this] {
        if (stretched_.size() > 8)
            std::erase_if(
                stretched_, [this](const auto& entry) { return !stretchUsed_.contains(entry.first); });
    };
    const auto errors
        = rackAudio_.sync(audio_.engine(), project_, currentPattern_, audio_.sampleRate(), songMode_);
    pruneStretched();
    if (errors.empty())
        return;
    const auto& first = errors.front();
    const QString name = QFileInfo(QString::fromStdString(first.path)).fileName();
    statusBar()->showMessage(
        tr("Could not load %1: %2").arg(name, QString::fromStdString(first.message)), 8000);
}

void MainWindow::assignSample(std::size_t channel, const QString& path)
{
    recordUndo(tr("Assign Sample"));
    const QString name = QFileInfo(path).completeBaseName();
    if (channel >= project_.channels().size()) {
        channel = project_.addChannel(name.toStdString());
        project_.setChannelColor(channel, trackPaletteColor(channel));
    } else {
        project_.setChannelName(channel, name.toStdString());
    }
    project_.setChannelSample(channel, QDir::cleanPath(path).toStdString());
    if (project_.channels()[channel].insert == 0)
        linkChannelToInsert(project_, mixer_, channel);
    syncMixerPanel();
    syncInsertNames();
    channelRack_->refresh();
    selectChannel(channel);
    updateMixer();
    syncChannelRack();
}

// ---------------------------------------------------------------- hosted plugins

std::optional<PluginSlot> MainWindow::loadPlugin(std::size_t plugin, vloom::PluginInfo::Kind wanted)
{
    if (plugin >= pluginStore_.plugins.size())
        return std::nullopt;
    const PluginInfo& info = pluginStore_.plugins[plugin];
    const QString name = QString::fromStdString(info.name);
    statusBar()->showMessage(tr("Loading %1…").arg(name));
    statusBar()->repaint(); // shown while the plugin loads
    const double rate = audio_.sampleRate() > 0 ? audio_.sampleRate() : audio_.requestedSampleRate();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    // A built-in is found by its id, which its path holds.
    const bool builtin = info.format == PluginFormat::Builtin;
    const PluginHost::Loaded loaded = builtin
        ? pluginHost_.load(info.format, {}, pathToUtf8(info.path), info.name, rate)
        : pluginHost_.load(info.format, info.path, {}, info.name, rate);
    QApplication::restoreOverrideCursor();
    if (loaded.handle == 0) {
        statusBar()->showMessage(
            tr("Could not load %1: %2").arg(name, QString::fromStdString(loaded.error)), 8000);
        return std::nullopt;
    }
    if (loaded.info.kind != wanted) {
        releaseUnusedPlugins(); // the one just loaded: the engine never had it
        statusBar()->showMessage(wanted == vloom::PluginInfo::Kind::Instrument
                ? tr("%1 is an effect: drop it on a mixer insert.").arg(name)
                : tr("%1 is an instrument: drop it on the channel rack.").arg(name),
            8000);
        return std::nullopt;
    }
    updatePluginIdle();
    statusBar()->showMessage(tr("%1 loaded").arg(name), 3000);
    PluginSlot slot;
    slot.format = info.format;
    if (!builtin)
        slot.path = pathToUtf8(info.path);
    slot.id = loaded.info.id;
    slot.name = loaded.info.name.empty() ? info.name : loaded.info.name;
    slot.instance = loaded.handle;
    return slot;
}

void MainWindow::addPluginToRack(std::size_t plugin, std::size_t channel)
{
    const auto slot = loadPlugin(plugin, vloom::PluginInfo::Kind::Instrument);
    if (!slot)
        return;
    recordUndo(tr("Load Instrument"));
    if (channel >= project_.channels().size()) {
        channel = project_.addChannel(slot->name); // an existing channel keeps its name
        project_.setChannelColor(channel, trackPaletteColor(channel));
    }
    project_.setChannelInstrument(channel, *slot);
    if (project_.channels()[channel].insert == 0)
        linkChannelToInsert(project_, mixer_, channel);
    if (audio_.sampleRate() <= 0)
        unpublishedInstruments_.push_back(slot->instance);
    syncMixerPanel();
    syncInsertNames();
    channelRack_->refresh();
    selectChannel(channel);
    updateMixer();
    syncChannelRack();
    releaseUnusedPlugins(); // the instrument it replaced, if any
    openPluginEditor(slot->instance, QString::fromStdString(slot->name));
}

void MainWindow::addPluginToInsert(std::size_t plugin, std::size_t track)
{
    if (track >= mixer_.size())
        return;
    if (mixer_.track(track).effects.size() >= MixerModel::kMaxEffects) {
        statusBar()->showMessage(tr("%1 has %2 effects, the most it can have.")
                                     .arg(QString::fromStdString(mixer_.track(track).name))
                                     .arg(static_cast<qulonglong>(MixerModel::kMaxEffects)),
            5000);
        return;
    }
    const auto slot = loadPlugin(plugin, vloom::PluginInfo::Kind::Effect);
    if (!slot)
        return;
    recordUndo(tr("Add Effect"));
    mixer_.addEffect(track, *slot);
    updateMixer();
    syncEffectCounts();
    openPluginEditor(slot->instance,
        tr("%1 - %2").arg(
            QString::fromStdString(slot->name), QString::fromStdString(mixer_.track(track).name)));
}

void MainWindow::chooseInsertForPlugin(std::size_t plugin)
{
    QMenu menu(this);
    if (mixerPanel_ != nullptr && mixerPanel_->selectedTrack() < mixer_.size()) {
        const std::size_t sel = mixerPanel_->selectedTrack();
        const QString selName = QString::fromStdString(mixer_.track(sel).name);
        menu.addAction(tr("Add to Selected (%1)").arg(selName), this,
            [this, plugin, sel] { addPluginToInsert(plugin, sel); });
        menu.addSeparator();
    }
    for (std::size_t i = 0; i < mixer_.size(); ++i) {
        menu.addAction(QString::fromStdString(mixer_.track(i).name), this,
            [this, plugin, i] { addPluginToInsert(plugin, i); });
        if (i == MixerModel::kMaster && mixer_.size() > 1)
            menu.addSeparator();
    }
    if (mixer_.insertCount() < MixerModel::kMaxInserts) {
        menu.addSeparator();
        menu.addAction(tr("+ New Insert && Add Effect"), this, [this, plugin] {
            addMixerInsert();
            if (mixerPanel_ != nullptr && mixerPanel_->selectedTrack() < mixer_.size())
                addPluginToInsert(plugin, mixerPanel_->selectedTrack());
        });
    }
    menu.exec(QCursor::pos());
}

void MainWindow::showEffectsMenu(std::size_t track, const QPoint& globalPos)
{
    if (track >= mixer_.size())
        return;
    const QString insertName = QString::fromStdString(mixer_.track(track).name);
    const auto& effects = mixer_.track(track).effects;
    QMenu menu(this);
    if (effects.size() < MixerModel::kMaxEffects) {
        menu.addAction(Theme::icon(QStringLiteral("plus")), tr("Add Effect…"), this,
            [this, track, slot = effects.size(), globalPos] {
                showEffectChooserMenu(track, slot, globalPos);
            });
        menu.addSeparator();
    }
    if (effects.empty())
        menu.addAction(tr("No effects loaded yet"))->setEnabled(false);
    for (std::size_t i = 0; i < effects.size(); ++i) {
        const PluginSlot& effect = effects[i];
        const QString name = QString::fromStdString(effect.name);
        QMenu* sub = menu.addMenu(effect.bypassed
                ? tr("%1. %2 (bypassed)").arg(static_cast<qulonglong>(i + 1)).arg(name)
                : tr("%1. %2").arg(static_cast<qulonglong>(i + 1)).arg(name));
        const auto instance = pluginHost_.instance(effect.instance);
        QAction* open
            = sub->addAction(tr("Open Editor"), this, [this, handle = effect.instance, name, insertName] {
                  openPluginEditor(handle, tr("%1 - %2").arg(name, insertName));
              });
        open->setEnabled(instance != nullptr
            && (instance->processor().hasEditor() || !instance->processor().parameters().empty()));
        QAction* bypass = sub->addAction(
            tr("Bypass"), this, [this, track, i](bool on) { setEffectBypassed(track, i, on); });
        bypass->setCheckable(true);
        bypass->setChecked(effect.bypassed);
        sub->addSeparator();
        sub->addAction(tr("Remove"), this, [this, track, i] {
            recordUndo(tr("Remove Effect"));
            mixer_.removeEffect(track, i);
            updateMixer();
            releaseUnusedPlugins();
            syncEffectCounts();
        });
    }
    menu.exec(globalPos);
}

void MainWindow::replacePluginInInsert(std::size_t plugin, std::size_t track, std::size_t slot)
{
    if (track >= mixer_.size() || slot >= mixer_.track(track).effects.size())
        return;
    const auto newSlot = loadPlugin(plugin, vloom::PluginInfo::Kind::Effect);
    if (!newSlot)
        return;
    recordUndo(tr("Replace Effect"));
    mixer_.replaceEffect(track, slot, *newSlot);
    updateMixer();
    releaseUnusedPlugins();
    syncEffectCounts();
    openPluginEditor(newSlot->instance,
        tr("%1 - %2").arg(
            QString::fromStdString(newSlot->name), QString::fromStdString(mixer_.track(track).name)));
}

void MainWindow::moveEffectInInsert(std::size_t track, std::size_t from, std::size_t to)
{
    if (track >= mixer_.size() || from == to)
        return;
    recordUndo(tr("Move Effect"));
    if (!mixer_.moveEffect(track, from, to))
        return;
    updateMixer();
    syncEffectCounts();
}

void MainWindow::showEffectSlotContextMenu(std::size_t track, std::size_t slot, const QPoint& globalPos)
{
    if (track >= mixer_.size())
        return;
    if (slot >= mixer_.track(track).effects.size()) {
        showEffectChooserMenu(track, slot, globalPos);
        return;
    }
    const PluginSlot& effect = mixer_.track(track).effects[slot];
    const QString effectName = QString::fromStdString(effect.name);
    const QString trackName = QString::fromStdString(mixer_.track(track).name);

    QMenu menu(this);
    const auto instance = pluginHost_.instance(effect.instance);
    QAction* open
        = menu.addAction(tr("Open Editor"), this, [this, handle = effect.instance, effectName, trackName] {
              openPluginEditor(handle, tr("%1 - %2").arg(effectName, trackName));
          });
    open->setEnabled(instance != nullptr
        && (instance->processor().hasEditor() || !instance->processor().parameters().empty()));

    QAction* bypass = menu.addAction(
        tr("Bypass"), this, [this, track, slot](bool on) { setEffectBypassed(track, slot, on); });
    bypass->setCheckable(true);
    bypass->setChecked(effect.bypassed);

    menu.addSeparator();

    QAction* moveUp = menu.addAction(tr("Move Up"), this, [this, track, slot] {
        if (slot > 0)
            moveEffectInInsert(track, slot, slot - 1);
    });
    moveUp->setEnabled(slot > 0);

    QAction* moveDown = menu.addAction(tr("Move Down"), this, [this, track, slot] {
        if (slot + 1 < mixer_.track(track).effects.size())
            moveEffectInInsert(track, slot, slot + 1);
    });
    moveDown->setEnabled(slot + 1 < mixer_.track(track).effects.size());

    menu.addSeparator();

    menu.addAction(tr("Replace…"), this,
        [this, track, slot, globalPos] { showEffectChooserMenu(track, slot, globalPos); });

    menu.addSeparator();
    menu.addAction(tr("Remove"), this, [this, track, slot] {
        recordUndo(tr("Remove Effect"));
        mixer_.removeEffect(track, slot);
        updateMixer();
        releaseUnusedPlugins();
        syncEffectCounts();
    });

    menu.exec(globalPos);
}

void MainWindow::showEffectChooserMenu(std::size_t track, std::size_t slot, const QPoint& globalPos)
{
    if (track >= mixer_.size())
        return;

    auto* picker = new PluginPickerPopup(pluginStore_, PluginPickerPopup::Filter::Effect, this);
    connect(picker, &PluginPickerPopup::pluginSelected, this, [this, track, slot](std::size_t pluginIndex) {
        if (slot < mixer_.track(track).effects.size())
            replacePluginInInsert(pluginIndex, track, slot);
        else
            addPluginToInsert(pluginIndex, track);
    });
    connect(picker, &PluginPickerPopup::morePluginsRequested, this, &MainWindow::showPluginManager);
    picker->showAt(globalPos);
}

void MainWindow::showInstrumentChooserMenu(std::size_t channel, const QPoint& globalPos)
{
    if (channel >= project_.channels().size())
        return;

    auto* picker = new PluginPickerPopup(pluginStore_, PluginPickerPopup::Filter::Instrument, this);
    connect(picker, &PluginPickerPopup::pluginSelected, this,
        [this, channel](std::size_t pluginIndex) { addPluginToRack(pluginIndex, channel); });
    connect(picker, &PluginPickerPopup::morePluginsRequested, this, &MainWindow::showPluginManager);
    picker->showAt(globalPos);
}

void MainWindow::handlePluginDroppedOnSlot(std::size_t track, std::size_t slot, std::size_t plugin)
{
    if (track >= mixer_.size())
        return;
    if (slot < mixer_.track(track).effects.size())
        replacePluginInInsert(plugin, track, slot);
    else
        addPluginToInsert(plugin, track);
}

PluginEditorContext MainWindow::buildPluginEditorContext(std::uint64_t handle)
{
    PluginEditorContext ctx;
    const PluginSlot* matchedSlot = nullptr;
    bool isInstrument = false;
    std::size_t foundChannel = 0;
    std::size_t foundTrack = 0;
    std::size_t foundEffectSlot = 0;

    for (std::size_t c = 0; c < project_.channels().size(); ++c) {
        const auto& inst = project_.channels()[c].instrument;
        if (inst && inst->instance == handle) {
            matchedSlot = &(*inst);
            isInstrument = true;
            foundChannel = c;
            break;
        }
    }
    if (matchedSlot == nullptr) {
        for (std::size_t t = 0; t < mixer_.size(); ++t) {
            const auto& effects = mixer_.track(t).effects;
            for (std::size_t s = 0; s < effects.size(); ++s) {
                if (effects[s].instance == handle) {
                    matchedSlot = &effects[s];
                    foundTrack = t;
                    foundEffectSlot = s;
                    break;
                }
            }
            if (matchedSlot != nullptr)
                break;
        }
    }

    if (matchedSlot == nullptr)
        return ctx;

    const std::filesystem::path slotPath = matchedSlot->format == PluginFormat::Builtin
        ? std::filesystem::path(matchedSlot->id)
        : pathFromUtf8(matchedSlot->path);
    ctx.thumbnailId = pluginThumbnailId(matchedSlot->format, slotPath, matchedSlot->name);

    for (const PluginInfo& info : pluginStore_.plugins) {
        if (pluginThumbnailId(info) == ctx.thumbnailId) {
            ctx.favorite = info.favorite;
            break;
        }
    }

    const std::string thumbIdCopy = ctx.thumbnailId;
    ctx.onToggleFavorite = [this, thumbIdCopy](bool fav) {
        bool changed = false;
        for (PluginInfo& info : pluginStore_.plugins) {
            if (pluginThumbnailId(info) == thumbIdCopy) {
                info.favorite = fav;
                changed = true;
            }
        }
        if (changed) {
            pluginStore_.save();
            if (pluginsPanel_ != nullptr)
                pluginsPanel_->setStore(pluginStore_);
            if (pluginManager_ != nullptr)
                pluginManager_->setStore(pluginStore_.scanned());
        }
    };
    ctx.onThumbnailChanged = [this] {
        if (pluginsPanel_ != nullptr)
            pluginsPanel_->refreshThumbnails();
    };

    if (isInstrument) {
        const Channel& ch = project_.channels()[foundChannel];
        ctx.locationLabel = tr("Channel %1").arg(static_cast<qulonglong>(foundChannel + 1));
        ctx.slotTitle = QString::fromStdString(matchedSlot->name);
        ctx.bypassed = ch.muted;
        ctx.onSetBypassed = [this, handle](bool muted) {
            for (std::size_t c = 0; c < project_.channels().size(); ++c) {
                if (project_.channels()[c].instrument
                    && project_.channels()[c].instrument->instance == handle) {
                    recordUndo(tr("Mute Channel"));
                    project_.setChannelMuted(c, muted);
                    channelRack_->update();
                    syncChannelRack();
                    refreshOpenPluginEditors();
                    break;
                }
            }
        };
        ctx.onReplace = [this, handle] {
            const QPoint pos = QCursor::pos();
            QTimer::singleShot(0, this, [this, handle, pos] {
                for (std::size_t c = 0; c < project_.channels().size(); ++c) {
                    if (project_.channels()[c].instrument
                        && project_.channels()[c].instrument->instance == handle) {
                        showInstrumentChooserMenu(c, pos);
                        break;
                    }
                }
            });
        };
        ctx.onRemove = [this, handle] {
            QTimer::singleShot(0, this, [this, handle] {
                for (std::size_t c = 0; c < project_.channels().size(); ++c) {
                    if (project_.channels()[c].instrument
                        && project_.channels()[c].instrument->instance == handle) {
                        recordUndo(tr("Remove Instrument"));
                        project_.setChannelInstrument(c, std::nullopt);
                        syncMixerPanel();
                        channelRack_->refresh();
                        syncChannelRack();
                        releaseUnusedPlugins();
                        break;
                    }
                }
            });
        };
        ctx.onShowInMixer = [this, handle] {
            for (std::size_t c = 0; c < project_.channels().size(); ++c) {
                if (project_.channels()[c].instrument
                    && project_.channels()[c].instrument->instance == handle) {
                    const int ins = project_.channels()[c].insert;
                    setMixerVisible(true);
                    if (mixerPanel_ != nullptr && ins >= 0
                        && static_cast<std::size_t>(ins) < mixerPanel_->trackCount()) {
                        mixerPanel_->setSelectedTrack(static_cast<std::size_t>(ins));
                    }
                    break;
                }
            }
        };
    } else {
        const PluginSlot& effect = mixer_.track(foundTrack).effects[foundEffectSlot];
        ctx.locationLabel = QString::fromStdString(mixer_.track(foundTrack).name);
        ctx.slotTitle = tr("%1 - %2")
                            .arg(static_cast<qulonglong>(foundEffectSlot + 1))
                            .arg(QString::fromStdString(effect.name));
        ctx.bypassed = allEffectsBypassed_ || effect.bypassed;
        ctx.onSetBypassed = [this, handle](bool bypassed) {
            for (std::size_t t = 0; t < mixer_.size(); ++t) {
                const auto& effects = mixer_.track(t).effects;
                for (std::size_t s = 0; s < effects.size(); ++s) {
                    if (effects[s].instance == handle) {
                        recordUndo(tr("Bypass Effect"));
                        mixer_.setEffectBypassed(t, s, bypassed);
                        updateMixer();
                        syncEffectCounts();
                        return;
                    }
                }
            }
        };
        ctx.onReplace = [this, handle] {
            const QPoint pos = QCursor::pos();
            QTimer::singleShot(0, this, [this, handle, pos] {
                for (std::size_t t = 0; t < mixer_.size(); ++t) {
                    const auto& effects = mixer_.track(t).effects;
                    for (std::size_t s = 0; s < effects.size(); ++s) {
                        if (effects[s].instance == handle) {
                            showEffectChooserMenu(t, s, pos);
                            return;
                        }
                    }
                }
            });
        };
        ctx.onRemove = [this, handle] {
            QTimer::singleShot(0, this, [this, handle] {
                for (std::size_t t = 0; t < mixer_.size(); ++t) {
                    const auto& effects = mixer_.track(t).effects;
                    for (std::size_t s = 0; s < effects.size(); ++s) {
                        if (effects[s].instance == handle) {
                            recordUndo(tr("Remove Effect"));
                            mixer_.removeEffect(t, s);
                            updateMixer();
                            releaseUnusedPlugins();
                            syncEffectCounts();
                            return;
                        }
                    }
                }
            });
        };
        ctx.onShowInMixer = [this, handle] {
            for (std::size_t t = 0; t < mixer_.size(); ++t) {
                for (const PluginSlot& eff : mixer_.track(t).effects) {
                    if (eff.instance == handle) {
                        setMixerVisible(true);
                        if (mixerPanel_ != nullptr && t < mixerPanel_->trackCount())
                            mixerPanel_->setSelectedTrack(t);
                        return;
                    }
                }
            }
        };
    }

    return ctx;
}

void MainWindow::refreshOpenPluginEditors()
{
    for (auto& [handle, widget] : pluginEditors_) {
        if (widget.isNull())
            continue;
        PluginEditorContext ctx = buildPluginEditorContext(handle);
        if (auto* ed = qobject_cast<PluginEditorWindow*>(widget.data()))
            ed->setEditorContext(std::move(ctx));
        else if (auto* paramEd = qobject_cast<ParameterEditorWindow*>(widget.data()))
            paramEd->setEditorContext(std::move(ctx));
    }
}

void MainWindow::openPluginEditor(std::uint64_t handle, const QString& title)
{
    if (const auto it = pluginEditors_.find(handle); it != pluginEditors_.end() && !it->second.isNull()) {
        PluginEditorContext ctx = buildPluginEditorContext(handle);
        if (auto* ed = qobject_cast<PluginEditorWindow*>(it->second.data()))
            ed->setEditorContext(std::move(ctx));
        else if (auto* paramEd = qobject_cast<ParameterEditorWindow*>(it->second.data()))
            paramEd->setEditorContext(std::move(ctx));
        it->second->showNormal();
        it->second->raise();
        it->second->activateWindow();
        return;
    }
    const auto instance = pluginHost_.instance(handle);
    if (instance == nullptr)
        return;
    if (!instance->processor().hasEditor() && EqEditorWindow::edits(*instance)) {
        auto* eq = new EqEditorWindow(instance, title, static_cast<double>(settings_.sampleRate), this);
        connect(eq, &EqEditorWindow::edited, this, [this] {
            pluginsEdited_ = true; // unsaved changes
            setWindowModified(true);
        });
        // The power button bypasses the insert effect this instance is.
        const auto slotOf = [this, handle]() -> std::optional<std::pair<std::size_t, std::size_t>> {
            for (std::size_t track = 0; track < mixer_.size(); ++track) {
                const auto& effects = mixer_.track(track).effects;
                for (std::size_t slot = 0; slot < effects.size(); ++slot)
                    if (effects[slot].instance == handle)
                        return std::pair{track, slot};
            }
            return std::nullopt;
        };
        if (const auto at = slotOf())
            eq->setBypassed(mixer_.track(at->first).effects[at->second].bypassed);
        connect(eq, &EqEditorWindow::bypassToggled, this, [this, slotOf](bool bypassed) {
            if (const auto at = slotOf())
                setEffectBypassed(at->first, at->second, bypassed);
        });
        pluginEditors_[handle] = eq;
        eq->show();
        return;
    }
    if (!instance->processor().hasEditor() && SynthEditorWindow::edits(*instance)) {
        auto* synth = new SynthEditorWindow(instance, title, this);
        connect(synth, &SynthEditorWindow::edited, this, [this] {
            pluginsEdited_ = true; // unsaved changes
            setWindowModified(true);
        });
        // The power button switches off the channel's instrument this instance is.
        const auto channelOf = [this, handle]() -> std::optional<std::size_t> {
            const auto& channels = project_.channels();
            for (std::size_t channel = 0; channel < channels.size(); ++channel)
                if (channels[channel].instrument && channels[channel].instrument->instance == handle)
                    return channel;
            return std::nullopt;
        };
        if (const auto channel = channelOf())
            synth->setBypassed(project_.channels()[*channel].instrument->bypassed);
        connect(synth, &SynthEditorWindow::bypassToggled, this, [this, channelOf](bool bypassed) {
            const auto channel = channelOf();
            if (!channel)
                return;
            recordUndo(tr("Bypass Instrument"));
            PluginSlot slot = *project_.channels()[*channel].instrument;
            slot.bypassed = bypassed;
            project_.setChannelInstrument(*channel, std::move(slot));
            syncChannelRack();
            pluginsEdited_ = true; // unsaved changes
            setWindowModified(true);
        });
        pluginEditors_[handle] = synth;
        synth->show();
        return;
    }
    PluginEditorContext context = buildPluginEditorContext(handle);
    if (!instance->processor().hasEditor()) {
        // No editor of its own (the other built-in effects): a knob per parameter.
        if (instance->processor().parameters().empty())
            return;
        auto* controls = new ParameterEditorWindow(instance, title, this);
        controls->setEditorContext(std::move(context));
        connect(controls, &ParameterEditorWindow::edited, this, [this] {
            pluginsEdited_ = true; // unsaved changes
            setWindowModified(true);
        });
        pluginEditors_[handle] = controls;
        controls->show();
        return;
    }
    auto* editor = new PluginEditorWindow(instance, title, this);
    editor->setEditorContext(std::move(context));
    connect(editor, &PluginEditorWindow::edited, this, [this] {
        pluginsEdited_ = true; // unsaved changes
        setWindowModified(true);
    });
    QString error;
    if (!editor->open(&error)) {
        editor->close(); // deletes it
        statusBar()->showMessage(tr("%1: %2").arg(title, error), 8000);
        return;
    }
    pluginEditors_[handle] = editor;
    updatePluginIdle();
}

void MainWindow::setEffectBypassed(std::size_t track, std::size_t slot, bool bypassed)
{
    if (track >= mixer_.size() || slot >= mixer_.track(track).effects.size())
        return;
    recordUndo(tr("Bypass Effect"));
    mixer_.setEffectBypassed(track, slot, bypassed);
    updateMixer();
    syncEffectCounts();
    const auto editor = pluginEditors_.find(mixer_.track(track).effects[slot].instance);
    if (editor != pluginEditors_.end())
        if (auto* eq = qobject_cast<EqEditorWindow*>(editor->second.data()))
            eq->setBypassed(bypassed);
}

std::vector<std::uint64_t> MainWindow::pluginsInUse() const
{
    std::vector<std::uint64_t> handles;
    for (const Channel& channel : project_.channels())
        if (channel.instrument)
            handles.push_back(channel.instrument->instance);
    for (std::size_t i = 0; i < mixer_.size(); ++i)
        for (const PluginSlot& effect : mixer_.track(i).effects)
            handles.push_back(effect.instance);
    return handles;
}

void MainWindow::releaseUnusedPlugins()
{
    pluginHost_.retainOnly(pluginsInUse(), [this](std::uint64_t handle) {
        std::erase(unpublishedInstruments_, handle);
        // Preserve the plugin's parameter state across Undo/Redo before unloading it.
        if (pluginHost_.instance(handle) != nullptr) {
            const std::vector<std::uint8_t> state = pluginHost_.saveState(handle);
            undoHistory_.forEachPluginSlot([handle, &state](PluginSlot& slot) {
                if (slot.instance == handle) {
                    if (!state.empty())
                        slot.savedState = state;
                    slot.instance = 0;
                }
            });
        }
        if (const auto it = pluginEditors_.find(handle); it != pluginEditors_.end()) {
            if (!it->second.isNull())
                delete it->second.data(); // closes the editor first
            pluginEditors_.erase(it);
        }
    });
    std::erase_if(pluginEditors_, [](const auto& entry) { return entry.second.isNull(); });
    updatePluginIdle();
}

void MainWindow::syncEffectCounts()
{
    for (std::size_t i = 0; i < mixer_.size(); ++i) {
        mixerPanel_->setTrackEffects(i, mixer_.track(i).effects);
        mixerPanel_->setEffectCount(i, mixer_.track(i).effects.size());
    }
    if (playlist_ != nullptr)
        playlist_->updateHeaders();
    refreshOpenPluginEditors();
}

void MainWindow::updatePluginIdle()
{
    if (pluginHost_.size() == 0)
        pluginIdleTimer_->stop();
    else if (!pluginIdleTimer_->isActive())
        pluginIdleTimer_->start(33);
}

void MainWindow::newProject()
{
    if (!maybeSaveChanges())
        return;
    resetProject();
    projectPath_.clear();
    markProjectSaved();
    statusBar()->showMessage(tr("New project"), 3000);
}

void MainWindow::closeProject()
{
    if (!maybeSaveChanges())
        return;

    if (previewer_ != nullptr)
        previewer_->stop();
    if (stemProgress_ != nullptr) {
        if (stemSeparator_ != nullptr)
            stemSeparator_->cancel();
        stemProgress_->deleteLater();
        stemProgress_ = nullptr;
    }
    settingsDialog_->close();
    if (pluginManager_ != nullptr)
        pluginManager_->close();

    if (coversScreen_)
        toggleFullScreen();
    saveWindowStateToSettings();

    // Remember which detached editor / mixer windows were open so they return
    // automatically once the user picks or creates a new project.
    const bool mixerWinWasVisible = mixerWindow_ != nullptr && mixerWindow_->isVisible();
    std::array<bool, std::size(detachable_)> pageWinWasVisible{};
    for (std::size_t i = 0; i < std::size(detachable_); ++i) {
        pageWinWasVisible[i] = detachable_[i].window != nullptr && detachable_[i].window->isVisible();
    }

    QApplication::setQuitOnLastWindowClosed(false);
    for (const DetachablePage& d : detachable_)
        if (d.window != nullptr)
            d.window->hide();
    if (mixerWindow_ != nullptr)
        mixerWindow_->hide();
    hide();

    // Completely unload the closed project's plugins, audio clips, and undo state.
    resetProject();
    projectPath_.clear();
    markProjectSaved();
    updateWindowTitle();

    const auto restoreWorkspaceWindows = [&, this] {
        show();
        for (std::size_t i = 0; i < std::size(detachable_); ++i) {
            if (pageWinWasVisible[i] && detachable_[i].window != nullptr)
                detachable_[i].window->show();
        }
        if (mixerWinWasVisible && mixerWindow_ != nullptr)
            mixerWindow_->show();
        raise();
        activateWindow();
        QApplication::setQuitOnLastWindowClosed(true);
    };

    for (;;) {
        LauncherDialog::AudioStatus status;
        if (audio_.isRunning()) {
            status.device = QString::fromStdString(audio_.deviceName());
            status.sampleRate = audio_.sampleRate();
            status.bufferFrames = audio_.bufferFrames();
        }
        LauncherDialog launcher(status, kDefaultTempo, nullptr, false);
        launcher.exec();

        switch (launcher.choice()) {
        case LauncherDialog::Choice::None:
            QApplication::setQuitOnLastWindowClosed(true);
            QApplication::quit();
            return;
        case LauncherDialog::Choice::NewProject:
            restoreWorkspaceWindows();
            startEmptyProject(launcher.tempo());
            statusBar()->showMessage(tr("New project"), 3000);
            return;
        case LauncherDialog::Choice::OpenFile:
            restoreWorkspaceWindows();
            openProjectFile(launcher.path());
            return;
        case LauncherDialog::Choice::BrowseOpen: {
            const QString picked = QFileDialog::getOpenFileName(nullptr, tr("Open Project"), projectFolder(),
                tr("VloomDAW & MIDI files (*.vloom *.mid *.midi);;VloomDAW project "
                   "(*.vloom);;MIDI files (*.mid *.midi)"));
            if (!isLocalPath(picked))
                continue;
            rememberFolder(picked);
            restoreWorkspaceWindows();
            openProjectFile(picked);
            return;
        }
        case LauncherDialog::Choice::AudioSettings:
            listDevices();
            refreshStemSettings();
            settingsDialog_->showPage(SettingsDialog::Page::Audio);
            settingsDialog_->exec();
            continue;
        }
    }
}

void MainWindow::resetProject()
{
    stopAndRewind();
    ++projectGeneration_;
    undoHistory_.clear();
    updateUndoActions();
    unpublishedInstruments_.clear(); // the old project's
    transport_->setTempo(kDefaultTempo);
    playlist_->setTempo(kDefaultTempo);
    pendingTempo_ = kDefaultTempo; // sent by refreshFromEngine()
    transport_->recordAction()->setChecked(false);
    transport_->keyDisplay()->setRootNote(0); // C
    transport_->keyDisplay()->setScale(Scale::Major);
    pianoRoll_->setKey(0, Scale::Major);
    noteHistory_.clear(); // another project: nothing of the old one to undo
    noteEditBefore_.reset();
    project_ = Project{0};
    rackAudio_.reset();
    clipAudio_.clear();
    playlist_->clearWaveCache();
    currentPattern_ = 0;
    currentChannel_ = 0;
    loopStartTicks_ = 0;
    loopEndTicks_ = 0;
    songLoopStart_ = -1.0;
    songLoopEnd_ = -1.0;
    transport_->setLoopEnabled(true);
    playlist_->setLoopRange(0, 0);
    playlist_->setLoopActive(true);
    transport_->songModeAction()->setChecked(false);
    allEffectsBypassed_ = false;
    mixerPanel_->setAllEffectsBypassed(false);
    mixer_ = MixerModel(kMixerInserts); // the old project's inserts and effects go too
    syncMixerPanel();
    syncInsertNames();
    syncPatternSelector();
    refreshEditors();
    playlist_->setSelectedTrack(0);
    mixerPanel_->setSelectedTrack(MixerModel::kMaster);
    selectChannel(0);
    syncChannelRack();
    updateMixer();
    releaseUnusedPlugins(); // the old project's instruments and effects
}

// ---------------------------------------------------------------- undo / redo & Edit menu

UndoSnapshot MainWindow::captureUndoSnapshot() const
{
    UndoSnapshot snap;
    snap.project = project_;
    snap.mixer = mixer_;
    snap.song.tempo = transport_->tempo();
    snap.song.keyRoot = transport_->keyDisplay()->rootNote();
    snap.song.scale = transport_->keyDisplay()->scale();
    snap.song.songMode = songMode_;
    snap.song.loopEnabled = transport_->loopEnabled();
    snap.song.loopStart = loopStartTicks_;
    snap.song.loopEnd = loopEndTicks_;
    snap.currentPattern = currentPattern_;
    snap.currentChannel = currentChannel_;
    snap.selectedTrack = playlist_ != nullptr ? playlist_->selectedTrack() : 0;
    snap.selectedMixerTrack = mixerPanel_ != nullptr ? mixerPanel_->selectedTrack() : 0;
    return snap;
}

void MainWindow::recordUndo(const QString& label, std::string coalesceKey)
{
    if (applyingUndoRedo_)
        return;
    UndoSnapshot snap = captureUndoSnapshot();
    undoHistory_.push(
        std::move(snap), label.toStdString(), std::move(coalesceKey), QDateTime::currentMSecsSinceEpoch());
    updateUndoActions();
}

void MainWindow::applyUndoSnapshot(UndoSnapshot snapshot)
{
    applyingUndoRedo_ = true;
    ++projectGeneration_;
    const double rate = audio_.sampleRate() > 0 ? audio_.sampleRate() : audio_.requestedSampleRate();

    const auto ensureLoaded = [&](PluginSlot& slot, bool instrument) {
        if (slot.instance != 0 && pluginHost_.instance(slot.instance) != nullptr)
            return;
        slot.instance = 0;
        if (!isScannedPlugin(slot))
            return;
        const PluginHost::Loaded loaded = pluginHost_.loadWithState(
            slot.format, pathFromUtf8(slot.path), slot.id, slot.name, rate, slot.savedState);
        if (loaded.handle != 0) {
            slot.instance = loaded.handle;
            slot.savedState.clear();
            if (instrument && audio_.sampleRate() <= 0)
                unpublishedInstruments_.push_back(slot.instance);
        }
    };

    for (std::size_t c = 0; c < snapshot.project.channels().size(); ++c) {
        if (auto slot = snapshot.project.channels()[c].instrument) {
            ensureLoaded(*slot, true);
            snapshot.project.setChannelInstrument(c, std::move(slot));
        }
    }
    for (std::size_t i = 0; i < snapshot.mixer.size(); ++i) {
        std::vector<PluginSlot> effects = snapshot.mixer.track(i).effects;
        while (snapshot.mixer.removeEffect(i, 0)) { }
        for (PluginSlot& eff : effects) {
            ensureLoaded(eff, false);
            snapshot.mixer.addEffect(i, std::move(eff));
        }
    }

    project_ = std::move(snapshot.project);
    mixer_ = std::move(snapshot.mixer);

    transport_->setTempo(snapshot.song.tempo);
    pendingTempo_ = snapshot.song.tempo;
    playlist_->setTempo(snapshot.song.tempo);
    transport_->keyDisplay()->setRootNote(snapshot.song.keyRoot);
    transport_->keyDisplay()->setScale(snapshot.song.scale);
    pianoRoll_->setKey(snapshot.song.keyRoot, snapshot.song.scale);
    {
        const QSignalBlocker block(transport_->songModeAction());
        transport_->songModeAction()->setChecked(snapshot.song.songMode);
    }
    songMode_ = snapshot.song.songMode;
    loopStartTicks_ = snapshot.song.loopEnd > snapshot.song.loopStart ? snapshot.song.loopStart : 0;
    loopEndTicks_ = snapshot.song.loopEnd > snapshot.song.loopStart ? snapshot.song.loopEnd : 0;
    transport_->setLoopEnabled(snapshot.song.loopEnabled);
    playlist_->setLoopRange(loopStartTicks_, loopEndTicks_);
    playlist_->setLoopActive(snapshot.song.loopEnabled);
    songLoopStart_ = -1.0;
    songLoopEnd_ = -1.0;

    currentPattern_
        = project_.patterns().empty() ? 0 : std::min(snapshot.currentPattern, project_.patterns().size() - 1);
    currentChannel_
        = project_.channels().empty() ? 0 : std::min(snapshot.currentChannel, project_.channels().size() - 1);

    syncMixerPanel();
    syncInsertNames();
    syncPatternSelector();
    refreshEditors();
    selectChannel(currentChannel_);
    if (!project_.tracks().empty()) {
        const int clampedTrack
            = std::clamp(snapshot.selectedTrack, 0, static_cast<int>(project_.tracks().size()) - 1);
        playlist_->setSelectedTrack(clampedTrack);
    }
    if (mixerPanel_->trackCount() > 0) {
        const std::size_t clampedMix = std::min(snapshot.selectedMixerTrack, mixerPanel_->trackCount() - 1);
        mixerPanel_->setSelectedTrack(clampedMix);
    }
    syncChannelRack();
    updateMixer();
    syncEffectCounts();
    releaseUnusedPlugins();
    updatePluginIdle();
    updateWindowTitle();

    applyingUndoRedo_ = false;
    updateUndoActions();
}

void MainWindow::performUndo()
{
    if (auto* edit = qobject_cast<QLineEdit*>(QApplication::focusWidget())) {
        edit->undo();
        return;
    }
    if (!undoHistory_.canUndo()) {
        statusBar()->showMessage(tr("Nothing to undo."), 2500);
        return;
    }
    const QString label = QString::fromStdString(std::string(undoHistory_.undoLabel()));
    if (auto target = undoHistory_.undo(captureUndoSnapshot())) {
        applyUndoSnapshot(std::move(*target));
        statusBar()->showMessage(label.isEmpty() ? tr("Undone") : tr("Undid: %1").arg(label), 3000);
    }
}

void MainWindow::performRedo()
{
    if (auto* edit = qobject_cast<QLineEdit*>(QApplication::focusWidget())) {
        edit->redo();
        return;
    }
    if (!undoHistory_.canRedo()) {
        statusBar()->showMessage(tr("Nothing to redo."), 2500);
        return;
    }
    const QString label = QString::fromStdString(std::string(undoHistory_.redoLabel()));
    if (auto target = undoHistory_.redo(captureUndoSnapshot())) {
        applyUndoSnapshot(std::move(*target));
        statusBar()->showMessage(label.isEmpty() ? tr("Redone") : tr("Redid: %1").arg(label), 3000);
    }
}

bool MainWindow::isPianoRollActive() const
{
    QWidget* focus = QApplication::focusWidget();
    if (pianoRoll_ != nullptr && (focus == pianoRoll_ || pianoRoll_->isAncestorOf(focus)))
        return true;
    if (playlist_ != nullptr && (focus == playlist_ || playlist_->isAncestorOf(focus)))
        return false;
    QWidget* activeWin = QApplication::activeWindow();
    const DetachablePage* pr = detachable(WorkspacePage::PianoRoll);
    const DetachablePage* pl = detachable(WorkspacePage::Playlist);
    if (pr != nullptr && isPageDetached(WorkspacePage::PianoRoll) && pr->window == activeWin)
        return true;
    if (pl != nullptr && isPageDetached(WorkspacePage::Playlist) && pl->window == activeWin)
        return false;
    if (workspace_ != nullptr && workspace_->currentIndex() == static_cast<int>(WorkspacePage::PianoRoll)) {
        return true;
    }
    if (workspace_ != nullptr && workspace_->currentIndex() == static_cast<int>(WorkspacePage::Playlist)) {
        return false;
    }
    if (pr != nullptr && isPageDetached(WorkspacePage::PianoRoll) && pr->window != nullptr
        && pr->window->isVisible()
        && !(pl != nullptr && isPageDetached(WorkspacePage::Playlist) && pl->window != nullptr
            && pl->window->isVisible())) {
        return true;
    }
    return false;
}

void MainWindow::performCut()
{
    if (auto* edit = qobject_cast<QLineEdit*>(QApplication::focusWidget())) {
        edit->cut();
        return;
    }
    if (isPianoRollActive()) {
        if (pianoRoll_ != nullptr && pianoRoll_->hasSelection()) {
            pianoRoll_->cutSelection();
            statusBar()->showMessage(tr("Cut selected notes"), 2500);
        }
        return;
    }
    if (playlist_ != nullptr && playlist_->hasSelectedClips()) {
        playlist_->cutSelectedClips();
        statusBar()->showMessage(tr("Cut selected clips"), 2500);
    }
}

void MainWindow::performCopy()
{
    if (auto* edit = qobject_cast<QLineEdit*>(QApplication::focusWidget())) {
        edit->copy();
        return;
    }
    if (isPianoRollActive()) {
        if (pianoRoll_ != nullptr && pianoRoll_->hasSelection()) {
            pianoRoll_->copySelection();
            statusBar()->showMessage(tr("Copied selected notes"), 2500);
        }
        return;
    }
    if (playlist_ != nullptr && playlist_->hasSelectedClips()) {
        playlist_->copySelectedClips();
        statusBar()->showMessage(tr("Copied selected clips"), 2500);
    }
}

void MainWindow::performPaste()
{
    if (auto* edit = qobject_cast<QLineEdit*>(QApplication::focusWidget())) {
        edit->paste();
        return;
    }
    if (isPianoRollActive()) {
        if (pianoRoll_ != nullptr && pianoRoll_->canPaste())
            pianoRoll_->paste();
        return;
    }
    if (playlist_ != nullptr && playlist_->hasClipClipboard())
        playlist_->pasteClips();
}

void MainWindow::performDuplicate()
{
    if (qobject_cast<QLineEdit*>(QApplication::focusWidget()) != nullptr)
        return;
    if (isPianoRollActive()) {
        if (pianoRoll_ != nullptr && pianoRoll_->hasSelection())
            pianoRoll_->duplicateSelection();
        return;
    }
    if (playlist_ != nullptr && playlist_->hasSelectedClips())
        playlist_->duplicateSelectedClips();
}

void MainWindow::performDelete()
{
    if (auto* edit = qobject_cast<QLineEdit*>(QApplication::focusWidget())) {
        edit->del();
        return;
    }
    if (isPianoRollActive()) {
        if (pianoRoll_ != nullptr && pianoRoll_->hasSelection())
            pianoRoll_->deleteSelection();
        return;
    }
    if (playlist_ != nullptr && playlist_->hasSelectedClips())
        playlist_->deleteSelectedClips();
}

void MainWindow::performSelectAll()
{
    if (auto* edit = qobject_cast<QLineEdit*>(QApplication::focusWidget())) {
        edit->selectAll();
        return;
    }
    if (isPianoRollActive()) {
        if (pianoRoll_ != nullptr)
            pianoRoll_->selectAll();
        return;
    }
    if (playlist_ != nullptr)
        playlist_->selectAllClips();
}

void MainWindow::performQuantize()
{
    if (qobject_cast<QLineEdit*>(QApplication::focusWidget()) != nullptr)
        return;
    if (isPianoRollActive()) {
        if (pianoRoll_ != nullptr)
            pianoRoll_->quantize();
        return;
    }
    if (playlist_ != nullptr)
        playlist_->quantizeSelectedClips();
}

void MainWindow::updateUndoActions()
{
    if (undoAction_ != nullptr) {
        const bool can = undoHistory_.canUndo();
        const QString label = QString::fromStdString(std::string(undoHistory_.undoLabel()));
        undoAction_->setText(can && !label.isEmpty() ? tr("&Undo %1").arg(label) : tr("&Undo"));
        undoAction_->setEnabled(can);
    }
    if (redoAction_ != nullptr) {
        const bool can = undoHistory_.canRedo();
        const QString label = QString::fromStdString(std::string(undoHistory_.redoLabel()));
        redoAction_->setText(can && !label.isEmpty() ? tr("&Redo %1").arg(label) : tr("&Redo"));
        redoAction_->setEnabled(can);
    }
}

// ---------------------------------------------------------------- project files

namespace {

const QString kProjectFilter = QObject::tr("VloomDAW project (*.vloom)");

bool samePath(const QString& a, const QString& b)
{
#if defined(Q_OS_WIN)
    return QDir::cleanPath(a).compare(QDir::cleanPath(b), Qt::CaseInsensitive) == 0;
#else
    return QDir::cleanPath(a) == QDir::cleanPath(b);
#endif
}

} // namespace

ProjectData MainWindow::projectData(bool withPluginStates) const
{
    ProjectData out;
    out.project = project_;
    out.mixer = mixer_;
    out.song.tempo = transport_->tempo();
    out.song.keyRoot = transport_->keyDisplay()->rootNote();
    out.song.scale = transport_->keyDisplay()->scale();
    out.song.songMode = songMode_;
    out.song.loopEnabled = transport_->loopEnabled();
    out.song.loopStart = loopStartTicks_;
    out.song.loopEnd = loopEndTicks_;
    if (!withPluginStates)
        return out;
    // A plugin that is not loaded (or whose bridge ended) keeps its last good settings.
    const auto stateOf
        = [this](const PluginSlot& slot) { return pluginHost_.saveState(slot.instance, slot.savedState); };
    for (const Channel& channel : project_.channels())
        out.states.instruments.push_back(
            channel.instrument ? stateOf(*channel.instrument) : std::vector<std::uint8_t>{});
    for (std::size_t i = 0; i < mixer_.size(); ++i) {
        auto& states = out.states.effects.emplace_back();
        for (const PluginSlot& effect : mixer_.track(i).effects)
            states.push_back(stateOf(effect));
    }
    return out;
}

std::string MainWindow::projectSnapshot() const
{
    return ProjectFile::write(projectData(false), projectPath_.parent_path());
}

bool MainWindow::isProjectModified() const { return pluginsEdited_ || projectSnapshot() != savedSnapshot_; }

std::uint64_t MainWindow::pluginFingerprint() const
{
    std::uint64_t print = 14695981039346656037ull; // FNV-1a
    const auto mix = [&print](std::uint8_t byte) { print = (print ^ byte) * 1099511628211ull; };
    const auto add = [&](const PluginSlot& slot) {
        const auto instance = pluginHost_.instance(slot.instance);
        if (instance == nullptr)
            return;
        const std::vector<std::uint8_t> state = pluginHost_.saveState(slot.instance, slot.savedState);
        for (std::size_t size = state.size(), i = 0; i < 8; ++i, size >>= 8)
            mix(static_cast<std::uint8_t>(size)); // the length too: states do not run together
        for (const std::uint8_t byte : state)
            mix(byte);
    };
    for (const Channel& channel : project_.channels())
        if (channel.instrument)
            add(*channel.instrument);
    for (std::size_t i = 0; i < mixer_.size(); ++i)
        for (const PluginSlot& effect : mixer_.track(i).effects)
            add(effect);
    return print;
}

void MainWindow::markProjectSaved()
{
    savedSnapshot_ = projectSnapshot();
    savedPluginPrint_ = pluginFingerprint();
    pluginsEdited_ = false;
    updateWindowTitle();
}

void MainWindow::updateWindowTitle()
{
    const QString name = projectPath_.empty() ? tr("Untitled") : toQString(projectPath_.stem());
    setWindowTitle(tr("%1[*] - VloomDAW").arg(name));
    // Comparing means writing the project out as text: on a very large
    // project that takes a while, so the check then runs less often (at
    // most about 2 % of the UI thread's time).
    QElapsedTimer clock;
    clock.start();
    setWindowModified(isProjectModified());
    if (modifiedTimer_ != nullptr)
        modifiedTimer_->setInterval(static_cast<int>(std::clamp<qint64>(clock.elapsed() * 50, 1000, 30000)));
}

bool MainWindow::maybeSaveChanges()
{
    // Plugin settings are compared here too (not every plugin reports edits).
    if (!isProjectModified() && pluginFingerprint() == savedPluginPrint_)
        return true;
    const QString name = projectPath_.empty() ? tr("Untitled") : toQString(projectPath_.stem());
    const auto answer = QMessageBox::warning(this, tr("Unsaved changes"),
        tr("Save the changes to “%1” before closing it?").arg(name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    return answer == QMessageBox::Discard || saveProject();
}

bool MainWindow::saveProject()
{
    if (projectPath_.empty())
        return saveProjectAs();
    return writeProject(toQString(projectPath_));
}

bool MainWindow::saveProjectAs()
{
    const QString suggested = projectPath_.empty() ? QDir(projectFolder()).filePath(tr("Untitled.vloom"))
                                                   : toQString(projectPath_);
    QString path = QFileDialog::getSaveFileName(this, tr("Save Project As"), suggested, kProjectFilter);
    if (path.isEmpty())
        return false;
    if (!path.endsWith(QStringLiteral(".vloom"), Qt::CaseInsensitive))
        path += QStringLiteral(".vloom");
    return writeProject(path);
}

bool MainWindow::writeProject(const QString& path)
{
    std::string error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool saved = ProjectFile::save(toPath(path), projectData(true), &error);
    QApplication::restoreOverrideCursor();
    if (!saved) {
        QMessageBox::critical(this, tr("Could not save"),
            tr("The project could not be saved to %1.\n\n%2")
                .arg(QDir::toNativeSeparators(path), QString::fromStdString(error)));
        return false;
    }
    projectPath_ = toPath(path);
    markProjectSaved();
    recent::add(path);
    rememberFolder(path);
    statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(path).fileName()), 3000);
    return true;
}

void MainWindow::openProject()
{
    if (!maybeSaveChanges())
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Project"), projectFolder(),
        tr("VloomDAW & MIDI files (*.vloom *.mid *.midi);;VloomDAW project (*.vloom);;MIDI files "
           "(*.mid *.midi)"));
    if (!path.isEmpty())
        openProjectFile(path);
}

void MainWindow::openProjectFile(const QString& path)
{
    if (!isLocalPath(path)) {
        QMessageBox::warning(this, tr("Could not open"),
            tr("%1 could not be opened.\n\nProjects on network shares cannot be opened.")
                .arg(QDir::toNativeSeparators(path)));
        return;
    }

    const QFileInfo fileInfo(path);
    if (isMidiFile(fileInfo.fileName().toStdString())) {
        resetProject();
        projectPath_.clear();
        const std::size_t importedTracks = importMidiFile(path, -1, 0, false);
        if (importedTracks > 0) {
            undoHistory_.clear();
            updateUndoActions();
            markProjectSaved();
            rememberFolder(path);
        }
        return;
    }
    const QString projectTitle
        = fileInfo.completeBaseName().isEmpty() ? fileInfo.fileName() : fileInfo.completeBaseName();
    ProjectLoadDialog progress(projectTitle, this);
    if (!progress.advance(0, 100, tr("Reading project file…"), fileInfo.fileName())) {
        progress.finishAndClose();
        statusBar()->showMessage(tr("Opening project cancelled."), 3000);
        return;
    }

    ProjectFile::ReadResult read = ProjectFile::load(toPath(path));
    if (!read.data) {
        progress.finishAndClose();
        if (!QFileInfo::exists(path))
            recent::remove(path);
        QMessageBox::warning(this, tr("Could not open"),
            tr("%1 could not be opened.\n\n%2")
                .arg(QDir::toNativeSeparators(path), QString::fromStdString(read.error)));
        return;
    }
    if (progress.wasCanceled()) {
        progress.finishAndClose();
        statusBar()->showMessage(tr("Opening project cancelled."), 3000);
        return;
    }

    QStringList problems;
    for (const std::string& warning : read.warnings)
        problems << QString::fromStdString(warning);
    if (!applyProject(std::move(*read.data), problems, &progress)) {
        progress.finishAndClose();
        statusBar()->showMessage(tr("Opening project cancelled."), 3000);
        return;
    }

    progress.finishAndClose();
    undoHistory_.clear();
    updateUndoActions();
    projectPath_ = toPath(path);
    markProjectSaved();
    recent::add(path);
    rememberFolder(path);
    statusBar()->showMessage(tr("Opened %1").arg(fileInfo.fileName()), 3000);
    if (!problems.isEmpty()) {
        QMessageBox box(QMessageBox::Information, tr("Opened with changes"),
            tr("%1 opened, but some things could not be restored.").arg(fileInfo.fileName()), QMessageBox::Ok,
            this);
        box.setDetailedText(problems.join(QLatin1Char('\n')));
        box.exec();
    }
}

void MainWindow::startEmptyProject(double tempo)
{
    undoHistory_.clear();
    updateUndoActions();
    const double clamped = std::clamp(tempo, 20.0, 999.0);
    transport_->setTempo(clamped);
    pendingTempo_ = clamped;
    playlist_->setTempo(clamped);
    syncChannelRack();
    projectPath_.clear();
    markProjectSaved();
}

void MainWindow::showLauncher()
{
    LauncherDialog::AudioStatus status;
    if (audio_.isRunning()) {
        status.device = QString::fromStdString(audio_.deviceName());
        status.sampleRate = audio_.sampleRate();
        status.bufferFrames = audio_.bufferFrames();
    }
    LauncherDialog launcher(status, transport_->tempo(), this, true);
    launcher.exec();
    switch (launcher.choice()) {
    case LauncherDialog::Choice::NewProject:
        if (!maybeSaveChanges())
            return;
        resetProject();
        transport_->setTempo(launcher.tempo());
        playlist_->setTempo(launcher.tempo());
        pendingTempo_ = launcher.tempo(); // sent by refreshFromEngine()
        syncChannelRack(); // fitted audio clips follow the tempo
        projectPath_.clear();
        markProjectSaved();
        statusBar()->showMessage(tr("New project"), 3000);
        break;
    case LauncherDialog::Choice::OpenFile:
        if (maybeSaveChanges())
            openProjectFile(launcher.path());
        break;
    case LauncherDialog::Choice::BrowseOpen:
        openProject();
        break;
    case LauncherDialog::Choice::AudioSettings:
        showSettings(SettingsDialog::Page::Audio);
        break;
    case LauncherDialog::Choice::None:
        break;
    }
}

bool MainWindow::isScannedPlugin(const PluginSlot& slot) const
{
    if (slot.format == PluginFormat::Builtin)
        return true; // part of the app: no file of the project's choosing is loaded
    const QString wanted = QString::fromStdString(slot.path);
    return std::any_of(pluginStore_.plugins.begin(), pluginStore_.plugins.end(), [&](const PluginInfo& info) {
        return info.format == slot.format && samePath(toQString(info.path), wanted);
    });
}

bool MainWindow::applyProject(ProjectData incoming, QStringList& problems, ProjectLoadDialog* progress)
{
    stopAndRewind();

    int totalInstruments = 0;
    std::vector<std::string> uniqueSamplePaths;
    for (const Channel& channel : incoming.project.channels()) {
        if (channel.instrument.has_value())
            ++totalInstruments;
        if (!channel.samplePath.empty()
            && std::find(uniqueSamplePaths.begin(), uniqueSamplePaths.end(), channel.samplePath)
                == uniqueSamplePaths.end()) {
            uniqueSamplePaths.push_back(channel.samplePath);
        }
    }
    int totalEffects = 0;
    for (std::size_t i = 0; i < incoming.mixer.size(); ++i)
        totalEffects += static_cast<int>(incoming.mixer.track(i).effects.size());

    const int totalSamples = static_cast<int>(uniqueSamplePaths.size());
    const int totalSteps = 2 + totalInstruments + totalEffects + totalSamples + 2;
    int step = 1;

    if (progress != nullptr && !progress->advance(step++, totalSteps, tr("Preparing audio engine…")))
        return false;

    const double rate = audio_.sampleRate() > 0
        ? audio_.sampleRate()
        : (audio_.requestedSampleRate() > 0 ? static_cast<double>(audio_.requestedSampleRate()) : 48000.0);
    std::vector<std::uint64_t> newlyUnpublished;

    // Only plugins the user's scan found are loaded: a project file may name
    // any library, and loading one runs its code.
    const auto load = [&](PluginSlot& slot, std::span<const std::uint8_t> state) {
        slot.instance = 0;
        slot.savedState.assign(state.begin(), state.end()); // kept unless it loads
        const QString name = QString::fromStdString(slot.name);
        if (!isScannedPlugin(slot)) {
            problems << tr("%1: not among your scanned plugins (Options > Plugin Manager)").arg(name);
            return;
        }
        const PluginHost::Loaded loaded = pluginHost_.loadWithState(
            slot.format, pathFromUtf8(slot.path), slot.id, slot.name, rate, state);
        if (loaded.handle == 0) {
            problems << tr("%1: %2").arg(name, QString::fromStdString(loaded.error));
            return;
        }
        if (!loaded.error.empty())
            problems << tr("%1: %2").arg(name, QString::fromStdString(loaded.error));
        slot.instance = loaded.handle;
        slot.savedState.clear(); // the instance holds them now
    };
    const auto stateAt = [](const auto& list, std::size_t i) -> std::span<const std::uint8_t> {
        return i < list.size() ? std::span<const std::uint8_t>(list[i]) : std::span<const std::uint8_t>();
    };

    int loadedInstIdx = 0;
    for (std::size_t c = 0; c < incoming.project.channels().size(); ++c) {
        if (auto slot = incoming.project.channels()[c].instrument) {
            ++loadedInstIdx;
            if (progress != nullptr
                && !progress->advance(step++, totalSteps,
                    tr("Loading Device: %1").arg(QString::fromStdString(slot->name)),
                    tr("Instrument %1 of %2").arg(loadedInstIdx).arg(totalInstruments))) {
                releaseUnusedPlugins();
                return false;
            }
            load(*slot, stateAt(incoming.states.instruments, c));
            if (progress != nullptr && progress->wasCanceled()) {
                releaseUnusedPlugins();
                return false;
            }
            // No device yet: startDevice() activates it at the device's rate.
            if (slot->instance != 0 && audio_.sampleRate() <= 0)
                newlyUnpublished.push_back(slot->instance);
            incoming.project.setChannelInstrument(c, std::move(slot));
        }
    }

    int loadedFxIdx = 0;
    for (std::size_t i = 0; i < incoming.mixer.size(); ++i) {
        std::vector<PluginSlot> effects = incoming.mixer.track(i).effects;
        while (incoming.mixer.removeEffect(i, 0)) { }
        const std::vector<std::vector<std::uint8_t>> none;
        const auto& states = i < incoming.states.effects.size() ? incoming.states.effects[i] : none;
        const QString trackTitle = QString::fromStdString(incoming.mixer.track(i).name);
        for (std::size_t e = 0; e < effects.size(); ++e) {
            ++loadedFxIdx;
            if (progress != nullptr
                && !progress->advance(step++, totalSteps,
                    tr("Loading Device: %1").arg(QString::fromStdString(effects[e].name)),
                    tr("FX %1 of %2 · %3").arg(loadedFxIdx).arg(totalEffects).arg(trackTitle))) {
                releaseUnusedPlugins();
                return false;
            }
            load(effects[e], stateAt(states, e));
            if (progress != nullptr && progress->wasCanceled()) {
                releaseUnusedPlugins();
                return false;
            }
            incoming.mixer.addEffect(i, std::move(effects[e]));
        }
    }

    // Decode and build waveform peaks for audio samples while reporting live progress,
    // so syncChannelRack() below never blocks the UI thread silently.
    std::map<std::string, std::shared_ptr<const vloom::SampleBuffer>> decodedSamples;
    std::map<std::string, std::shared_ptr<const ClipAudio>> decodedWaveforms;
    for (int s = 0; s < totalSamples; ++s) {
        const std::string& samplePath = uniqueSamplePaths[static_cast<std::size_t>(s)];
        const QString qPath = QString::fromStdString(samplePath);
        const QFileInfo sInfo(qPath);
        if (progress != nullptr
            && !progress->advance(step++, totalSteps, tr("Loading Audio: %1").arg(sInfo.fileName()),
                tr("Audio %1 of %2").arg(s + 1).arg(totalSamples))) {
            releaseUnusedPlugins();
            return false;
        }
        if (!sInfo.exists()) {
            problems << tr("Sample not found: %1").arg(QDir::toNativeSeparators(qPath));
            continue;
        }
        try {
            QString why;
            const QString wav = playableWav(qPath, rate, &why);
            std::string loadErr;
            if (!wav.isEmpty()) {
                if (auto buf = vloom::loadSample(wav.toStdString(), rate, &loadErr); buf != nullptr) {
                    decodedWaveforms[samplePath] = makeClipAudio(*buf);
                    decodedSamples[samplePath] = std::move(buf);
                } else if (!loadErr.empty()) {
                    problems
                        << tr("Could not load %1: %2").arg(sInfo.fileName(), QString::fromStdString(loadErr));
                }
            } else if (!why.isEmpty()) {
                problems << tr("Could not decode %1: %2").arg(sInfo.fileName(), why);
            }
        } catch (const std::exception&) {
            problems << tr("Not enough memory to load %1").arg(sInfo.fileName());
        }
        if (progress != nullptr && progress->wasCanceled()) {
            releaseUnusedPlugins();
            return false;
        }
    }

    if (progress != nullptr)
        (void)progress->advance(step++, totalSteps, tr("Building mixer & arrangement…"));

    ++projectGeneration_;
    undoHistory_.clear();
    unpublishedInstruments_ = std::move(newlyUnpublished);

    project_ = std::move(incoming.project);
    mixer_ = std::move(incoming.mixer);
    transport_->setTempo(incoming.song.tempo);
    playlist_->setTempo(incoming.song.tempo);
    pendingTempo_ = incoming.song.tempo; // sent by refreshFromEngine()
    transport_->recordAction()->setChecked(false);
    transport_->keyDisplay()->setRootNote(incoming.song.keyRoot);
    transport_->keyDisplay()->setScale(incoming.song.scale);
    pianoRoll_->setKey(incoming.song.keyRoot, incoming.song.scale);
    noteHistory_.clear(); // another project: nothing of the old one to undo
    noteEditBefore_.reset();
    rackAudio_.reset();
    if (audio_.sampleRate() > 0.0 && audio_.sampleRate() == rate) {
        for (auto& [pathKey, sampleBuf] : decodedSamples)
            rackAudio_.store(pathKey, std::move(sampleBuf), rate);
    }
    clipAudio_ = std::move(decodedWaveforms);
    playlist_->clearWaveCache();
    currentPattern_ = 0;
    currentChannel_ = 0;
    loopStartTicks_ = incoming.song.loopEnd > incoming.song.loopStart ? incoming.song.loopStart : 0;
    loopEndTicks_ = incoming.song.loopEnd > incoming.song.loopStart ? incoming.song.loopEnd : 0;
    songLoopStart_ = -1.0;
    songLoopEnd_ = -1.0;
    transport_->setLoopEnabled(incoming.song.loopEnabled);
    playlist_->setLoopRange(loopStartTicks_, loopEndTicks_);
    playlist_->setLoopActive(incoming.song.loopEnabled);
    allEffectsBypassed_ = false;
    mixerPanel_->setAllEffectsBypassed(false);
    transport_->songModeAction()->setChecked(incoming.song.songMode); // syncs through songModeChanged
    syncMixerPanel();
    syncInsertNames();
    syncPatternSelector();
    refreshEditors();
    selectChannel(0);
    syncChannelRack();
    updateMixer();
    syncEffectCounts();
    releaseUnusedPlugins(); // the old project's plugins
    updatePluginIdle();

    if (progress != nullptr)
        (void)progress->advance(totalSteps, totalSteps, tr("Ready"));
    return true;
}

void MainWindow::syncMixerPanel()
{
    while (mixerPanel_->trackCount() > mixer_.size())
        mixerPanel_->removeInsert(mixerPanel_->trackCount() - 1);
    while (mixerPanel_->trackCount() < mixer_.size())
        mixerPanel_->addInsert();
    for (std::size_t i = 0; i < mixer_.size(); ++i) {
        const MixerTrack& t = mixer_.track(i);
        if (i != MixerModel::kMaster) {
            mixerPanel_->setTrackRole(i, t.role);
            mixerPanel_->setTrackName(i, QString::fromStdString(t.name));

            bool hasAudio = false;
            bool hasInstrument = false;
            QString instrumentName;
            QString sourceLabel;
            const auto insNum = static_cast<int>(i);
            for (std::size_t chIdx = 0; chIdx < project_.channels().size(); ++chIdx) {
                const Channel& ch = project_.channels()[chIdx];
                if (ch.insert != insNum)
                    continue;
                if (ch.instrument.has_value()) {
                    hasInstrument = true;
                    if (instrumentName.isEmpty())
                        instrumentName = QString::fromStdString(ch.instrument->name);
                } else if (!ch.samplePath.empty()) {
                    hasAudio = true;
                    if (sourceLabel.isEmpty())
                        sourceLabel = t.mono ? tr("Audio · Mono") : tr("Audio · Stereo");
                } else {
                    const bool hasMidiNotes = std::any_of(
                        project_.patterns().begin(), project_.patterns().end(), [chIdx](const Pattern& pat) {
                            return chIdx < pat.channels.size() && !pat.channels[chIdx].notes.empty();
                        });
                    if (hasMidiNotes)
                        hasInstrument = true;
                    else
                        hasAudio = true;
                    if (sourceLabel.isEmpty()) {
                        const QString chTitle = QString::fromStdString(ch.name);
                        sourceLabel = hasMidiNotes
                            ? tr("MIDI · + VSTi")
                            : ((t.mono || chTitle.contains(QStringLiteral("(M)"), Qt::CaseInsensitive))
                                      ? tr("Audio · Mono")
                                      : tr("IN L+R"));
                    }
                }
            }

            const int outTarget
                = (t.output > 0 && static_cast<std::size_t>(t.output) < mixer_.size()) ? t.output : 0;
            const QString outputLabel = outTarget == 0
                ? tr("Main")
                : QStringLiteral("→ %1").arg(
                      QString::fromStdString(mixer_.track(static_cast<std::size_t>(outTarget)).name));

            mixerPanel_->setTrackChannelRouting(
                i, hasAudio, hasInstrument, instrumentName, sourceLabel, outTarget, outputLabel);

            std::vector<MixerSendDisplay> sendViews;
            sendViews.reserve(t.sends.size());
            for (const MixerSend& s : t.sends) {
                const QString dstName = (s.target > 0 && static_cast<std::size_t>(s.target) < mixer_.size())
                    ? QString::fromStdString(mixer_.track(static_cast<std::size_t>(s.target)).name)
                    : tr("Ch %1").arg(s.target);
                sendViews.push_back(MixerSendDisplay{s.target, dstName, s.level, s.muted, s.preFader});
            }
            mixerPanel_->setTrackSends(i, sendViews);
        }
        mixerPanel_->setTrackColor(i, t.color);
        mixerPanel_->setTrackMono(i, t.mono);
        mixerPanel_->setTrackValues(i, t.fader, t.pan, t.muted, t.soloed);
        mixerPanel_->setEffectCount(i, t.effects.size());
    }
    if (mixerPanel_->trackCount() > 0) {
        mixerPanel_->setSelectedTrack(std::min(mixerPanel_->selectedTrack(), mixerPanel_->trackCount() - 1));
    }
}

void MainWindow::rebuildRecentMenu()
{
    recentMenu_->clear();
    const QStringList projects = recent::projects();
    if (projects.isEmpty())
        recentMenu_->addAction(tr("No recent projects"))->setEnabled(false);
    for (int i = 0; i < projects.size(); ++i) {
        const QString path = projects[i];
        const QFileInfo info(path);
        // &1 .. &9, &0: keyboard access to the first ten.
        const QString label = tr("&%1  %2").arg((i + 1) % 10).arg(info.completeBaseName());
        QAction* action = recentMenu_->addAction(label, this, [this, path] {
            if (maybeSaveChanges())
                openProjectFile(path);
        });
        action->setToolTip(QDir::toNativeSeparators(path));
        action->setStatusTip(QDir::toNativeSeparators(path));
        action->setEnabled(info.exists());
    }
    recentMenu_->addSeparator();
    QAction* clear = recentMenu_->addAction(tr("Clear Recent Projects"), this, [] { recent::clear(); });
    clear->setEnabled(!projects.isEmpty());
}

void MainWindow::importAudioFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Import Audio"), projectFolder(),
        tr("Audio files (*.wav *.mp3 *.flac *.ogg *.aif *.aiff *.m4a *.opus);;All files (*)"));
    const std::uint64_t gen = projectGeneration_;
    int added = 0;
    for (const QString& path : files) {
        if (!isLocalPath(path) || !isAudioFile(QFileInfo(path).fileName().toStdString()))
            continue;
        ++added;
        // Each becomes a new channel once it has loaded (decoded off the UI thread).
        loadSampleThen(path, [this, gen, path] {
            if (projectGeneration_ == gen)
                assignSample(project_.channels().size(), path);
        });
    }
    if (!files.isEmpty())
        rememberFolder(files.front());
    if (added < files.size())
        statusBar()->showMessage(tr("%n file(s) skipped: not audio VloomDAW can read.", nullptr,
                                     static_cast<int>(files.size() - added)),
            5000);
}

void MainWindow::movePlaylistTrackTo(std::size_t from, std::size_t to)
{
    if (from >= project_.tracks().size() || to >= project_.tracks().size() || from == to)
        return;

    const auto dedicatedInsert = [this](std::size_t trk) -> int {
        const int ins = trackMixerInsert(project_, mixer_, trk);
        if (ins > 0 && static_cast<std::size_t>(ins) < mixer_.size()
            && insertPrimaryTrack(project_, mixer_, static_cast<std::size_t>(ins)) == trk) {
            return ins;
        }
        return -1;
    };

    const int insFrom = dedicatedInsert(from);
    int insTarget = -1;
    if (insFrom > 0) {
        const int step = (to < from) ? 1 : -1;
        for (int t = static_cast<int>(to); t != static_cast<int>(from); t += step) {
            const int candidate = dedicatedInsert(static_cast<std::size_t>(t));
            if (candidate > 0) {
                insTarget = candidate;
                break;
            }
        }
    }

    recordUndo(tr("Move Track"));
    const std::size_t movedTo = project_.moveTrack(from, to);

    if (insFrom > 0 && insTarget > 0 && insFrom != insTarget) {
        mixer_.moveInsert(static_cast<std::size_t>(insFrom), static_cast<std::size_t>(insTarget));
        project_.moveInsert(insFrom, insTarget);
    }

    syncMixerPanel();
    syncInsertNames();
    if (playlist_ != nullptr) {
        playlist_->refresh();
        playlist_->setSelectedTrack(static_cast<int>(movedTo));
    }
    if (mixerPanel_ != nullptr) {
        const int newIns = trackMixerInsert(project_, mixer_, movedTo);
        if (newIns > 0 && static_cast<std::size_t>(newIns) < mixerPanel_->trackCount())
            mixerPanel_->setSelectedTrack(static_cast<std::size_t>(newIns));
    }
    updateMixer();
    syncEffectCounts();
    syncChannelRack();
    refreshOpenPluginEditors();
}

void MainWindow::moveMixerInsertTo(std::size_t from, std::size_t to)
{
    if (from == MixerModel::kMaster || to == MixerModel::kMaster || from >= mixer_.size()
        || to >= mixer_.size() || from == to)
        return;

    const auto trkFrom = insertPrimaryTrack(project_, mixer_, from);
    std::optional<std::size_t> trkTarget;
    if (trkFrom && *trkFrom < project_.tracks().size()) {
        const int step = (to < from) ? 1 : -1;
        for (int ins = static_cast<int>(to); ins != static_cast<int>(from); ins += step) {
            if (const auto cand = insertPrimaryTrack(project_, mixer_, static_cast<std::size_t>(ins));
                cand && *cand < project_.tracks().size()) {
                trkTarget = *cand;
                break;
            }
        }
    }

    recordUndo(tr("Move Insert"));
    mixer_.moveInsert(from, to);
    project_.moveInsert(static_cast<int>(from), static_cast<int>(to));

    if (trkFrom && trkTarget && *trkFrom != *trkTarget)
        project_.moveTrack(*trkFrom, *trkTarget);

    syncMixerPanel();
    mixerPanel_->setSelectedTrack(to);
    if (playlist_ != nullptr) {
        playlist_->refresh();
        if (const auto movedTrk = insertPrimaryTrack(project_, mixer_, to))
            playlist_->setSelectedTrack(static_cast<int>(*movedTrk));
    }
    syncInsertNames();
    updateMixer();
    syncChannelRack();
    syncEffectCounts();
    refreshOpenPluginEditors();
}

void MainWindow::dropMidiFiles(const QStringList& paths, int track, int startTick)
{
    if (paths.isEmpty())
        return;
    bool recorded = false;
    int nextTrack = track;
    std::size_t totalTracks = 0;
    for (const QString& path : paths) {
        if (!isLocalPath(path))
            continue;
        if (!recorded) {
            recordUndo(tr("Import MIDI"));
            recorded = true;
        }
        const std::size_t added = importMidiFile(path, nextTrack, startTick, false);
        totalTracks += added;
        if (nextTrack >= 0)
            nextTrack += static_cast<int>(added);
    }
    if (totalTracks > 0 && !paths.isEmpty())
        rememberFolder(paths.front());
}

void MainWindow::importMidiFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Import MIDI"), projectFolder(), tr("MIDI files (*.mid *.midi);;All files (*)"));
    if (files.isEmpty())
        return;
    bool recorded = false;
    std::size_t totalTracks = 0;
    for (const QString& path : files) {
        if (!isLocalPath(path))
            continue;
        if (!recorded) {
            recordUndo(tr("Import MIDI"));
            recorded = true;
        }
        totalTracks += importMidiFile(path, -1, 0, false);
    }
    if (totalTracks > 0)
        rememberFolder(files.front());
}

std::size_t MainWindow::importMidiFile(
    const QString& path, int startTrack, int startTick, bool recordUndoStep)
{
    if (!isLocalPath(path))
        return 0;
    const MidiReadResult parsed = readMidiFile(toPath(path));
    if (!parsed.ok) {
        QMessageBox::warning(this, tr("Could not import MIDI"),
            tr("%1 could not be imported.\n\n%2")
                .arg(QDir::toNativeSeparators(path), QString::fromStdString(parsed.error)));
        return 0;
    }

    const std::size_t trackBudget
        = project_.tracks().size() < static_cast<std::size_t>(Project::kMaxPlaylistTracks)
        ? static_cast<std::size_t>(Project::kMaxPlaylistTracks) - project_.tracks().size()
        : 0;
    const bool reuseFirstPattern
        = project_.patterns().size() == 1 && project_.isPatternEmpty(0) && project_.clips().empty();
    const std::size_t patternBudget = (project_.patterns().size() < Project::kMaxPatterns
                                              ? Project::kMaxPatterns - project_.patterns().size()
                                              : 0)
        + (reuseFirstPattern ? 1 : 0);

    // Check whether `startTrack` targets an existing untouched empty track we can reuse for track 0.
    bool reuseTargetTrack = false;
    if (startTrack >= 0 && static_cast<std::size_t>(startTrack) < project_.tracks().size()) {
        const auto trkIdx = static_cast<std::size_t>(startTrack);
        const PlaylistTrack& pt = project_.tracks()[trkIdx];
        reuseTargetTrack = !pt.named && pt.insert < 0 && project_.clipsOnTrack(trkIdx) == 0
            && !trackPrimaryChannel(project_, trkIdx).has_value();
    } else if (startTrack < 0 && project_.tracks().size() == 1) {
        const PlaylistTrack& pt = project_.tracks()[0];
        if (!pt.named && pt.insert < 0 && project_.clipsOnTrack(0) == 0
            && !trackPrimaryChannel(project_, 0).has_value()) {
            startTrack = 0;
            reuseTargetTrack = true;
        }
    }

    const std::size_t maxTracks = std::min(
        parsed.file.tracks.size(), std::min(trackBudget + (reuseTargetTrack ? 1 : 0), patternBudget));
    if (maxTracks == 0) {
        statusBar()->showMessage(tr("No room for additional MIDI tracks or patterns."), 4000);
        return 0;
    }

    if (recordUndoStep)
        recordUndo(tr("Import MIDI"));

    // Adopt the MIDI file's tempo when importing into a blank arrangement.
    if (project_.clips().empty() && project_.audioClips().empty() && parsed.file.tempoBpm.has_value()) {
        transport_->setTempo(*parsed.file.tempoBpm);
        pendingTempo_ = *parsed.file.tempoBpm;
        playlist_->setTempo(*parsed.file.tempoBpm);
    }

    const QFileInfo fileInfo(path);
    const std::string fileStem
        = cleanName(fileInfo.completeBaseName().toStdString(), Project::kMaxTrackNameLength);

    std::optional<std::size_t> firstChannel;
    std::optional<std::size_t> firstPattern;
    std::optional<std::size_t> firstTrack;

    std::size_t insertCursor = startTrack >= 0
        ? std::min(static_cast<std::size_t>(startTrack), project_.tracks().size())
        : project_.tracks().size();

    for (std::size_t i = 0; i < maxTracks; ++i) {
        const MidiTrackImport& mt = parsed.file.tracks[i];
        std::string title = cleanName(mt.name, Project::kMaxTrackNameLength);
        if (title.empty() || (maxTracks == 1 && mt.name.starts_with("MIDI ")))
            title = !fileStem.empty() ? fileStem : ("MIDI " + std::to_string(i + 1));

        const std::uint32_t col = trackPaletteColor(project_.tracks().size() + i);

        // 1. Empty MIDI Channel (NO auto-loaded instrument: user adds their own VSTi per track).
        const std::size_t chIdx = project_.addChannel(title);
        project_.setChannelColor(chIdx, col);

        // 2. Dedicated Mixer Insert linked 1:1 with the MIDI channel & Playlist track.
        int dedicatedIns = 0;
        if (const auto ins = mixer_.addInsert()) {
            mixer_.setName(*ins, title);
            mixer_.setColor(*ins, col);
            project_.setChannelInsert(chIdx, static_cast<int>(*ins));
            dedicatedIns = static_cast<int>(*ins);
        }

        // 3. Dedicated Pattern holding this MIDI track's notes.
        std::size_t patIdx = 0;
        if (i == 0 && reuseFirstPattern) {
            patIdx = 0;
            project_.setPatternName(0, title);
        } else {
            patIdx = project_.addPattern();
            project_.setPatternName(patIdx, title);
        }
        project_.setNotes(patIdx, chIdx, mt.notes);

        // 4. Playlist Track & Pattern Clip placed at `startTick`.
        std::size_t trkIdx = 0;
        if (i == 0 && reuseTargetTrack) {
            trkIdx = static_cast<std::size_t>(startTrack);
            insertCursor = trkIdx + 1;
        } else {
            const auto added = project_.addTrack();
            if (!added)
                break;
            trkIdx = project_.moveTrack(*added, insertCursor);
            insertCursor = trkIdx + 1;
        }
        project_.setTrackName(trkIdx, title);
        project_.setTrackColor(trkIdx, col);
        if (dedicatedIns > 0)
            project_.setTrackInsert(trkIdx, dedicatedIns);

        project_.addClip(Clip{patIdx, static_cast<int>(trkIdx), std::max(0, startTick)});

        if (!firstChannel)
            firstChannel = chIdx;
        if (!firstPattern)
            firstPattern = patIdx;
        if (!firstTrack)
            firstTrack = trkIdx;
    }

    if (!songMode_)
        transport_->songModeAction()->setChecked(true);

    syncMixerPanel();
    syncInsertNames();
    syncPatternSelector();
    channelRack_->refresh();
    playlist_->refresh();
    pianoRoll_->refresh();
    rebuildClipSources();

    if (firstChannel)
        selectChannel(*firstChannel);
    if (firstPattern)
        selectPattern(*firstPattern);
    if (firstTrack) {
        playlist_->setSelectedTrack(static_cast<int>(*firstTrack));
        const int ins = trackMixerInsert(project_, mixer_, *firstTrack);
        if (ins > 0 && static_cast<std::size_t>(ins) < mixerPanel_->trackCount())
            mixerPanel_->setSelectedTrack(static_cast<std::size_t>(ins));
    }

    syncChannelRack();
    updateMixer();
    syncEffectCounts();

    statusBar()->showMessage(
        tr("Imported %1 (%n MIDI track(s) — assign an Instrument on each track to hear playback).", nullptr,
            static_cast<int>(maxTracks))
            .arg(fileInfo.fileName()),
        6000);
    return maxTracks;
}

void MainWindow::exportSong(bool forceLoopRange)
{
    flushChannelRackSync();
    const double rate = audio_.sampleRate() > 0 ? audio_.sampleRate() : audio_.requestedSampleRate();
    const bool hasCustomLoop = loopEndTicks_ > loopStartTicks_;
    const bool song
        = forceLoopRange || songMode_ || !project_.clips().empty() || !project_.audioClips().empty();
    std::vector<ChannelRackAudio::LoadError> errors;
    auto state = rackAudio_.build(project_, currentPattern_, rate, song, errors);
    const std::int64_t endTicks = state != nullptr ? vloom::songEndTicks(*state, transport_->tempo()) : 0;
    if (state == nullptr || (endTicks <= 0 && !(forceLoopRange && hasCustomLoop))) {
        QMessageBox::information(this, tr("Nothing to export"),
            tr("The song is empty: place patterns or audio in the playlist, or put steps or notes in the "
               "pattern."));
        return;
    }

    ExportSong about;
    about.folder = projectFolder();
    about.name = projectPath_.empty() ? tr("Untitled") : toQString(projectPath_.stem());
    const std::int64_t fullTicks = std::max<std::int64_t>(endTicks, hasCustomLoop ? loopEndTicks_ : 0);
    about.bars = static_cast<int>((fullTicks + kTicksPerBar - 1) / kTicksPerBar);
    about.seconds = static_cast<double>(fullTicks) / kTicksPerBeat * 60.0 / transport_->tempo();
    about.sampleRate = rate;
    if (song && hasCustomLoop) {
        about.hasLoopRange = true;
        about.preferLoopRange = forceLoopRange || transport_->loopEnabled();
        const int spanTicks = loopEndTicks_ - loopStartTicks_;
        about.loopBars = std::max(1, (spanTicks + kTicksPerBar - 1) / kTicksPerBar);
        about.loopSeconds = static_cast<double>(spanTicks) / kTicksPerBeat * 60.0 / transport_->tempo();
        const int b1 = loopStartTicks_ / kTicksPerBar + 1;
        const int beat1 = (loopStartTicks_ % kTicksPerBar) / kTicksPerBeat + 1;
        const int b2 = loopEndTicks_ / kTicksPerBar + 1;
        const int beat2 = (loopEndTicks_ % kTicksPerBar) / kTicksPerBeat + 1;
        about.loopLabel = (beat1 == 1 && beat2 == 1)
            ? tr("Bar %1 – %2").arg(b1).arg(b2)
            : tr("Bar %1.%2 – %3.%4").arg(b1).arg(beat1).arg(b2).arg(beat2);
    }
    ExportDialog dialog(about, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const ExportSettings chosen = dialog.settings();
    const QString path = chosen.path;
    if (!isLocalPath(path)) {
        QMessageBox::warning(
            this, tr("Could not export"), tr("Songs cannot be exported directly to network shares."));
        return;
    }
    const bool wav = chosen.format == AudioFileFormat::Wav;

    // Other formats: the engine renders a 32-bit float WAV into a temporary
    // folder, which FFmpeg then encodes.
    QTemporaryDir scratch;
    if (!wav && !scratch.isValid()) {
        QMessageBox::critical(this, tr("Could not export"), tr("No room for temporary files."));
        return;
    }
    const QString rendered = wav ? path : scratch.filePath(QStringLiteral("render.wav"));

    vloom::SongExportOptions options;
    options.format = !wav || chosen.wavDepth == ExportSettings::WavDepth::Float32 ? vloom::WavFormat::Float32
        : chosen.wavDepth == ExportSettings::WavDepth::Pcm16                      ? vloom::WavFormat::Pcm16
                                                                                  : vloom::WavFormat::Pcm24;
    options.dither = chosen.dither;
    options.channels = chosen.mono ? 1 : 2;
    if (!chosen.tail)
        options.maxTailSeconds = 0.0;
    options.tempo = transport_->tempo();
    if (chosen.loopRange && hasCustomLoop) {
        options.startTicks = loopStartTicks_;
        options.endTicks = loopEndTicks_;
    }
    options.masterGain = volumeToGain(transport_->masterVolume())
        * static_cast<float>(mixer_.effectiveGain(MixerModel::kMaster));
    options.masterPan = static_cast<float>(mixer_.track(MixerModel::kMaster).pan);

    // The live engine's plugin instances render the export too: the device
    // stops meanwhile, so only one engine processes them.
    stopAndRewind();
    const auto copy = vloom::copyWithOwnVoices(*state);
    const MixerModel effective = effectiveMixerModel();
    const auto mixer = engineMixerState(effective, pluginHost_.resolver());
    audio_.closeDevice();

    QProgressDialog progress(
        tr("Exporting %1…").arg(QFileInfo(path).fileName()), tr("Cancel"), 0, 1000, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    // Rendering is most of the work; encoding takes the rest of the bar.
    const double renderShare = wav ? 1.0 : 0.8;
    const auto step = [&progress](double done) {
        progress.setValue(static_cast<int>(done * 1000.0));
        QApplication::processEvents();
        return !progress.wasCanceled();
    };
    vloom::SongExportResult result = vloom::exportSong(rendered.toStdString(), *copy, mixer.get(), rate,
        options, [&step, renderShare](double done) { return step(done * renderShare); });
    if (result.ok && !wav) {
        EncodeOptions encode;
        encode.format = chosen.format;
        encode.bitDepth = chosen.flacBits;
        encode.dither = chosen.dither;
        encode.bitrateKbps = chosen.bitrateKbps;
        encode.metadata = chosen.metadata;
        std::string why;
        try {
            result.ok = encodeWavFile(rendered.toStdString(), path.toStdString(), encode, &why,
                [&step, renderShare](double done) { return step(renderShare + done * (1.0 - renderShare)); });
        } catch (const std::exception&) {
            result.ok = false;
            why = "not enough memory";
        }
        result.cancelled = !result.ok && why.empty();
        result.error = why;
    }
    // A WAV file gets its tags after the engine has written the audio. One
    // that cannot take them is still a good export: said, not failed.
    QString tagsNote;
    if (result.ok && wav) {
        std::string why;
        if (!writeWavMetadata(path.toStdString(), chosen.metadata, &why))
            tagsNote = tr(" The tags could not be added (%1).").arg(QString::fromStdString(why));
    }
    progress.reset();
    openSelectedDevice();

    rememberFolder(path);
    if (result.cancelled)
        statusBar()->showMessage(tr("Export cancelled"), 3000);
    else if (!result.ok)
        QMessageBox::critical(this, tr("Could not export"),
            tr("%1 could not be written.\n\n%2")
                .arg(QDir::toNativeSeparators(path), QString::fromStdString(result.error)));
    else {
        statusBar()->showMessage(tr("Exported %1 (%2 s)")
                                     .arg(QFileInfo(path).fileName())
                                     .arg(static_cast<double>(result.frames) / rate, 0, 'f', 1)
                + tagsNote,
            6000);
        if (chosen.showFile)
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    }
}

void MainWindow::togglePlay()
{
    flushChannelRackSync();
    if (audio_.isRunning())
        applySongLoop();
    auto& engine = audio_.engine();
    scrubStopPending_ = false; // the user's own play or stop wins
    if (engine.isPlaying()) {
        engine.stop();
    } else {
        if (songMode_ && transport_->loopEnabled() && loopEndTicks_ > loopStartTicks_) {
            const double pos = engine.positionTicks();
            if (pos < loopStartTicks_ || pos >= loopEndTicks_)
                (void)engine.locate(static_cast<double>(loopStartTicks_));
        }
        engine.play();
    }
}

void MainWindow::stopAndRewind()
{
    auto& engine = audio_.engine();
    scrubStopPending_ = false;
    engine.stop();
    if (songMode_ && transport_->loopEnabled() && loopEndTicks_ > loopStartTicks_
        && engine.positionTicks() > loopStartTicks_ + 1.0) {
        engine.locate(static_cast<double>(loopStartTicks_));
    } else {
        engine.locate(0.0);
    }
}

void MainWindow::tapTempo()
{
    if (!tapClock_.isValid())
        tapClock_.start();
    const double seconds = static_cast<double>(tapClock_.nsecsElapsed()) / 1e9;
    if (const auto bpm = tapTempo_.tap(seconds)) {
        transport_->setTempo(*bpm);
        playlist_->setTempo(*bpm);
        pendingTempo_ = *bpm; // sent by refreshFromEngine()
    }
}

void MainWindow::toggleFullScreen()
{
#if defined(Q_OS_MACOS) && QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    // Cover the screen ourselves instead of macOS native full screen, so the
    // window buttons stay on the tool bar. The menu bar and Dock auto-hide.
    coversScreen_ = !coversScreen_;
    if (coversScreen_) {
        geometryBeforeFullScreen_ = geometry();
        setMacMenuBarAndDockHidden(true);
        setGeometry(screen()->geometry());
    } else {
        setMacMenuBarAndDockHidden(false);
        setGeometry(geometryBeforeFullScreen_);
    }
    const bool fullScreen = coversScreen_;
#else
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
    const bool fullScreen = isFullScreen();
#endif
    fullScreenAction_->setText(fullScreen ? tr("Exit Full Screen") : tr("Enter Full Screen"));
}

void MainWindow::refreshFromEngine()
{
    auto& engine = audio_.engine();
    engine.collectGarbage(); // free channel-rack states the audio thread is done with
    watchDevice();

    if (scrubStopPending_) {
        // A scrub's stop: sent again if the queue was full, done once heard.
        if (!scrubStopSent_)
            scrubStopSent_ = engine.stop();
        else if (!engine.isPlaying())
            scrubStopPending_ = false;
    }

    // Nothing to show while minimised; still send pending changes and drain
    // the meters below so they do not show a stale peak when restored.
    if (isMinimized()) {
        if (pendingTempo_ && audio_.isRunning() && engine.setTempo(*pendingTempo_))
            pendingTempo_.reset();
        if (pendingGain_ && audio_.isRunning() && engine.setMasterGain(*pendingGain_))
            pendingGain_.reset();
        if (pendingPan_ && audio_.isRunning() && engine.setMasterPan(*pendingPan_))
            pendingPan_.reset();
        if (audio_.isRunning()) {
            (void)engine.takeOutputPeak(0);
            (void)engine.takeOutputPeak(1);
            for (std::size_t i = 1; i < mixer_.size(); ++i) {
                const auto insert = static_cast<int>(i);
                (void)engine.takeInsertPeak(insert, 0);
                (void)engine.takeInsertPeak(insert, 1);
            }
        }
        return;
    }

    // Send the latest tempo once per tick. Without a running device nothing
    // drains the command queue, so keep it pending: startDevice() sends the
    // shown tempo anyway.
    bool tempoSent = false;
    if (pendingTempo_ && audio_.isRunning() && engine.setTempo(*pendingTempo_)) {
        pendingTempo_.reset();
        tempoSent = true;
    }
    // Show the engine's tempo only once it has caught up, and never while
    // there is no device (the display is the truth until one opens).
    if (audio_.isRunning() && !tempoSent && !pendingTempo_) {
        transport_->setTempo(engine.tempo());
        playlist_->setTempo(engine.tempo());
    }

    // Real time since the last tick: meters fall by it, and slower readouts
    // update on their own schedule whatever the refresh rate is.
    const double now = static_cast<double>(frameClock_.nsecsElapsed()) / 1e9;
    const double dt = lastTickAt_ < 0.0 ? refreshIntervalMs_ / 1000.0 : std::min(now - lastTickAt_, 0.25);
    lastTickAt_ = now;
    const auto due = [now](double& next, double period) {
        if (now < next)
            return false;
        next = now + period;
        return true;
    };

    // Performance overlay: show the figures twice a second (this tick is
    // recorded at the end, with how long it took).
    if (!performanceOverlay_->isHidden()) {
        if (due(nextOverlayAt_, 0.5))
            performanceOverlay_->setFigures(
                frameStats_.snapshot(now), audio_.isRunning() ? engine.cpuLoad() : 0.0f, refreshIntervalMs_);
    }

    const double ticks = engine.positionTicks();
    transport_->setPosition(QString::fromStdString(formatPosition(ticks, engine.beatsPerBar())));
    transport_->setTime(QString::fromStdString(formatTime(ticksToSeconds(ticks, engine.tempo()))));
    transport_->setTimeSignature(engine.beatsPerBar(), 4); // engine has no beat unit yet
    transport_->setPlaying(engine.isPlaying());

    // Editors' playheads. Nothing plays the patterns yet; the lines show
    // where the transport is (the piano roll and rack wrap at the pattern).
    const bool showPlayhead = engine.isPlaying() || ticks > 0.0;
    const double patternTicks = project_.patternLength(currentPattern_);
    playlist_->setPlayhead(showPlayhead ? ticks : -1.0);
    double prPlayhead = showPlayhead ? std::fmod(ticks, patternTicks) : -1.0;
    if (showPlayhead && songMode_) {
        for (const Clip& clip : project_.clips()) {
            if (clip.pattern != currentPattern_ || clip.muted)
                continue;
            const double clipLen = project_.clipLength(clip);
            if (ticks >= clip.start && ticks < clip.start + clipLen) {
                prPlayhead = std::fmod(ticks - clip.start + clip.offset, patternTicks);
                break;
            }
        }
    }
    pianoRoll_->setPlayhead(prPlayhead);
    channelRack_->setPlayStep(engine.isPlaying()
            ? static_cast<int>(std::fmod(ticks, static_cast<double>(kTicksPerBar)) / kTicksPerStep)
            : -1);

    if (pendingGain_ && audio_.isRunning() && engine.setMasterGain(*pendingGain_))
        pendingGain_.reset();
    if (pendingPan_ && audio_.isRunning() && engine.setMasterPan(*pendingPan_))
        pendingPan_.reset();

    // Meters: always take the engine's peaks once per tick (so a reopened mixer
    // or playlist does not show a stale peak, and both share the same sample).
    const bool running = audio_.isRunning();
    const float left = running ? engine.takeOutputPeak(0) : 0.0f;
    const float right = running ? engine.takeOutputPeak(1) : 0.0f;
    insertPeaks_.resize(mixer_.size());
    if (!insertPeaks_.empty())
        insertPeaks_[MixerModel::kMaster] = {left, right};
    const bool showMeters = mixerPanel_->isVisible();
    if (showMeters)
        mixerPanel_->meter(MixerModel::kMaster)->addPeaks(left, right);
    for (std::size_t i = 1; i < mixer_.size(); ++i) {
        const auto insert = static_cast<int>(i);
        const float l = running ? engine.takeInsertPeak(insert, 0) : 0.0f;
        const float r = running ? engine.takeInsertPeak(insert, 1) : 0.0f;
        insertPeaks_[i] = {l, r};
        if (showMeters && i < mixerPanel_->trackCount())
            mixerPanel_->meter(i)->addPeaks(l, r);
    }
    if (showMeters) {
        for (std::size_t i = 0; i < mixerPanel_->trackCount(); ++i)
            mixerPanel_->meter(i)->tick(dt);
    }
    if (playlist_->isVisible())
        playlist_->updateTrackMeters(insertPeaks_, dt);

    // Fitted audio clips follow the tempo: resampled at once, stretched
    // copies once it settles (stretchedSample()).
    if (transport_->tempo() != fitTempo_ && songMode_
        && std::any_of(project_.audioClips().begin(), project_.audioClips().end(),
            [](const AudioClip& clip) { return clip.fitted(); }))
        syncChannelRack();

    // Song mode: the song loops at its end (clips, tempo and mode may have changed).
    if (running)
        applySongLoop();

    // The browser's preview strip: where the preview of its file is.
    if (SampleWaveform* strip = browser_->waveform(); strip->isVisible())
        strip->setPlaying(previewer_->playingPath() == strip->file() ? previewer_->playingSeconds() : -1.0);

    // A meter readable by eye: CPU about 4 times a second (every tick would
    // only flicker and repaint); memory changes slowly and costs a system
    // call, so about once a second.
    if (due(nextCpuAt_, 0.25))
        transport_->performanceMeter()->setCpuLoad(audio_.isRunning() ? engine.cpuLoad() : 0.0f);
    if (due(nextMemoryAt_, 1.0))
        transport_->performanceMeter()->setMemoryBytes(processMemoryBytes());

    if (!performanceOverlay_->isHidden()) {
        const double workMs = (static_cast<double>(frameClock_.nsecsElapsed()) / 1e9 - now) * 1000.0;
        frameStats_.addTick(now, workMs);
    }
}

void MainWindow::updateRefreshRate()
{
    // Refresh at the screen's rate (60, 120, 144 Hz…) so the playhead moves
    // every frame the screen shows, capped to keep the UI thread light.
    qreal hz = screen() != nullptr ? screen()->refreshRate() : 60.0;
    // Debug override, e.g. VLOOM_REFRESH_HZ=240 to test frame pacing.
    bool overridden = false;
    const int forced = qEnvironmentVariableIntValue("VLOOM_REFRESH_HZ", &overridden);
    if (overridden && forced > 0)
        hz = forced;
    const qreal clamped = std::clamp(hz > 1.0 ? hz : 60.0, kMinRefreshHz, kMaxRefreshHz);
    const int interval = std::max(1, static_cast<int>(std::lround(1000.0 / clamped)));
    if (interval == refreshIntervalMs_ && refreshTimer_->isActive())
        return;
    refreshIntervalMs_ = interval;
    refreshTimer_->start(refreshIntervalMs_);
}

void MainWindow::showAbout()
{
    AboutRuntimeInfo info;
    if (audio_.isRunning()) {
        info.audioStatus = tr("%1  ·  %2 Hz (%3 smp)")
                               .arg(QString::fromStdString(audio_.deviceName()))
                               .arg(audio_.sampleRate())
                               .arg(audio_.bufferFrames());
    } else {
        info.audioStatus = tr("No active output device");
    }
    info.scannedPluginCount = static_cast<int>(pluginStore_.plugins.size());

    AboutDialog dlg(info, this);
    dlg.exec();
}

} // namespace vloomdaw
