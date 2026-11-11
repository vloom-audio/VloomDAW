// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/AudioController.hpp"
#include "ui/MainWindow.hpp"
#include "ui/Theme.hpp"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("VloomDAW"));
    QApplication::setApplicationVersion(QStringLiteral(VLOOMDAW_VERSION));
    QApplication::setOrganizationName(QStringLiteral("Vloom Audio"));
    QApplication::setOrganizationDomain(QStringLiteral("vloom-audio.com"));
    vloomdaw::Theme::apply(app);

    // The controller outlives the window, so audio stops after the UI is gone.
    vloomdaw::AudioController audio;
    vloomdaw::MainWindow window(audio);
    window.show();

    return QApplication::exec();
}
