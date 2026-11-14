// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/AudioController.hpp"
#include "ui/AppSettings.hpp"
#include "ui/MainWindow.hpp"
#include "ui/Theme.hpp"
#include "ui/launcher/LauncherDialog.hpp"

#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QTimer>

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
    // The saved theme, before any window exists.
    const vloomdaw::AppSettings settings = vloomdaw::AppSettings::load();
    vloomdaw::Theme::select(settings.theme);
    vloomdaw::Theme::apply(app);

    // The controller outlives the window, so audio stops after the UI is gone.
    vloomdaw::AudioController audio;
    vloomdaw::MainWindow window(audio);
    window.show();
    // "VloomDAW song.vloom" (and opening a .vloom with the app) opens it.
    const QStringList args = QApplication::arguments();
    if (args.size() > 1 && args.at(1).endsWith(QStringLiteral(".vloom"), Qt::CaseInsensitive)) {
        const QString path = QFileInfo(args.at(1)).absoluteFilePath();
        QTimer::singleShot(0, &window, [&window, path] { window.openProjectFile(path); });
    } else if (vloomdaw::LauncherDialog::showAtStartup()) {
        QTimer::singleShot(0, &window, [&window] { window.showLauncher(); });
    }

    return QApplication::exec();
}
