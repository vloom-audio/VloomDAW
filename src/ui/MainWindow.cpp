// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/MainWindow.hpp"

#include "audio/AudioController.hpp"
#include "core/DeviceRef.hpp"
#include "core/PositionFormat.hpp"
#include "ui/browser/BrowserPanel.hpp"
#include "ui/dialogs/SettingsDialog/AudioSettingsPage.hpp"
#include "ui/dialogs/SettingsDialog/GeneralSettingsPage.hpp"
#include "ui/dialogs/SettingsDialog/SettingsDialog.hpp"
#include "ui/widgets/ActivityBar/ActivityBar.hpp"
#include "ui/widgets/TransportBar/TransportBar.hpp"

#if defined(Q_OS_MACOS)
#include "ui/MacWindow.hpp"
#endif

#include <vloom/engine/core/Version.hpp>

#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QSettings>
#include <QShowEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWindow>

#include <string>

namespace vloomdaw {

namespace {
constexpr int kRefreshIntervalMs = 33; // ~30 fps playhead display

// QSettings keys for the window layout.
constexpr QLatin1StringView kGeometryKey{"mainWindow/geometry"};
constexpr QLatin1StringView kStateKey{"mainWindow/state"};
constexpr QLatin1StringView kSplitterKey{"mainWindow/sidebarSplitter"};
constexpr QLatin1StringView kSidebarPageKey{"mainWindow/sidebarPage"};

constexpr int kDefaultSidebarWidth = 260;

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

    refreshTimer_ = new QTimer(this);
    connect(refreshTimer_, &QTimer::timeout, this, &MainWindow::refreshFromEngine);
    refreshTimer_->start(kRefreshIntervalMs);
}

// ---------------------------------------------------------------- construction

void MainWindow::createMenus()
{
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    // close(), not QApplication::quit(): closeEvent() saves the window state
    // and restores the menu bar / Dock; closing the last window then quits.
    auto* quit = fileMenu->addAction(tr("&Quit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);

    // No Transport menu: the tool bar buttons carry those shortcuts. Go to
    // Start has no button, so it is added to the window to keep Home working.
    addAction(transport_->rewindAction());

    // On macOS the preferences role moves this into the app menu.
    auto* optionsMenu = menuBar()->addMenu(tr("&Options"));
    settingsAction_ = optionsMenu->addAction(QIcon(QStringLiteral(":/icons/settings.svg")), tr("Settings…"),
        this, [this] { showSettings(SettingsDialog::Page::General); });
    settingsAction_->setShortcut(QKeySequence::Preferences);
    settingsAction_->setMenuRole(QAction::PreferencesRole);
    settingsAction_->setToolTip(
        tr("Settings (%1)").arg(settingsAction_->shortcut().toString(QKeySequence::NativeText)));
    settingsAction_->setIconVisibleInMenu(false);
    optionsMenu->addAction(
        tr("Audio Settings…"), this, [this] { showSettings(SettingsDialog::Page::Audio); });

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    fullScreenAction_ = viewMenu->addAction(tr("Enter Full Screen"), this, &MainWindow::toggleFullScreen);
    fullScreenAction_->setShortcut(QKeySequence::FullScreen);

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
    connect(transport_, &TransportBar::metronomeToggled, this,
        [this](bool on) { audio_.engine().setMetronomeEnabled(on); });
}

void MainWindow::createSettingsDialog()
{
    settingsDialog_ = new SettingsDialog(this);

    // Every change applies now and is saved at once.
    auto* audio = settingsDialog_->audioPage();
    audio->setFormat(settings_.sampleRate, settings_.bufferFrames);
    connect(audio, &AudioSettingsPage::deviceSelected, this, [this](int index) {
        // `index` is into the list the page shows, which is deviceNames_.
        const DeviceRef ref = deviceRefAt(deviceNames_, index);
        settings_.outputDevice = QString::fromStdString(ref.name);
        settings_.outputDeviceOccurrence = ref.occurrence;
        settings_.save();
        openSelectedDevice();
    });
    connect(audio, &AudioSettingsPage::formatChanged, this, &MainWindow::changeAudioFormat);

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
    activityBar_->addPage(QIcon(QStringLiteral(":/icons/folder.svg")), tr("Browser"));
    activityBar_->addBottomAction(settingsAction_);
    connect(activityBar_, &ActivityBar::currentChanged, this, &MainWindow::showSidePanel);

    splitter_->addWidget(createWorkspace());
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
}

QWidget* MainWindow::createWorkspace()
{
    // Empty state until the channel rack arrives in M1.
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
                               "The channel rack and step sequencer arrive in milestone M1.")
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
    deviceStatus_ = new QLabel(this);
    statusBar()->addPermanentWidget(deviceStatus_);
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
#endif
}

void MainWindow::placeWindowButtons()
{
#if defined(Q_OS_MACOS) && QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    placeMacWindowButtons(this, transport_->height(), kWindowButtonsInset);
    // AppKit may lay the title bar out again after this event; repeat once
    // the event loop is idle so our position wins.
    QTimer::singleShot(
        0, this, [this] { placeMacWindowButtons(this, transport_->height(), kWindowButtonsInset); });
#endif
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    placeWindowButtons();
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    placeWindowButtons();
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
    settings.setValue(kSidebarPageKey, activityBar_->current());
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
        settingsDialog_->audioPage()->setStatus(tr("Not running: %1").arg(QString::fromStdString(error)));
        statusBar()->showMessage(tr("Audio error: %1").arg(QString::fromStdString(error)), 8000);
        return;
    }

    // Re-send UI state so the engine matches what the user sees.
    pendingTempo_.reset();
    audio_.engine().setTempo(transport_->tempo());
    audio_.engine().setMetronomeEnabled(transport_->metronomeEnabled());

    const QString format = tr("%1 Hz  ·  %2 samples").arg(audio_.sampleRate()).arg(audio_.bufferFrames());
    deviceStatus_->setText(
        QStringLiteral("%1  ·  %2").arg(QString::fromStdString(audio_.deviceName()), format));

    // Say so when the device could not run at the requested rate.
    QString status = tr("Running at %1 Hz").arg(audio_.sampleRate());
    if (audio_.sampleRate() != audio_.requestedSampleRate())
        status += tr(" (device does not support %1 Hz)").arg(audio_.requestedSampleRate());
    settingsDialog_->audioPage()->setStatus(status);
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

    const double ticks = engine.positionTicks();
    transport_->setPosition(QString::fromStdString(formatPosition(ticks, engine.beatsPerBar())));
    transport_->setTime(QString::fromStdString(formatTime(ticksToSeconds(ticks, engine.tempo()))));
    transport_->setTimeSignature(engine.beatsPerBar(), 4); // engine has no beat unit yet
    transport_->setPlaying(engine.isPlaying());
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
