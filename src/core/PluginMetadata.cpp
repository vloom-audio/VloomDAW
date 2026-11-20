// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/PluginMetadata.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <exception>
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
            if (old.format != plugin.format || old.path != plugin.path || old.name != plugin.name)
                continue;
            if (!old.userCategory.empty()) {
                plugin.userCategory = old.userCategory;
                plugin.userInstrumentType = old.userInstrumentType;
            }
            plugin.enabled = old.enabled;
            plugin.favorite = old.favorite;
            break;
        }
    }
}

std::string pluginMainGroup(const PluginInfo& plugin)
{
    return effectiveCategory(plugin) == "Instrument" ? "Instrument" : "Effect";
}

const std::vector<std::string>& pluginSubCategories()
{
    static const std::vector<std::string> list{
        "Drums / Perc",
        "Piano / Keys",
        "Guitar / Bass",
        "Strings / Orch",
        "Sampler / ROMpler",
        "Synth / Lead",
        "Reverb",
        "Delay / Echo",
        "Pitch / Vocal",
        "EQ / Filter",
        "Dynamics",
        "Amp / Drive",
        "Modulation",
        "Channel / Master",
        "Spatial / Imaging",
        "Utility / Meter",
        "Other FX",
    };
    return list;
}

namespace {

bool containsAny(std::string_view haystack, std::initializer_list<std::string_view> needles)
{
    for (const std::string_view needle : needles) {
        if (!needle.empty() && haystack.find(needle) != std::string_view::npos)
            return true;
    }
    return false;
}

} // namespace

std::string detectPluginSubCategory(const PluginInfo& plugin)
{
    const std::string cat = effectiveCategory(plugin);
    const std::string n = lower(plugin.name);
    const std::string v = lower(plugin.vendor);
    const std::vector<std::string> tags = tagList(plugin.tags);
    const auto hasTag
        = [&](std::string_view t) { return std::find(tags.begin(), tags.end(), t) != tags.end(); };

    if (cat == "Instrument") {
        // Explicit user instrument type wins when chosen manually.
        if (plugin.userCategory == "Instrument" && !plugin.userInstrumentType.empty()) {
            const std::string& ut = plugin.userInstrumentType;
            if (ut == "Drums")
                return "Drums / Perc";
            if (ut == "Keys")
                return "Piano / Keys";
            if (ut == "Sampler")
                return "Sampler / ROMpler";
            if (ut == "Synth")
                return "Synth / Lead";
        }

        if (hasTag("drum") || hasTag("drum-machine")
            || containsAny(n,
                {"drum", "ezdrummer", "superior", "addictive drums", "ssd", "bfd", "battery", "groove agent",
                    "atlas", "kick", "snare", "perc", "mt-power", "sitala"}))
            return "Drums / Perc";

        if (containsAny(n,
                {"guitar", "ample", "realguitar", "realstrat", "reallpc", "realrick", "realeight",
                    "shreddage", "odin", "strum", "modo bass", "ezbass", "trilian", "sublab", "scarbee",
                    "virtual guitarist", "virtual bassist", "prominy", "electri6ity", "bass"}))
            return "Guitar / Bass";

        if (hasTag("piano")
            || containsAny(n,
                {"piano", "keys", "keyscape", "pianoteq", "ezkeys", "addictive keys", "noire", "alicia",
                    "grand", "rhodes", "wurli", "organ", "b-3", "clav", "epiano", "ravenscroft", "ivory"}))
            return "Piano / Keys";

        if (containsAny(n,
                {"orchestra", "strings", "violin", "cello", "brass", "choir", "symphobia", "philharmonik",
                    "bbc symphony", "swam"}))
            return "Strings / Orch";

        if (hasTag("sampler")
            || containsAny(n,
                {"kontakt", "halion", "falcon", "uvi", "omnisphere", "nexus", "xpand", "zenology", "jv-1080",
                    "xv-5080", "purity", "sampletank", "arcade", "serato sample", "decent sampler", "sampler",
                    "rompler"}))
            return "Sampler / ROMpler";

        return "Synth / Lead";
    }

    // Effects: check user category override first (and a built-in's own
    // category, which is exact), then probed category + producer name heuristics.
    if ((!plugin.userCategory.empty() && plugin.userCategory != "Other"
            && plugin.userCategory != "Uncategorized")
        || plugin.format == PluginFormat::Builtin) {
        if (cat == "Reverb")
            return "Reverb";
        if (cat == "Delay")
            return "Delay / Echo";
        if (cat == "Pitch")
            return "Pitch / Vocal";
        if (cat == "EQ" || cat == "Filter")
            return "EQ / Filter";
        if (cat == "Dynamics")
            return "Dynamics";
        if (cat == "Amp" || cat == "Distortion")
            return "Amp / Drive";
        if (cat == "Modulation")
            return "Modulation";
        if (cat == "Mastering" || cat == "Restoration")
            return "Channel / Master";
        if (cat == "Spatial")
            return "Spatial / Imaging";
        if (cat == "Analyzer" || cat == "Utility")
            return "Utility / Meter";
    }

    if (cat == "Pitch"
        || containsAny(n,
            {"autotune", "auto-tune", "melodyne", "waves tune", "metatune", "crispytuner", "graillon",
                "alterboy", "vocalign", "nectar", "cla vocals", "rvox", "deesser", "de-esser", "sibilance",
                "pro-ds", "vocal", "doubler", "harmony"})
        || containsAny(v, {"antares", "celemony", "synchro arts"}))
        return "Pitch / Vocal";

    if (cat == "Reverb"
        || containsAny(n,
            {"reverb", "valhalla", "pro-r", "lexicon", "altiverb", "seventh heaven", "raum", "tai chi",
                "plate", "hall", "room", "shimmer", "ambience"}))
        return "Reverb";

    if (cat == "Delay"
        || containsAny(n, {"delay", "echo", "echoboy", "timeless", "repeater", "replika", "h-delay"}))
        return "Delay / Echo";

    if (cat == "EQ" || cat == "Filter"
        || containsAny(n,
            {"pro-q", "equalizer", "pultec", "eqp-1a", "meq", "1073", "1081", "maag", "kirchhoff", "api 550",
                "api 560", "volcano", "soothe", "smooth operator", "gullfoss", "teote", "tonal balance",
                " eq", "-eq", "eq ", "filter"}))
        return "EQ / Filter";

    if (cat == "Dynamics"
        || containsAny(n,
            {"comp", "pro-c", "pro-l", "pro-g", "pro-mb", "1176", "la-2a", "la2a", "cla-76", "cla-2a",
                "cla-3a", "distressor", "fairchild", "dbx", "limiter", "maximizer", "gate", "transient",
                "ott", "shadow hills", "api 2500", "glue"}))
        return "Dynamics";

    if (cat == "Amp" || cat == "Distortion"
        || containsAny(n,
            {"amp", "guitar rig", "amplitube", "archetype", "helix", "bias", "th-u", "tonex", "cab",
                "saturat", "saturn", "decapitator", "distort", "overdrive", "fuzz", "tube", "tape", "j37",
                "kramer", "studer", "oxide", "black box", "rc-20", "thermal", "trash"})
        || containsAny(v, {"neural dsp", "ik multimedia"}))
        return "Amp / Drive";

    if (cat == "Modulation"
        || containsAny(n,
            {"chorus", "flanger", "phaser", "tremolo", "vibrato", "rotary", "leslie", "microshift", "modulat",
                "shaperbox", "kickstart"}))
        return "Modulation";

    if (cat == "Mastering" || cat == "Restoration"
        || containsAny(n,
            {"ozone", "master", "channel", "scheps omni", "ssl", "console", "cla mixhub", "abbey road",
                "linmb", "denoise", "declick", "dehum", "dereverb", "rx "}))
        return "Channel / Master";

    if (cat == "Spatial"
        || containsAny(n, {"imager", "wider", "stereo", "binaural", "atmos", "surround", "dearvr"}))
        return "Spatial / Imaging";

    if (cat == "Analyzer" || cat == "Utility"
        || containsAny(n,
            {"meter", "span", "insight", "youlean", "loudness", "analyzer", "spectrum", "tuner", "gain",
                "trim", "phase", "correlat", "oscilloscope"}))
        return "Utility / Meter";

    return "Other FX";
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

std::string pluginThumbnailId(PluginFormat format, const std::filesystem::path& path, std::string_view name)
{
    std::string rawPath;
    try {
        const std::u8string u8 = path.generic_u8string();
        rawPath.assign(u8.begin(), u8.end());
    } catch (const std::exception&) {
#if defined(_WIN32)
        for (wchar_t wc : path.native()) {
            rawPath.push_back(static_cast<char>(wc & 0xFF));
            rawPath.push_back(static_cast<char>((wc >> 8) & 0xFF));
        }
#else
        rawPath = path.native();
#endif
    }

    std::string normPath;
    normPath.reserve(rawPath.size());
    for (unsigned char ch : rawPath) {
        char c = static_cast<char>(ch);
        if (c == '\\')
            c = '/';
        normPath.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    while (normPath.size() > 1 && normPath.back() == '/')
        normPath.pop_back();

    const char* fmtPrefix = "vst3|";
    if (format == PluginFormat::Clap)
        fmtPrefix = "clap|";
    else if (format == PluginFormat::Vst2)
        fmtPrefix = "vst2|";
    else if (format == PluginFormat::Builtin)
        fmtPrefix = "builtin|";

    const std::string key = std::string(fmtPrefix) + normPath + "|" + lower(name);

    // 64-bit FNV-1a digest rendered as 16 lowercase hex digits.
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char byte : key) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= 1099511628211ULL;
    }

    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex(16, '0');
    for (int i = 15; i >= 0; --i) {
        hex[static_cast<std::size_t>(i)] = kHex[hash & 0x0FULL];
        hash >>= 4U;
    }
    return hex;
}

std::string pluginThumbnailId(const PluginInfo& plugin)
{
    return pluginThumbnailId(plugin.format, plugin.path, plugin.name);
}

bool isValidPluginThumbnailId(std::string_view thumbId) noexcept
{
    if (thumbId.size() != 16)
        return false;
    for (char c : thumbId) {
        const bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!isHex)
            return false;
    }
    return true;
}

} // namespace vloomdaw
