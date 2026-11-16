// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/ProjectFile.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>

using namespace vloomdaw;

namespace {

// Absolute paths as this platform writes them ("C:/x" is relative on POSIX).
#if defined(_WIN32)
const std::string kHere = "C:/Music/";
const std::string kElsewhere = "D:/Packs/";
const std::string kMovedTo = "E:/Backup/";
#else
const std::string kHere = "/home/me/music/";
const std::string kElsewhere = "/mnt/packs/";
const std::string kMovedTo = "/media/backup/";
#endif
const std::filesystem::path kFolder = pathFromUtf8(kHere + "My Song");

PluginSlot plugin(PluginFormat format, std::string name)
{
    PluginSlot slot;
    slot.format = format;
    slot.path = "C:/Program Files/Common Files/VST3/" + name + ".vst3";
    slot.id = "ID-" + name;
    slot.name = name;
    slot.instance = 42; // runtime only: never saved
    return slot;
}

ProjectData sample()
{
    ProjectData data;
    Project& p = data.project;
    p.setChannelSample(0, pathToUtf8(kFolder / "samples" / "kick.wav"));
    p.setChannelSample(1, kElsewhere + "clap.wav"); // outside the folder
    p.setChannelVolume(1, 0.5);
    p.setChannelPan(1, -0.25);
    p.setChannelInsert(1, 2);
    p.setChannelColor(2, 0x12ab34);
    p.setChannelMuted(3, true);
    p.setChannelInstrument(3, plugin(PluginFormat::Vst3, "Sylenth1"));
    p.setStep(0, 0, 0, true);
    p.setStep(0, 0, 4, true);
    p.addNote(0, 3, Note{64, 0, 480});
    p.addNote(0, 3, Note{67, 960, 240, 64}); // with a velocity of its own
    const std::size_t second = p.addPattern();
    p.setPatternName(second, "Chorus");
    p.setStepCount(second, 32);
    p.addTrack();
    p.setTrackName(1, "Drums");
    p.setTrackColor(1, 0xff8a3d);
    p.setTrackHeight(1, 60);
    p.setTrackVolume(1, 0.7);
    p.setTrackPan(1, -0.3);
    p.addClip(Clip{second, 1, 3840, false, 0, 0});
    AudioClip audio;
    audio.channel = 0;
    audio.track = 0;
    audio.start = 960;
    audio.fit = AudioClip::Fit::Stretch;
    audio.sourceBpm = 140.0;
    p.addAudioClip(audio);

    data.mixer = MixerModel(3);
    data.mixer.setName(2, "Bass");
    data.mixer.setFader(2, 0.6);
    data.mixer.setColor(2, 0x5eead4);
    data.mixer.addEffect(2, plugin(PluginFormat::Clap, "Saturn"));
    data.mixer.addEffect(MixerModel::kMaster, plugin(PluginFormat::Vst3, "Pro-L 2"));
    data.mixer.setEffectBypassed(MixerModel::kMaster, 0, true);

    data.song = SongSettings{140.0, 9, Scale::NaturalMinor, true};
    data.states.instruments = {{}, {}, {}, {1, 2, 3, 4, 5}};
    data.states.effects = {{{0xff, 0x00}}, {}, {{9}}, {}};
    return data;
}

} // namespace

TEST_CASE("A project survives a save and an open", "[project-file]")
{
    const ProjectData saved = sample();
    const std::string text = ProjectFile::write(saved, kFolder);
    CHECK(text.find("\"samples/kick.wav\"") != std::string::npos); // relative inside the folder
    CHECK(text.find(kElsewhere + "clap.wav") != std::string::npos); // absolute outside it
    CHECK(text.find("42") == std::string::npos); // no runtime instance handles

    const auto read = ProjectFile::read(text, kFolder);
    INFO(read.error);
    REQUIRE(read.data.has_value());
    CHECK(read.warnings.empty());
    const ProjectData& opened = *read.data;
    const Project& a = saved.project;
    const Project& b = opened.project;

    REQUIRE(b.channels().size() == a.channels().size());
    for (std::size_t c = 0; c < a.channels().size(); ++c) {
        CAPTURE(c);
        const Channel& x = a.channels()[c];
        const Channel& y = b.channels()[c];
        CHECK(y.name == x.name);
        CHECK(y.muted == x.muted);
        CHECK(pathFromUtf8(y.samplePath).lexically_normal() == pathFromUtf8(x.samplePath).lexically_normal());
        CHECK(y.volume == x.volume);
        CHECK(y.pan == x.pan);
        CHECK(y.insert == x.insert);
        CHECK(y.color == x.color);
        CHECK(y.instrument.has_value() == x.instrument.has_value());
    }
    REQUIRE(b.channels()[3].instrument);
    CHECK(b.channels()[3].instrument->name == "Sylenth1");
    CHECK(b.channels()[3].instrument->instance == 0);

    REQUIRE(b.patterns().size() == 2);
    CHECK(b.patterns()[1].name == "Chorus");
    CHECK(b.stepCount(1) == 32);
    CHECK(b.step(0, 0, 0));
    CHECK(b.step(0, 0, 4));
    CHECK_FALSE(b.step(0, 0, 1));
    REQUIRE(b.notes(0, 3) != nullptr);
    CHECK(*b.notes(0, 3) == *a.notes(0, 3));

    REQUIRE(b.tracks().size() == 2);
    CHECK(b.tracks()[1].name == "Drums");
    CHECK(b.tracks()[1].named);
    CHECK(b.tracks()[1].height == 60);
    CHECK(b.tracks()[1].volume == 0.7);
    CHECK(b.tracks()[1].pan == -0.3);
    CHECK(b.tracks()[1].color == a.tracks()[1].color);
    CHECK(b.clips() == a.clips());
    CHECK(b.audioClips() == a.audioClips());

    REQUIRE(opened.mixer.size() == 4);
    CHECK(opened.mixer.track(2).name == "Bass");
    CHECK(opened.mixer.track(2).fader == 0.6);
    CHECK(opened.mixer.track(2).color == 0x5eead4u);
    REQUIRE(opened.mixer.track(2).effects.size() == 1);
    CHECK(opened.mixer.track(2).effects[0].format == PluginFormat::Clap);
    REQUIRE(opened.mixer.track(0).effects.size() == 1);
    CHECK(opened.mixer.track(0).effects[0].bypassed);

    CHECK(opened.song.tempo == 140.0);
    CHECK(opened.song.keyRoot == 9);
    CHECK(opened.song.scale == Scale::NaturalMinor);
    CHECK(opened.song.songMode);

    REQUIRE(opened.states.instruments.size() == 4);
    CHECK(opened.states.instruments[3] == std::vector<std::uint8_t>{1, 2, 3, 4, 5});
    REQUIRE(opened.states.effects.size() == 4);
    CHECK(opened.states.effects[0] == std::vector<std::vector<std::uint8_t>>{{0xff, 0x00}});
    CHECK(opened.states.effects[2] == std::vector<std::vector<std::uint8_t>>{{9}});
}

TEST_CASE("A moved project finds its samples in its new folder", "[project-file]")
{
    const std::string text = ProjectFile::write(sample(), kFolder);
    const auto moved = ProjectFile::read(text, pathFromUtf8(kMovedTo + "My Song"));
    REQUIRE(moved.data);
    CHECK(pathFromUtf8(moved.data->project.channels()[0].samplePath)
        == pathFromUtf8(kMovedTo + "My Song/samples/kick.wav").lexically_normal());
    CHECK(moved.data->project.channels()[1].samplePath == kElsewhere + "clap.wav");
}

TEST_CASE("Plugin states of every size round-trip through base64", "[project-file]")
{
    for (std::size_t size = 0; size < 8; ++size) {
        CAPTURE(size);
        ProjectData data;
        data.project.setChannelInstrument(0, plugin(PluginFormat::Clap, "Synth"));
        std::vector<std::uint8_t> state(size);
        for (std::size_t i = 0; i < size; ++i)
            state[i] = static_cast<std::uint8_t>(250 + i);
        data.states.instruments = {state};
        const auto read = ProjectFile::read(ProjectFile::write(data, kFolder), kFolder);
        REQUIRE(read.data);
        CHECK(read.data->states.instruments.at(0) == state);
    }
}

TEST_CASE("Files that are not projects, or from a newer VloomDAW, are refused", "[project-file]")
{
    CHECK_FALSE(ProjectFile::read("", kFolder).data);
    CHECK_FALSE(ProjectFile::read("not json", kFolder).data);
    CHECK_FALSE(ProjectFile::read("[1,2,3]", kFolder).data);
    CHECK_FALSE(ProjectFile::read(R"({"format":"something-else","version":1})", kFolder).data);
    CHECK_FALSE(ProjectFile::read(R"({"format":"vloomdaw-project"})", kFolder).data);
    CHECK_FALSE(ProjectFile::read(R"({"format":"vloomdaw-project","version":0})", kFolder).data);
    // Values of the wrong type anywhere must not throw (review finding: a
    // number as "format" crashed File > Open and the launcher's preview).
    CHECK_FALSE(ProjectFile::read(R"({"format":5,"version":1})", kFolder).data);
    CHECK_FALSE(ProjectFile::read(R"({"format":"vloomdaw-project","version":"1"})", kFolder).data);
    const auto wrongTypes = ProjectFile::read(R"({"format":"vloomdaw-project","version":1,
        "song":[1],"channels":{"a":1},"patterns":"x","tracks":7,"clips":null,"audioClips":true,
        "mixer":[5,"x",{"effects":"none","name":3,"fader":"loud"}]})",
        kFolder);
    REQUIRE(wrongTypes.data);
    CHECK(wrongTypes.data->mixer.size() == 3);
    const auto newer = ProjectFile::read(R"({"format":"vloomdaw-project","version":99})", kFolder);
    CHECK_FALSE(newer.data);
    CHECK(newer.error.find("newer") != std::string::npos);

    // Nesting deep enough to exhaust a recursive parser's stack.
    const std::string deep = R"({"format":"vloomdaw-project","version":1,"x":)" + std::string(100000, '[');
    CHECK_FALSE(ProjectFile::read(deep, kFolder).data);

    // The smallest valid project: defaults everywhere.
    const auto minimal = ProjectFile::read(R"({"format":"vloomdaw-project","version":1})", kFolder);
    REQUIRE(minimal.data);
    CHECK(minimal.data->project.channels().empty());
    CHECK(minimal.data->project.patterns().size() == 1);
    CHECK(minimal.data->project.tracks().size() == 1);
    CHECK(minimal.data->mixer.size() == 1); // the master
    CHECK(minimal.data->states.effects.size() == 1);
}

TEST_CASE("Hostile values in a project are clamped or left out", "[project-file]")
{
    const std::string text = R"({
        "format": "vloomdaw-project", "version": 1,
        "song": {"tempo": 1e308, "key": -5, "scale": "no-such-scale", "songMode": "yes"},
        "channels": [
            {"name": "A", "volume": 7, "pan": -9, "insert": -3, "color": "#zzzzzz",
             "sample": "\\\\evil-server\\share\\kick.wav"},
            {"name": "B", "sample": "//evil-server/share/x.wav",
             "instrument": {"format": "dll", "path": "C:/x.dll", "id": "x"}},
            {"name": "C", "instrument": {"format": "clap", "path": "C:/a.clap", "id": "a", "state": "@@@"}},
            "not an object"
        ],
        "patterns": [
            {"name": "  ", "steps": 99999, "channels": [
                {"steps": "x.x", "notes": [[999, -10, 0], [60, "a", 1], [60, 0]]}
            ]}
        ],
        "tracks": [{"height": 99999, "depth": 77}],
        "clips": [{"pattern": 500, "track": 0}, {"pattern": 0, "track": 9999, "start": -5}],
        "audioClips": [{"channel": 99, "track": 0}, {"channel": 0, "fit": "warp", "sourceBpm": 5}],
        "mixer": [{"fader": -1, "effects": [{"format": "vst3", "path": "", "id": ""}]}]
    })";
    // Routed to insert 5 of a mixer with two inserts: into the master.
    const auto routed = ProjectFile::read(R"({"format":"vloomdaw-project","version":1,
        "channels":[{"name":"A","insert":5},{"name":"B","insert":2}],"mixer":[{},{},{}]})",
        kFolder);
    REQUIRE(routed.data);
    CHECK(routed.data->project.channels()[0].insert == 0);
    CHECK(routed.data->project.channels()[1].insert == 2);
    CHECK_FALSE(routed.warnings.empty());
    const auto read = ProjectFile::read(text, kFolder);
    INFO(read.error);
    REQUIRE(read.data);
    const ProjectData& d = *read.data;
    CHECK(d.song.tempo == 999.0);
    CHECK(d.song.keyRoot == 0);
    CHECK(d.song.scale == Scale::Major);
    CHECK_FALSE(d.song.songMode);

    REQUIRE(d.project.channels().size() == 3);
    const Channel& a = d.project.channels()[0];
    CHECK(a.volume == 1.0);
    CHECK(a.pan == -1.0);
    CHECK(a.insert == 0);
    CHECK_FALSE(a.color);
    CHECK(a.samplePath.empty()); // a network share: could leak credentials
    CHECK(d.project.channels()[1].samplePath.empty());
    CHECK_FALSE(d.project.channels()[1].instrument); // unknown format
    REQUIRE(d.project.channels()[2].instrument);
    CHECK(d.states.instruments.at(2).empty()); // bad base64: defaults

    REQUIRE(d.project.patterns().size() == 1);
    CHECK(d.project.patterns()[0].name == "Pattern 1");
    CHECK(d.project.stepCount(0) == Project::kMaxSteps);
    CHECK(d.project.step(0, 0, 0));
    CHECK(d.project.step(0, 0, 2));
    REQUIRE(d.project.notes(0, 0) != nullptr);
    REQUIRE(d.project.notes(0, 0)->size() == 1); // the malformed ones are dropped
    CHECK(d.project.notes(0, 0)->front().key == kHighestKey);
    CHECK(d.project.notes(0, 0)->front().length >= kMinNoteLength);

    CHECK(d.project.tracks()[0].height == PlaylistTrack::kMaxHeight);
    CHECK(d.project.tracks()[0].depth == 0);
    REQUIRE(d.project.clips().size() == 1); // the one of a missing pattern is dropped
    CHECK(d.project.clips()[0].track == 0);
    REQUIRE(d.project.audioClips().size() == 1);
    CHECK(d.project.audioClips()[0].fit == AudioClip::Fit::Off);
    CHECK_FALSE(d.project.audioClips()[0].fitted());

    CHECK(d.mixer.track(0).fader == 0.0);
    CHECK(d.mixer.track(0).effects.empty());
    CHECK_FALSE(read.warnings.empty());
}

TEST_CASE("An insert with more effects than the mixer takes opens with the first ones and a warning",
    "[project-file]")
{
    // Earlier builds took one more effect per insert; such a file still opens.
    std::string effects;
    for (std::size_t i = 0; i <= MixerModel::kMaxEffects; ++i)
        effects += std::string(i == 0 ? "" : ",")
            + R"({"format":"clap","path":"fx.clap","id":"fx","name":"FX )" + std::to_string(i) + R"("})";
    const auto read = ProjectFile::read(
        R"({"format":"vloomdaw-project","version":1,"mixer":[{},{"effects":[)" + effects + "]}]}", kFolder);
    REQUIRE(read.data);
    const auto& kept = read.data->mixer.track(1).effects;
    REQUIRE(kept.size() == MixerModel::kMaxEffects);
    CHECK(kept.front().name == "FX 0");
    CHECK(kept.back().name == "FX " + std::to_string(MixerModel::kMaxEffects - 1));
    REQUIRE(read.data->states.effects.size() == 2);
    CHECK(read.data->states.effects[1].size() == MixerModel::kMaxEffects);
    CHECK_FALSE(read.warnings.empty());
}

TEST_CASE("A built-in effect is saved by its id, without a path", "[project-file]")
{
    ProjectData data;
    data.mixer = MixerModel(1);
    PluginSlot eq;
    eq.format = PluginFormat::Builtin;
    eq.id = "vloom.eq";
    eq.name = "EQ";
    REQUIRE(data.mixer.addEffect(1, eq));
    data.states.effects = {{}, {{'V', 'L', 'B', '1', 0, 0, 0, 0}}};

    const std::string text = ProjectFile::write(data, kFolder);
    CHECK(text.find("\"builtin\"") != std::string::npos);
    const auto read = ProjectFile::read(text, kFolder);
    REQUIRE(read.data);
    CHECK(read.warnings.empty());
    REQUIRE(read.data->mixer.track(1).effects.size() == 1);
    const PluginSlot& opened = read.data->mixer.track(1).effects[0];
    CHECK(opened.format == PluginFormat::Builtin);
    CHECK(opened.id == "vloom.eq");
    CHECK(opened.path.empty());
    CHECK(read.data->states.effects[1] == data.states.effects[1]);

    // A path in the file is not kept (nothing is loaded from it), and a
    // built-in without an id is left out.
    const auto odd = ProjectFile::read(R"({"format":"vloomdaw-project","version":1,"mixer":[{"effects":[
        {"format":"builtin","path":"C:/evil.dll","id":"vloom.eq","name":"EQ"},
        {"format":"builtin","path":"x","id":"","name":"No id"}]}]})",
        kFolder);
    REQUIRE(odd.data);
    REQUIRE(odd.data->mixer.track(0).effects.size() == 1);
    CHECK(odd.data->mixer.track(0).effects[0].path.empty());
    CHECK_FALSE(odd.warnings.empty());
}

TEST_CASE("Projects are saved to and opened from files, under any name", "[project-file]")
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path folder = std::filesystem::temp_directory_path()
        / pathFromUtf8("vloomdaw-project-\xE0\xB8\x97\xE0\xB8\x94-" + std::to_string(stamp));
    std::filesystem::create_directories(folder);
    const std::filesystem::path file = folder / pathFromUtf8("เพลง.vloom");

    std::string error;
    REQUIRE(ProjectFile::save(file, sample(), &error));
    std::filesystem::path temp = file;
    temp += u8".saving";
    CHECK_FALSE(std::filesystem::exists(temp)); // renamed into place
    const auto opened = ProjectFile::load(file);
    INFO(opened.error);
    REQUIRE(opened.data);
    CHECK(opened.data->project.patterns().size() == 2);

    // Saving again replaces it.
    ProjectData changed = sample();
    changed.song.tempo = 90.0;
    REQUIRE(ProjectFile::save(file, changed, &error));
    CHECK(ProjectFile::load(file).data->song.tempo == 90.0);

    CHECK_FALSE(ProjectFile::load(folder / "missing.vloom").data);
    CHECK_FALSE(ProjectFile::save(folder / "no-such-folder" / "x.vloom", sample(), &error));
    CHECK_FALSE(error.empty());
    std::error_code ec;
    std::filesystem::remove_all(folder, ec);
}

TEST_CASE("A project's summary gives the launcher its preview", "[project-file]")
{
    const ProjectSummary s = ProjectFile::summarize(sample());
    CHECK(s.tempo == 140.0);
    CHECK(s.channels == 4);
    CHECK(s.plugins == 3); // Sylenth1, Saturn, Pro-L 2
    // The chorus (32 steps: 2 bars) starts after one bar: 3 bars in all.
    CHECK(s.bars == 3);
    REQUIRE(s.blocks.size() == 2);
    const auto& pattern = s.blocks[0];
    CHECK(pattern.lane == 1);
    CHECK(pattern.pattern == 1);
    CHECK(pattern.start + pattern.length <= 1.0 + 1e-9);
    CHECK(s.blocks[1].pattern == ProjectSummary::kAudio);

    const ProjectSummary empty = ProjectFile::summarize(ProjectData{});
    CHECK(empty.bars == 0);
    CHECK(empty.blocks.empty());
}

TEST_CASE("Network share UNC paths in plugin slots and project paths are refused", "[project-file][security]")
{
    const auto read = ProjectFile::read(R"({
        "format": "vloomdaw-project",
        "version": 1,
        "channels": [{
            "name": "Lead",
            "instrument": {"format": "vst3", "path": "\\\\attacker\\share\\synth.vst3", "id": "abc", "name": "Synth"}
        }],
        "mixer": [{
            "effects": [{"format": "clap", "path": "//attacker/share/fx.clap", "id": "xyz", "name": "FX"}]
        }]
    })",
        std::filesystem::path("C:/Projects"));
    REQUIRE(read.data);
    REQUIRE(read.data->project.channels().size() == 1);
    CHECK_FALSE(read.data->project.channels()[0].instrument.has_value());
    CHECK(read.data->mixer.track(0).effects.empty());
    CHECK_FALSE(read.warnings.empty());

    // Relative sample paths inside a network folder must not resolve to a UNC share either.
    const auto netFolder = ProjectFile::read(R"({
        "format": "vloomdaw-project",
        "version": 1,
        "channels": [{"name": "Kick", "sample": "samples/kick.wav"}]
    })",
        std::filesystem::path("\\\\127.0.0.1\\share\\proj"));
    REQUIRE(netFolder.data);
    REQUIRE(netFolder.data->project.channels().size() == 1);
    CHECK(netFolder.data->project.channels()[0].samplePath.empty());

    // ProjectFile::load and summarize refuse UNC paths immediately without SMB probes.
    const auto uncLoad = ProjectFile::load(std::filesystem::path("\\\\127.0.0.1\\share\\song.vloom"));
    CHECK_FALSE(uncLoad.data);
    CHECK(uncLoad.error.find("network") != std::string::npos);
    CHECK_FALSE(
        ProjectFile::summarize(std::filesystem::path("//127.0.0.1/share/song.vloom"), 4096).has_value());
}

TEST_CASE("Mixer Bus, FX, Aux roles, submix outputs and sends round-trip cleanly", "[project-file]")
{
    ProjectData saved;
    const auto gtr = saved.mixer.addInsert();
    const auto bus = saved.mixer.addChannel(MixerRole::Bus, "Drum Bus");
    const auto fx = saved.mixer.addChannel(MixerRole::Fx, "Hall Reverb");
    REQUIRE(gtr == 1);
    REQUIRE(bus == 2);
    REQUIRE(fx == 3);
    REQUIRE(saved.mixer.setOutput(*gtr, static_cast<int>(*bus)));
    REQUIRE(saved.mixer.addSend(*gtr, static_cast<int>(*fx), 0.65, true));
    saved.mixer.setSendMuted(*gtr, 0, true);

    const std::string json = ProjectFile::write(saved, {});
    const auto read = ProjectFile::read(json, {});
    REQUIRE(read.data);
    CHECK(read.warnings.empty());
    REQUIRE(read.data->mixer.size() == 4);
    CHECK(read.data->mixer.track(2).role == MixerRole::Bus);
    CHECK(read.data->mixer.track(2).name == "Drum Bus");
    CHECK(read.data->mixer.track(3).role == MixerRole::Fx);
    CHECK(read.data->mixer.track(3).name == "Hall Reverb");
    CHECK(read.data->mixer.track(1).output == 2);
    REQUIRE(read.data->mixer.track(1).sends.size() == 1);
    CHECK(read.data->mixer.track(1).sends[0].target == 3);
    CHECK(read.data->mixer.track(1).sends[0].level == Catch::Approx(0.65));
    CHECK(read.data->mixer.track(1).sends[0].preFader);
    CHECK(read.data->mixer.track(1).sends[0].muted);
}

TEST_CASE("Empty zero-track projects and mixer Mono mode round-trip cleanly", "[project-file]")
{
    ProjectData saved;
    saved.project = Project(0);
    REQUIRE(saved.project.tracks().empty());
    const auto ins = saved.mixer.addInsert();
    REQUIRE(ins == 1);
    saved.mixer.setMono(MixerModel::kMaster, true);
    saved.mixer.setMono(*ins, true);

    const std::string json = ProjectFile::write(saved, {});
    const auto read = ProjectFile::read(json, {});
    REQUIRE(read.data);
    CHECK(read.warnings.empty());
    CHECK(read.data->project.tracks().empty());
    CHECK(read.data->mixer.track(MixerModel::kMaster).mono);
    REQUIRE(read.data->mixer.size() == 2);
    CHECK(read.data->mixer.track(1).mono);
}
