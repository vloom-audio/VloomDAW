// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/PluginScanner.hpp"

#include <QDateTime>

#include <vector>

namespace vloomdaw {

/// What the user's last plugin scan found, and where to scan: kept with
/// QSettings so plugins are known at start-up without scanning again.
/// Nothing scans automatically; the Plugin Manager writes this.
struct PluginStore {
    std::vector<PluginInfo> plugins; ///< from the last scan
    QDateTime lastScan; ///< invalid: never scanned
    std::vector<PluginFolder> extraFolders; ///< added by the user, besides the defaults
    /// Load each plugin (in vloomdaw-plugin-probe) while scanning to read its
    /// vendor and category. Off: file names only, no plugin code runs.
    bool probePlugins = true;

    [[nodiscard]] static PluginStore load();
    void save() const;

    /// The default folders of this OS followed by the user's.
    [[nodiscard]] std::vector<PluginFolder> scanFolders() const;
};

} // namespace vloomdaw
