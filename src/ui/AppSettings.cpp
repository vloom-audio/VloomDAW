// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/AppSettings.hpp"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace vloomdaw {

namespace {
constexpr QLatin1StringView kOutputDevice{"audio/outputDevice"};
constexpr QLatin1StringView kOutputDeviceOccurrence{"audio/outputDeviceOccurrence"};
constexpr QLatin1StringView kSampleRate{"audio/sampleRate"};
constexpr QLatin1StringView kBufferFrames{"audio/bufferFrames"};
constexpr QLatin1StringView kStartPositionMode{"general/startPositionMode"};
constexpr QLatin1StringView kMetronomeOnStartup{"general/metronomeOnStartup"};
constexpr QLatin1StringView kBrowserFolders{"browser/folders"};
constexpr QLatin1StringView kBrowserFolder{"browser/current"};
} // namespace

AppSettings AppSettings::load()
{
    const QSettings s;
    AppSettings a;
    a.outputDevice = s.value(kOutputDevice).toString();
    a.outputDeviceOccurrence = std::max(1, s.value(kOutputDeviceOccurrence, 1).toInt());
    a.sampleRate = validSampleRate(s.value(kSampleRate, a.sampleRate).toInt());
    a.bufferFrames = validBufferFrames(s.value(kBufferFrames, a.bufferFrames).toInt());
    a.startPositionMode
        = s.value(kStartPositionMode, 0).toInt() == 1 ? PositionMode::Time : PositionMode::BarsBeats;
    a.metronomeOnStartup = s.value(kMetronomeOnStartup, a.metronomeOnStartup).toBool();

    if (s.contains(kBrowserFolders)) {
        a.browserFolders = s.value(kBrowserFolders).toStringList();
    } else {
        // First run: start with the user's Music folder if there is one.
        const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (!music.isEmpty() && QDir(music).exists())
            a.browserFolders << music;
    }
    a.browserFolder = s.value(kBrowserFolder, a.browserFolders.value(0)).toString();
    return a;
}

void AppSettings::save() const
{
    QSettings s;
    s.setValue(kOutputDevice, outputDevice);
    s.setValue(kOutputDeviceOccurrence, outputDeviceOccurrence);
    s.setValue(kSampleRate, sampleRate);
    s.setValue(kBufferFrames, bufferFrames);
    s.setValue(kStartPositionMode, static_cast<int>(startPositionMode));
    s.setValue(kMetronomeOnStartup, metronomeOnStartup);
    s.setValue(kBrowserFolders, browserFolders);
    s.setValue(kBrowserFolder, browserFolder);
}

} // namespace vloomdaw
