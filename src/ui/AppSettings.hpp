// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/AudioFormat.hpp"

#include <QString>
#include <QStringList>

namespace vloomdaw {

/// User preferences that survive a restart, stored with QSettings.
/// Window layout is saved separately by MainWindow.
struct AppSettings {
    enum class PositionMode { BarsBeats = 0, Time = 1 };

    // Audio
    QString outputDevice; ///< device name; empty = system default
    int outputDeviceOccurrence = 1; ///< which device of that name (see DeviceRef)
    int sampleRate = kDefaultSampleRate;
    int bufferFrames = kDefaultBufferFrames;

    // Browser
    QStringList browserFolders; ///< the user's sample folders
    QString browserFolder; ///< the one shown

    // General
    PositionMode startPositionMode = PositionMode::BarsBeats;
    bool metronomeOnStartup = true;

    /// Reads the saved values; anything missing or invalid gets its default.
    [[nodiscard]] static AppSettings load();
    void save() const;
};

} // namespace vloomdaw
