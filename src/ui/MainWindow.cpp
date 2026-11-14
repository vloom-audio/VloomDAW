// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/MainWindow.hpp"

#include "audio/AudioController.hpp"
#include "audio/MixerAudio.hpp"
#include "audio/TempoDetector.hpp"
#include "audio/TimeStretch.hpp"
#include "core/DeviceRef.hpp"
#include "core/NoteNames.hpp"
#include "core/PositionFormat.hpp"
#include "core/ProcessMemory.hpp"
#include "core/Scales.hpp"
#include "ui/AudioDecoder.hpp"
#include "ui/DeviceScanner.hpp"
#include "ui/Theme.hpp"
#include "ui/browser/BrowserPanel.hpp"
#include "ui/browser/SamplePreviewer.hpp"
#include "ui/browser/SampleWaveform.hpp"
#include "ui/channelrack/ChannelRackView.hpp"
#include "ui/dialogs/SettingsDialog/AudioSettingsPage.hpp"
#include "ui/dialogs/SettingsDialog/GeneralSettingsPage.hpp"
#include "ui/dialogs/SettingsDialog/SettingsDialog.hpp"
#include "ui/dialogs/SettingsDialog/ThemeSettingsPage.hpp"
#include "ui/mixer/MixerPanel.hpp"
#include "ui/pianoroll/PianoRollView.hpp"
#include "ui/playlist/PlaylistView.hpp"
#include "ui/plugins/PluginManagerDialog.hpp"
#include "ui/plugins/PluginsPanel.hpp"
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
#include <vloom/engine/sample/SampleBuffer.hpp>
#include <vloom/engine/sample/SampleLoader.hpp>

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
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
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace vloomdaw {

namespace {

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

constexpr int kDefaultSidebarWidth = 260;
constexpr int kThemeApplyMs = 200; ///< full restyle once theme edits pause this long
constexpr int kDefaultMixerHeight = 300;
constexpr std::size_t kMixerInserts = 16;
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
    rackAudio_.setStretchProvider(
        [this](const std::string& path, double ratio) { return stretchedSample(path, ratio); });
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
    extendIntoTitleBar();
    restoreWindowState();
    applyStartupSettings();
    openSelectedDevice();

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
    const auto addFileAction = [&](const QString& text, const QKeySequence& shortcut, auto&& slot) {
        auto* action = fileMenu->addAction(text, this, slot);
        action->setShortcut(shortcut);
        return action;
    };
    // Project files arrive with the project model (milestone M4). Until then
    // these say so instead of doing nothing.
    const auto notYet = [this](const QString& feature) {
        return [this, feature] {
            statusBar()->showMessage(
                tr("%1 is not available yet: projects arrive in milestone M4.").arg(feature), 5000);
        };
    };

    addFileAction(tr("&New Project"), QKeySequence(Qt::CTRL | Qt::Key_N), [this] { newProject(); });
    addFileAction(tr("&Open…"), QKeySequence(Qt::CTRL | Qt::Key_O), notYet(tr("Open")));

    auto* recentMenu = fileMenu->addMenu(tr("Open &Recent"));
    recentMenu->addAction(tr("No recent projects"))->setEnabled(false);
    recentMenu->addSeparator();
    recentMenu->addAction(tr("Clear Recent Projects"))->setEnabled(false);

    fileMenu->addSeparator();
    addFileAction(tr("&Save"), QKeySequence(Qt::CTRL | Qt::Key_S), notYet(tr("Save")));
    addFileAction(tr("Save &As…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), notYet(tr("Save As")));

    fileMenu->addSeparator();
    addFileAction(tr("&Import…"), QKeySequence(Qt::CTRL | Qt::Key_I), notYet(tr("Import")));
    addFileAction(tr("&Export…"), QKeySequence(Qt::CTRL | Qt::Key_E), notYet(tr("Export")));

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

    // Edit: in place now so the menu bar has its usual order and the keys are
    // claimed; the commands arrive with undo (start of M2) and the editors'
    // selection tools (M2). Text fields keep their own copy / paste / undo:
    // a focused QLineEdit takes those keys before these actions see them.
    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    const auto notYetIn = [this](const QString& feature, const QString& when) {
        return [this, feature, when] {
            statusBar()->showMessage(tr("%1 is not available yet: it arrives %2.").arg(feature, when), 5000);
        };
    };
    const QString withUndo = tr("at the start of milestone M2");
    const QString withEditing = tr("with the editing tools in milestone M2");
    const auto addEditAction = [&](const QString& text, const QList<QKeySequence>& shortcuts, auto&& slot) {
        auto* action = editMenu->addAction(text, this, slot);
        action->setShortcuts(shortcuts);
        return action;
    };
    addEditAction(tr("&Undo"), {QKeySequence(Qt::CTRL | Qt::Key_Z)}, notYetIn(tr("Undo"), withUndo));
#if defined(Q_OS_WIN)
    // Windows apps also redo with Ctrl+Y.
    const QList<QKeySequence> redoKeys{
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), QKeySequence(Qt::CTRL | Qt::Key_Y)};
#else
    const QList<QKeySequence> redoKeys{QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)};
#endif
    addEditAction(tr("&Redo"), redoKeys, notYetIn(tr("Redo"), withUndo));
    editMenu->addSeparator();
    addEditAction(tr("Cu&t"), {QKeySequence(Qt::CTRL | Qt::Key_X)}, notYetIn(tr("Cut"), withEditing));
    addEditAction(tr("&Copy"), {QKeySequence(Qt::CTRL | Qt::Key_C)}, notYetIn(tr("Copy"), withEditing));
    addEditAction(tr("&Paste"), {QKeySequence(Qt::CTRL | Qt::Key_V)}, notYetIn(tr("Paste"), withEditing));
    addEditAction(
        tr("&Duplicate"), {QKeySequence(Qt::CTRL | Qt::Key_D)}, notYetIn(tr("Duplicate"), withEditing));
    addEditAction(tr("De&lete"), {QKeySequence(Qt::Key_Delete)}, notYetIn(tr("Delete"), withEditing));
    editMenu->addSeparator();
    addEditAction(
        tr("Select &All"), {QKeySequence(Qt::CTRL | Qt::Key_A)}, notYetIn(tr("Select All"), withEditing));

    // Patterns, as in FL Studio. Without undo yet, deleting a pattern that
    // has steps or notes asks first.
    auto* patternsMenu = menuBar()->addMenu(tr("&Patterns"));
    const auto addPatternAction = [&](const QString& text, const QKeySequence& shortcut, auto&& slot) {
        auto* action = patternsMenu->addAction(text, this, slot);
        action->setShortcut(shortcut);
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
    connect(transport_, &TransportBar::masterPitchChanged, this, [this](double semitones) {
        statusBar()->showMessage(
            tr("Master pitch %1 semitones: it applies to instruments, which arrive in milestone M2.")
                .arg(QString::number(semitones, 'f', 1)),
            4000);
    });
    connect(transport_, &TransportBar::songModeChanged, this, [this](bool song) {
        songMode_ = song;
        syncChannelRack();
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
        if (roomForPatterns(1))
            patternsChanged(project_.addPattern());
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
        && QMessageBox::question(this, tr("Remove Bar"),
               tr("The last bar has steps that are on. Remove it anyway?") + QLatin1Char(' ')
                   + tr("This cannot be undone yet."))
            != QMessageBox::Yes)
        return;
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
    if (!mixer_.addInsert()) {
        statusBar()->showMessage(tr("The mixer has up to %1 inserts.").arg(MixerModel::kMaxInserts), 4000);
        return;
    }
    mixerPanel_->addInsert();
    syncInsertNames();
    updateMixer();
}

void MainWindow::renameMixerInsert(std::size_t track)
{
    if (track == MixerModel::kMaster || track >= mixer_.size())
        return;
    const MixerTrack& t = mixer_.track(track);
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Insert %1").arg(track),
        tr("Name (leave empty for \u201CInsert %1\u201D):").arg(track), QLineEdit::Normal,
        t.named ? QString::fromStdString(t.name) : QString(), &ok);
    if (!ok)
        return;
    mixer_.setName(track, name.toStdString());
    mixerPanel_->setTrackName(track, QString::fromStdString(mixer_.track(track).name));
    syncInsertNames();
}

void MainWindow::addPlaylistTrack()
{
    const auto added = project_.addTrack();
    if (!added) {
        statusBar()->showMessage(
            tr("The playlist has up to %1 tracks.").arg(Project::kMaxPlaylistTracks), 4000);
        return;
    }
    playlist_->refresh();
    playlist_->verticalScrollBar()->setValue(playlist_->verticalScrollBar()->maximum()); // show it
}

void MainWindow::removePlaylistTrack(std::size_t track)
{
    if (track >= project_.tracks().size())
        return;
    if (project_.tracks().size() <= 1) {
        statusBar()->showMessage(tr("The playlist has at least one track."), 4000);
        return;
    }
    const QString name = QString::fromStdString(project_.tracks()[track].name);
    const std::size_t clips = project_.clipsOnTrack(track);
    if (clips > 0
        && QMessageBox::question(this, tr("Delete Track"),
               (clips == 1 ? tr("“%1” has 1 clip.").arg(name) : tr("“%1” has %2 clips.").arg(name).arg(clips))
                   + QLatin1Char(' ') + tr("Delete the track and its clips?") + QLatin1Char(' ')
                   + tr("This cannot be undone yet."))
            != QMessageBox::Yes)
        return;
    project_.removeTrack(track);
    playlist_->refresh();
    if (songMode_) // its clips stop; removing the only solo track lets the others play
        syncChannelRack();
    statusBar()->showMessage(tr("“%1” deleted.").arg(name), 3000);
}

const ClipAudio* MainWindow::clipAudio(const std::string& path)
{
    if (const auto found = clipAudio_.find(path); found != clipAudio_.end())
        return found->second.get();
    // First sight of this file: its length and a coarse waveform, once.
    auto info = std::make_shared<ClipAudio>();
    if (const auto sample = rackAudio_.sample(path); sample && sample->sampleRate() > 0.0) {
        info->seconds = static_cast<double>(sample->frames()) / sample->sampleRate();
        const auto perPeak
            = std::max<std::int64_t>(1, static_cast<std::int64_t>(sample->sampleRate() / 200.0));
        info->peakSeconds = static_cast<double>(perPeak) / sample->sampleRate();
        info->peaks.reserve(static_cast<std::size_t>(sample->frames() / perPeak + 1));
        for (std::int64_t from = 0; from < sample->frames(); from += perPeak) {
            const std::int64_t to = std::min(sample->frames(), from + perPeak);
            float peak = 0.0f;
            for (int ch = 0; ch < sample->channels(); ++ch) {
                const float* samples = sample->channel(ch);
                for (std::int64_t i = from; i < to; ++i)
                    peak = std::max(peak, std::abs(samples[i]));
            }
            info->peaks.push_back(peak);
        }
    }
    return clipAudio_.emplace(path, std::move(info)).first->second.get();
}

void MainWindow::loadSampleThen(const QString& path, std::function<void()> then)
{
    const std::string file = QDir::cleanPath(path).toStdString();
    const double rate = audio_.sampleRate();
    if (rate <= 0.0 || rackAudio_.hasSample(file, rate)) {
        then(); // loaded already, or no device: nothing to wait for
        return;
    }
    startLoader();
    ++loading_;
    const QString name = QFileInfo(path).fileName();
    statusBar()->showMessage(tr("Loading %1…").arg(name));
    QMetaObject::invokeMethod(
        loader_,
        [this, file, rate, name, then = std::move(then)] {
            // Loader thread: decode (MP3 and others) and resample.
            std::string error;
            std::shared_ptr<const vloom::SampleBuffer> sample;
            try {
                QString why;
                const QString wav = playableWav(QString::fromStdString(file), rate, &why);
                if (wav.isEmpty())
                    error = why.toStdString();
                else
                    sample = vloom::loadSample(wav.toStdString(), rate, &error);
            } catch (const std::exception&) {
                error = "not enough memory to load it";
            }
            QMetaObject::invokeMethod(
                this,
                [this, file, rate, name, sample, error, then] {
                    --loading_;
                    if (loading_ == 0)
                        statusBar()->clearMessage();
                    if (sample == nullptr) {
                        statusBar()->showMessage(
                            tr("Could not load %1: %2").arg(name, QString::fromStdString(error)), 8000);
                        return;
                    }
                    rackAudio_.store(file, sample, rate);
                    then();
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
    menu.exec(globalPos);
}

void MainWindow::setAudioClipTempo(std::size_t index, double bpm)
{
    if (index >= project_.audioClips().size())
        return;
    AudioClip clip = project_.audioClips()[index];
    // Offset and length count in the tempo the sample is read at: when that
    // changes, they scale so they cover the same part of the sample.
    const double before = clip.fitted() ? clip.sourceBpm : transport_->tempo();
    clip.sourceBpm = bpm;
    const double after = clip.fitted() ? clip.sourceBpm : transport_->tempo();
    const auto rescale = [&](int ticks) {
        return static_cast<int>(std::lround(ticks * after / before / kTicksPerStep)) * kTicksPerStep;
    };
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
    const auto apply = [this, index](AudioClip::Fit mode) {
        if (index >= project_.audioClips().size())
            return;
        AudioClip changed = project_.audioClips()[index];
        const double before = changed.fitted() ? changed.sourceBpm : transport_->tempo();
        changed.fit = mode;
        const double after = changed.fitted() ? changed.sourceBpm : transport_->tempo();
        const auto rescale = [&](int ticks) {
            return static_cast<int>(std::lround(ticks * after / before / kTicksPerStep)) * kTicksPerStep;
        };
        changed.offset = rescale(changed.offset);
        changed.length = rescale(changed.length);
        project_.setAudioClip(index, changed);
        playlist_->refresh();
        if (songMode_)
            syncChannelRack();
    };
    if (fit == AudioClip::Fit::Off || clip.sourceBpm > 0.0 || clip.channel >= project_.channels().size()) {
        apply(fit);
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
        AudioClip withTempo = project_.audioClips()[index];
        withTempo.sourceBpm = bpm; // not fitted yet: offset and length keep their meaning
        project_.setAudioClip(index, withTempo);
        apply(fit);
        statusBar()->showMessage(tr("Fitted from %1 BPM").arg(QString::number(bpm, 'f', 2)), 4000);
    });
}

void MainWindow::dropAudio(const QString& path, int track, int start)
{
    if (track >= static_cast<int>(project_.tracks().size())) {
        // Below the last track: a new one, when there is room.
        const auto added = project_.addTrack();
        track = added ? static_cast<int>(*added) : static_cast<int>(project_.tracks().size()) - 1;
    }
    const std::string file = QDir::cleanPath(path).toStdString();
    if (clipAudio(file)->seconds <= 0.0) {
        clipAudio_.erase(file); // it may become readable: try again next time
        statusBar()->showMessage(tr("Could not load %1.").arg(QFileInfo(path).fileName()), 6000);
        playlist_->refresh();
        return;
    }
    // As in FL Studio, the clip plays through a channel of the channel rack:
    // the one already playing this file, or a new one named after it.
    std::size_t channel = 0;
    if (const auto existing = project_.channelWithSample(file)) {
        channel = *existing;
    } else {
        channel = project_.addChannel(QFileInfo(path).completeBaseName().toStdString());
        project_.setChannelSample(channel, file);
        channelRack_->refresh();
    }
    project_.addAudioClip(AudioClip{channel, track, start});
    playlist_->refresh();
    // As in FL Studio: Draw and Paint now place this file too.
    playlist_->setSource(PlaylistView::ClipSource{true, channel});
    rebuildClipSources();
    selectChannel(channel);
    if (!songMode_) {
        // The playlist only plays in song mode: switch, or Play would stay silent.
        transport_->songModeAction()->setChecked(true); // syncs through songModeChanged
        statusBar()->showMessage(
            tr("Song mode on: Play runs through the playlist (L for pattern mode)."), 5000);
    } else {
        syncChannelRack();
    }
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
    project_.setTrackName(track, name.toStdString());
    playlist_->refresh();
}

void MainWindow::removeMixerInsert(std::size_t track)
{
    if (track == MixerModel::kMaster || track >= mixer_.size())
        return;
    const auto insert = static_cast<int>(track);
    const auto routed = static_cast<std::size_t>(std::count_if(project_.channels().begin(),
        project_.channels().end(), [insert](const Channel& c) { return c.insert == insert; }));
    if (routed > 0
        && QMessageBox::question(this, tr("Delete Insert"),
               (routed == 1 ? tr("1 channel plays into Insert %1.").arg(track)
                            : tr("%1 channels play into Insert %2.").arg(routed).arg(track))
                   + QLatin1Char(' ') + tr("Delete it? They will play into the master.") + QLatin1Char(' ')
                   + tr("This cannot be undone yet."))
            != QMessageBox::Yes)
        return;
    project_.removeInsert(insert);
    mixer_.removeInsert(track);
    mixerPanel_->removeInsert(track);
    syncInsertNames();
    updateMixer();
    syncChannelRack();
    statusBar()->showMessage(tr("Insert %1 deleted.").arg(track), 3000);
}

void MainWindow::selectChannel(std::size_t channel)
{
    currentChannel_ = std::min(channel, project_.channels().size() - 1);
    channelRack_->setSelectedChannel(currentChannel_);
    pianoRoll_->setTarget(currentPattern_, currentChannel_);
    updatePanelContexts();
}

void MainWindow::updatePanelContexts()
{
    const QString pattern = QString::fromStdString(project_.patterns()[currentPattern_].name);
    const QString channel = QString::fromStdString(project_.channels()[currentChannel_].name);
    channelRackFrame_->setContext(pattern);
    pianoRollFrame_->setContext(QStringLiteral("%1  ·  %2").arg(pattern, channel));
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
    project_.transposePattern(currentPattern_, semitones);
    patternsChanged(currentPattern_);
}

void MainWindow::insertPattern()
{
    if (roomForPatterns(1))
        patternsChanged(project_.insertPattern(currentPattern_ + 1));
}

void MainWindow::cloneCurrentPattern()
{
    if (!roomForPatterns(1))
        return;
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
                tr("Delete \u201C%1\u201D?").arg(name) + QStringLiteral("\n\n") + detail + QLatin1Char(' ')
                    + tr("This cannot be undone yet."))
            != QMessageBox::Yes)
            return;
    }
    project_.removePattern(currentPattern_);
    patternsChanged(currentPattern_ > 0 ? currentPattern_ - 1 : 0);
}

void MainWindow::moveCurrentPattern(int by)
{
    const auto count = static_cast<long long>(project_.patterns().size());
    const long long to = static_cast<long long>(currentPattern_) + by;
    if (to < 0 || to >= count)
        return;
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
    mixerPanel_->setMinimumHeight(200);
    connect(mixerPanel_, &MixerPanel::closeRequested, this, [this] { setMixerVisible(false); });
    connect(
        mixerPanel_, &MixerPanel::detachRequested, this, [this] { setMixerDetached(!isMixerDetached()); });

    connect(mixerPanel_, &MixerPanel::faderChanged, this, [this](std::size_t track, double position) {
        mixer_.setFader(track, position);
        updateMixer();
    });
    connect(mixerPanel_, &MixerPanel::panChanged, this, [this](std::size_t track, double pan) {
        mixer_.setPan(track, pan);
        updateMixer();
    });
    connect(mixerPanel_, &MixerPanel::muteToggled, this, [this](std::size_t track, bool muted) {
        mixer_.setMuted(track, muted);
        updateMixer();
    });
    connect(mixerPanel_, &MixerPanel::addInsertRequested, this, &MainWindow::addMixerInsert);
    connect(mixerPanel_, &MixerPanel::removeInsertRequested, this, &MainWindow::removeMixerInsert);
    connect(mixerPanel_, &MixerPanel::renameRequested, this, &MainWindow::renameMixerInsert);
    connect(mixerPanel_, &MixerPanel::colorChanged, this,
        [this](std::size_t track, std::optional<std::uint32_t> color) {
            if (track >= mixer_.size())
                return;
            mixer_.setColor(track, color);
            mixerPanel_->setTrackColor(track, mixer_.track(track).color);
        });
    connect(mixerPanel_, &MixerPanel::moveInsertRequested, this, [this](std::size_t from, std::size_t to) {
        if (from == MixerModel::kMaster || to == MixerModel::kMaster || from >= mixer_.size()
            || to >= mixer_.size())
            return;
        mixer_.moveInsert(from, to);
        project_.moveInsert(static_cast<int>(from), static_cast<int>(to));
        mixerPanel_->moveInsert(from, to);
        syncInsertNames();
        updateMixer();
        syncChannelRack(); // channels follow their insert
    });
    connect(mixerPanel_, &MixerPanel::soloToggled, this, [this](std::size_t track, bool soloed) {
        mixer_.setSoloed(track, soloed);
        updateMixer();
    });
    mixerPanel_->hide();
    return mixerPanel_;
}

void MainWindow::updateMixer()
{
    // Grey out inserts that another insert's solo silences.
    const bool solo = mixer_.anySoloed();
    for (std::size_t i = 1; i < mixer_.size(); ++i)
        mixerPanel_->setSilenced(i, solo && !mixer_.track(i).soloed);
    updateMasterOutput();
    audio_.engine().setMixerState(engineMixerState(mixer_));
}

void MainWindow::updateMasterOutput()
{
    // The engine has one output stage: the transport's master volume knob and
    // the mixer's master track (fader, mute, pan) both feed it.
    pendingGain_ = static_cast<float>(
        volumeToGain(transport_->masterVolume()) * mixer_.effectiveGain(MixerModel::kMaster));
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
    connect(general, &GeneralSettingsPage::startPositionModeChanged, this, [this](int mode) {
        settings_.startPositionMode
            = mode == 1 ? AppSettings::PositionMode::Time : AppSettings::PositionMode::BarsBeats;
        settings_.save();
    });
    connect(general, &GeneralSettingsPage::metronomeOnStartupChanged, this, [this](bool on) {
        settings_.metronomeOnStartup = on;
        settings_.save();
    });
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
    splitter_->setHandleWidth(1);
    rootLayout->addWidget(splitter_, 1);

    sidePanels_ = new QStackedWidget(splitter_);
    sidePanels_->setMinimumWidth(180);
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
                pluginManager_->setStore(pluginStore_); // its next scan starts from this
        });
    sidePanels_->addWidget(pluginsPanel_);
    activityBar_->addPage(Theme::icon(QStringLiteral("plug")), tr("Plugins"));

    activityBar_->addBottomAction(settingsAction_);
    connect(activityBar_, &ActivityBar::currentChanged, this, &MainWindow::showSidePanel);

    // Workspace above, mixer below (F9); the user drags the split.
    workspaceSplitter_ = new QSplitter(Qt::Vertical, splitter_);
    workspaceSplitter_->setObjectName(QStringLiteral("workspaceSplitter"));
    workspaceSplitter_->setChildrenCollapsible(false);
    workspaceSplitter_->setHandleWidth(1);
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
        const auto index
            = project_.addChannel(tr("Channel %1").arg(project_.channels().size() + 1).toStdString());
        channelRack_->refresh();
        selectChannel(index);
        syncChannelRack();
    });
    connect(channelRack_, &ChannelRackView::stepSet, this, [this](std::size_t channel, int step, bool on) {
        project_.setStep(currentPattern_, channel, step, on);
        channelRack_->update();
        syncChannelRack();
    });
    connect(channelRack_, &ChannelRackView::channelColorChanged, this,
        [this](std::size_t channel, std::optional<std::uint32_t> color) {
            project_.setChannelColor(channel, color);
            channelRack_->update();
            pianoRoll_->viewport()->update(); // its notes take the channel's colour
        });
    connect(channelRack_, &ChannelRackView::muteToggled, this, [this](std::size_t channel) {
        project_.setChannelMuted(channel, !project_.channels()[channel].muted);
        channelRack_->update();
        syncChannelRack();
    });
    connect(channelRack_, &ChannelRackView::volumeChanged, this, [this](std::size_t channel, double volume) {
        project_.setChannelVolume(channel, volume);
        channelRack_->update();
        syncChannelRack();
    });
    connect(channelRack_, &ChannelRackView::panChanged, this, [this](std::size_t channel, double pan) {
        project_.setChannelPan(channel, pan);
        channelRack_->update();
        syncChannelRack();
    });
    connect(channelRack_, &ChannelRackView::insertChanged, this, [this](std::size_t channel, int insert) {
        project_.setChannelInsert(channel, insert);
        channelRack_->update();
        syncChannelRack();
    });
    syncInsertNames();
    // Long files take a while to decode and resample: off the UI thread.
    connect(channelRack_, &ChannelRackView::sampleDropped, this,
        [this](std::size_t channel, const QString& path) {
            loadSampleThen(path, [this, channel, path] { assignSample(channel, path); });
        });
    connect(channelRack_, &ChannelRackView::channelSelected, this, &MainWindow::selectChannel);
    connect(channelRack_, &ChannelRackView::addBarRequested, this, [this] { changeBars(1); });
    connect(channelRack_, &ChannelRackView::channelOpened, this, [this](std::size_t channel) {
        selectChannel(channel);
        showWorkspace(WorkspacePage::PianoRoll);
    });

    // Piano roll.
    pianoRoll_ = new PianoRollView;
    pianoRoll_->setProject(&project_);
    pianoRollFrame_ = new PanelFrame(tr("PIANO ROLL"), pianoRoll_, workspace_);
    workspace_->addWidget(pianoRollFrame_);
    makeDetachable(detachable_[0], WorkspacePage::PianoRoll, pianoRollFrame_, tr("Piano roll"),
        tr("piano roll"), QStringLiteral("pianoRoll"));
    const auto notesEdited = [this] {
        pianoRoll_->viewport()->update();
        playlist_->viewport()->update(); // clip lengths follow the pattern
    };
    connect(pianoRoll_, &PianoRollView::noteAdded, this, [this, notesEdited](Note note) {
        project_.addNote(currentPattern_, currentChannel_, note);
        notesEdited();
    });
    connect(pianoRoll_, &PianoRollView::noteChanged, this, [this, notesEdited](std::size_t index, Note note) {
        project_.setNote(currentPattern_, currentChannel_, index, note);
        notesEdited();
    });
    connect(pianoRoll_, &PianoRollView::noteRemoved, this, [this](std::size_t index) {
        project_.removeNote(currentPattern_, currentChannel_, index);
        pianoRoll_->refresh();
        playlist_->refresh();
    });

    // Playlist.
    playlist_ = new PlaylistView;
    playlist_->setProject(&project_);
    playlistFrame_ = new PanelFrame(tr("PLAYLIST"), playlist_, workspace_);
    playlistFrame_->setContext(tr("Click to place the chosen clip"));
    workspace_->addWidget(playlistFrame_);
    auto* removeTrack = headerButton(QStringLiteral("minus"), QString(), tr("Delete the last track"));
    auto* addTrack = headerButton(QStringLiteral("plus"), tr("Track"), tr("Add a track"));
    connect(removeTrack, &QToolButton::clicked, this,
        [this] { removePlaylistTrack(project_.tracks().size() - 1); });
    connect(addTrack, &QToolButton::clicked, this, &MainWindow::addPlaylistTrack);
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
                tr("Slice: click, or drag across tracks, to cut every clip the line crosses (Shift: step)")},
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
        clipSource_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
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
        snap->setToolTip(tr("Snap: what clips line up to when placed, moved, trimmed or cut (Shift: steps)"));
        snap->setAccessibleName(tr("Snap"));
        snap->addItem(tr("Snap: Auto"), static_cast<int>(PlaylistView::Snap::Auto));
        snap->addItem(tr("Snap: Bar"), static_cast<int>(PlaylistView::Snap::Bar));
        snap->addItem(tr("Snap: Beat"), static_cast<int>(PlaylistView::Snap::Beat));
        snap->addItem(tr("Snap: Step"), static_cast<int>(PlaylistView::Snap::Step));
        connect(snap, &QComboBox::activated, this, [this, snap](int row) {
            playlist_->setSnap(static_cast<PlaylistView::Snap>(snap->itemData(row).toInt()));
        });
        playlistFrame_->addHeaderWidget(snap);
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
        playlistFrame_->setContext(tools[0].context);
    }
    // "+ Track" and "−" side by side in a box of their own.
    playlistFrame_->addHeaderWidget(makePanelButtonGroup({addTrack, removeTrack}));
    connect(playlist_, &PlaylistView::trackRenameRequested, this, &MainWindow::renamePlaylistTrack);
    connect(playlist_, &PlaylistView::trackNameEdited, this, [this](std::size_t track, const QString& name) {
        if (track >= project_.tracks().size())
            return;
        project_.setTrackName(track, name.toStdString());
        playlist_->refresh();
    });
    connect(playlist_, &PlaylistView::trackRemoveRequested, this, &MainWindow::removePlaylistTrack);
    connect(playlist_, &PlaylistView::trackColorChanged, this,
        [this](std::size_t track, std::optional<std::uint32_t> color) {
            project_.setTrackColor(track, color);
            playlist_->viewport()->update();
        });
    connect(playlist_, &PlaylistView::trackHeightChanged, this, [this](std::size_t track, int height) {
        project_.setTrackHeight(track, height);
        playlist_->refresh();
    });
    connect(playlist_, &PlaylistView::trackCollapseToggled, this, [this](std::size_t track, bool collapsed) {
        project_.setTrackCollapsed(track, collapsed);
        playlist_->refresh();
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
    connect(
        playlist_, &PlaylistView::trackMoveRequested, this, [this, songEdited](std::size_t track, bool down) {
            if (project_.moveTrackGroup(track, down)) {
                playlist_->refresh();
                songEdited();
            }
        });
    connect(playlist_, &PlaylistView::trackIndentRequested, this, [this, songEdited](std::size_t track) {
        if (project_.indentTrack(track)) {
            playlist_->refresh();
            songEdited();
        }
    });
    connect(playlist_, &PlaylistView::trackOutdentRequested, this, [this, songEdited](std::size_t track) {
        if (project_.outdentTrack(track)) {
            playlist_->refresh();
            songEdited();
        }
    });
    connect(
        playlist_, &PlaylistView::trackMuteToggled, this, [this, songEdited](std::size_t track, bool muted) {
            project_.setTrackMuted(track, muted);
            playlist_->viewport()->update();
            songEdited();
        });
    connect(
        playlist_, &PlaylistView::trackSoloToggled, this, [this, songEdited](std::size_t track, bool solo) {
            project_.setTrackSolo(track, solo);
            playlist_->viewport()->update();
            songEdited();
        });
    connect(playlist_, &PlaylistView::clipAdded, this, [this, songEdited](Clip clip) {
        project_.addClip(clip);
        playlist_->viewport()->update();
        songEdited();
    });
    connect(playlist_, &PlaylistView::clipChanged, this, [this, songEdited](std::size_t index, Clip clip) {
        if (index < project_.clips().size() && project_.clips()[index] == clip)
            return; // the drag has not reached the next grid line
        project_.setClip(index, clip);
        playlist_->viewport()->update();
        songEdited();
    });
    connect(playlist_, &PlaylistView::clipsChanged, this, [this, songEdited](const PlaylistClipEdit& edit) {
        for (const auto& [index, clip] : edit.clips)
            project_.setClip(index, clip);
        for (const auto& [index, clip] : edit.audioClips)
            project_.setAudioClip(index, clip);
        playlist_->viewport()->update();
        songEdited();
    });
    connect(playlist_, &PlaylistView::clipsRemoved, this,
        [this, songEdited](const std::vector<std::size_t>& clips, const std::vector<std::size_t>& audio) {
            // From the last: removing a clip moves the ones after it down.
            for (auto it = clips.rbegin(); it != clips.rend(); ++it)
                project_.removeClip(*it);
            for (auto it = audio.rbegin(); it != audio.rend(); ++it)
                project_.removeAudioClip(*it);
            playlist_->refresh();
            songEdited();
        });
    // Playback tool: song mode, play from the pointer while the button is held.
    connect(playlist_, &PlaylistView::playbackRequested, this, [this](double tick) {
        if (!songMode_)
            transport_->songModeAction()->setChecked(true); // syncs through songModeChanged
        auto& engine = audio_.engine();
        engine.locate(tick);
        if (!engine.isPlaying())
            engine.play();
    });
    connect(playlist_, &PlaylistView::playbackReleased, this, [this] { audio_.engine().stop(); });
    connect(playlist_, &PlaylistView::clipRemoved, this, [this, songEdited](std::size_t index) {
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
    connect(playlist_, &PlaylistView::audioDropped, this, [this](const QString& path, int track, int start) {
        loadSampleThen(path, [this, path, track, start] { dropAudio(path, track, start); });
    });
    connect(playlist_, &PlaylistView::audioClipChanged, this,
        [this, songEdited](std::size_t index, AudioClip clip) {
            if (index < project_.audioClips().size() && project_.audioClips()[index] == clip)
                return;
            project_.setAudioClip(index, std::move(clip));
            playlist_->viewport()->update();
            songEdited();
        });
    connect(playlist_, &PlaylistView::audioClipMenuRequested, this, &MainWindow::showAudioClipMenu);
    connect(playlist_, &PlaylistView::audioClipAdded, this, [this, songEdited](AudioClip clip) {
        project_.addAudioClip(std::move(clip));
        playlist_->refresh();
        songEdited();
    });
    connect(playlist_, &PlaylistView::audioClipRemoved, this, [this, songEdited](std::size_t index) {
        project_.removeAudioClip(index);
        playlist_->refresh();
        songEdited();
    });
    connect(playlist_, &PlaylistView::patternPicked, this, &MainWindow::selectPattern);

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
    // The tool bars hold the transport and the window buttons' space; never
    // come back with them hidden, whatever an older saved state says.
    for (auto* bar : findChildren<QToolBar*>())
        bar->show();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // A visible Settings window would otherwise keep the app running.
    settingsDialog_->close();

    // Save the normal window size and give the menu bar and Dock back.
    if (coversScreen_)
        toggleFullScreen();

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
    settingsDialog_->showPage(page);
}

void MainWindow::showPluginManager()
{
    if (pluginManager_ == nullptr) {
        // Kept for the session (hidden on close) so a running scan survives
        // closing the window.
        pluginManager_ = new PluginManagerDialog(this);
        pluginManager_->setStore(pluginStore_);
        connect(pluginManager_, &PluginManagerDialog::storeChanged, this, [this](const PluginStore& store) {
            pluginStore_ = store;
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

    // Re-send UI state so the engine matches what the user sees.
    pendingTempo_.reset();
    songLoopEnd_ = -1.0; // sent again on the next tick
    audio_.engine().setTempo(transport_->tempo());
    audio_.engine().setMetronomeEnabled(transport_->metronomeEnabled());
    updateMasterOutput();
    audio_.engine().setMasterGain(*pendingGain_);
    audio_.engine().setMasterPan(*pendingPan_);
    pendingGain_.reset();
    pendingPan_.reset();
    audio_.engine().setMixerState(engineMixerState(mixer_));

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
    // Pattern mode loops the pattern itself; an empty song has nothing to loop.
    const double end = songMode_ ? static_cast<double>(songEndTicks()) : 0.0;
    if (end == songLoopEnd_)
        return;
    auto& engine = audio_.engine();
    if ((end > 0.0 ? engine.setLoop(0.0, end) : engine.clearLoop()))
        songLoopEnd_ = end; // else the queue was full: tried again next tick
}

void MainWindow::syncChannelRack()
{
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
    const QString name = QFileInfo(path).completeBaseName();
    if (channel >= project_.channels().size())
        channel = project_.addChannel(name.toStdString());
    else
        project_.setChannelName(channel, name.toStdString());
    project_.setChannelSample(channel, QDir::cleanPath(path).toStdString());
    channelRack_->refresh();
    selectChannel(channel);
    syncChannelRack();
}

void MainWindow::newProject()
{
    stopAndRewind();
    transport_->setTempo(kDefaultTempo);
    pendingTempo_ = kDefaultTempo; // sent by refreshFromEngine()
    transport_->recordAction()->setChecked(false);
    transport_->keyDisplay()->setRootNote(0); // C
    transport_->keyDisplay()->setScale(Scale::Major);
    pianoRoll_->setKey(0, Scale::Major);
    project_ = Project{};
    rackAudio_.reset();
    currentPattern_ = 0;
    currentChannel_ = 0;
    refreshEditors();
    selectChannel(0);
    statusBar()->showMessage(tr("New project"), 3000);
}

void MainWindow::togglePlay()
{
    auto& engine = audio_.engine();
    if (engine.isPlaying())
        engine.stop();
    else
        engine.play();
}

void MainWindow::stopAndRewind()
{
    auto& engine = audio_.engine();
    engine.stop();
    engine.locate(0.0);
}

void MainWindow::tapTempo()
{
    if (!tapClock_.isValid())
        tapClock_.start();
    const double seconds = static_cast<double>(tapClock_.nsecsElapsed()) / 1e9;
    if (const auto bpm = tapTempo_.tap(seconds)) {
        transport_->setTempo(*bpm);
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
    if (audio_.isRunning() && !tempoSent && !pendingTempo_)
        transport_->setTempo(engine.tempo());

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
    pianoRoll_->setPlayhead(showPlayhead ? std::fmod(ticks, patternTicks) : -1.0);
    channelRack_->setPlayStep(engine.isPlaying()
            ? static_cast<int>(std::fmod(ticks, static_cast<double>(kTicksPerBar)) / kTicksPerStep)
            : -1);

    if (pendingGain_ && audio_.isRunning() && engine.setMasterGain(*pendingGain_))
        pendingGain_.reset();
    if (pendingPan_ && audio_.isRunning() && engine.setMasterPan(*pendingPan_))
        pendingPan_.reset();

    // Meters: always take the engine's peaks (so a reopened mixer does not
    // show an old one).
    const bool running = audio_.isRunning();
    const float left = running ? engine.takeOutputPeak(0) : 0.0f;
    const float right = running ? engine.takeOutputPeak(1) : 0.0f;
    const bool showMeters = mixerPanel_->isVisible();
    if (showMeters)
        mixerPanel_->meter(MixerModel::kMaster)->addPeaks(left, right);
    for (std::size_t i = 1; i < mixer_.size() && i < mixerPanel_->trackCount(); ++i) {
        const auto insert = static_cast<int>(i);
        const float l = running ? engine.takeInsertPeak(insert, 0) : 0.0f;
        const float r = running ? engine.takeInsertPeak(insert, 1) : 0.0f;
        if (showMeters)
            mixerPanel_->meter(i)->addPeaks(l, r);
    }
    if (showMeters) {
        for (std::size_t i = 0; i < mixerPanel_->trackCount(); ++i)
            mixerPanel_->meter(i)->tick(dt);
    }

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
    QMessageBox::about(this, tr("About VloomDAW"),
        tr("<h3>VloomDAW %1</h3>"
           "<p>An open-source digital audio workstation by Vloom Audio.</p>"
           "<p>Engine %2 · Qt %3</p>"
           "<p>Licensed under the GNU GPL v3 or later.<br>"
           "<a href='https://github.com/vloom-audio'>github.com/vloom-audio</a></p>")
            .arg(QStringLiteral(VLOOMDAW_VERSION), QString::fromLatin1(vloom::engineVersion()),
                QString::fromLatin1(qVersion())));
}

} // namespace vloomdaw
