// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/AudioController.hpp"
#include "ui/AppSettings.hpp"
#include "ui/MainWindow.hpp"
#include "ui/Theme.hpp"

#include <QApplication>

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
    // The saved theme, before any window exists.
    const vloomdaw::AppSettings settings = vloomdaw::AppSettings::load();
    vloomdaw::Theme::select(settings.theme);
    vloomdaw::Theme::apply(app);

    // The controller outlives the window, so audio stops after the UI is gone.
    vloomdaw::AudioController audio;
    vloomdaw::MainWindow window(audio);
    window.show();

    return QApplication::exec();
}
