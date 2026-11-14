// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/PluginHost.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

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
