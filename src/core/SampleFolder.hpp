// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vloomdaw {

/// One row of the sample browser.
struct FolderEntry {
    std::filesystem::path path;
    std::string name; ///< file name as UTF-8 (POSIX: the raw bytes), for sorting and search
    bool isDir = false;
};

/// What listSampleFolder() found.
struct FolderListing {
    std::vector<FolderEntry> entries; ///< folders first, then audio files, each in natural order
    bool truncated = false; ///< more than the limit: the rest is not listed
    bool failed = false; ///< the folder could not be read (missing, no permission)
};

/// Most entries listed from one folder; a bigger one is cut (`truncated`).
inline constexpr std::size_t kMaxFolderEntries = 100'000;

/// Lists one folder for the sample browser: sub-folders and audio files
/// (BrowserFilter's extensions), skipping hidden ones (a leading dot; on
/// Windows also folders with the hidden attribute) and entries that cannot
/// be read.
/// Never throws for file system errors. Slow on big folders: call it off the
/// UI thread.
[[nodiscard]] FolderListing listSampleFolder(
    const std::filesystem::path& folder, std::size_t maxEntries = kMaxFolderEntries);

/// One file found by searchSampleFolder().
struct SearchHit {
    std::filesystem::path path;
    std::string name; ///< file name, UTF-8
    std::string folder; ///< its folder relative to the searched root, UTF-8 ("" = the root)
};

/// What searchSampleFolder() found.
struct SearchResult {
    std::vector<SearchHit> hits; ///< natural order by name, then folder
    bool truncated = false; ///< stopped at the result limit
    bool cancelled = false; ///< `cancel` was set: `hits` is incomplete
};

/// Most hits one search returns, and deepest folder level it enters.
inline constexpr std::size_t kMaxSearchResults = 2'000;
inline constexpr int kMaxSearchDepth = 32;

/// Searches `root` and every folder below it for audio files whose name
/// matches `search` (core/BrowserFilter's matchesSearch). Skips the same
/// hidden entries as listSampleFolder(), does not follow folder links, and
/// stops early when `*cancel` turns true. Never throws for file system
/// errors. Slow on big libraries: call it off the UI thread.
[[nodiscard]] SearchResult searchSampleFolder(const std::filesystem::path& root, std::string_view search,
    const std::atomic<bool>* cancel = nullptr, std::size_t maxResults = kMaxSearchResults);

/// "Natural" order: letters ignore ASCII case, and runs of digits compare as
/// numbers ("kick 2" before "kick 10"). A strict weak order, total on
/// distinct strings.
[[nodiscard]] bool naturalLess(std::string_view a, std::string_view b) noexcept;

} // namespace vloomdaw
