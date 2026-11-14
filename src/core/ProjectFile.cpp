// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/ProjectFile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <system_error>

namespace vloomdaw {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaxNameBytes = 256;
constexpr std::size_t kMaxPathBytes = 4096;
constexpr std::size_t kMaxIdBytes = 512;

// ---------------------------------------------------------------- base64

constexpr std::string_view kBase64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string toBase64(const std::vector<std::uint8_t>& bytes)
{
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::uint32_t chunk = static_cast<std::uint32_t>(bytes[i]) << 16
            | (i + 1 < bytes.size() ? static_cast<std::uint32_t>(bytes[i + 1]) << 8 : 0u)
            | (i + 2 < bytes.size() ? static_cast<std::uint32_t>(bytes[i + 2]) : 0u);
        out += kBase64[(chunk >> 18) & 63];
        out += kBase64[(chunk >> 12) & 63];
        out += i + 1 < bytes.size() ? kBase64[(chunk >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? kBase64[chunk & 63] : '=';
    }
    return out;
}

/// Strict base64 (no whitespace); nullopt if malformed.
std::optional<std::vector<std::uint8_t>> fromBase64(std::string_view text)
{
    if (text.size() % 4 != 0)
        return std::nullopt;
    std::array<int, 256> value{};
    value.fill(-1);
    for (std::size_t i = 0; i < kBase64.size(); ++i)
        value[static_cast<unsigned char>(kBase64[i])] = static_cast<int>(i);
    std::vector<std::uint8_t> out;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        const bool last = i + 4 == text.size();
        int pad = 0;
        std::uint32_t chunk = 0;
        for (std::size_t k = 0; k < 4; ++k) {
            const char ch = text[i + k];
            if (ch == '=' && last && k >= 2) {
                ++pad;
                chunk <<= 6;
                continue;
            }
            const int v = value[static_cast<unsigned char>(ch)];
            if (v < 0 || pad > 0)
                return std::nullopt;
            chunk = chunk << 6 | static_cast<std::uint32_t>(v);
        }
        out.push_back(static_cast<std::uint8_t>(chunk >> 16));
        if (pad < 2)
            out.push_back(static_cast<std::uint8_t>(chunk >> 8));
        if (pad < 1)
            out.push_back(static_cast<std::uint8_t>(chunk));
    }
    return out;
}

// ---------------------------------------------------------------- names for enums

struct ScaleName {
    Scale scale;
    std::string_view name;
};
constexpr std::array<ScaleName, 13> kScaleNames{{
    {Scale::Major, "major"},
    {Scale::NaturalMinor, "natural-minor"},
    {Scale::HarmonicMinor, "harmonic-minor"},
    {Scale::MelodicMinor, "melodic-minor"},
    {Scale::Dorian, "dorian"},
    {Scale::Phrygian, "phrygian"},
    {Scale::Lydian, "lydian"},
    {Scale::Mixolydian, "mixolydian"},
    {Scale::Locrian, "locrian"},
    {Scale::MajorPentatonic, "major-pentatonic"},
    {Scale::MinorPentatonic, "minor-pentatonic"},
    {Scale::Blues, "blues"},
    {Scale::Chromatic, "chromatic"},
}};

std::string_view scaleName(Scale scale)
{
    for (const ScaleName& s : kScaleNames)
        if (s.scale == scale)
            return s.name;
    return "major";
}

std::string_view fitName(AudioClip::Fit fit)
{
    switch (fit) {
    case AudioClip::Fit::Stretch:
        return "stretch";
    case AudioClip::Fit::Resample:
        return "resample";
    default:
        return "off";
    }
}

std::string_view pluginFormatText(PluginFormat format)
{
    return format == PluginFormat::Clap ? "clap" : "vst3";
}

std::optional<std::string> colorText(const std::optional<std::uint32_t>& color)
{
    if (!color)
        return std::nullopt;
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out = "#";
    for (int shift = 20; shift >= 0; shift -= 4)
        out += kHex[(*color >> shift) & 0xF];
    return out;
}

// ---------------------------------------------------------------- paths

/// `path` relative to `folder` with forward slashes when it lies inside it;
/// otherwise the absolute path as given.
std::string samplePathText(const std::string& path, const std::filesystem::path& folder)
{
    if (path.empty() || folder.empty())
        return path;
    const std::filesystem::path file = pathFromUtf8(path).lexically_normal();
    const std::filesystem::path relative = file.lexically_relative(folder.lexically_normal());
    if (relative.empty() || relative == "." || *relative.begin() == "..")
        return path;
    const std::u8string generic = relative.generic_u8string();
    return {generic.begin(), generic.end()};
}

/// A path from a network share (\\server\share, //server/share): opening one
/// can leak the user's Windows credentials to that server.
bool isNetworkPath(std::string_view path)
{
    return path.size() >= 2 && (path[0] == '\\' || path[0] == '/') && (path[1] == '\\' || path[1] == '/');
}

// ---------------------------------------------------------------- reading helpers

/// Reads checked values out of one JSON object; problems become warnings.
class Reader {
public:
    explicit Reader(std::vector<std::string>& warnings)
        : warnings_(warnings)
    {
    }

    template <typename T> T number(const Json& object, const char* key, T fallback, T low, T high)
    {
        const auto it = object.find(key);
        if (it == object.end() || !it->is_number())
            return fallback;
        const double v = it->get<double>();
        if (!std::isfinite(v))
            return fallback;
        const double clampedValue = std::clamp(v, static_cast<double>(low), static_cast<double>(high));
        if constexpr (std::is_integral_v<T>)
            return static_cast<T>(std::llround(clampedValue));
        else
            return static_cast<T>(clampedValue);
    }

    static bool flag(const Json& object, const char* key, bool fallback = false)
    {
        const auto it = object.find(key);
        return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
    }

    std::string text(const Json& object, const char* key, std::size_t maxBytes)
    {
        const auto it = object.find(key);
        if (it == object.end() || !it->is_string())
            return {};
        const auto& s = it->get_ref<const std::string&>();
        if (s.size() > maxBytes) {
            warn(std::string("A ") + key + " that was too long was left out");
            return {};
        }
        return s;
    }

    std::optional<std::uint32_t> color(const Json& object, const char* key)
    {
        const std::string s = text(object, key, 7);
        if (s.size() != 7 || s[0] != '#')
            return std::nullopt;
        std::uint32_t value = 0;
        for (std::size_t i = 1; i < 7; ++i) {
            const char c = s[i];
            const int digit = c >= '0' && c <= '9' ? c - '0'
                : c >= 'a' && c <= 'f'             ? c - 'a' + 10
                : c >= 'A' && c <= 'F'             ? c - 'A' + 10
                                                   : -1;
            if (digit < 0)
                return std::nullopt;
            value = value << 4 | static_cast<std::uint32_t>(digit);
        }
        return value;
    }

    /// The items of the array at `key` (none if it is missing); at most
    /// `max` are kept.
    std::vector<const Json*> array(const Json& object, const char* key, std::size_t max)
    {
        std::vector<const Json*> items;
        const auto it = object.find(key);
        if (it == object.end() || !it->is_array())
            return items;
        if (it->size() > max)
            warn(std::string("The file has more ") + key + " than VloomDAW opens; the rest were left out");
        const std::size_t count = std::min(it->size(), max);
        items.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
            items.push_back(&(*it)[i]);
        return items;
    }

    void warn(std::string message)
    {
        if (std::find(warnings_.begin(), warnings_.end(), message) == warnings_.end()
            && warnings_.size() < 50)
            warnings_.push_back(std::move(message));
    }

private:
    std::vector<std::string>& warnings_;
};

/// The deepest nesting of arrays and objects in `text`, stopping early past
/// `limit`; strings are skipped (brackets in them do not count).
int nesting(std::string_view text, int limit)
{
    int depth = 0;
    int deepest = 0;
    bool inString = false;
    bool escaped = false;
    for (const char c : text) {
        if (inString) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                inString = false;
            continue;
        }
        if (c == '"')
            inString = true;
        else if (c == '[' || c == '{')
            deepest = std::max(deepest, ++depth);
        else if (c == ']' || c == '}')
            --depth;
        if (deepest > limit)
            break;
    }
    return deepest;
}

std::optional<PluginSlot> readPlugin(const Json& object, Reader& in, std::vector<std::uint8_t>& state)
{
    if (!object.is_object())
        return std::nullopt;
    PluginSlot slot;
    const std::string format = in.text(object, "format", 8);
    if (format == "clap")
        slot.format = PluginFormat::Clap;
    else if (format == "vst3")
        slot.format = PluginFormat::Vst3;
    else {
        in.warn("A plugin of an unknown format was left out");
        return std::nullopt;
    }
    slot.path = in.text(object, "path", kMaxPathBytes);
    slot.id = in.text(object, "id", kMaxIdBytes);
    slot.name = in.text(object, "name", kMaxNameBytes);
    slot.bypassed = Reader::flag(object, "bypassed");
    if (slot.path.empty() || slot.id.empty()) {
        in.warn("A plugin without a path or id was left out");
        return std::nullopt;
    }
    const std::string encoded = in.text(object, "state", (ProjectFile::kMaxStateBytes + 2) / 3 * 4);
    if (!encoded.empty()) {
        if (auto bytes = fromBase64(encoded))
            state = std::move(*bytes);
        else
            in.warn("The saved settings of " + slot.name + " could not be read; it starts at its defaults");
    }
    return slot;
}

Json pluginJson(const PluginSlot& slot, const std::vector<std::uint8_t>* state)
{
    Json p = {{"format", pluginFormatText(slot.format)}, {"path", slot.path}, {"id", slot.id},
        {"name", slot.name}, {"bypassed", slot.bypassed}};
    if (state != nullptr && !state->empty())
        p["state"] = toBase64(*state);
    return p;
}

template <typename T> const T* at(const std::vector<T>& list, std::size_t index)
{
    return index < list.size() ? &list[index] : nullptr;
}

} // namespace

std::filesystem::path pathFromUtf8(std::string_view utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string pathToUtf8(const std::filesystem::path& path)
{
    const std::u8string s = path.u8string();
    return {s.begin(), s.end()};
}

// ---------------------------------------------------------------- write

std::string ProjectFile::write(const ProjectData& data, const std::filesystem::path& folder)
{
    const Project& project = data.project;
    Json root;
    root["format"] = kFormat;
    root["version"] = kVersion;
    root["app"] = std::string("VloomDAW ") + VLOOMDAW_VERSION;
    root["song"] = {{"tempo", data.song.tempo}, {"key", data.song.keyRoot},
        {"scale", scaleName(data.song.scale)}, {"songMode", data.song.songMode}};

    Json channels = Json::array();
    for (std::size_t c = 0; c < project.channels().size(); ++c) {
        const Channel& ch = project.channels()[c];
        Json j = {{"name", ch.name}, {"muted", ch.muted}, {"sample", samplePathText(ch.samplePath, folder)},
            {"volume", ch.volume}, {"pan", ch.pan}, {"insert", ch.insert}};
        if (const auto color = colorText(ch.color))
            j["color"] = *color;
        if (ch.instrument)
            j["instrument"] = pluginJson(*ch.instrument, at(data.states.instruments, c));
        channels.push_back(std::move(j));
    }
    root["channels"] = std::move(channels);

    Json patterns = Json::array();
    for (const Pattern& pattern : project.patterns()) {
        Json perChannel = Json::array();
        for (const ChannelPattern& cp : pattern.channels) {
            std::string steps;
            steps.reserve(cp.steps.size());
            for (const bool on : cp.steps)
                steps += on ? 'x' : '.';
            Json notes = Json::array();
            for (const Note& n : cp.notes)
                notes.push_back(Json::array({n.key, n.start, n.length}));
            perChannel.push_back({{"steps", steps}, {"notes", std::move(notes)}});
        }
        patterns.push_back(
            {{"name", pattern.name}, {"steps", pattern.steps}, {"channels", std::move(perChannel)}});
    }
    root["patterns"] = std::move(patterns);

    Json tracks = Json::array();
    for (const PlaylistTrack& t : project.tracks()) {
        Json j = {{"name", t.name}, {"named", t.named}, {"muted", t.muted}, {"solo", t.solo},
            {"height", t.height}, {"depth", t.depth}, {"collapsed", t.collapsed}};
        if (const auto color = colorText(t.color))
            j["color"] = *color;
        tracks.push_back(std::move(j));
    }
    root["tracks"] = std::move(tracks);

    Json clips = Json::array();
    for (const Clip& c : project.clips())
        clips.push_back({{"pattern", c.pattern}, {"track", c.track}, {"start", c.start}, {"muted", c.muted},
            {"offset", c.offset}, {"length", c.length}});
    root["clips"] = std::move(clips);

    Json audioClips = Json::array();
    for (const AudioClip& c : project.audioClips())
        audioClips.push_back({{"channel", c.channel}, {"track", c.track}, {"start", c.start},
            {"muted", c.muted}, {"offset", c.offset}, {"length", c.length}, {"fit", fitName(c.fit)},
            {"sourceBpm", c.sourceBpm}});
    root["audioClips"] = std::move(audioClips);

    Json mixer = Json::array();
    for (std::size_t i = 0; i < data.mixer.size(); ++i) {
        const MixerTrack& t = data.mixer.track(i);
        Json effects = Json::array();
        const auto* states = at(data.states.effects, i);
        for (std::size_t e = 0; e < t.effects.size(); ++e)
            effects.push_back(pluginJson(t.effects[e], states != nullptr ? at(*states, e) : nullptr));
        Json j = {{"name", t.name}, {"named", t.named}, {"fader", t.fader}, {"pan", t.pan},
            {"muted", t.muted}, {"soloed", t.soloed}, {"effects", std::move(effects)}};
        if (const auto color = colorText(t.color))
            j["color"] = *color;
        mixer.push_back(std::move(j));
    }
    root["mixer"] = std::move(mixer);
    return root.dump(1, '\t') + "\n";
}

// ---------------------------------------------------------------- read

namespace {
ProjectFile::ReadResult readUnguarded(std::string_view text, const std::filesystem::path& folder);
} // namespace

ProjectFile::ReadResult ProjectFile::read(std::string_view text, const std::filesystem::path& folder)
{
    // Every value is type-checked before it is read; this is the backstop,
    // so a file can never take the app down through an exception.
    try {
        return readUnguarded(text, folder);
    } catch (const std::exception&) {
        ReadResult result;
        result.error = "The project file is damaged";
        return result;
    }
}

namespace {
ProjectFile::ReadResult readUnguarded(std::string_view text, const std::filesystem::path& folder)
{
    using ReadResult = ProjectFile::ReadResult;
    constexpr auto kMaxFileBytes = ProjectFile::kMaxFileBytes;
    constexpr auto kMaxDepth = ProjectFile::kMaxDepth;
    constexpr auto kVersion = ProjectFile::kVersion;
    constexpr auto kFormat = ProjectFile::kFormat;
    constexpr auto kMaxChannels = ProjectFile::kMaxChannels;
    constexpr auto kMaxNotes = ProjectFile::kMaxNotes;
    constexpr auto kMaxClips = ProjectFile::kMaxClips;
    ReadResult result;
    const auto fail = [&result](std::string message) {
        result.error = std::move(message);
        return std::move(result);
    };
    if (text.size() > kMaxFileBytes)
        return fail("The project file is too large");
    if (nesting(text, kMaxDepth) > kMaxDepth)
        return fail("The project file is damaged (nested too deeply)");
    const Json root = Json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object())
        return fail("This is not a VloomDAW project, or the file is damaged");
    const auto format = root.find("format");
    if (format == root.end() || !format->is_string() || format->get_ref<const std::string&>() != kFormat)
        return fail("This is not a VloomDAW project");
    const auto version = root.find("version");
    if (version == root.end() || !version->is_number_integer())
        return fail("The project file has no version");
    if (version->get<std::int64_t>() > kVersion)
        return fail("This project was saved by a newer VloomDAW; update VloomDAW to open it");
    if (version->get<std::int64_t>() < 1)
        return fail("The project file's version is not valid");

    Reader in(result.warnings);
    ProjectData data;

    if (const auto song = root.find("song"); song != root.end() && song->is_object()) {
        data.song.tempo = in.number<double>(*song, "tempo", 120.0, 10.0, 999.0);
        data.song.keyRoot = in.number<int>(*song, "key", 0, 0, 11);
        const std::string scale = in.text(*song, "scale", 32);
        for (const ScaleName& s : kScaleNames)
            if (s.name == scale)
                data.song.scale = s.scale;
        data.song.songMode = Reader::flag(*song, "songMode");
    }

    std::vector<Channel> channels;
    for (const Json* jp : in.array(root, "channels", kMaxChannels)) {
        const Json& j = *jp;
        if (!j.is_object())
            continue;
        Channel c;
        c.name = in.text(j, "name", kMaxNameBytes);
        c.muted = Reader::flag(j, "muted");
        std::string sample = in.text(j, "sample", kMaxPathBytes);
        if (isNetworkPath(sample)) {
            in.warn("Samples on network shares were left out: " + sample);
            sample.clear();
        } else if (!sample.empty()) {
            const std::filesystem::path p = pathFromUtf8(sample);
            if (p.is_relative() && !folder.empty())
                sample = pathToUtf8((folder / p).lexically_normal());
        }
        c.samplePath = sample;
        c.volume = in.number<double>(j, "volume", 0.8, 0.0, 1.0);
        c.pan = in.number<double>(j, "pan", 0.0, -1.0, 1.0);
        c.insert = in.number<int>(j, "insert", 0, 0, static_cast<int>(MixerModel::kMaxInserts));
        c.color = in.color(j, "color");
        std::vector<std::uint8_t> state;
        if (const auto inst = j.find("instrument"); inst != j.end())
            c.instrument = readPlugin(*inst, in, state);
        data.states.instruments.push_back(c.instrument ? std::move(state) : std::vector<std::uint8_t>{});
        channels.push_back(std::move(c));
    }

    std::vector<Pattern> patterns;
    for (const Json* jp : in.array(root, "patterns", Project::kMaxPatterns)) {
        const Json& j = *jp;
        if (!j.is_object())
            continue;
        Pattern p;
        p.name = in.text(j, "name", kMaxNameBytes);
        p.steps = in.number<int>(j, "steps", kStepsPerBar, 1, Project::kMaxSteps);
        for (const Json* cjp : in.array(j, "channels", kMaxChannels)) {
            const Json& cj = *cjp;
            ChannelPattern cp;
            if (cj.is_object()) {
                const std::string steps = in.text(cj, "steps", static_cast<std::size_t>(Project::kMaxSteps));
                cp.steps.assign(steps.size(), false);
                for (std::size_t s = 0; s < steps.size(); ++s)
                    cp.steps[s] = steps[s] == 'x';
                for (const Json* np : in.array(cj, "notes", kMaxNotes)) {
                    const Json& n = *np;
                    if (!n.is_array() || n.size() != 3 || !n[0].is_number() || !n[1].is_number()
                        || !n[2].is_number())
                        continue;
                    const auto value = [](const Json& v) {
                        const double d = v.get<double>();
                        return std::isfinite(d)
                            ? static_cast<int>(std::clamp(
                                  d, 0.0, static_cast<double>(std::numeric_limits<int>::max() / 2)))
                            : 0;
                    };
                    cp.notes.push_back(Note{value(n[0]), value(n[1]), value(n[2])});
                }
            }
            p.channels.push_back(std::move(cp));
        }
        patterns.push_back(std::move(p));
    }

    std::vector<PlaylistTrack> tracks;
    for (const Json* jp : in.array(root, "tracks", static_cast<std::size_t>(Project::kMaxPlaylistTracks))) {
        const Json& j = *jp;
        if (!j.is_object())
            continue;
        PlaylistTrack t;
        t.name = in.text(j, "name", kMaxNameBytes);
        t.named = Reader::flag(j, "named");
        t.muted = Reader::flag(j, "muted");
        t.solo = Reader::flag(j, "solo");
        t.height = in.number<int>(
            j, "height", PlaylistTrack::kDefaultHeight, PlaylistTrack::kMinHeight, PlaylistTrack::kMaxHeight);
        t.depth = in.number<int>(j, "depth", 0, 0, PlaylistTrack::kMaxDepth);
        t.collapsed = Reader::flag(j, "collapsed");
        t.color = in.color(j, "color");
        tracks.push_back(std::move(t));
    }

    constexpr int kMaxTick = std::numeric_limits<int>::max() / 2;
    std::vector<Clip> clips;
    for (const Json* jp : in.array(root, "clips", kMaxClips)) {
        const Json& j = *jp;
        if (!j.is_object())
            continue;
        Clip c;
        c.pattern = in.number<std::size_t>(j, "pattern", 0, 0, Project::kMaxPatterns);
        c.track = in.number<int>(j, "track", 0, 0, Project::kMaxPlaylistTracks);
        c.start = in.number<int>(j, "start", 0, 0, kMaxTick);
        c.muted = Reader::flag(j, "muted");
        c.offset = in.number<int>(j, "offset", 0, 0, kMaxTick);
        c.length = in.number<int>(j, "length", 0, 0, kMaxTick);
        clips.push_back(c);
    }

    std::vector<AudioClip> audioClips;
    for (const Json* jp : in.array(root, "audioClips", kMaxClips)) {
        const Json& j = *jp;
        if (!j.is_object())
            continue;
        AudioClip c;
        c.channel = in.number<std::size_t>(j, "channel", 0, 0, kMaxChannels);
        c.track = in.number<int>(j, "track", 0, 0, Project::kMaxPlaylistTracks);
        c.start = in.number<int>(j, "start", 0, 0, kMaxTick);
        c.muted = Reader::flag(j, "muted");
        c.offset = in.number<int>(j, "offset", 0, 0, kMaxTick);
        c.length = in.number<int>(j, "length", 0, 0, kMaxTick);
        const std::string fit = in.text(j, "fit", 16);
        c.fit = fit == "stretch" ? AudioClip::Fit::Stretch
            : fit == "resample"  ? AudioClip::Fit::Resample
                                 : AudioClip::Fit::Off;
        c.sourceBpm = in.number<double>(j, "sourceBpm", 0.0, 0.0, Project::kMaxSourceBpm);
        audioClips.push_back(c);
    }

    data.project = Project::restored(channels, patterns, tracks, clips, audioClips);

    // The mixer: the master first, then the inserts.
    const std::vector<const Json*> mixer = in.array(root, "mixer", MixerModel::kMaxInserts + 1);
    data.mixer = MixerModel(mixer.empty() ? 0 : mixer.size() - 1);
    for (std::size_t i = 0; i < mixer.size(); ++i) {
        const Json& j = *mixer[i];
        std::vector<std::vector<std::uint8_t>> states;
        if (j.is_object()) {
            if (Reader::flag(j, "named"))
                data.mixer.setName(i, in.text(j, "name", kMaxNameBytes));
            data.mixer.setFader(i, in.number<double>(j, "fader", 0.8, 0.0, 1.0));
            data.mixer.setPan(i, in.number<double>(j, "pan", 0.0, -1.0, 1.0));
            data.mixer.setMuted(i, Reader::flag(j, "muted"));
            data.mixer.setSoloed(i, Reader::flag(j, "soloed"));
            data.mixer.setColor(i, in.color(j, "color"));
            for (const Json* ep : in.array(j, "effects", MixerModel::kMaxEffects)) {
                const Json& e = *ep;
                std::vector<std::uint8_t> state;
                if (auto slot = readPlugin(e, in, state); slot && data.mixer.addEffect(i, std::move(*slot)))
                    states.push_back(std::move(state));
            }
        }
        data.states.effects.push_back(std::move(states));
    }
    if (mixer.empty())
        data.states.effects.emplace_back(); // the master

    // A channel plays into an insert the mixer has; otherwise into the master.
    for (std::size_t c = 0; c < data.project.channels().size(); ++c)
        if (static_cast<std::size_t>(data.project.channels()[c].insert) > data.mixer.insertCount()) {
            data.project.setChannelInsert(c, 0);
            in.warn("A channel routed to a mixer insert that does not exist now plays into the master");
        }

    result.data = std::move(data);
    return result;
}
} // namespace

// ---------------------------------------------------------------- files

bool ProjectFile::save(const std::filesystem::path& path, const ProjectData& data, std::string* error)
{
    const auto fail = [error](std::string message) {
        if (error != nullptr)
            *error = std::move(message);
        return false;
    };
    const std::string text = write(data, path.parent_path());
    std::filesystem::path temp = path;
    temp += u8".saving";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
            return fail("Could not write to " + pathToUtf8(path.parent_path()));
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out)
            return fail("Could not write the project (is the disk full?)");
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return fail("Could not replace " + pathToUtf8(path.filename()));
    }
    return true;
}

ProjectFile::ReadResult ProjectFile::load(const std::filesystem::path& path)
{
    ReadResult result;
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        result.error = "Could not open " + pathToUtf8(path.filename());
        return result;
    }
    if (size > kMaxFileBytes) {
        result.error = "The project file is too large";
        return result;
    }
    std::ifstream file(path, std::ios::binary);
    std::string text(static_cast<std::size_t>(size), '\0');
    if (!file || !file.read(text.data(), static_cast<std::streamsize>(size))) {
        result.error = "Could not read " + pathToUtf8(path.filename());
        return result;
    }
    return read(text, path.parent_path());
}

// ---------------------------------------------------------------- summaries

ProjectSummary ProjectFile::summarize(const ProjectData& data)
{
    const Project& p = data.project;
    ProjectSummary summary;
    summary.tempo = data.song.tempo;
    summary.channels = p.channels().size();
    for (const Channel& c : p.channels())
        summary.plugins += c.instrument ? 1 : 0;
    for (std::size_t i = 0; i < data.mixer.size(); ++i)
        summary.plugins += data.mixer.track(i).effects.size();

    // An audio clip's length depends on its sample: a bar stands in for it.
    const auto audioLength = [](const AudioClip& c) { return c.length > 0 ? c.length : kTicksPerBar; };
    std::int64_t end = 0;
    for (const Clip& c : p.clips())
        end = std::max<std::int64_t>(end, std::int64_t{c.start} + p.clipLength(c));
    for (const AudioClip& c : p.audioClips())
        end = std::max<std::int64_t>(end, std::int64_t{c.start} + audioLength(c));
    summary.bars = static_cast<int>((end + kTicksPerBar - 1) / kTicksPerBar);
    if (end <= 0)
        return summary;
    const auto place = [&](int track, std::int64_t start, std::int64_t length, std::size_t pattern) {
        if (track >= ProjectSummary::kLanes || summary.blocks.size() >= ProjectSummary::kMaxBlocks)
            return;
        summary.blocks.push_back({track, static_cast<double>(start) / static_cast<double>(end),
            static_cast<double>(length) / static_cast<double>(end), pattern});
    };
    for (const Clip& c : p.clips())
        place(c.track, c.start, p.clipLength(c), c.pattern);
    for (const AudioClip& c : p.audioClips())
        place(c.track, c.start, audioLength(c), ProjectSummary::kAudio);
    return summary;
}

std::optional<ProjectSummary> ProjectFile::summarize(
    const std::filesystem::path& path, std::uintmax_t maxBytes)
{
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec || size > maxBytes)
        return std::nullopt;
    const ReadResult read = load(path);
    if (!read.data)
        return std::nullopt;
    return summarize(*read.data);
}

} // namespace vloomdaw
