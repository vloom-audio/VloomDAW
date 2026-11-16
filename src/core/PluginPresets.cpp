// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/PluginPresets.hpp"

#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>

namespace vloomdaw {

namespace {

using Json = nlohmann::json;

constexpr int kVersion = 1;

} // namespace

std::vector<PluginPreset> parsePluginPresets(std::string_view text)
{
    std::vector<PluginPreset> presets;
    if (text.size() > kMaxPresetFileBytes)
        return presets;
    // No exceptions: a damaged file gives a discarded value, not a throw.
    const Json root = Json::parse(text, nullptr, false);
    if (!root.is_object())
        return presets;
    const auto list = root.find("presets");
    if (list == root.end() || !list->is_array())
        return presets;
    for (const Json& entry : *list) {
        if (presets.size() >= kMaxPresets)
            break;
        if (!entry.is_object())
            continue;
        const auto name = entry.find("name");
        const auto values = entry.find("values");
        if (name == entry.end() || !name->is_string() || values == entry.end() || !values->is_array())
            continue;
        PluginPreset preset;
        preset.name = name->get<std::string>();
        if (preset.name.empty() || preset.name.size() > kMaxPresetNameBytes)
            continue;
        for (const Json& pair : *values) {
            if (preset.values.size() >= kMaxPresetValues)
                break;
            if (!pair.is_array() || pair.size() != 2 || !pair[0].is_number_unsigned() || !pair[1].is_number())
                continue;
            const auto id = pair[0].get<std::uint64_t>();
            const double value = pair[1].get<double>();
            if (id > std::numeric_limits<std::uint32_t>::max() || !std::isfinite(value))
                continue;
            preset.values.emplace_back(static_cast<std::uint32_t>(id), value);
        }
        presets.push_back(std::move(preset));
    }
    return presets;
}

std::string writePluginPresets(const std::vector<PluginPreset>& presets)
{
    Json list = Json::array();
    for (const PluginPreset& preset : presets) {
        Json values = Json::array();
        for (const auto& [id, value] : preset.values)
            if (std::isfinite(value))
                values.push_back(Json::array({id, value}));
        list.push_back(Json{{"name", preset.name}, {"values", std::move(values)}});
    }
    // Names are the user's text: bytes that are not UTF-8 are replaced, not thrown on.
    return Json{{"version", kVersion}, {"presets", std::move(list)}}.dump(
        2, ' ', false, Json::error_handler_t::replace);
}

} // namespace vloomdaw
