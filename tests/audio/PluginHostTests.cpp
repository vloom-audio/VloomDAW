// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/PluginHost.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace vloomdaw;

TEST_CASE("A plugin that cannot be loaded gives no handle and says why", "[plugins]")
{
    PluginHost host;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto missing
        = std::filesystem::temp_directory_path() / ("vloomdaw_missing_" + std::to_string(stamp) + ".clap");
    for (const PluginFormat format : {PluginFormat::Clap, PluginFormat::Vst3}) {
        const PluginHost::Loaded loaded = host.load(format, missing, {}, "Missing", 48000.0);
        CHECK(loaded.handle == 0);
        CHECK_FALSE(loaded.error.empty());
    }

    // A file that is not a plugin at all.
    const auto bogus
        = std::filesystem::temp_directory_path() / ("vloomdaw_bogus_" + std::to_string(stamp) + ".clap");
    std::ofstream(bogus) << "not a plugin";
    const PluginHost::Loaded loaded = host.load(PluginFormat::Clap, bogus, {}, "Bogus", 48000.0);
    CHECK(loaded.handle == 0);
    CHECK_FALSE(loaded.error.empty());
    std::error_code ec;
    std::filesystem::remove(bogus, ec);

    CHECK(host.size() == 0);
    CHECK(host.instance(1) == nullptr);
    CHECK(host.resolver()(1) == nullptr);
    host.retainOnly({}); // nothing to drop
    host.idle();
}

TEST_CASE("The plugin to load is found by id, then by name, then as a file's only plugin", "[plugins]")
{
    const auto plugin = [](std::string id, std::string name) {
        vloom::PluginInfo info;
        info.id = std::move(id);
        info.name = std::move(name);
        return info;
    };
    const std::vector<vloom::PluginInfo> several{
        plugin("com.a.synth", "Synth"), plugin("com.a.verb", "Verb")};

    CHECK(pickPlugin(several, "com.a.verb", "Synth").plugin == &several[1]); // the id wins
    const PluginPick gone = pickPlugin(several, "com.a.delay", "Delay");
    CHECK(gone.plugin == nullptr);
    CHECK_FALSE(gone.error.empty());

    CHECK(pickPlugin(several, {}, "Verb").plugin == &several[1]);
    // Several and no name matches (an unprobed scan names a plugin after
    // its file): no guess.
    const PluginPick unknown = pickPlugin(several, {}, "Bundle");
    CHECK(unknown.plugin == nullptr);
    CHECK(unknown.error.find("2 plugins") != std::string::npos);

    const std::vector<vloom::PluginInfo> one{plugin("com.a.synth", "Synth")};
    CHECK(pickPlugin(one, {}, "synth-file").plugin == &one[0]);
    CHECK(pickPlugin({}, {}, "Synth").plugin == nullptr);
}
