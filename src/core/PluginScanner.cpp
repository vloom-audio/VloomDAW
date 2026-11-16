// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/PluginScanner.hpp"

#include <vloom/engine/plugin/Vst2Host.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
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

const char* extensionOf(PluginFormat format)
{
    switch (format) {
    case PluginFormat::Vst3:
        return ".vst3";
    case PluginFormat::Clap:
        return ".clap";
    case PluginFormat::Vst2:
#if defined(_WIN32)
        return ".dll";
#elif defined(__APPLE__)
        return ".vst";
#else
        return ".so";
#endif
    case PluginFormat::Builtin:
        break;
    }
    return "";
}

#if defined(_WIN32)
/// True unless `file` is a Windows PE image that lacks a VST 2 entry point
/// (`VSTPluginMain` or `main`). Helper DLLs shipped alongside plugins in a
/// VSTPlugins folder are skipped without executing any code.
bool isVst2CandidateDll(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    const auto readAt = [&in](std::uint64_t offset, unsigned char* dst, std::size_t n) {
        in.clear();
        in.seekg(static_cast<std::streamoff>(offset));
        in.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(n));
        return in.gcount() == static_cast<std::streamsize>(n);
    };
    const auto le16 = [](const unsigned char* p) -> std::uint16_t {
        return static_cast<std::uint16_t>(p[0] | (std::uint16_t{p[1]} << 8));
    };
    const auto le32 = [](const unsigned char* p) -> std::uint32_t {
        return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16)
            | (std::uint32_t{p[3]} << 24);
    };

    std::array<unsigned char, 64> dos{};
    if (!readAt(0, dos.data(), dos.size()))
        return true; // non-PE stub (e.g. unit-test placeholder)
    if (dos[0] != 'M' || dos[1] != 'Z')
        return true;

    const std::uint32_t peOffset = le32(dos.data() + 60);
    std::array<unsigned char, 24> coff{};
    if (!readAt(peOffset, coff.data(), coff.size()) || coff[0] != 'P' || coff[1] != 'E' || coff[2] != 0
        || coff[3] != 0) {
        return false;
    }
    const std::uint16_t sectionCount = le16(coff.data() + 6);
    const std::uint16_t optionalSize = le16(coff.data() + 20);
    if (sectionCount == 0 || sectionCount > 128 || optionalSize < 96)
        return false;

    const std::uint64_t optional = peOffset + 24;
    std::array<unsigned char, 2> magicBuf{};
    if (!readAt(optional, magicBuf.data(), magicBuf.size()))
        return false;
    const std::uint16_t optMagic = le16(magicBuf.data());
    const std::uint64_t dirOffset = optMagic == 0x20B ? 112 : (optMagic == 0x10B ? 96 : 0);
    if (dirOffset == 0 || optionalSize < dirOffset + 8)
        return false;

    std::array<unsigned char, 8> exportDir{};
    if (!readAt(optional + dirOffset, exportDir.data(), exportDir.size()))
        return false;
    const std::uint32_t exportRva = le32(exportDir.data());
    if (exportRva == 0)
        return false;

    struct Sec {
        std::uint32_t va, rawPos, rawSize;
    };
    std::vector<Sec> secs;
    secs.reserve(sectionCount);
    for (std::uint16_t i = 0; i < sectionCount; ++i) {
        std::array<unsigned char, 40> sh{};
        if (!readAt(optional + optionalSize + std::uint64_t{i} * 40, sh.data(), sh.size()))
            return false;
        secs.push_back({le32(sh.data() + 12), le32(sh.data() + 20), le32(sh.data() + 16)});
    }
    const auto rvaToFile = [&secs](std::uint32_t rva, std::uint64_t& out) {
        for (const Sec& s : secs) {
            if (rva >= s.va && rva - s.va < s.rawSize) {
                out = std::uint64_t{s.rawPos} + (rva - s.va);
                return true;
            }
        }
        return false;
    };

    std::uint64_t expPos = 0;
    std::array<unsigned char, 40> expHdr{};
    if (!rvaToFile(exportRva, expPos) || !readAt(expPos, expHdr.data(), expHdr.size()))
        return false;
    const std::uint32_t nameCount = std::min<std::uint32_t>(le32(expHdr.data() + 24), 65536);
    std::uint64_t namesPos = 0;
    if (nameCount == 0 || !rvaToFile(le32(expHdr.data() + 32), namesPos))
        return false;

    for (std::uint32_t i = 0; i < nameCount; ++i) {
        std::array<unsigned char, 4> rvaBuf{};
        std::uint64_t strPos = 0;
        if (!readAt(namesPos + std::uint64_t{i} * 4, rvaBuf.data(), rvaBuf.size())
            || !rvaToFile(le32(rvaBuf.data()), strPos)) {
            return false;
        }
        std::array<char, 32> name{};
        in.clear();
        in.seekg(static_cast<std::streamoff>(strPos));
        in.read(name.data(), static_cast<std::streamsize>(name.size() - 1));
        const std::string_view sv(name.data());
        if (sv == "VSTPluginMain" || sv == "main")
            return true;
    }
    return false;
}
#endif

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

std::string_view formatName(PluginFormat format)
{
    switch (format) {
    case PluginFormat::Vst3:
        return "VST3";
    case PluginFormat::Vst2:
        return "VST2";
    case PluginFormat::Builtin:
        return "Built-in";
    case PluginFormat::Clap:
        break;
    }
    return "CLAP";
}

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
    std::set<std::string> seenVst2;
    for (const fs::path& vst2Dir : vloom::vst2SearchPaths()) {
        if (vst2Dir.empty())
            continue;
        std::string key = utf8(vst2Dir.lexically_normal());
#if defined(_WIN32)
        std::transform(key.begin(), key.end(), key.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
        if (seenVst2.insert(key).second)
            folders.push_back({PluginFormat::Vst2, vst2Dir});
    }
    return folders;
}

std::vector<PluginInfo> scanPluginFolders(const std::vector<PluginFolder>& folders, int maxDepth)
{
    std::vector<PluginInfo> found;
    std::set<std::pair<PluginFormat, fs::path>> seen;

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
            const bool rightShape = folder.format == PluginFormat::Vst2
                ? (
#if defined(__APPLE__)
                      isDir
#else
                      !isDir && entry.is_regular_file(statError)
#endif
                      )
                : (isDir || entry.is_regular_file(statError));
            if (!wanted.empty() && lowerExtension(entry.path()) == wanted && rightShape) {
#if defined(_WIN32)
                if (folder.format == PluginFormat::Vst2 && !isVst2CandidateDll(entry.path()))
                    continue;
#endif
                const fs::path canonical = fs::weakly_canonical(entry.path(), statError);
                const fs::path seenPath = statError ? entry.path() : canonical;
                if (seen.insert({folder.format, seenPath}).second) {
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
