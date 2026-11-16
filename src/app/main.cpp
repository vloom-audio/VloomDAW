// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/AudioController.hpp"
#include "core/DeviceRef.hpp"
#include "ui/AppSettings.hpp"
#include "ui/FilePaths.hpp"
#include "ui/MainWindow.hpp"
#include "ui/Theme.hpp"
#include "ui/dialogs/SettingsDialog/AudioSettingsPage.hpp"
#include "ui/dialogs/SettingsDialog/GeneralSettingsPage.hpp"
#include "ui/dialogs/SettingsDialog/SettingsDialog.hpp"
#include "ui/dialogs/SettingsDialog/ThemeSettingsPage.hpp"
#include "ui/launcher/LauncherDialog.hpp"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <optional>

int main(int argc, char* argv[])
{
    // Qt delays each repaint request by its own 5 ms timer, which Windows
    // rounds up to its 15.6 ms tick. Against our screen-rate refresh timer
    // that dropped frames (98 fps on a 144 Hz screen). Paint at once instead;
    // the refresh timer already paces the frames. Must be set before the
    // QApplication exists; a value from the environment wins.
    if (!qEnvironmentVariableIsSet("QT_QPA_UPDATE_IDLE_TIME"))
        qputenv("QT_QPA_UPDATE_IDLE_TIME", "0");

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("VloomDAW"));
    QApplication::setApplicationVersion(QStringLiteral(VLOOMDAW_VERSION));
    QApplication::setOrganizationName(QStringLiteral("Vloom Audio"));
    QApplication::setOrganizationDomain(QStringLiteral("vloom-audio.com"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app/vloomdaw.png")));

    // Apply the saved theme before any window exists.
    vloomdaw::AppSettings settings = vloomdaw::AppSettings::load();
    vloomdaw::Theme::select(settings.theme);
    vloomdaw::Theme::apply(app);

    // The controller outlives the window, so audio stops after the UI is gone.
    vloomdaw::AudioController audio;

    // "VloomDAW song.vloom" (and opening a .vloom with the app) opens it directly;
    // UNC network paths are refused before QFileInfo touches SMB.
    const QStringList args = QApplication::arguments();
    QString startupFile;
    if (args.size() > 1 && vloomdaw::isLocalPath(args.at(1))
        && args.at(1).endsWith(QStringLiteral(".vloom"), Qt::CaseInsensitive)) {
        const QString abs = QFileInfo(args.at(1)).absoluteFilePath();
        if (vloomdaw::isLocalPath(abs))
            startupFile = abs;
    }

    std::optional<double> startupTempo;
    if (startupFile.isEmpty() && vloomdaw::LauncherDialog::showAtStartup()) {
        // Keep the application alive while standalone startup dialogs open and close
        // before MainWindow is constructed.
        QApplication::setQuitOnLastWindowClosed(false);

        const auto openPreferredAudio = [&audio, &settings] {
            audio.setFormat(settings.sampleRate, settings.bufferFrames);
            const auto devices = audio.outputDevices();
            int index = vloomdaw::findDevice(
                devices.names, {settings.outputDevice.toStdString(), settings.outputDeviceOccurrence});
            if (index < 0)
                index = devices.defaultIndex;
            audio.openDevice(index);
            audio.engine().setMetronomeEnabled(settings.metronomeOnStartup);
        };
        openPreferredAudio();

        for (;;) {
            vloomdaw::LauncherDialog::AudioStatus status;
            if (audio.isRunning()) {
                status.device = QString::fromStdString(audio.deviceName());
                status.sampleRate = audio.sampleRate();
                status.bufferFrames = audio.bufferFrames();
            }
            vloomdaw::LauncherDialog launcher(status, 120.0);
            launcher.exec();

            bool chosen = false;
            switch (launcher.choice()) {
            case vloomdaw::LauncherDialog::Choice::None:
                return 0;
            case vloomdaw::LauncherDialog::Choice::NewProject:
                startupTempo = launcher.tempo();
                chosen = true;
                break;
            case vloomdaw::LauncherDialog::Choice::OpenFile:
                startupFile = launcher.path();
                chosen = true;
                break;
            case vloomdaw::LauncherDialog::Choice::BrowseOpen: {
                constexpr QLatin1StringView kFolderKey{"project/lastFolder"};
                QString folder = QSettings().value(kFolderKey).toString();
                if (!vloomdaw::isLocalPath(folder) || !QDir(folder).exists())
                    folder = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
                const QString picked = QFileDialog::getOpenFileName(
                    nullptr, QObject::tr("Open Project"), folder, QObject::tr("VloomDAW project (*.vloom)"));
                if (vloomdaw::isLocalPath(picked)) {
                    QSettings().setValue(kFolderKey, QFileInfo(picked).absolutePath());
                    startupFile = picked;
                    chosen = true;
                }
                break;
            }
            case vloomdaw::LauncherDialog::Choice::AudioSettings: {
                vloomdaw::SettingsDialog dlg;
                const auto syncDialogAudio = [&] {
                    const auto devs = audio.outputDevices();
                    int idx = vloomdaw::findDevice(
                        devs.names, {settings.outputDevice.toStdString(), settings.outputDeviceOccurrence});
                    if (idx < 0)
                        idx = devs.defaultIndex;
                    QStringList names;
                    for (const auto& name : devs.names)
                        names << QString::fromStdString(name);
                    dlg.audioPage()->setDevices(names, idx);
                    dlg.audioPage()->setFormat(settings.sampleRate, settings.bufferFrames);
                    if (audio.isRunning()) {
                        QString text = QObject::tr("Running at %1 Hz").arg(audio.sampleRate());
                        if (audio.sampleRate() != audio.requestedSampleRate())
                            text += QObject::tr(" (device does not support %1 Hz)")
                                        .arg(audio.requestedSampleRate());
                        dlg.audioPage()->setStatus(text, true);
                    } else {
                        dlg.audioPage()->setStatus(QObject::tr("Not running"), false);
                    }
                };
                syncDialogAudio();
                QObject::connect(
                    dlg.audioPage(), &vloomdaw::AudioSettingsPage::deviceSelected, &dlg, [&](int index) {
                        const auto devs = audio.outputDevices();
                        const vloomdaw::DeviceRef ref = vloomdaw::deviceRefAt(devs.names, index);
                        settings.outputDevice = QString::fromStdString(ref.name);
                        settings.outputDeviceOccurrence = ref.occurrence;
                        settings.save();
                        openPreferredAudio();
                        syncDialogAudio();
                    });
                QObject::connect(dlg.audioPage(), &vloomdaw::AudioSettingsPage::formatChanged, &dlg,
                    [&](int sampleRate, int bufferFrames) {
                        settings.sampleRate = sampleRate;
                        settings.bufferFrames = bufferFrames;
                        settings.save();
                        openPreferredAudio();
                        syncDialogAudio();
                    });
                dlg.generalPage()->setStartPositionMode(static_cast<int>(settings.startPositionMode));
                dlg.generalPage()->setMetronomeOnStartup(settings.metronomeOnStartup);
                QObject::connect(dlg.generalPage(), &vloomdaw::GeneralSettingsPage::startPositionModeChanged,
                    &dlg, [&](int mode) {
                        settings.startPositionMode = mode == 1
                            ? vloomdaw::AppSettings::PositionMode::Time
                            : vloomdaw::AppSettings::PositionMode::BarsBeats;
                        settings.save();
                    });
                QObject::connect(dlg.generalPage(), &vloomdaw::GeneralSettingsPage::metronomeOnStartupChanged,
                    &dlg, [&](bool on) {
                        settings.metronomeOnStartup = on;
                        settings.save();
                    });
                dlg.themePage()->setChoice(settings.theme);
                QObject::connect(dlg.themePage(), &vloomdaw::ThemeSettingsPage::choiceChanged, &dlg,
                    [&](const vloomdaw::ThemeChoice& choice) {
                        settings.theme = choice;
                        settings.save();
                        vloomdaw::Theme::select(settings.theme);
                        vloomdaw::Theme::apply(app);
                        dlg.update();
                    });
                dlg.showPage(vloomdaw::SettingsDialog::Page::Audio);
                dlg.exec();
                break;
            }
            }
            if (chosen)
                break;
        }

        QApplication::setQuitOnLastWindowClosed(true);
    }

    vloomdaw::MainWindow window(audio);
    if (startupTempo)
        window.startEmptyProject(*startupTempo);
    window.show();
    if (!startupFile.isEmpty())
        QTimer::singleShot(0, &window, [&window, startupFile] { window.openProjectFile(startupFile); });

    return QApplication::exec();
}
