// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/UpdateFeed.hpp"

#include <algorithm>
#include <cstddef>
#include <nlohmann/json.hpp>

namespace vloomdaw {

namespace {

using nlohmann::json;

std::string releasePrefix() { return "https://github.com/" + std::string(kUpdateRepository) + "/releases/"; }

std::string stringField(const json& object, const char* key)
{
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool boolField(const json& object, const char* key)
{
    const auto it = object.find(key);
    return it != object.end() && it->is_boolean() && it->get<bool>();
}

std::optional<UpdateAsset> findAsset(const json& assets, std::string_view name)
{
    std::optional<UpdateAsset> found;
    for (const json& asset : assets) {
        if (!asset.is_object() || stringField(asset, "name") != name)
            continue;
        if (found)
            return std::nullopt; // twice: ambiguous
        UpdateAsset a;
        a.name = std::string(name);
        a.url = stringField(asset, "browser_download_url");
        const auto size = asset.find("size");
        if (size != asset.end() && size->is_number_unsigned())
            a.size = size->get<std::uint64_t>();
        if (!isReleaseDownloadUrl(a.url) || a.size == 0)
            return std::nullopt;
        found = std::move(a);
    }
    return found;
}

bool isHex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

/// Cuts at most `limit` bytes without splitting a UTF-8 sequence.
std::string clipUtf8(std::string text, std::size_t limit)
{
    if (text.size() <= limit)
        return text;
    std::size_t end = limit;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0u) == 0x80u)
        --end;
    text.resize(end);
    return text;
}

} // namespace

UpdatePlatform currentUpdatePlatform()
{
#if defined(_WIN32)
    return UpdatePlatform::Windows;
#elif defined(__APPLE__)
    return UpdatePlatform::MacOS;
#else
    return UpdatePlatform::Linux;
#endif
}

std::string installerFileName(const Version& version, UpdatePlatform platform)
{
    const std::string v = version.toString();
    switch (platform) {
    case UpdatePlatform::Windows:
        return "VloomDAW-" + v + "-windows-x64-setup.exe";
    case UpdatePlatform::MacOS:
        return "VloomDAW-" + v + "-macos-universal.dmg";
    case UpdatePlatform::Linux:
        return "VloomDAW-" + v + "-linux-x86_64.AppImage";
    }
    return {};
}

bool isReleaseDownloadUrl(std::string_view url)
{
    const std::string prefix = releasePrefix() + "download/";
    if (url.size() <= prefix.size() || url.substr(0, prefix.size()) != prefix)
        return false;
    // No way out of the prefix: no "..", query, fragment or backslash.
    const std::string_view rest = url.substr(prefix.size());
    return rest.find("..") == std::string_view::npos && rest.find_first_of("?#\\%") == std::string_view::npos;
}

UpdateFeedResult newestUpdate(
    std::string_view text, const Version& current, bool includePrereleases, UpdatePlatform platform)
{
    UpdateFeedResult result;
    const json releases = json::parse(text, nullptr, false);
    if (releases.is_discarded() || !releases.is_array()) {
        result.error = "the list of releases could not be read";
        return result;
    }
    for (const json& r : releases) {
        if (!r.is_object() || boolField(r, "draft"))
            continue;
        const std::string tag = stringField(r, "tag_name");
        const auto version = parseVersion(tag);
        if (!version || *version <= current)
            continue;
        const bool pre = boolField(r, "prerelease") || version->isPrerelease();
        if (pre && !includePrereleases)
            continue;
        if (result.release && *version <= result.release->version)
            continue;
        const auto assets = r.find("assets");
        if (assets == r.end() || !assets->is_array())
            continue;
        auto installer = findAsset(*assets, installerFileName(*version, platform));
        auto sums = findAsset(*assets, kChecksumsFile);
        auto signature = findAsset(*assets, kChecksumsSignatureFile);
        if (!installer || !sums || !signature)
            continue; // not for this platform, or not signed (yet)
        const std::string page = stringField(r, "html_url");

        UpdateRelease release;
        release.version = *version;
        release.tag = tag;
        release.title = stringField(r, "name");
        if (release.title.empty())
            release.title = "VloomDAW " + tag;
        release.notes = clipUtf8(stringField(r, "body"), kMaxNotes);
        release.pageUrl = page.rfind(releasePrefix(), 0) == 0 ? page : releasePrefix();
        release.prerelease = pre;
        release.installer = std::move(*installer);
        release.checksums = std::move(*sums);
        release.signature = std::move(*signature);
        result.release = std::move(release);
    }
    return result;
}

std::optional<std::string> checksumFor(std::string_view sums, std::string_view fileName)
{
    std::optional<std::string> found;
    while (!sums.empty()) {
        const std::size_t end = sums.find('\n');
        std::string_view line = sums.substr(0, end);
        sums = end == std::string_view::npos ? std::string_view() : sums.substr(end + 1);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        // "<64 hex>  <name>" (text mode) or "<64 hex> *<name>" (binary mode).
        if (line.size() < 66 || line[64] != ' ' || (line[65] != ' ' && line[65] != '*'))
            continue;
        const std::string_view hash = line.substr(0, 64);
        if (!std::all_of(hash.begin(), hash.end(), isHex) || line.substr(66) != fileName)
            continue;
        if (found)
            return std::nullopt; // listed twice
        std::string h(hash);
        std::transform(h.begin(), h.end(), h.begin(), lower);
        found = std::move(h);
    }
    return found;
}

} // namespace vloomdaw
