// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/Project.hpp"

#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("Removing a pattern drops its clips and renumbers the rest", "[project]")
{
    Project p;
    p.addPattern(); // 1
    p.addPattern(); // 2
    p.addClip(Clip{0, 0, 0});
    p.addClip(Clip{1, 1, kTicksPerBar});
    p.addClip(Clip{2, 2, 0});

    p.removePattern(1);
    REQUIRE(p.patterns().size() == 2);
    CHECK(p.patterns()[1].name == "Pattern 2");
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
    CHECK(p.clips()[*i] == Clip{0, Project::kPlaylistTracks - 1, 0});
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
