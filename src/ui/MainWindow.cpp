// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/MainWindow.hpp"

#include "audio/AudioController.hpp"
#include "core/DeviceRef.hpp"
#include "core/NoteNames.hpp"
#include "core/PositionFormat.hpp"
#include "core/ProcessMemory.hpp"
#include "core/Scales.hpp"
#include "ui/Theme.hpp"
#include "ui/browser/BrowserPanel.hpp"
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
#include "ui/widgets/TransportBar/TransportBar.hpp"

#if defined(Q_OS_MACOS)
#include "ui/MacWindow.hpp"
#endif
#if defined(Q_OS_WIN)
#include "ui/WinWindow.hpp"
#include "ui/widgets/WindowButtons/WindowButtons.hpp"
#endif

#include <vloom/engine/core/Version.hpp>

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QPainter>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QShowEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <string>

namespace vloomdaw {

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
            statusBar()->showMessage(tr("%1 is not available yet: projects arrive in milestone M4.").arg(feature), 5000);
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

    // No Transport menu: the tool bar buttons carry those shortcuts. Go to
    // Start has no button, so it is added to the window to keep Home working.
    addAction(transport_->rewindAction());

    // On macOS the preferences role moves this into the app menu.
    auto* optionsMenu = menuBar()->addMenu(tr("&Options"));
    settingsAction_ = optionsMenu->addAction(Theme::icon(QStringLiteral("settings")), tr("Settings…"),
        this, [this] { showSettings(SettingsDialog::Page::General); });
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
    connect(transport_, &TransportBar::tempoChanged, this, [this](double bpm) { pendingTempo_ = bpm; });
    // The engine cannot record yet (no audio input): keep the armed state in
    // the button and say so, so the button is not silently dead.
    connect(transport_, &TransportBar::recordArmedChanged, this, [this](bool armed) {
        statusBar()->showMessage(
            armed ? tr("Recording armed. Audio input is not available yet.") : tr("Recording disarmed"), 4000);
    });
    connect(transport_, &TransportBar::metronomeToggled, this,
        [this](bool on) { audio_.engine().setMetronomeEnabled(on); });

    // Like the tempo, the volume is sent once per tick (a knob drag fires on
    // every mouse move) and only while a device drains the queue.
    connect(transport_, &TransportBar::masterVolumeChanged, this, [this] { updateMasterOutput(); });
    connect(transport_, &TransportBar::masterPitchChanged, this, [this](double semitones) {
        statusBar()->showMessage(tr("Master pitch %1 semitones: it applies to instruments, which arrive in milestone M2.")
                                     .arg(QString::number(semitones, 'f', 1)),
            4000);
    });
    connect(transport_, &TransportBar::songModeChanged, this, [this](bool song) {
        statusBar()->showMessage(song ? tr("Song mode: the playlist arrives in milestone M3.")
                                      : tr("Pattern mode: patterns arrive with the channel rack in milestone M1."),
            4000);
    });

    connect(transport_->viewButtons(), &ViewButtons::viewRequested, this, &MainWindow::showView);

    // Transport and view keys (Space, F5-F9, …) also work while a detached
    // mixer or playlist window has the focus.
    for (QAction* action : transport_->findChildren<QAction*>())
        action->setShortcutContext(Qt::ApplicationShortcut);
    // The key is shown only for now; the piano roll will highlight its scale.
    const auto announceKey = [this] {
        const KeyDisplay* key = transport_->keyDisplay();
        const std::string_view root = noteName(key->rootNote());
        const std::string_view scale = scaleInfo(key->scale()).name;
        statusBar()->showMessage(tr("Key: %1 %2 (highlighted in the piano roll)")
                                     .arg(QString::fromLatin1(root.data(), static_cast<qsizetype>(root.size())),
                                         QString::fromLatin1(scale.data(), static_cast<qsizetype>(scale.size()))),
            4000);
        pianoRoll_->setKey(key->rootNote(), key->scale());
    };
    connect(transport_->keyDisplay(), &KeyDisplay::rootNoteChanged, this, announceKey);
    connect(transport_->keyDisplay(), &KeyDisplay::scaleChanged, this, announceKey);

    // Patterns: the selector numbers from 1, the project indexes from 0.
    PatternSelector* patterns = transport_->patternSelector();
    connect(patterns, &PatternSelector::currentChanged, this, [this](int number) {
        while (static_cast<int>(project_.patterns().size()) < number)
            project_.addPattern(); // the selector added one
        selectPattern(static_cast<std::size_t>(number - 1));
    });
    connect(patterns, &PatternSelector::patternDeleted, this, [this](int number) {
        project_.removePattern(static_cast<std::size_t>(number - 1));
        refreshEditors();
    });
}

void MainWindow::selectPattern(std::size_t pattern)
{
    currentPattern_ = std::min(pattern, project_.patterns().size() - 1);
    transport_->patternSelector()->setCurrent(static_cast<int>(currentPattern_) + 1);
    channelRack_->setPattern(currentPattern_);
    pianoRoll_->setTarget(currentPattern_, currentChannel_);
    playlist_->setCurrentPattern(currentPattern_);
    updatePanelContexts();
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

FloatingPanel* MainWindow::makeFloatingPanel(const QString& title, const QString& settingsKey,
    std::function<void()> onClosed)
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

bool MainWindow::isMixerDetached() const { return mixerWindow_ != nullptr && mixerWindow_->content() == mixerPanel_; }

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
            mixerWindow_ = makeFloatingPanel(tr("Mixer"), QString(kMixerWindowKey), [this] {
                transport_->viewButtons()->setViewOpen(ViewButtons::View::Mixer, false);
            });
        // Takes it out of the splitter; the window buttons join its header.
        mixerWindow_->host(mixerPanel_, [this](QWidget* buttons) { mixerPanel_->addHeaderWidget(buttons); });
    } else {
        QWidget* panel = mixerWindow_->release();
        workspaceSplitter_->insertWidget(1, panel);
        workspaceSplitter_->setSizes({std::max(200, workspaceSplitter_->height() - kDefaultMixerHeight), kDefaultMixerHeight});
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

void MainWindow::makeDetachable(DetachablePage& d, WorkspacePage page, PanelFrame* frame, const QString& title,
    const QString& name, const QString& settingsId)
{
    d.page = page;
    d.frame = frame;
    d.title = title;
    d.settingsId = settingsId;

    d.button = new DetachButton(name);
    frame->addHeaderWidget(d.button);
    connect(d.button, &QToolButton::clicked, this, [this, page] { setPageDetached(page, !isPageDetached(page)); });

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
    connect(mixerPanel_, &MixerPanel::detachRequested, this, [this] { setMixerDetached(!isMixerDetached()); });

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
}

void MainWindow::updateMasterOutput()
{
    // The engine has one output stage: the transport's master volume knob and
    // the mixer's master track (fader, mute, pan) both feed it.
    pendingGain_ = static_cast<float>(volumeToGain(transport_->masterVolume())
        * mixer_.effectiveGain(MixerModel::kMaster));
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
    activityBar_->addPage(Theme::icon(QStringLiteral("folder")), tr("Browser"));

    // Plugins: only what the user's last scan found; nothing scans on its own.
    pluginStore_ = PluginStore::load();
    pluginsPanel_ = new PluginsPanel(sidePanels_);
    pluginsPanel_->setStore(pluginStore_);
    connect(pluginsPanel_, &PluginsPanel::managerRequested, this, &MainWindow::showPluginManager);
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
    channelRackFrame_->addHeaderWidget(addChannel);
    workspace_->addWidget(channelRackFrame_);
    connect(addChannel, &QToolButton::clicked, this, [this] {
        const auto index = project_.addChannel(tr("Channel %1").arg(project_.channels().size() + 1).toStdString());
        channelRack_->refresh();
        selectChannel(index);
    });
    connect(channelRack_, &ChannelRackView::stepSet, this, [this](std::size_t channel, int step, bool on) {
        project_.setStep(currentPattern_, channel, step, on);
        channelRack_->update();
    });
    connect(channelRack_, &ChannelRackView::muteToggled, this, [this](std::size_t channel) {
        project_.setChannelMuted(channel, !project_.channels()[channel].muted);
        channelRack_->update();
    });
    connect(channelRack_, &ChannelRackView::channelSelected, this, &MainWindow::selectChannel);
    connect(channelRack_, &ChannelRackView::channelOpened, this, [this](std::size_t channel) {
        selectChannel(channel);
        showWorkspace(WorkspacePage::PianoRoll);
    });

    // Piano roll.
    pianoRoll_ = new PianoRollView;
    pianoRoll_->setProject(&project_);
    pianoRollFrame_ = new PanelFrame(tr("PIANO ROLL"), pianoRoll_, workspace_);
    workspace_->addWidget(pianoRollFrame_);
    makeDetachable(detachable_[0], WorkspacePage::PianoRoll, pianoRollFrame_, tr("Piano roll"), tr("piano roll"),
        QStringLiteral("pianoRoll"));
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
    playlistFrame_->setContext(tr("Click to place the current pattern"));
    workspace_->addWidget(playlistFrame_);
    // Settings ids match the keys of the first release that had a detachable playlist.
    makeDetachable(detachable_[1], WorkspacePage::Playlist, playlistFrame_, tr("Playlist"), tr("playlist"),
        QStringLiteral("playlist"));
    connect(playlist_, &PlaylistView::clipAdded, this, [this](Clip clip) {
        project_.addClip(clip);
        playlist_->viewport()->update();
    });
    connect(playlist_, &PlaylistView::clipChanged, this, [this](std::size_t index, Clip clip) {
        project_.setClip(index, clip);
        playlist_->viewport()->update();
    });
    connect(playlist_, &PlaylistView::clipRemoved, this, [this](std::size_t index) {
        project_.removeClip(index);
        playlist_->refresh();
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
    auto* hint = new QLabel(tr("Press %1 to start the metronome.\n"
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
            QAction* action = menu->addAction(QString::fromStdString(deviceNames_[static_cast<std::size_t>(i)]));
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
        settings.setValue(QStringLiteral("mainWindow/%1WindowOpen").arg(d.settingsId), detached && d.window->isVisible());
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

void MainWindow::startDevice(int index)
{
    std::string error;
    if (!audio_.openDevice(index, &error)) {
        deviceStatus_->setText(tr("No audio output"));
        settingsDialog_->audioPage()->setStatus(tr("Not running: %1").arg(QString::fromStdString(error)), false);
        statusBar()->showMessage(tr("Audio error: %1").arg(QString::fromStdString(error)), 8000);
        return;
    }

    // Re-send UI state so the engine matches what the user sees.
    pendingTempo_.reset();
    audio_.engine().setTempo(transport_->tempo());
    audio_.engine().setMetronomeEnabled(transport_->metronomeEnabled());
    updateMasterOutput();
    audio_.engine().setMasterGain(*pendingGain_);
    audio_.engine().setMasterPan(*pendingPan_);
    pendingGain_.reset();
    pendingPan_.reset();

    const QString format = tr("%1 Hz  ·  %2 samples").arg(audio_.sampleRate()).arg(audio_.bufferFrames());
    deviceStatus_->setText(
        QStringLiteral("%1  ·  %2").arg(QString::fromStdString(audio_.deviceName()), format));

    // Say so when the device could not run at the requested rate.
    QString status = tr("Running at %1 Hz").arg(audio_.sampleRate());
    if (audio_.sampleRate() != audio_.requestedSampleRate())
        status += tr(" (device does not support %1 Hz)").arg(audio_.requestedSampleRate());
    settingsDialog_->audioPage()->setStatus(status, true);
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
    transport_->patternSelector()->reset();
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
            performanceOverlay_->setFigures(frameStats_.snapshot(now), audio_.isRunning() ? engine.cpuLoad() : 0.0f,
                refreshIntervalMs_);
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
    // show an old one); insert tracks have no audio until M1.
    const float left = audio_.isRunning() ? engine.takeOutputPeak(0) : 0.0f;
    const float right = audio_.isRunning() ? engine.takeOutputPeak(1) : 0.0f;
    if (mixerPanel_->isVisible()) {
        mixerPanel_->meter(MixerModel::kMaster)->addPeaks(left, right);
        for (std::size_t i = 0; i < mixerPanel_->trackCount(); ++i)
            mixerPanel_->meter(i)->tick(dt);
    }

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
