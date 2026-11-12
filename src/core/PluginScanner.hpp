// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vloomdaw {

enum class PluginFormat { Vst3, Clap };

[[nodiscard]] std::string_view formatName(PluginFormat format); ///< "VST3", "CLAP"

/// A plugin found on disk. Nothing is loaded: the name comes from the file.
struct PluginInfo {
    std::string name; ///< file name without its extension, e.g. "Vital"
    PluginFormat format = PluginFormat::Vst3;
    std::filesystem::path path; ///< the .vst3 / .clap file or bundle folder
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
