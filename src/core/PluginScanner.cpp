// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/PluginScanner.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <set>
#include <system_error>

namespace fs = std::filesystem;

namespace vloomdaw {

namespace {

/// Extension in lower case, e.g. ".vst3".
std::string lowerExtension(const fs::path& path)
{
    // UTF-8, not path::string(): on Windows that throws for names the ANSI
    // code page cannot hold. Only ASCII is lower-cased; ".vst3" is ASCII.
    const std::u8string wide = path.extension().u8string();
    std::string ext(wide.begin(), wide.end());
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

/// A path as UTF-8 (path::string() uses the ANSI code page on Windows).
std::string utf8(const fs::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

/// The binary inside a bundle (what changes when the plugin is updated), or
/// the bundle itself when its layout is unknown.
fs::path bundleBinary(const fs::path& bundle)
{
#if defined(_WIN32)
    fs::path binary = bundle / "Contents" / "x86_64-win" / bundle.filename();
#elif defined(__APPLE__)
    fs::path binary = bundle / "Contents" / "MacOS" / bundle.stem();
#else
    fs::path binary = bundle / "Contents" / "x86_64-linux" / bundle.filename();
    binary.replace_extension(".so");
#endif
    std::error_code ec;
    return fs::is_regular_file(binary, ec) ? binary : bundle;
}

const char* extensionOf(PluginFormat format) { return format == PluginFormat::Vst3 ? ".vst3" : ".clap"; }

fs::path environmentPath(const char* name)
{
#if defined(_WIN32)
    // Wide: a user name outside the ANSI code page must survive.
    const std::wstring wideName(name, name + std::strlen(name));
    wchar_t* value = nullptr;
    std::size_t size = 0;
    if (_wdupenv_s(&value, &size, wideName.c_str()) != 0 || value == nullptr)
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
                if (seen.insert(statError ? entry.path() : canonical).second) {
                    PluginInfo info;
                    info.name = utf8(entry.path().stem());
                    info.format = folder.format;
                    info.path = entry.path();
                    // Installers group plugins in a vendor folder: VST3/FabFilter/Pro-Q 4.vst3.
                    const fs::path relative = entry.path().lexically_relative(folder.path);
                    if (std::distance(relative.begin(), relative.end()) > 1)
                        info.vendor = utf8(*relative.begin());
                    // Cache key: the file, or for a bundle the binary inside it
                    // (updates replace that without touching the folder's time).
                    const fs::path binary = isDir ? bundleBinary(entry.path()) : entry.path();
                    if (fs::is_regular_file(binary, statError))
                        info.fileSize = fs::file_size(binary, statError);
                    info.modified = static_cast<std::int64_t>(
                        fs::last_write_time(binary, statError).time_since_epoch().count());
                    found.push_back(std::move(info));
                }
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
