// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/AudioFormat.hpp"
#include "core/ThemePresets.hpp"

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
    bool browserAutoPreview = true; ///< play files as the arrow keys reach them

    // Stem extractor
    QString stemModel; ///< id of the model in use (core/StemModels); empty: none chosen yet
    bool stemUseGpu = true; ///< run models that can on the graphics card
    int stemOverlap = 25; ///< ONNX models: percent of each piece shared with the next (25, 50 or 75)
    bool stemMuteOriginal = true; ///< mute a clip once its stems are placed under it
    bool stemFasterOffered = false; ///< asked once whether to leave a slow model for a faster one

    // General
    PositionMode startPositionMode = PositionMode::BarsBeats;
    bool metronomeOnStartup = false;
    bool associateProjectFiles = true; ///< Windows: register `.vloom` icon & double-click open

    // Updates (ui/update)
    bool checkForUpdates = true; ///< look for a new version once a day
    bool betaUpdates = false; ///< also offer pre-releases (0.3.0-rc.1)
    QString skippedUpdate; ///< tag the user chose to skip ("v0.3.0"); automatic checks pass it over
    qint64 lastUpdateCheck = 0; ///< seconds since the epoch, UTC

    // Theme: preset ids, tuning knobs and per-role colours (core/ThemePresets)
    ThemeChoice theme;

    /// Reads the saved values; anything missing or invalid gets its default.
    [[nodiscard]] static AppSettings load();
    void save() const;
};

} // namespace vloomdaw
