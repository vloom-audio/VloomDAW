// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/PluginMetadata.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace vloomdaw {

namespace {

constexpr std::string_view kPrefix = "@@vloom\t";
constexpr std::size_t kMaxPlugins = 1000;

std::vector<std::string> splitTabs(std::string_view line)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t tab = line.find('\t', start);
        out.emplace_back(
            line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
        if (tab == std::string_view::npos)
            break;
        start = tab + 1;
    }
    return out;
}

std::string lower(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

/// The tags in lower case, split on '|'.
std::vector<std::string> tagList(std::string_view tags)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= tags.size()) {
        const std::size_t bar = tags.find('|', start);
        const std::string_view tag
            = tags.substr(start, bar == std::string_view::npos ? std::string_view::npos : bar - start);
        if (!tag.empty())
            out.push_back(lower(tag));
        if (bar == std::string_view::npos)
            break;
        start = bar + 1;
    }
    return out;
}

/// Tag -> category, first match in this order wins (an instrument with a
/// reverb inside is an instrument). Covers VST3 sub-categories and CLAP features.
const std::vector<std::pair<std::string_view, std::string_view>>& rules()
{
    static const std::vector<std::pair<std::string_view, std::string_view>> table{
        {"instrument", "Instrument"},
        {"synth", "Instrument"},
        {"synthesizer", "Instrument"},
        {"sampler", "Instrument"},
        {"drum", "Instrument"},
        {"drum-machine", "Instrument"},
        {"piano", "Instrument"},
        {"reverb", "Reverb"},
        {"delay", "Delay"},
        {"amp", "Amp"},
        {"amp simulator", "Amp"},
        {"cabinet", "Amp"},
        {"dynamics", "Dynamics"},
        {"compressor", "Dynamics"},
        {"limiter", "Dynamics"},
        {"expander", "Dynamics"},
        {"gate", "Dynamics"},
        {"transient-shaper", "Dynamics"},
        {"eq", "EQ"},
        {"equalizer", "EQ"},
        {"filter", "Filter"},
        {"distortion", "Distortion"},
        {"modulation", "Modulation"},
        {"chorus", "Modulation"},
        {"flanger", "Modulation"},
        {"phaser", "Modulation"},
        {"tremolo", "Modulation"},
        {"pitch shift", "Pitch"},
        {"pitch-shifter", "Pitch"},
        {"pitch-correction", "Pitch"},
        {"spatial", "Spatial"},
        {"surround", "Spatial"},
        {"ambisonic", "Spatial"},
        {"up-downmix", "Spatial"},
        {"analyzer", "Analyzer"},
        {"mastering", "Mastering"},
        {"restoration", "Restoration"},
        {"tools", "Utility"},
        {"utility", "Utility"},
        {"generator", "Utility"},
        {"network", "Utility"},
    };
    return table;
}

} // namespace

ProbeOutput parseProbeOutput(std::string_view text)
{
    ProbeOutput out;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        std::string_view line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (!line.starts_with(kPrefix))
            continue; // printed by the plugin itself
        const std::vector<std::string> f = splitTabs(line.substr(kPrefix.size()));
        if (f[0] == "probe" && f.size() >= 2 && f[1] == "1") {
            out.ok = true;
        } else if (f[0] == "error" && f.size() >= 2) {
            out.ok = false;
            out.error = f[1];
            return out;
        } else if (f[0] == "plugin" && f.size() >= 5 && out.ok && out.plugins.size() < kMaxPlugins
            && !f[1].empty()) {
            out.plugins.push_back({f[1], f[2], f[3], f[4]});
        }
    }
    if (!out.ok && out.error.empty())
        out.error = "the probe gave no answer";
    return out;
}

const std::vector<std::string>& pluginCategories()
{
    static const std::vector<std::string> list{"Instrument", "Reverb", "Delay", "Dynamics", "EQ", "Filter",
        "Distortion", "Amp", "Modulation", "Pitch", "Spatial", "Analyzer", "Mastering", "Restoration",
        "Utility", "Other", "Uncategorized"};
    return list;
}

std::string pluginCategory(std::string_view tags)
{
    const std::vector<std::string> list = tagList(tags);
    if (list.empty())
        return "Uncategorized";
    for (const auto& [tag, category] : rules())
        if (std::find(list.begin(), list.end(), tag) != list.end())
            return std::string(category);
    return "Other";
}

std::vector<PluginInfo> applyProbe(const PluginInfo& file, const ProbeOutput& probe)
{
    if (!probe.ok || probe.plugins.empty()) {
        PluginInfo failed = file;
        failed.probe = PluginInfo::Probe::Failed;
        failed.probeError = probe.ok ? "no plugins inside" : probe.error;
        failed.vendor = normalizeVendor(file.vendor);
        return {failed};
    }
    std::vector<PluginInfo> out;
    out.reserve(probe.plugins.size());
    for (const ProbedPlugin& p : probe.plugins) {
        PluginInfo info = file;
        info.name = p.name;
        info.vendor = normalizeVendor(p.vendor.empty() ? file.vendor : p.vendor);
        info.version = p.version;
        info.tags = p.tags;
        info.category = pluginCategory(p.tags);
        info.probe = PluginInfo::Probe::Ok;
        info.probeError.clear();
        out.push_back(std::move(info));
    }
    return out;
}

bool sameModule(const PluginInfo& a, const PluginInfo& b)
{
    return a.path == b.path && a.format == b.format && a.fileSize == b.fileSize && a.modified == b.modified;
}

const std::vector<std::string>& instrumentTypes()
{
    static const std::vector<std::string> list{"Synth", "Sampler", "Drums", "Keys", "External", "Other"};
    return list;
}

std::string instrumentType(std::string_view tags)
{
    if (pluginCategory(tags) != "Instrument")
        return {};
    const std::vector<std::string> list = tagList(tags);
    const auto has
        = [&](std::string_view tag) { return std::find(list.begin(), list.end(), tag) != list.end(); };
    if (has("drum") || has("drum-machine"))
        return "Drums";
    if (has("piano"))
        return "Keys";
    if (has("sampler"))
        return "Sampler";
    if (has("synth") || has("synthesizer"))
        return "Synth";
    if (has("external"))
        return "External";
    return "Other";
}

std::string effectiveCategory(const PluginInfo& plugin)
{
    return plugin.userCategory.empty() ? plugin.category : plugin.userCategory;
}

std::string effectiveInstrumentType(const PluginInfo& plugin)
{
    if (!plugin.userCategory.empty()) {
        if (plugin.userCategory != "Instrument")
            return {};
        if (!plugin.userInstrumentType.empty())
            return plugin.userInstrumentType;
        // The user made it an instrument without a type: the tags may know one.
        const std::string fromTags = instrumentType(plugin.tags);
        return fromTags.empty() ? "Other" : fromTags;
    }
    return instrumentType(plugin.tags);
}

void carryUserCategories(std::vector<PluginInfo>& fresh, const std::vector<PluginInfo>& earlier)
{
    for (PluginInfo& plugin : fresh) {
        for (const PluginInfo& old : earlier) {
            if (old.userCategory.empty() || old.format != plugin.format || old.path != plugin.path
                || old.name != plugin.name)
                continue;
            plugin.userCategory = old.userCategory;
            plugin.userInstrumentType = old.userInstrumentType;
            break;
        }
    }
}

std::string normalizeVendor(std::string_view vendor)
{
    std::string out(vendor);
    const auto trim = [&] {
        while (!out.empty() && (out.back() == ' ' || out.back() == ',' || out.back() == '.'))
            out.pop_back();
        std::size_t first = 0;
        while (first < out.size() && out[first] == ' ')
            ++first;
        out.erase(0, first);
    };
    trim();
    static const std::array<std::string_view, 9> suffixes{
        " inc", " ltd", " llc", " gmbh", " ag", " co", " limited", " corp", " s.a"};
    bool changed = true;
    while (changed) {
        changed = false;
        const std::string low = lower(out);
        for (const std::string_view suffix : suffixes) {
            if (low.size() > suffix.size() && low.ends_with(suffix)) {
                out.resize(out.size() - suffix.size());
                trim();
                changed = true;
                break;
            }
        }
    }
    return out;
}

} // namespace vloomdaw
