// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vloomdaw {

enum class PluginFormat { Vst3, Clap };

[[nodiscard]] std::string_view formatName(PluginFormat format); ///< "VST3", "CLAP"

/// A plugin. scanPluginFolders() fills in what the file system tells
/// (name from the file, vendor from the install folder); a probe of the
/// module (vloomdaw-plugin-probe) then replaces them with what the plugin's
/// own factory says, and adds category and version.
struct PluginInfo {
    enum class Probe { NotProbed, Ok, Failed };

    std::string name; ///< file name without its extension until probed, e.g. "Vital"
    PluginFormat format = PluginFormat::Vst3;
    std::filesystem::path path; ///< the .vst3 / .clap file or bundle folder
    std::string vendor; ///< factory vendor, else the install folder ("FabFilter/..."), else empty
    std::string category = "Uncategorized"; ///< see pluginCategories()
    std::string version;
    std::string tags; ///< raw VST3 sub-categories / CLAP features
    std::uintmax_t fileSize = 0; ///< with `modified`: skip unchanged files when scanning again
    std::int64_t modified = 0; ///< last write time, file-clock ticks
    Probe probe = Probe::NotProbed;
    std::string probeError; ///< why the probe failed
    /// Set by the user (empty: none). They win over what the plugin says and
    /// survive scans; see effectiveCategory() and carryUserCategories().
    std::string userCategory;
    std::string userInstrumentType; ///< only with userCategory == "Instrument"
};

struct PluginFolder {
    PluginFormat format;
    std::filesystem::path path;
};

/// The standard install folders of this OS (Windows: Common Files\VST3 and
/// \CLAP; macOS: /Library/Audio/Plug-Ins and the user's; Linux: /usr/lib,
/// /usr/local/lib and ~/.vst3, ~/.clap). Folders that do not exist are kept;
/// scanning skips them.
[[nodiscard]] std::vector<PluginFolder> defaultPluginFolders();

/// Lists the plugins in `folders` and their sub-folders, without loading any
/// of them (no plugin code runs). A ".vst3" / ".clap" entry is a plugin
/// whether it is a file or a bundle folder; bundles are not searched inside.
/// Unreadable folders are skipped, symbolic links are not followed, and the
/// search stops `maxDepth` levels down. Sorted by name, then format; a path
/// reachable from two folders is listed once.
[[nodiscard]] std::vector<PluginInfo> scanPluginFolders(
    const std::vector<PluginFolder>& folders, int maxDepth = 8);

} // namespace vloomdaw
