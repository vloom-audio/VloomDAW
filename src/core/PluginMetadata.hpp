// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/PluginScanner.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace vloomdaw {

/// One plugin as its own factory describes it (vloomdaw-plugin-probe).
struct ProbedPlugin {
    std::string name;
    std::string vendor;
    std::string version;
    std::string tags; ///< VST3 sub-categories ("Fx|Reverb") or CLAP features ("audio-effect|reverb")
};

struct ProbeOutput {
    bool ok = false; ///< the probe ran to the end
    std::string error; ///< why not, when !ok
    std::vector<ProbedPlugin> plugins;
};

/// Reads the probe's stdout. Lines that are not probe records (things a
/// plugin printed itself) are ignored; so is anything after 1000 plugins.
[[nodiscard]] ProbeOutput parseProbeOutput(std::string_view text);

/// Display categories, in the order the browser lists them.
[[nodiscard]] const std::vector<std::string>& pluginCategories();

/// The display category for VST3 sub-categories or CLAP features, e.g.
/// "Fx|Reverb" -> "Reverb", "audio-effect|compressor" -> "Dynamics",
/// "Instrument|Synth" -> "Instrument". Unknown tags give "Other"; no tags at
/// all give "Uncategorized".
[[nodiscard]] std::string pluginCategory(std::string_view tags);

/// Instrument types, in the order the browser lists them.
[[nodiscard]] const std::vector<std::string>& instrumentTypes();

/// For an instrument, its type from the same tags: "Drums", "Keys",
/// "Sampler", "Synth", "External" or "Other" (the most specific wins, so a
/// drum sampler is "Drums"). Empty for anything that is not an instrument.
[[nodiscard]] std::string instrumentType(std::string_view tags);

/// Vendor names without legal suffixes and stray spaces, so one company
/// lands in one group: "iZotope, Inc." -> "iZotope".
[[nodiscard]] std::string normalizeVendor(std::string_view vendor);

/// What a probe of `file` (an entry from scanPluginFolders()) found: one
/// entry per plugin in the module, with the factory's name, vendor (or the
/// install folder's when the factory gives none), version and category.
/// A failed probe, or a module with no plugins, gives `file` back marked
/// Probe::Failed with the reason.
[[nodiscard]] std::vector<PluginInfo> applyProbe(const PluginInfo& file, const ProbeOutput& probe);

/// True when `a` and `b` are the same module, unchanged on disk (path, size
/// and time match), so an earlier probe of it still holds.
[[nodiscard]] bool sameModule(const PluginInfo& a, const PluginInfo& b);

/// The category to show: the user's, else the plugin's.
[[nodiscard]] std::string effectiveCategory(const PluginInfo& plugin);
/// The instrument type to show (empty for non-instruments): the user's when
/// they set the category, else the one from the plugin's tags.
[[nodiscard]] std::string effectiveInstrumentType(const PluginInfo& plugin);

/// Copies the user's categories from an earlier scan to the same plugins in
/// a new one (same format, path and name), so scans never undo them.
void carryUserCategories(std::vector<PluginInfo>& fresh, const std::vector<PluginInfo>& earlier);

} // namespace vloomdaw
