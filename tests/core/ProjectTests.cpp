// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/Project.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <optional>

using namespace vloomdaw;

TEST_CASE("A new project has four channels and one empty pattern", "[project]")
{
    const Project p;
    REQUIRE(p.channels().size() == 4);
    CHECK(p.channels()[0].name == "Kick");
    REQUIRE(p.patterns().size() == 1);
    CHECK(p.patterns()[0].name == "Pattern 1");
    CHECK(p.patterns()[0].channels.size() == 4);
    CHECK(p.clips().empty());
    CHECK(p.patternLength(0) == kTicksPerBar);
}

TEST_CASE("Steps toggle and ignore bad indices", "[project]")
{
    Project p;
    p.toggleStep(0, 1, 4);
    CHECK(p.step(0, 1, 4));
    p.toggleStep(0, 1, 4);
    CHECK_FALSE(p.step(0, 1, 4));
    p.setStep(0, 1, kStepsPerBar, true); // past the end
    p.setStep(0, 9, 0, true); // no such channel
    p.setStep(5, 0, 0, true); // no such pattern
    CHECK_FALSE(p.step(0, 1, kStepsPerBar));
    CHECK_FALSE(p.step(0, 9, 0));
    CHECK_FALSE(p.step(0, 0, -1));
}

TEST_CASE("New channels appear in every pattern", "[project]")
{
    Project p;
    p.addPattern();
    const auto index = p.addChannel("Bass");
    CHECK(index == 4);
    for (const Pattern& pat : p.patterns())
        CHECK(pat.channels.size() == 5);
    p.setStep(1, index, 0, true);
    CHECK(p.step(1, index, 0));
}

TEST_CASE("Notes are clamped to valid values", "[project]")
{
    Project p;
    const auto i = p.addNote(0, 0, Note{200, -50, 0});
    REQUIRE(i.has_value());
    const Note& n = p.notes(0, 0)->at(*i);
    CHECK(n.key == kHighestKey);
    CHECK(n.start == 0);
    CHECK(n.length == kMinNoteLength);

    p.setNote(0, 0, *i, Note{-5, 100, 480});
    CHECK(p.notes(0, 0)->at(*i) == Note{kLowestKey, 100, 480});

    CHECK_FALSE(p.addNote(3, 0, Note{}).has_value());
    p.removeNote(0, 0, 99); // ignored
    p.removeNote(0, 0, *i);
    CHECK(p.notes(0, 0)->empty());
}

TEST_CASE("Pattern length covers its notes in whole bars", "[project]")
{
    Project p;
    p.addNote(0, 0, Note{60, 0, kTicksPerStep});
    CHECK(p.patternLength(0) == kTicksPerBar);
    p.addNote(0, 2, Note{64, kTicksPerBar, 1}); // just into bar 2
    CHECK(p.patternLength(0) == 2 * kTicksPerBar);
    p.addNote(0, 1, Note{67, 3 * kTicksPerBar - kTicksPerStep, kTicksPerStep}); // ends exactly at bar 3
    CHECK(p.patternLength(0) == 3 * kTicksPerBar);
}

TEST_CASE("Removing a pattern drops its clips; the others keep their names", "[project]")
{
    Project p;
    p.addPattern(); // 1
    p.addPattern(); // 2
    while (p.tracks().size() < 3)
        p.addTrack();
    p.addClip(Clip{0, 0, 0});
    p.addClip(Clip{1, 1, kTicksPerBar});
    p.addClip(Clip{2, 2, 0});

    p.removePattern(1);
    REQUIRE(p.patterns().size() == 2);
    CHECK(p.patterns()[1].name == "Pattern 3");
    CHECK(p.patterns()[p.addPattern()].name == "Pattern 2"); // the free number comes back
    p.removePattern(2);
    REQUIRE(p.clips().size() == 2);
    CHECK(p.clips()[0].pattern == 0);
    CHECK(p.clips()[1] == Clip{1, 2, 0}); // was pattern 2

    p.removePattern(0);
    p.removePattern(0); // the last pattern stays
    CHECK(p.patterns().size() == 1);
}

TEST_CASE("Clips are clamped to the playlist", "[project]")
{
    Project p;
    const auto i = p.addClip(Clip{0, 99, -10});
    REQUIRE(i.has_value());
    CHECK(p.clips()[*i] == Clip{0, Project::kDefaultPlaylistTracks - 1, 0});
    CHECK_FALSE(p.addClip(Clip{7, 0, 0}).has_value()); // no pattern 7
    p.setClip(*i, Clip{0, -3, kTicksPerBar});
    CHECK(p.clips()[*i] == Clip{0, 0, kTicksPerBar});
    p.removeClip(*i);
    CHECK(p.clips().empty());
}

TEST_CASE("Muting a channel", "[project]")
{
    Project p;
    p.setChannelMuted(2, true);
    CHECK(p.channels()[2].muted);
    p.setChannelMuted(42, true); // ignored
}

TEST_CASE("Channels take a sample and a new name", "[project]")
{
    Project p;
    CHECK(p.channels()[1].samplePath.empty());
    p.setChannelSample(1, "C:/samples/clap \xE0\xB8\x81.wav");
    p.setChannelName(1, "clap");
    CHECK(p.channels()[1].samplePath == "C:/samples/clap \xE0\xB8\x81.wav");
    CHECK(p.channels()[1].name == "clap");
    p.setChannelSample(99, "x.wav"); // ignored
    p.setChannelName(99, "x");
    CHECK(p.channels().size() == 4);
}

TEST_CASE("Pattern steps follow the pattern length", "[project]")
{
    Project p;
    CHECK(p.patternSteps(0) == kStepsPerBar);
    p.addNote(0, 0, Note{60, kTicksPerBar + 10, kTicksPerStep}); // reaches into bar 2
    CHECK(p.patternSteps(0) == 2 * kStepsPerBar);
}

TEST_CASE("Channels have a volume, a pan and a mixer insert", "[project]")
{
    Project p;
    CHECK(p.channels()[0].volume == 0.8); // unity, like a mixer fader
    CHECK(p.channels()[0].pan == 0.0);
    CHECK(p.channels()[0].insert == 0); // the master
    p.setChannelVolume(0, 0.5);
    p.setChannelPan(0, -0.25);
    p.setChannelInsert(0, 3);
    CHECK(p.channels()[0].volume == 0.5);
    CHECK(p.channels()[0].pan == -0.25);
    CHECK(p.channels()[0].insert == 3);
    p.setChannelVolume(0, 7.0);
    p.setChannelPan(0, -9.0);
    p.setChannelInsert(0, -2);
    CHECK(p.channels()[0].volume == 1.0);
    CHECK(p.channels()[0].pan == -1.0);
    CHECK(p.channels()[0].insert == 0);
    p.setChannelVolume(0, std::nan(""));
    CHECK(p.channels()[0].volume == 1.0);
}

TEST_CASE("Inserting a pattern moves the later clips along", "[project]")
{
    Project p;
    p.addPattern(); // Pattern 2
    p.addClip(Clip{0, 0, 0});
    p.addClip(Clip{1, 1, 0});
    const std::size_t at = p.insertPattern(1);
    CHECK(at == 1);
    REQUIRE(p.patterns().size() == 3);
    CHECK(p.patterns()[1].name == "Pattern 3");
    CHECK(p.isPatternEmpty(1));
    CHECK(p.clips()[0].pattern == 0);
    CHECK(p.clips()[1].pattern == 2); // still Pattern 2
    CHECK(p.insertPattern(99) == 3); // clamped to the end
}

TEST_CASE("Cloning copies steps and notes under a new name", "[project]")
{
    Project p;
    p.setStep(0, 0, 4, true);
    p.addNote(0, 1, Note{64, 0, kTicksPerStep});
    p.addClip(Clip{0, 0, 0});
    const auto copy = p.clonePattern(0);
    REQUIRE(copy == 1u);
    CHECK(p.patterns()[1].name == "Pattern 1 #2");
    CHECK(p.step(1, 0, 4));
    REQUIRE(p.notes(1, 1)->size() == 1);
    CHECK(p.patterns()[p.clonePattern(0).value()].name == "Pattern 1 #3");
    CHECK(p.clips()[0].pattern == 0); // the clip stays on the original
    CHECK_FALSE(p.clonePattern(42).has_value());
}

TEST_CASE("Moving a pattern keeps every clip on its pattern", "[project]")
{
    Project p;
    p.addPattern();
    p.addPattern();
    p.addPattern(); // 1 2 3 4
    for (std::size_t i = 0; i < 4; ++i)
        p.addClip(Clip{i, static_cast<int>(i), 0});
    const std::string moved = p.patterns()[0].name;
    CHECK(p.movePattern(0, 2) == 2);
    CHECK(p.patterns()[2].name == moved);
    // Each clip still points at the pattern with the name it had.
    const std::vector<std::string> expected{"Pattern 1", "Pattern 2", "Pattern 3", "Pattern 4"};
    for (std::size_t i = 0; i < 4; ++i)
        CHECK(p.patterns()[p.clips()[i].pattern].name == expected[i]);
    CHECK(p.movePattern(2, 0) == 0);
    for (std::size_t i = 0; i < 4; ++i)
        CHECK(p.clips()[i].pattern == i);
    CHECK(p.movePattern(3, 99) == 3); // clamped
}

TEST_CASE("Renaming ignores blank names", "[project]")
{
    Project p;
    p.setPatternName(0, "Verse");
    CHECK(p.patterns()[0].name == "Verse");
    p.setPatternName(0, "   ");
    p.setPatternName(0, "");
    p.setPatternName(5, "Nope");
    CHECK(p.patterns()[0].name == "Verse");
}

TEST_CASE("Empty patterns are found from a starting index", "[project]")
{
    Project p;
    p.addPattern();
    p.addPattern();
    p.setStep(0, 0, 0, true);
    p.addNote(2, 0, Note{});
    CHECK_FALSE(p.isPatternEmpty(0));
    CHECK(p.isPatternEmpty(1));
    CHECK_FALSE(p.isPatternEmpty(2));
    CHECK(p.findEmptyPattern() == 1u);
    CHECK_FALSE(p.findEmptyPattern(2).has_value());
}

TEST_CASE("Transposing moves every note and keeps the shape at the range ends", "[project]")
{
    Project p;
    p.addNote(0, 0, Note{60, 0, kTicksPerStep});
    p.addNote(0, 1, Note{64, 0, kTicksPerStep});
    p.transposePattern(0, 5);
    CHECK(p.notes(0, 0)->front().key == 65);
    CHECK(p.notes(0, 1)->front().key == 69);
    p.transposePattern(0, 100); // only as far as the top note allows
    CHECK(p.notes(0, 1)->front().key == kHighestKey);
    CHECK(p.notes(0, 0)->front().key == kHighestKey - 4); // still 4 apart
}

TEST_CASE("Splitting by channel makes one pattern per used channel", "[project]")
{
    Project p; // Kick, Clap, Hat, Snare
    p.setStep(0, 0, 0, true);
    p.addNote(0, 2, Note{});
    p.addPattern();
    p.addClip(Clip{1, 0, 0});
    const auto parts = p.splitPatternByChannel(0);
    REQUIRE(parts == std::vector<std::size_t>{1, 2});
    CHECK(p.patterns()[1].name == "Pattern 1 - Kick");
    CHECK(p.patterns()[2].name == "Pattern 1 - Hat");
    CHECK(p.step(1, 0, 0));
    CHECK(p.notes(1, 2)->empty());
    CHECK(p.notes(2, 2)->size() == 1);
    CHECK_FALSE(p.step(2, 0, 0));
    CHECK(p.patterns()[p.clips()[0].pattern].name == "Pattern 2"); // the clip followed
    CHECK(p.splitPatternByChannel(p.findEmptyPattern().value_or(99)).empty());
}

TEST_CASE("A pattern's step pads can grow and shrink", "[project]")
{
    Project p;
    CHECK(p.stepCount(0) == kStepsPerBar);
    p.setStep(0, 1, 15, true);

    p.setStepCount(0, 2 * kStepsPerBar);
    CHECK(p.stepCount(0) == 32);
    CHECK(p.patternLength(0) == 2 * kTicksPerBar); // clips and the loop follow
    CHECK(p.patternSteps(0) == 32);
    CHECK(p.step(0, 1, 15)); // kept
    CHECK_FALSE(p.step(0, 1, 31)); // new pads are off
    p.setStep(0, 0, 31, true);
    CHECK(p.step(0, 0, 31));
    CHECK(p.hasStepsFrom(0, 16));
    CHECK_FALSE(p.hasStepsFrom(0, 32));

    SECTION("a channel added later gets every pad")
    {
        const auto ch = p.addChannel("Perc");
        p.setStep(0, ch, 31, true);
        CHECK(p.step(0, ch, 31));
    }
    SECTION("shrinking drops the steps past the end")
    {
        p.setStepCount(0, kStepsPerBar);
        CHECK(p.patternLength(0) == kTicksPerBar);
        CHECK_FALSE(p.step(0, 0, 31));
        p.setStepCount(0, 2 * kStepsPerBar);
        CHECK_FALSE(p.step(0, 0, 31)); // gone, not hidden
        CHECK(p.step(0, 1, 15));
    }
    SECTION("clone and split keep the length")
    {
        const auto copy = p.clonePattern(0);
        REQUIRE(copy);
        CHECK(p.stepCount(*copy) == 32);
        CHECK(p.step(*copy, 0, 31));
        const auto parts = p.splitPatternByChannel(0);
        REQUIRE(parts.size() == 2);
        for (const auto part : parts) {
            CHECK(p.stepCount(part) == 32);
            for (const auto& cp : p.patterns()[part].channels)
                CHECK(cp.steps.size() == 32);
        }
    }
    SECTION("other patterns keep their own length")
    {
        const auto other = p.addPattern();
        CHECK(p.stepCount(other) == kStepsPerBar);
    }
}

TEST_CASE("Step counts are clamped and out-of-range patterns ignored", "[project]")
{
    Project p;
    p.setStepCount(0, 0);
    CHECK(p.stepCount(0) == 1);
    p.setStepCount(0, Project::kMaxSteps + 100);
    CHECK(p.stepCount(0) == Project::kMaxSteps);
    p.setStepCount(7, 32);
    CHECK(p.stepCount(7) == kStepsPerBar);
    CHECK_FALSE(p.hasStepsFrom(7, 0));
}

TEST_CASE("Deleting a mixer insert reroutes the channels", "[project]")
{
    Project p; // Kick, Clap, Hat, Snare
    p.setChannelInsert(0, 2);
    p.setChannelInsert(1, 3);
    p.setChannelInsert(2, 1);
    CHECK(p.removeInsert(2) == 1);
    CHECK(p.channels()[0].insert == 0); // on the deleted insert: the master
    CHECK(p.channels()[1].insert == 2); // followed its insert down
    CHECK(p.channels()[2].insert == 1);
    CHECK(p.channels()[3].insert == 0);
    CHECK(p.removeInsert(0) == 0); // the master cannot go
}

TEST_CASE("Playlist tracks can be added, named and removed", "[project]")
{
    Project p;
    REQUIRE(p.tracks().size() == 1); // a new project has one track
    CHECK(p.tracks()[0].name == "Track 1");
    CHECK(p.addTrack() == std::optional<std::size_t>(1));
    CHECK(p.tracks()[1].name == "Track 2");
    while (p.tracks().size() < 16)
        p.addTrack();

    p.setTrackName(2, "  Drums ");
    CHECK(p.tracks()[2].name == "Drums");
    p.addClip(Clip{0, 1, 0});
    p.addClip(Clip{0, 1, kTicksPerBar});
    p.addClip(Clip{0, 3, 0});
    CHECK(p.clipsOnTrack(1) == 2);

    p.removeTrack(1);
    CHECK(p.tracks().size() == 15);
    CHECK(p.tracks()[1].name == "Drums"); // named: keeps its name
    CHECK(p.tracks()[2].name == "Track 3"); // was Track 4: follows its number
    REQUIRE(p.clips().size() == 1); // the clips on the removed track went
    CHECK(p.clips()[0].track == 2); // and the later ones moved up

    p.setTrackName(1, "");
    CHECK(p.tracks()[1].name == "Track 2");
    CHECK_FALSE(p.tracks()[1].named);
    p.setClip(0, Clip{0, 99, 0});
    CHECK(p.clips()[0].track == 14); // clamped to the tracks there are
}

TEST_CASE("The playlist keeps between one and the most tracks", "[project]")
{
    Project p;
    while (p.tracks().size() > 1)
        p.removeTrack(0);
    p.removeTrack(0); // the last one stays
    CHECK(p.tracks().size() == 1);
    CHECK(p.tracks()[0].name == "Track 1");
    while (p.addTrack()) { }
    CHECK(p.tracks().size() == static_cast<std::size_t>(Project::kMaxPlaylistTracks));
}

TEST_CASE("Audio clips are placed, moved and removed with their track", "[project]")
{
    Project p;
    p.addTrack();
    p.addTrack(); // three tracks
    CHECK_FALSE(p.addAudioClip(AudioClip{9, 0, 0})); // no channel 9
    const auto a = p.addAudioClip(AudioClip{0, 9, -5});
    REQUIRE(a);
    CHECK(p.audioClips()[*a] == AudioClip{0, 2, 0}); // clamped
    p.setAudioClip(*a, AudioClip{0, 1, kTicksPerBar});
    p.setAudioClip(*a, AudioClip{9, 1, 0}); // no channel 9: ignored
    CHECK(p.audioClips()[*a] == AudioClip{0, 1, kTicksPerBar});
    p.addAudioClip(AudioClip{1, 2, 0});
    p.addClip(Clip{0, 1, 0});
    CHECK(p.clipsOnTrack(1) == 2); // a pattern clip and an audio clip

    p.removeTrack(1);
    REQUIRE(p.audioClips().size() == 1);
    CHECK(p.audioClips()[0] == AudioClip{1, 1, 0}); // moved up with its track
    p.removeAudioClip(0);
    CHECK(p.audioClips().empty());

    p.setChannelSample(2, "hat.wav");
    CHECK(p.channelWithSample("hat.wav") == std::optional<std::size_t>(2));
    CHECK_FALSE(p.channelWithSample("snare.wav"));
    CHECK_FALSE(p.channelWithSample("")); // channels without a sample do not match
}

TEST_CASE("Moving an insert or a track keeps routing and clips in place", "[project]")
{
    Project p; // Kick, Clap, Hat, Snare
    p.setChannelInsert(0, 1);
    p.setChannelInsert(1, 2);
    p.setChannelInsert(2, 3);
    p.moveInsert(1, 3); // 1 -> 3, 2 -> 1, 3 -> 2
    CHECK(p.channels()[0].insert == 3);
    CHECK(p.channels()[1].insert == 1);
    CHECK(p.channels()[2].insert == 2);
    CHECK(p.channels()[3].insert == 0); // the master never moves

    p.addTrack();
    p.addTrack(); // Track 1..3
    p.setTrackName(0, "Drums");
    p.addClip(Clip{0, 0, 0});
    p.addAudioClip(AudioClip{0, 2, 0});
    CHECK(p.moveTrack(0, 2) == 2);
    CHECK(p.tracks()[2].name == "Drums");
    CHECK(p.tracks()[0].name == "Track 1"); // unnamed: its new number
    CHECK(p.clips()[0].track == 2); // went with Drums
    CHECK(p.audioClips()[0].track == 1); // Track 3 moved up one
    CHECK(p.moveTrack(1, 50) == 2); // clamped
}
