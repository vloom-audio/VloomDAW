// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/PluginScanner.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace vloomdaw;

namespace {

/// A fresh, empty folder under the system temp folder, removed afterwards.
struct TempFolder {
    fs::path path;
    TempFolder()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("vloom_plugins_" + std::to_string(stamp));
        fs::create_directories(path);
    }
    ~TempFolder()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    void touch(const fs::path& relative) const
    {
        fs::create_directories((path / relative).parent_path());
        std::ofstream(path / relative) << "not a real plugin";
    }
};

std::vector<std::string> names(const std::vector<PluginInfo>& plugins)
{
    std::vector<std::string> out;
    for (const PluginInfo& p : plugins)
        out.push_back(p.name);
    return out;
}

} // namespace

TEST_CASE("Plugin files and bundles are found, other files are not", "[plugins]")
{
    TempFolder vst3;
    vst3.touch("Vital.vst3"); // single-file plugin
    vst3.touch(
        "Vendor/Surge XT.vst3/Contents/x86_64-win/Surge XT.vst3"); // bundle with a same-named file inside
    vst3.touch("Readme.txt");
    vst3.touch("Other.clap"); // wrong format for this folder
    TempFolder clap;
    clap.touch("Vital.clap");
    clap.touch("Deep/Nested/Synth.CLAP"); // upper-case extension

    const auto plugins
        = scanPluginFolders({{PluginFormat::Vst3, vst3.path}, {PluginFormat::Clap, clap.path}});

    CHECK(names(plugins) == std::vector<std::string>{"Surge XT", "Synth", "Vital", "Vital"});
    // Same name: VST3 sorts before CLAP.
    CHECK(plugins[2].format == PluginFormat::Vst3);
    CHECK(plugins[3].format == PluginFormat::Clap);
    // The bundle itself is listed, not the file inside it.
    CHECK(plugins[0].path.filename() == "Surge XT.vst3");
    CHECK(fs::is_directory(plugins[0].path));
}

TEST_CASE("Missing folders are skipped", "[plugins]")
{
    TempFolder folder;
    CHECK(scanPluginFolders({{PluginFormat::Vst3, folder.path / "does-not-exist"}}).empty());
    CHECK(scanPluginFolders({}).empty());
}

TEST_CASE("Search depth is limited", "[plugins]")
{
    TempFolder folder;
    folder.touch("a/b/c/d/Deep.vst3");
    CHECK(scanPluginFolders({{PluginFormat::Vst3, folder.path}}, 2).empty());
    CHECK(
        names(scanPluginFolders({{PluginFormat::Vst3, folder.path}}, 8)) == std::vector<std::string>{"Deep"});
}

TEST_CASE("A folder listed twice yields each plugin once", "[plugins]")
{
    TempFolder folder;
    folder.touch("One.vst3");
    const auto plugins
        = scanPluginFolders({{PluginFormat::Vst3, folder.path}, {PluginFormat::Vst3, folder.path}});
    CHECK(plugins.size() == 1);
}

TEST_CASE("Default folders exist for both formats", "[plugins]")
{
    bool vst3 = false;
    bool clap = false;
    for (const PluginFolder& f : defaultPluginFolders()) {
        vst3 = vst3 || f.format == PluginFormat::Vst3;
        clap = clap || f.format == PluginFormat::Clap;
    }
    CHECK(vst3);
    CHECK(clap);
    CHECK(formatName(PluginFormat::Vst3) == "VST3");
    CHECK(formatName(PluginFormat::Clap) == "CLAP");
}

TEST_CASE("The install folder gives the vendor; size and time are recorded", "[plugins]")
{
    TempFolder vst3;
    vst3.touch("FabFilter/FabFilter Pro-Q 4.vst3");
    vst3.touch("Loose.vst3");
    const auto plugins = scanPluginFolders({{PluginFormat::Vst3, vst3.path}});
    REQUIRE(plugins.size() == 2);
    CHECK(plugins[0].name == "FabFilter Pro-Q 4");
    CHECK(plugins[0].vendor == "FabFilter");
    CHECK(plugins[0].fileSize > 0);
    CHECK(plugins[0].modified != 0);
    CHECK(plugins[1].vendor.empty()); // directly in the root: unknown until probed
    CHECK(plugins[1].category == "Uncategorized");
    CHECK(plugins[1].probe == PluginInfo::Probe::NotProbed);
}
