// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/PluginPresets.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace vloomdaw;

TEST_CASE("Presets survive being written and read back", "[presets]")
{
    const std::vector<PluginPreset> presets{
        {"Vocal", {{0, 1.0}, {1, 90.0}, {8, 3.5}}},
        {"เสียงร้อง", {{11, -2.25}}},
        {"Empty", {}},
    };
    CHECK(parsePluginPresets(writePluginPresets(presets)) == presets);
    CHECK(parsePluginPresets(writePluginPresets({})).empty());
}

TEST_CASE("Text that is not a preset file gives no presets", "[presets]")
{
    CHECK(parsePluginPresets("").empty());
    CHECK(parsePluginPresets("not json").empty());
    CHECK(parsePluginPresets("[1, 2, 3]").empty());
    CHECK(parsePluginPresets(R"({"presets": 5})").empty());
    CHECK(parsePluginPresets(std::string(kMaxPresetFileBytes + 1, ' ')).empty());
}

TEST_CASE("Entries that do not fit are left out, the rest kept", "[presets]")
{
    const std::string text = R"({"presets": [
        {"name": "Good", "values": [[2, 120.0], [3, 4.5]]},
        {"name": "", "values": []},
        {"values": [[1, 2.0]]},
        {"name": 7, "values": []},
        "nonsense",
        {"name": "Odd values", "values": [[1], [-1, 2.0], ["x", 1.0], [4, "y"], [4294967296, 1.0], [5, 6.5]]}
    ]})";
    const std::vector<PluginPreset> presets = parsePluginPresets(text);
    REQUIRE(presets.size() == 2);
    CHECK(presets[0] == PluginPreset{"Good", {{2, 120.0}, {3, 4.5}}});
    CHECK(presets[1] == PluginPreset{"Odd values", {{5, 6.5}}});

    const std::string longName(kMaxPresetNameBytes + 1, 'n');
    CHECK(parsePluginPresets(R"({"presets": [{"name": ")" + longName + R"(", "values": []}]})").empty());
}

TEST_CASE("A file with too many presets is cut at the limit", "[presets]")
{
    std::vector<PluginPreset> many;
    for (std::size_t i = 0; i < kMaxPresets + 20; ++i)
        many.push_back({"Preset " + std::to_string(i), {{0, static_cast<double>(i)}}});
    const std::vector<PluginPreset> read = parsePluginPresets(writePluginPresets(many));
    REQUIRE(read.size() == kMaxPresets);
    CHECK(read.back().name == "Preset " + std::to_string(kMaxPresets - 1));
}
