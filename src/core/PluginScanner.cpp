// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/PluginScanner.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <system_error>

namespace fs = std::filesystem;

namespace vloomdaw {

namespace {

/// Extension in lower case, e.g. ".vst3".
std::string lowerExtension(const fs::path& path)
{
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

const char* extensionOf(PluginFormat format) { return format == PluginFormat::Vst3 ? ".vst3" : ".clap"; }

fs::path environmentPath(const char* name)
{
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr)
        return {};
    fs::path path(value);
    std::free(value);
    return path;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? fs::path(value) : fs::path();
#endif
}

} // namespace

std::string_view formatName(PluginFormat format) { return format == PluginFormat::Vst3 ? "VST3" : "CLAP"; }

std::vector<PluginFolder> defaultPluginFolders()
{
    std::vector<PluginFolder> folders;
#if defined(_WIN32)
    const fs::path common = environmentPath("CommonProgramFiles");
    if (!common.empty()) {
        folders.push_back({PluginFormat::Vst3, common / "VST3"});
        folders.push_back({PluginFormat::Clap, common / "CLAP"});
    }
    const fs::path local = environmentPath("LOCALAPPDATA");
    if (!local.empty()) {
        folders.push_back({PluginFormat::Vst3, local / "Programs" / "Common" / "VST3"});
        folders.push_back({PluginFormat::Clap, local / "Programs" / "Common" / "CLAP"});
    }
#elif defined(__APPLE__)
    folders.push_back({PluginFormat::Vst3, "/Library/Audio/Plug-Ins/VST3"});
    folders.push_back({PluginFormat::Clap, "/Library/Audio/Plug-Ins/CLAP"});
    if (const fs::path home = environmentPath("HOME"); !home.empty()) {
        folders.push_back({PluginFormat::Vst3, home / "Library/Audio/Plug-Ins/VST3"});
        folders.push_back({PluginFormat::Clap, home / "Library/Audio/Plug-Ins/CLAP"});
    }
#else
    folders.push_back({PluginFormat::Vst3, "/usr/lib/vst3"});
    folders.push_back({PluginFormat::Vst3, "/usr/local/lib/vst3"});
    folders.push_back({PluginFormat::Clap, "/usr/lib/clap"});
    folders.push_back({PluginFormat::Clap, "/usr/local/lib/clap"});
    if (const fs::path home = environmentPath("HOME"); !home.empty()) {
        folders.push_back({PluginFormat::Vst3, home / ".vst3"});
        folders.push_back({PluginFormat::Clap, home / ".clap"});
    }
#endif
    return folders;
}

std::vector<PluginInfo> scanPluginFolders(const std::vector<PluginFolder>& folders, int maxDepth)
{
    std::vector<PluginInfo> found;
    std::set<fs::path> seen;

    for (const PluginFolder& folder : folders) {
        std::error_code ec;
        if (!fs::is_directory(folder.path, ec))
            continue;
        const std::string wanted = extensionOf(folder.format);
        fs::recursive_directory_iterator it(folder.path, fs::directory_options::skip_permission_denied, ec);
        for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            const fs::directory_entry& entry = *it;
            std::error_code statError;
            const bool isLink = entry.is_symlink(statError);
            const bool isDir = !isLink && entry.is_directory(statError);
            if (lowerExtension(entry.path()) == wanted && (isDir || entry.is_regular_file(statError))) {
                const fs::path canonical = fs::weakly_canonical(entry.path(), statError);
                if (seen.insert(statError ? entry.path() : canonical).second)
                    found.push_back({entry.path().stem().string(), folder.format, entry.path()});
                if (isDir)
                    it.disable_recursion_pending(); // a bundle: its insides are not plugins
                continue;
            }
            // Do not follow links (loops) or go deeper than maxDepth.
            if (isLink || it.depth() >= maxDepth)
                it.disable_recursion_pending();
        }
    }

    std::sort(found.begin(), found.end(), [](const PluginInfo& a, const PluginInfo& b) {
        if (a.name != b.name)
            return a.name < b.name;
        return a.format < b.format;
    });
    return found;
}

} // namespace vloomdaw
