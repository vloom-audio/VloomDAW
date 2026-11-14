// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/ProjectFile.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>

using namespace vloomdaw;

namespace {

const std::filesystem::path kFolder = pathFromUtf8("C:/Music/My Song");

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
    p.setChannelSample(1, "D:/Packs/clap.wav"); // outside the folder
    p.setChannelVolume(1, 0.5);
    p.setChannelPan(1, -0.25);
    p.setChannelInsert(1, 2);
    p.setChannelColor(2, 0x12ab34);
    p.setChannelMuted(3, true);
    p.setChannelInstrument(3, plugin(PluginFormat::Vst3, "Sylenth1"));
    p.setStep(0, 0, 0, true);
    p.setStep(0, 0, 4, true);
    p.addNote(0, 3, Note{64, 0, 480});
    p.addNote(0, 3, Note{67, 960, 240});
    const std::size_t second = p.addPattern();
    p.setPatternName(second, "Chorus");
    p.setStepCount(second, 32);
    p.addTrack();
    p.setTrackName(1, "Drums");
    p.setTrackColor(1, 0xff8a3d);
    p.setTrackHeight(1, 60);
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
    CHECK(text.find("D:/Packs/clap.wav") != std::string::npos); // absolute outside it
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
    const auto moved = ProjectFile::read(text, pathFromUtf8("E:/Backup/My Song"));
    REQUIRE(moved.data);
    CHECK(pathFromUtf8(moved.data->project.channels()[0].samplePath)
        == pathFromUtf8("E:/Backup/My Song/samples/kick.wav").lexically_normal());
    CHECK(moved.data->project.channels()[1].samplePath == "D:/Packs/clap.wav");
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
