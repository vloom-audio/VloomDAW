// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/SampleFolder.hpp"

#include "core/BrowserFilter.hpp"

#include <algorithm>
#include <exception>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace vloomdaw {

namespace fs = std::filesystem;

namespace {

constexpr bool isDigit(char c) noexcept { return c >= '0' && c <= '9'; }
constexpr char lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

/// `path` as UTF-8; empty when it cannot be converted.
std::string utf8(const fs::path& path)
{
    try {
        const std::u8string text = path.u8string();
        return {text.begin(), text.end()};
    } catch (const std::exception&) {
        return {}; // e.g. unpaired UTF-16 surrogates on Windows
    }
}

/// The file name as UTF-8; empty when it cannot be converted (then skipped).
std::string utf8Name(const fs::path& path) { return utf8(path.filename()); }

/// A leading dot hides an entry everywhere (also macOS "._name" resource
/// forks on shared drives).
bool isDotHidden(std::string_view name) { return !name.empty() && name.front() == '.'; }

/// Windows' hidden attribute ($RECYCLE.BIN, System Volume Information...).
/// Checked for folders only: one system call per entry would make a folder
/// of 20 000 samples several times slower to read, and hidden samples are rare.
bool isHiddenFolder([[maybe_unused]] const fs::path& path)
{
#if defined(_WIN32)
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_HIDDEN) != 0;
#else
    return false;
#endif
}

} // namespace

bool naturalLess(std::string_view a, std::string_view b) noexcept
{
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.size() && j < b.size()) {
        if (isDigit(a[i]) && isDigit(b[j])) {
            std::size_t endA = i;
            while (endA < a.size() && isDigit(a[endA]))
                ++endA;
            std::size_t endB = j;
            while (endB < b.size() && isDigit(b[endB]))
                ++endB;
            // Compare the numbers without leading zeros: longer is larger.
            std::size_t startA = i;
            while (startA + 1 < endA && a[startA] == '0')
                ++startA;
            std::size_t startB = j;
            while (startB + 1 < endB && b[startB] == '0')
                ++startB;
            const std::size_t lengthA = endA - startA;
            const std::size_t lengthB = endB - startB;
            if (lengthA != lengthB)
                return lengthA < lengthB;
            const int order = a.substr(startA, lengthA).compare(b.substr(startB, lengthB));
            if (order != 0)
                return order < 0;
            i = endA;
            j = endB;
            continue;
        }
        const char x = lower(a[i]);
        const char y = lower(b[j]);
        if (x != y)
            return static_cast<unsigned char>(x) < static_cast<unsigned char>(y);
        ++i;
        ++j;
    }
    const std::size_t restA = a.size() - i;
    const std::size_t restB = b.size() - j;
    if (restA != restB)
        return restA < restB;
    return a < b; // equal ignoring case and zeros: any fixed order
}

FolderListing listSampleFolder(const fs::path& folder, std::size_t maxEntries)
{
    FolderListing listing;
    std::error_code ec;
    fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        listing.failed = true;
        return listing;
    }
    for (const fs::directory_iterator end; it != end; it.increment(ec)) {
        if (ec) {
            listing.failed = true; // stop at the error, keep what was read
            break;
        }
        const fs::directory_entry& entry = *it;
        std::string name = utf8Name(entry.path());
        if (name.empty() || isDotHidden(name))
            continue;
        std::error_code typeError;
        const bool isDir = entry.is_directory(typeError);
        if (typeError || (isDir ? isHiddenFolder(entry.path()) : !isAudioFile(name)))
            continue;
        if (listing.entries.size() >= maxEntries) {
            listing.truncated = true;
            break;
        }
        listing.entries.push_back({entry.path(), std::move(name), isDir});
    }
    std::sort(listing.entries.begin(), listing.entries.end(), [](const FolderEntry& x, const FolderEntry& y) {
        if (x.isDir != y.isDir)
            return x.isDir;
        return naturalLess(x.name, y.name);
    });
    return listing;
}

SearchResult searchSampleFolder(
    const fs::path& root, std::string_view search, const std::atomic<bool>* cancel, std::size_t maxResults)
{
    SearchResult result;
    std::error_code ec;
    // Folder links are not followed (no follow_directory_symlink): no loops.
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    if (ec)
        return result;
    for (const fs::recursive_directory_iterator end; it != end; it.increment(ec)) {
        if (ec)
            break; // the iterator cannot go on after an error
        if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) {
            result.cancelled = true;
            break;
        }
        const fs::directory_entry& entry = *it;
        const std::string name = utf8Name(entry.path());
        std::error_code typeError;
        const bool isDir = entry.is_directory(typeError);
        if (typeError)
            continue;
        if (isDir) {
            if (name.empty() || isDotHidden(name) || it.depth() + 1 >= kMaxSearchDepth
                || isHiddenFolder(entry.path()))
                it.disable_recursion_pending();
            continue;
        }
        if (name.empty() || isDotHidden(name) || !isAudioFile(name) || !matchesSearch(name, search))
            continue;
        if (result.hits.size() >= maxResults) {
            result.truncated = true;
            break;
        }
        result.hits.push_back(
            {entry.path(), name, utf8(entry.path().parent_path().lexically_relative(root))});
    }
    for (SearchHit& hit : result.hits) {
        if (hit.folder == ".")
            hit.folder.clear();
    }
    std::sort(result.hits.begin(), result.hits.end(), [](const SearchHit& x, const SearchHit& y) {
        if (x.name != y.name)
            return naturalLess(x.name, y.name);
        return naturalLess(x.folder, y.folder);
    });
    return result;
}

} // namespace vloomdaw
