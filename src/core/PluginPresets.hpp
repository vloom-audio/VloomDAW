// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vloomdaw {

/// A named set of parameter values of a built-in plugin, as the user saved it.
struct PluginPreset {
    std::string name; ///< UTF-8, 1 .. kMaxPresetNameBytes bytes
    std::vector<std::pair<std::uint32_t, double>> values; ///< parameter id and value

    friend bool operator==(const PluginPreset&, const PluginPreset&) = default;
};

inline constexpr std::size_t kMaxPresetNameBytes = 128;
inline constexpr std::size_t kMaxPresets = 500; ///< per plugin
inline constexpr std::size_t kMaxPresetValues = 1024; ///< per preset
inline constexpr std::size_t kMaxPresetFileBytes = 4 * 1024 * 1024;

/// The presets in the text of a preset file. The file is the user's and may
/// be damaged or edited by hand: what does not fit (a preset without a
/// name, a value that is not a finite number, anything past the limits) is
/// left out, and text that is not a preset file gives no presets.
[[nodiscard]] std::vector<PluginPreset> parsePluginPresets(std::string_view text);
/// The text of a preset file holding `presets`.
[[nodiscard]] std::string writePluginPresets(const std::vector<PluginPreset>& presets);

} // namespace vloomdaw
