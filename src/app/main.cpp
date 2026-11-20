// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/AudioController.hpp"
#include "core/BrowserFilter.hpp"
#include "core/DeviceRef.hpp"
#include "core/StemModels.hpp"
#include "ui/AppSettings.hpp"
#include "ui/FilePaths.hpp"
#include "ui/MainWindow.hpp"
#include "ui/Theme.hpp"
#include "ui/dialogs/AudioIoDialog/AudioIoDialog.hpp"
#include "ui/dialogs/SettingsDialog/AudioSettingsPage.hpp"
#include "ui/dialogs/SettingsDialog/GeneralSettingsPage.hpp"
#include "ui/dialogs/SettingsDialog/SettingsDialog.hpp"
#include "ui/dialogs/SettingsDialog/StemSettingsPage.hpp"
#include "ui/dialogs/SettingsDialog/ThemeSettingsPage.hpp"
#include "ui/launcher/LauncherDialog.hpp"
#include "ui/stems/StemModelStore.hpp"
#include "ui/stems/StemSeparator.hpp"
#if defined(Q_OS_WIN)
#include "ui/WinWindow.hpp"
#endif

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QLocale>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <array>
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

    // Keep numeric spin boxes, BPM readouts, and file-size labels in standard
    // Western Arabic digits (0-9) matching the English UI, even when the host
    // OS regional format uses native digits (such as Thai regional digits).
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));

    QApplication app(argc, argv);
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    QApplication::setApplicationName(QStringLiteral("VloomDAW"));
    QApplication::setApplicationVersion(QStringLiteral(VLOOMDAW_VERSION));
    QApplication::setOrganizationName(QStringLiteral("Vloom Audio"));
    QApplication::setOrganizationDomain(QStringLiteral("vloom-audio.com"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app/vloomdaw.png")));

    // Apply the saved theme before any window exists.
    vloomdaw::AppSettings settings = vloomdaw::AppSettings::load();
    vloomdaw::Theme::select(settings.theme);
    vloomdaw::Theme::apply(app);
#if defined(Q_OS_WIN)
    if (settings.associateProjectFiles)
        vloomdaw::setWindowsProjectFileAssociation(QCoreApplication::applicationFilePath(), true);
#endif

    // The controller outlives the window, so audio stops after the UI is gone.
    vloomdaw::AudioController audio;

    // "VloomDAW song.vloom" (such as double-clicking TESTTTTT.vloom in Explorer)
    // or "VloomDAW song.mid" opens it directly; UNC network paths are refused
    // before QFileInfo touches SMB.
    const QStringList args = QApplication::arguments();
    QString startupFile;
    for (int i = 1; i < args.size(); ++i) {
        const QString& arg = args.at(i);
        if (arg.startsWith(QLatin1Char('-')) || !vloomdaw::isLocalPath(arg))
            continue;
        if (arg.endsWith(QStringLiteral(".vloom"), Qt::CaseInsensitive)
            || vloomdaw::isMidiFile(QFileInfo(arg).fileName().toStdString())) {
            const QString abs = QFileInfo(arg).absoluteFilePath();
            if (vloomdaw::isLocalPath(abs)) {
                startupFile = abs;
                break;
            }
        }
    }

    std::optional<double> startupTempo;
    if (startupFile.isEmpty() && vloomdaw::LauncherDialog::showAtStartup()) {
        // Keep the application alive while standalone startup dialogs open and close
        // before MainWindow is constructed.
        QApplication::setQuitOnLastWindowClosed(false);

        QString lastInputError;
        const auto applyIoToAudio = [&audio, &settings] {
            settings.ioConfig = vloomdaw::clampedAudioIoConfig(settings.ioConfig);
            const vloomdaw::AudioIoBus& inBus
                = settings.ioConfig.inputs[static_cast<std::size_t>(settings.ioConfig.activeInputBus)];
            const vloomdaw::AudioIoBus& outBus
                = settings.ioConfig.outputs[static_cast<std::size_t>(settings.ioConfig.activeOutputBus)];
            audio.setActiveInputRouting(inBus.stereo, inBus.leftChannel, inBus.rightChannel);
            audio.setActiveOutputRouting(outBus.stereo, outBus.leftChannel, outBus.rightChannel);
        };

        const auto openPreferredAudio = [&audio, &settings, &applyIoToAudio, &lastInputError] {
            applyIoToAudio();
            audio.setFormat(settings.sampleRate, settings.bufferFrames);
            const auto devices = audio.outputDevices();
            int index = vloomdaw::findDevice(
                devices.names, {settings.outputDevice.toStdString(), settings.outputDeviceOccurrence});
            if (index < 0)
                index = devices.defaultIndex;
            audio.openDevice(index);
            audio.engine().setMetronomeEnabled(settings.metronomeOnStartup);

            lastInputError.clear();
            if (settings.inputEnabled) {
                const auto inDevs = audio.inputDevices();
                int inIdx = settings.inputDevice.isEmpty()
                    ? -1
                    : vloomdaw::findDevice(
                          inDevs.names, {settings.inputDevice.toStdString(), settings.inputDeviceOccurrence});
                if (!settings.inputDevice.isEmpty() && inIdx < 0)
                    inIdx = -1;
                std::string err;
                if (!audio.openInputDevice(inIdx, &err) && !err.empty())
                    lastInputError = QString::fromStdString(err);
            } else {
                audio.closeInputDevice();
            }

            if (!settings.midiInputDevice.isEmpty()) {
                const auto midiNames = vloomdaw::AudioController::midiInputDevices();
                const std::string wanted = settings.midiInputDevice.toStdString();
                int midiIdx = -1;
                for (std::size_t i = 0; i < midiNames.size(); ++i) {
                    if (midiNames[i] == wanted) {
                        midiIdx = static_cast<int>(i);
                        break;
                    }
                }
                if (midiIdx >= 0)
                    audio.openMidiInput(midiIdx);
                else
                    audio.closeMidiInput();
            } else {
                audio.closeMidiInput();
            }
        };
        openPreferredAudio();

        const auto selectStartupOutput = [&](int index) {
            const auto devs = audio.outputDevices();
            const vloomdaw::DeviceRef ref = vloomdaw::deviceRefAt(devs.names, index);
            settings.outputDevice = QString::fromStdString(ref.name);
            settings.outputDeviceOccurrence = ref.occurrence;
            if (settings.ioConfig.unifiedDevice) {
                const QString outQ = QString::fromStdString(ref.name);
                QString hwToken = outQ;
                if (const qsizetype lp = outQ.indexOf(QLatin1Char('(')),
                    rp = outQ.lastIndexOf(QLatin1Char(')'));
                    lp >= 0 && rp > lp + 1) {
                    hwToken = outQ.mid(lp + 1, rp - lp - 1).trimmed();
                }
                if (!hwToken.isEmpty()) {
                    const auto inDevs = audio.inputDevices();
                    for (std::size_t i = 0; i < inDevs.names.size(); ++i) {
                        const QString cand = QString::fromStdString(inDevs.names[i]);
                        if (cand.contains(hwToken, Qt::CaseInsensitive)) {
                            const vloomdaw::DeviceRef inRef
                                = vloomdaw::deviceRefAt(inDevs.names, static_cast<int>(i));
                            settings.inputEnabled = true;
                            settings.inputDevice = QString::fromStdString(inRef.name);
                            settings.inputDeviceOccurrence = inRef.occurrence;
                            break;
                        }
                    }
                }
            }
            settings.save();
            openPreferredAudio();
        };

        const auto selectStartupInput = [&](int code) {
            if (code == vloomdaw::AudioSettingsPage::kInputDisabled) {
                settings.inputEnabled = false;
            } else if (code == vloomdaw::AudioSettingsPage::kInputSystemDefault) {
                settings.inputEnabled = true;
                settings.inputDevice.clear();
                settings.inputDeviceOccurrence = 0;
            } else {
                const auto inDevs = audio.inputDevices();
                const vloomdaw::DeviceRef ref = vloomdaw::deviceRefAt(inDevs.names, code);
                settings.inputEnabled = true;
                settings.inputDevice = QString::fromStdString(ref.name);
                settings.inputDeviceOccurrence = ref.occurrence;
            }
            settings.save();
            openPreferredAudio();
        };

        const auto syncIoDialog = [&](vloomdaw::AudioIoDialog& ioDlg) {
            ioDlg.setConfig(settings.ioConfig);
            const auto devs = audio.outputDevices();
            QStringList outNames;
            for (const auto& n : devs.names)
                outNames << QString::fromStdString(n);
            int outIdx = vloomdaw::findDevice(
                devs.names, {settings.outputDevice.toStdString(), settings.outputDeviceOccurrence});
            if (outIdx < 0)
                outIdx = devs.defaultIndex;

            const auto inDevs = audio.inputDevices();
            QStringList inNames;
            for (const auto& n : inDevs.names)
                inNames << QString::fromStdString(n);
            int inCode = vloomdaw::AudioSettingsPage::kInputDisabled;
            if (settings.inputEnabled) {
                if (settings.inputDevice.isEmpty()) {
                    inCode = vloomdaw::AudioSettingsPage::kInputSystemDefault;
                } else {
                    const int found = vloomdaw::findDevice(
                        inDevs.names, {settings.inputDevice.toStdString(), settings.inputDeviceOccurrence});
                    inCode = found >= 0 ? found : vloomdaw::AudioSettingsPage::kInputSystemDefault;
                }
            }
            const QString outName
                = audio.isRunning() ? QString::fromStdString(audio.deviceName()) : QString();
            const QString inName
                = audio.isInputRunning() ? QString::fromStdString(audio.inputDeviceName()) : QString();
            ioDlg.setHardwareState(outName, audio.outputChannels(), outNames, outIdx, inName,
                audio.inputChannels(), inNames, inCode);
        };

        QPointer<vloomdaw::AudioSettingsPage> liveAudioPage;
        QPointer<vloomdaw::AudioIoDialog> liveIoDialog;
        QTimer liveMeterTimer;
        liveMeterTimer.setTimerType(Qt::PreciseTimer);
        liveMeterTimer.setInterval(16);
        QObject::connect(&liveMeterTimer, &QTimer::timeout, [&] {
            const bool inRun = audio.isInputRunning();
            const bool outRun = audio.isRunning();
            const float inL = inRun ? audio.takeInputPeak(0) : 0.0f;
            const float inR = inRun ? audio.takeInputPeak(1) : 0.0f;
            std::array<float, vloomdaw::AudioIoConfig::kMaxHardwareChannels> hwIn{};
            if (inRun) {
                for (int ch = 0; ch < vloomdaw::AudioIoConfig::kMaxHardwareChannels; ++ch)
                    hwIn[static_cast<std::size_t>(ch)] = audio.takeHardwareInputPeak(ch);
            }
            const float outL = outRun ? audio.engine().takeOutputPeak(0) : 0.0f;
            const float outR = outRun ? audio.engine().takeOutputPeak(1) : 0.0f;
            if (liveAudioPage != nullptr && liveAudioPage->isVisible()) {
                liveAudioPage->addInputPeaks(inL, inR);
                liveAudioPage->tickInputMeter(0.016);
            }
            if (liveIoDialog != nullptr && liveIoDialog->isVisible())
                liveIoDialog->setHardwarePeaks(hwIn, outL, outR, 0.016);
        });

        const auto runStartupSettingsDialog = [&] {
            vloomdaw::SettingsDialog dlg;
            liveAudioPage = dlg.audioPage();

            const auto syncDialogAudio = [&] {
                settings.ioConfig = vloomdaw::clampedAudioIoConfig(settings.ioConfig);
                const auto& inBus
                    = settings.ioConfig.inputs[static_cast<std::size_t>(settings.ioConfig.activeInputBus)];
                const auto& outBus
                    = settings.ioConfig.outputs[static_cast<std::size_t>(settings.ioConfig.activeOutputBus)];
                dlg.audioPage()->setUnifiedDeviceMode(settings.ioConfig.unifiedDevice);
                dlg.audioPage()->setIoRoutingSummary(QObject::tr("%1 (%2)  →  %3 (%4)")
                        .arg(QString::fromStdString(inBus.name),
                            inBus.stereo ? QObject::tr("Stereo") : QObject::tr("Mono"),
                            QString::fromStdString(outBus.name),
                            outBus.stereo ? QObject::tr("Stereo") : QObject::tr("Mono")));

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

                const auto inDevs = audio.inputDevices();
                QStringList inNames;
                for (const auto& name : inDevs.names)
                    inNames << QString::fromStdString(name);
                int inCode = vloomdaw::AudioSettingsPage::kInputDisabled;
                if (settings.inputEnabled) {
                    if (settings.inputDevice.isEmpty()) {
                        inCode = vloomdaw::AudioSettingsPage::kInputSystemDefault;
                    } else {
                        const int found = vloomdaw::findDevice(inDevs.names,
                            {settings.inputDevice.toStdString(), settings.inputDeviceOccurrence});
                        inCode = found >= 0 ? found : vloomdaw::AudioSettingsPage::kInputSystemDefault;
                    }
                }
                dlg.audioPage()->setInputDevices(inNames, inCode);
                if (audio.isInputRunning()) {
                    dlg.audioPage()->setInputStatus(QObject::tr("Capturing %1 (%2 ch @ %3 Hz)")
                                                        .arg(QString::fromStdString(audio.inputDeviceName()))
                                                        .arg(audio.inputChannels())
                                                        .arg(audio.inputSampleRate()),
                        true);
                } else if (settings.inputEnabled) {
                    dlg.audioPage()->setInputStatus(lastInputError.isEmpty()
                            ? QObject::tr("Input unavailable")
                            : QObject::tr("Input unavailable: %1").arg(lastInputError),
                        false);
                } else {
                    dlg.audioPage()->setInputStatus(QObject::tr("Input disabled"), false);
                }

                const auto midiDevs = vloomdaw::AudioController::midiInputDevices();
                QStringList midiNames;
                for (const auto& name : midiDevs)
                    midiNames << QString::fromStdString(name);
                dlg.audioPage()->setMidiDevices(midiNames, settings.midiInputDevice);
                if (audio.isMidiRunning()) {
                    dlg.audioPage()->setMidiStatus(
                        QObject::tr("Connected: %1").arg(QString::fromStdString(audio.midiDeviceName())),
                        true);
                } else {
                    dlg.audioPage()->setMidiStatus(settings.midiInputDevice.isEmpty()
                            ? QObject::tr("No MIDI controller selected")
                            : QObject::tr("Controller disconnected"),
                        false);
                }
            };
            syncDialogAudio();

            QObject::connect(
                dlg.audioPage(), &vloomdaw::AudioSettingsPage::deviceSelected, &dlg, [&](int index) {
                    selectStartupOutput(index);
                    syncDialogAudio();
                });
            QObject::connect(
                dlg.audioPage(), &vloomdaw::AudioSettingsPage::inputDeviceSelected, &dlg, [&](int code) {
                    selectStartupInput(code);
                    syncDialogAudio();
                });
            QObject::connect(dlg.audioPage(), &vloomdaw::AudioSettingsPage::unifiedDeviceModeChanged, &dlg,
                [&](bool unified) {
                    settings.ioConfig.unifiedDevice = unified;
                    settings.save();
                    if (unified) {
                        const auto devs = audio.outputDevices();
                        int curOut = vloomdaw::findDevice(devs.names,
                            {settings.outputDevice.toStdString(), settings.outputDeviceOccurrence});
                        if (curOut < 0)
                            curOut = devs.defaultIndex;
                        selectStartupOutput(curOut);
                    }
                    syncDialogAudio();
                });
            QObject::connect(dlg.audioPage(), &vloomdaw::AudioSettingsPage::audioIoSetupRequested, &dlg, [&] {
                vloomdaw::AudioIoDialog ioDlg(&dlg);
                liveIoDialog = &ioDlg;
                syncIoDialog(ioDlg);
                QObject::connect(&ioDlg, &vloomdaw::AudioIoDialog::configChanged, &ioDlg,
                    [&](const vloomdaw::AudioIoConfig& cfg) {
                        settings.ioConfig = vloomdaw::clampedAudioIoConfig(cfg);
                        settings.save();
                        applyIoToAudio();
                        syncDialogAudio();
                        syncIoDialog(ioDlg);
                    });
                QObject::connect(
                    &ioDlg, &vloomdaw::AudioIoDialog::outputDeviceSelected, &ioDlg, [&](int index) {
                        selectStartupOutput(index);
                        syncDialogAudio();
                        syncIoDialog(ioDlg);
                    });
                QObject::connect(
                    &ioDlg, &vloomdaw::AudioIoDialog::inputDeviceSelected, &ioDlg, [&](int code) {
                        selectStartupInput(code);
                        syncDialogAudio();
                        syncIoDialog(ioDlg);
                    });
                QObject::connect(
                    &ioDlg, &vloomdaw::AudioIoDialog::audioSettingsRequested, &ioDlg, &QDialog::accept);
                ioDlg.showTab(vloomdaw::AudioIoDialog::Tab::Inputs);
                ioDlg.exec();
                liveIoDialog = nullptr;
            });
            QObject::connect(dlg.audioPage(), &vloomdaw::AudioSettingsPage::midiDeviceSelected, &dlg,
                [&](const QString& portName) {
                    settings.midiInputDevice = portName;
                    settings.save();
                    openPreferredAudio();
                    syncDialogAudio();
                });
            QObject::connect(dlg.audioPage(), &vloomdaw::AudioSettingsPage::refreshMidiRequested, &dlg, [&] {
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
            dlg.generalPage()->setAssociateProjectFiles(settings.associateProjectFiles);
            dlg.generalPage()->setAnimatePluginDisplays(settings.animatePluginDisplays);
            dlg.generalPage()->setCheckForUpdates(settings.checkForUpdates);
            dlg.generalPage()->setBetaUpdates(settings.betaUpdates);
            QObject::connect(dlg.generalPage(), &vloomdaw::GeneralSettingsPage::startPositionModeChanged,
                &dlg, [&](int mode) {
                    settings.startPositionMode = mode == 1 ? vloomdaw::AppSettings::PositionMode::Time
                                                           : vloomdaw::AppSettings::PositionMode::BarsBeats;
                    settings.save();
                });
            QObject::connect(dlg.generalPage(), &vloomdaw::GeneralSettingsPage::metronomeOnStartupChanged,
                &dlg, [&](bool on) {
                    settings.metronomeOnStartup = on;
                    audio.engine().setMetronomeEnabled(on);
                    settings.save();
                });
            QObject::connect(dlg.generalPage(), &vloomdaw::GeneralSettingsPage::associateProjectFilesChanged,
                &dlg, [&](bool on) {
                    settings.associateProjectFiles = on;
                    settings.save();
#if defined(Q_OS_WIN)
                    vloomdaw::setWindowsProjectFileAssociation(QCoreApplication::applicationFilePath(), on);
#endif
                });
            QObject::connect(dlg.generalPage(), &vloomdaw::GeneralSettingsPage::animatePluginDisplaysChanged,
                &dlg, [&](bool on) {
                    settings.animatePluginDisplays = on;
                    settings.save();
                });
            QObject::connect(dlg.generalPage(), &vloomdaw::GeneralSettingsPage::checkForUpdatesChanged, &dlg,
                [&](bool on) {
                    settings.checkForUpdates = on;
                    settings.save();
                });
            QObject::connect(
                dlg.generalPage(), &vloomdaw::GeneralSettingsPage::betaUpdatesChanged, &dlg, [&](bool on) {
                    settings.betaUpdates = on;
                    settings.save();
                });

            const auto syncStemPage = [&] {
                QStringList installed;
                const std::filesystem::path folder = vloomdaw::toPath(vloomdaw::StemSeparator::modelFolder());
                for (const vloomdaw::StemModel& m : vloomdaw::stemModels()) {
                    if (vloomdaw::stemModelInstalled(m, folder))
                        installed << QString::fromLatin1(m.id.data(), static_cast<qsizetype>(m.id.size()));
                }
                dlg.stemPage()->setModels(installed, settings.stemModel);
                dlg.stemPage()->setUseGpu(settings.stemUseGpu);
                dlg.stemPage()->setOverlap(settings.stemOverlap);
                dlg.stemPage()->setMuteOriginal(settings.stemMuteOriginal);
            };
            syncStemPage();
            QObject::connect(
                dlg.stemPage(), &vloomdaw::StemSettingsPage::modelChosen, &dlg, [&](const QString& id) {
                    settings.stemModel = id;
                    settings.save();
                });
            QObject::connect(dlg.stemPage(), &vloomdaw::StemSettingsPage::useGpuChanged, &dlg, [&](bool on) {
                settings.stemUseGpu = on;
                settings.save();
            });
            QObject::connect(
                dlg.stemPage(), &vloomdaw::StemSettingsPage::overlapChanged, &dlg, [&](int percent) {
                    settings.stemOverlap = percent;
                    settings.save();
                });
            QObject::connect(
                dlg.stemPage(), &vloomdaw::StemSettingsPage::muteOriginalChanged, &dlg, [&](bool on) {
                    settings.stemMuteOriginal = on;
                    settings.save();
                });
            QObject::connect(dlg.stemPage(), &vloomdaw::StemSettingsPage::storeRequested, &dlg, [&] {
                vloomdaw::StemModelStore store(settings.stemModel, settings.stemUseGpu, &dlg);
                QObject::connect(
                    &store, &vloomdaw::StemModelStore::chosenChanged, &store, [&](const QString& id) {
                        settings.stemModel = id;
                        settings.save();
                    });
                QObject::connect(&store, &vloomdaw::StemModelStore::useGpuChanged, &store, [&](bool on) {
                    settings.stemUseGpu = on;
                    settings.save();
                });
                store.exec();
                syncStemPage();
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

            liveMeterTimer.start();
            dlg.showPage(vloomdaw::SettingsDialog::Page::Audio);
            dlg.exec();
            liveMeterTimer.stop();
            liveAudioPage = nullptr;
        };

        for (;;) {
            vloomdaw::LauncherDialog::AudioStatus status;
            if (audio.isRunning()) {
                status.device = QString::fromStdString(audio.deviceName());
                status.sampleRate = audio.sampleRate();
                status.bufferFrames = audio.bufferFrames();
            }
            if (audio.isInputRunning()) {
                status.inputDevice = QString::fromStdString(audio.inputDeviceName());
                status.inputChannels = audio.inputChannels();
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
                const QString picked
                    = QFileDialog::getOpenFileName(nullptr, QObject::tr("Open Project"), folder,
                        QObject::tr("VloomDAW & MIDI files (*.vloom *.mid *.midi);;VloomDAW project "
                                    "(*.vloom);;MIDI files (*.mid *.midi)"));
                if (vloomdaw::isLocalPath(picked)) {
                    QSettings().setValue(kFolderKey, QFileInfo(picked).absolutePath());
                    startupFile = picked;
                    chosen = true;
                }
                break;
            }
            case vloomdaw::LauncherDialog::Choice::AudioSettings:
                runStartupSettingsDialog();
                break;
            case vloomdaw::LauncherDialog::Choice::AudioIoSetup: {
                bool openSettingsAfter = false;
                {
                    vloomdaw::AudioIoDialog ioDlg;
                    liveIoDialog = &ioDlg;
                    syncIoDialog(ioDlg);
                    QObject::connect(&ioDlg, &vloomdaw::AudioIoDialog::configChanged, &ioDlg,
                        [&](const vloomdaw::AudioIoConfig& cfg) {
                            settings.ioConfig = vloomdaw::clampedAudioIoConfig(cfg);
                            settings.save();
                            applyIoToAudio();
                            syncIoDialog(ioDlg);
                        });
                    QObject::connect(
                        &ioDlg, &vloomdaw::AudioIoDialog::outputDeviceSelected, &ioDlg, [&](int index) {
                            selectStartupOutput(index);
                            syncIoDialog(ioDlg);
                        });
                    QObject::connect(
                        &ioDlg, &vloomdaw::AudioIoDialog::inputDeviceSelected, &ioDlg, [&](int code) {
                            selectStartupInput(code);
                            syncIoDialog(ioDlg);
                        });
                    QObject::connect(&ioDlg, &vloomdaw::AudioIoDialog::audioSettingsRequested, &ioDlg, [&] {
                        openSettingsAfter = true;
                        ioDlg.accept();
                    });
                    liveMeterTimer.start();
                    ioDlg.showTab(vloomdaw::AudioIoDialog::Tab::Inputs);
                    ioDlg.exec();
                    liveMeterTimer.stop();
                    liveIoDialog = nullptr;
                }
                if (openSettingsAfter)
                    runStartupSettingsDialog();
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
