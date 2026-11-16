// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/NoteEdits.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace vloomdaw;

namespace {

const std::vector<Note> kNotes{Note{60, 0, 240}, Note{64, 480, 240}, Note{67, 960, 480, 80}};

} // namespace

TEST_CASE("A selection names notes by position", "[notes]")
{
    const NoteSelection selection{false, true};
    CHECK_FALSE(isSelected(selection, 0));
    CHECK(isSelected(selection, 1));
    CHECK_FALSE(isSelected(selection, 2)); // past its end: not selected
    CHECK(anySelected(selection));
    CHECK_FALSE(anySelected({false, false}));
    CHECK(selectedNotes(kNotes, {true, false, true}) == std::vector<Note>{kNotes[0], kNotes[2]});
    CHECK(selectionSpan(kNotes, {false, true, true}) == std::pair{480, 1440});
    CHECK_FALSE(selectionSpan(kNotes, {}).has_value());
}

TEST_CASE("Selected notes move together and stop together at an edge", "[notes]")
{
    const NoteSelection both{true, true, false};
    auto moved = movedNotes(kNotes, both, 240, 2);
    CHECK(moved[0] == Note{62, 240, 240});
    CHECK(moved[1] == Note{66, 720, 240});
    CHECK(moved[2] == kNotes[2]); // not selected

    // The first is at the start already: neither goes left, and their spacing stays.
    moved = movedNotes(kNotes, both, -960, 0);
    CHECK(moved[0].start == 0);
    CHECK(moved[1].start == 480);

    // Up to the top key: the interval between them stays.
    moved = movedNotes(kNotes, both, 0, 500);
    CHECK(moved[1].key == kHighestKey);
    CHECK(moved[0].key == kHighestKey - 4);

    CHECK(movedNotes(kNotes, {}, 240, 2) == kNotes);
}

TEST_CASE("Resizing changes the length of each selected note, down to the shortest", "[notes]")
{
    auto resized = resizedNotes(kNotes, {true, false, true}, 120);
    CHECK(resized[0].length == 360);
    CHECK(resized[1].length == 240);
    CHECK(resized[2].length == 600);
    resized = resizedNotes(kNotes, {true, false, true}, -400);
    CHECK(resized[0].length == kMinNoteLength);
    CHECK(resized[2].length == 80);
}

TEST_CASE("Deleting, quantizing and setting the velocity act on the selection only", "[notes]")
{
    CHECK(withoutNotes(kNotes, {false, true, false}) == std::vector<Note>{kNotes[0], kNotes[2]});

    const std::vector<Note> loose{Note{60, 50, 240}, Note{60, 400, 240}, Note{60, 130, 240}};
    const auto tight = quantizedNotes(loose, {true, true, false}, 240);
    CHECK(tight[0].start == 0);
    CHECK(tight[1].start == 480);
    CHECK(tight[2].start == 130);
    CHECK(tight[0].length == 240);
    CHECK(quantizedNotes(loose, {true, true, true}, 0) == loose);

    const auto loud = withVelocity(kNotes, {false, false, true}, 999);
    CHECK(loud[2].velocity == kMaxVelocity);
    CHECK(loud[0].velocity == kDefaultVelocity);
}

TEST_CASE("Pasted notes keep their spacing and start where asked", "[notes]")
{
    const std::vector<Note> clip{Note{64, 480, 240}, Note{67, 960, 480, 80}};
    const auto pasted = withPasted(kNotes, clip, 3840);
    REQUIRE(pasted.size() == 5);
    CHECK(pasted[3] == Note{64, 3840, 240});
    CHECK(pasted[4] == Note{67, 4320, 480, 80});
    CHECK(withPasted(kNotes, {}, 0) == kNotes);
}

TEST_CASE("Slicing, legato, velocity scaling and scale-snap transform notes cleanly", "[notes]")
{
    const std::vector<Note> bar{Note{60, 0, 960, 100}, Note{63, 960, 480, 80}};
    const auto cut = slicedNotes(bar, 480, 58, 62);
    REQUIRE(cut.size() == 3);
    CHECK(cut[0] == Note{60, 0, 480, 100});
    CHECK(cut[1] == Note{60, 480, 480, 100});
    CHECK(cut[2] == bar[1]);

    const auto leg = legatoNotes({Note{60, 0, 240, 100}, Note{64, 960, 240, 100}}, {true, true});
    CHECK(leg[0].length == 960);
    CHECK(leg[1].length == 240);

    const auto softer = scaledVelocityNotes(bar, {true, false}, 0.5);
    CHECK(softer[0].velocity == 50);
    CHECK(softer[1].velocity == 80);
    const auto huge = scaledVelocityNotes(bar, {true, false}, 1e20);
    CHECK(huge[0].velocity == kMaxVelocity);

    // D#5 (63) snaps to D5 (62) in C Major.
    const auto inScale = snappedToScaleNotes(bar, {false, true}, 0, Scale::Major);
    CHECK(inScale[0].key == 60);
    CHECK(inScale[1].key == 62);
}
