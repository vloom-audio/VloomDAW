// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/SampleFolder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;
using vloomdaw::listSampleFolder;
using vloomdaw::naturalLess;
using vloomdaw::searchSampleFolder;

namespace {

/// A fresh folder under the system temp directory, removed afterwards.
struct TempFolder {
    TempFolder()
    {
        std::random_device seed;
        path = fs::temp_directory_path() / ("vloomdaw-sample-folder-" + std::to_string(seed()));
        fs::create_directories(path);
    }
    ~TempFolder()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;

    void file(const fs::path& name) const { std::ofstream(path / name).put('x'); }
    void folder(const fs::path& name) const { fs::create_directories(path / name); }

    fs::path path;
};

std::vector<std::string> names(const vloomdaw::FolderListing& listing)
{
    std::vector<std::string> out;
    for (const auto& entry : listing.entries)
        out.push_back(entry.name);
    return out;
}

} // namespace

TEST_CASE("Natural order compares digit runs as numbers and ignores case", "[core][browser]")
{
    CHECK(naturalLess("kick 2", "kick 10"));
    CHECK_FALSE(naturalLess("kick 10", "kick 2"));
    CHECK(naturalLess("Kick", "kick 1"));
    CHECK(naturalLess("hat", "Kick"));
    CHECK(naturalLess("a", "B"));
    CHECK(naturalLess("snare 007", "snare 8"));
    CHECK(naturalLess("x99999999999999999999", "x100000000000000000000")); // beyond 64 bits
    // Distinct strings are always ordered one way, equal ones neither way.
    CHECK(naturalLess("kick 01", "kick 1") != naturalLess("kick 1", "kick 01"));
    CHECK(naturalLess("Kick", "kick") != naturalLess("kick", "Kick"));
    CHECK_FALSE(naturalLess("same", "same"));
}

TEST_CASE("A folder lists sub-folders first, then audio files, in natural order", "[core][browser]")
{
    TempFolder temp;
    temp.file("kick 10.wav");
    temp.file("kick 2.WAV");
    temp.file("Hat.aiff");
    temp.file("readme.txt");
    temp.file("project.flp");
    temp.folder("Zeta");
    temp.folder("alpha");

    const auto listing = listSampleFolder(temp.path);
    CHECK_FALSE(listing.failed);
    CHECK_FALSE(listing.truncated);
    CHECK(
        names(listing) == std::vector<std::string>{"alpha", "Zeta", "Hat.aiff", "kick 2.WAV", "kick 10.wav"});
    CHECK(listing.entries[0].isDir);
    CHECK_FALSE(listing.entries[2].isDir);
    CHECK(listing.entries[2].path == temp.path / "Hat.aiff");
}

TEST_CASE("Hidden entries and resource forks are left out", "[core][browser]")
{
    TempFolder temp;
    temp.file("._kick.wav");
    temp.file(".hidden.wav");
    temp.folder(".git");
    temp.file("kick.wav");
    CHECK(names(listSampleFolder(temp.path)) == std::vector<std::string>{"kick.wav"});
}

#if defined(_WIN32)
TEST_CASE("Folders with the Windows hidden attribute are left out", "[core][browser]")
{
    TempFolder temp;
    temp.folder("System Volume Information");
    temp.folder("Drums");
    const fs::path hidden = temp.path / "System Volume Information";
    REQUIRE(SetFileAttributesW(hidden.c_str(), FILE_ATTRIBUTE_HIDDEN) != 0);
    CHECK(names(listSampleFolder(temp.path)) == std::vector<std::string>{"Drums"});
}
#endif

TEST_CASE("A big folder is cut at the limit", "[core][browser]")
{
    TempFolder temp;
    for (int i = 0; i < 12; ++i)
        temp.file("s" + std::to_string(i) + ".wav");
    const auto listing = listSampleFolder(temp.path, 5);
    CHECK(listing.truncated);
    CHECK(listing.entries.size() == 5);
}

TEST_CASE("A missing folder fails without throwing", "[core][browser]")
{
    const auto listing = listSampleFolder(fs::temp_directory_path() / "vloomdaw-no-such-folder-8f3a");
    CHECK(listing.failed);
    CHECK(listing.entries.empty());
}

TEST_CASE("Names outside ASCII survive as UTF-8", "[core][browser]")
{
    TempFolder temp;
    temp.file(fs::path(u8"กลอง 1.wav"));
    const auto listing = listSampleFolder(temp.path);
    REQUIRE(listing.entries.size() == 1);
    CHECK(listing.entries[0].name == std::string(reinterpret_cast<const char*>(u8"กลอง 1.wav")));
}

TEST_CASE("Search finds matching audio files in every sub-folder", "[core][browser]")
{
    TempFolder temp;
    temp.file("Kick top.wav");
    temp.folder("Drums/Kicks/Deep");
    temp.file("Drums/Kicks/kick 10.wav");
    temp.file("Drums/Kicks/Deep/KICK 2.wav");
    temp.file("Drums/snare.wav");
    temp.file("Drums/kick notes.txt");

    const auto result = searchSampleFolder(temp.path, "kick");
    CHECK_FALSE(result.truncated);
    CHECK_FALSE(result.cancelled);
    REQUIRE(result.hits.size() == 3);
    CHECK(result.hits[0].name == "KICK 2.wav");
    CHECK(result.hits[0].folder == (fs::path("Drums") / "Kicks" / "Deep").string());
    CHECK(result.hits[1].name == "kick 10.wav");
    CHECK(result.hits[2].name == "Kick top.wav");
    CHECK(result.hits[2].folder.empty());
    CHECK(result.hits[2].path == temp.path / "Kick top.wav");
}

TEST_CASE("Search skips hidden folders and files", "[core][browser]")
{
    TempFolder temp;
    temp.folder(".cache");
    temp.file(".cache/kick.wav");
    temp.file("._kick.wav");
    temp.file("kick.wav");
    const auto result = searchSampleFolder(temp.path, "kick");
    REQUIRE(result.hits.size() == 1);
    CHECK(result.hits[0].name == "kick.wav");
}

TEST_CASE("Search stops at the result limit", "[core][browser]")
{
    TempFolder temp;
    for (int i = 0; i < 10; ++i)
        temp.file("kick " + std::to_string(i) + ".wav");
    const auto result = searchSampleFolder(temp.path, "kick", nullptr, 4);
    CHECK(result.truncated);
    CHECK(result.hits.size() == 4);
}

TEST_CASE("A cancelled search stops without results", "[core][browser]")
{
    TempFolder temp;
    temp.file("kick.wav");
    const std::atomic<bool> cancel{true};
    const auto result = searchSampleFolder(temp.path, "kick", &cancel);
    CHECK(result.cancelled);
    CHECK(result.hits.empty());
}

TEST_CASE("Searching a missing folder finds nothing without throwing", "[core][browser]")
{
    const auto result
        = searchSampleFolder(fs::temp_directory_path() / "vloomdaw-no-such-folder-8f3a", "kick");
    CHECK(result.hits.empty());
}
