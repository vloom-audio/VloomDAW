// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/PluginScanner.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace vloomdaw {

/// A hosted plugin in the project: an effect on a mixer insert, or a
/// channel's instrument. The model only names it; the loaded instance lives
/// in the PluginHost (src/audio), found by `instance`.
struct PluginSlot {
    PluginFormat format = PluginFormat::Clap;
    std::string path; ///< UTF-8 path of the .clap / .vst3 file or bundle
    std::string id; ///< the plugin's id in that library (the engine's PluginInfo::id)
    std::string name; ///< shown to the user
    bool bypassed = false; ///< passes audio through, unprocessed
    /// PluginHost handle of the loaded instance; 0: not loaded. Valid for
    /// this run only.
    std::uint64_t instance = 0;
    /// The settings a project file held for it, kept while it is not loaded
    /// (not in the user's scan, or it failed to load), so saving the project
    /// again does not lose them. Empty once it is loaded.
    std::vector<std::uint8_t> savedState{};

    friend bool operator==(const PluginSlot&, const PluginSlot&) = default;
};

} // namespace vloomdaw
