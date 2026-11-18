// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/NoteHistory.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace vloomdaw;

namespace {

/// Sets the notes and records the edit, as the piano roll does.
void edit(Project& project, NoteHistory& history, std::vector<Note> notes)
{
    const std::vector<Note> before = *project.notes(0, 0);
    project.setNotes(0, 0, notes);
    history.record({0, 0}, before, *project.notes(0, 0));
}

} // namespace

TEST_CASE("Undo and redo walk back and forth through the note edits", "[history]")
{
    Project project;
    NoteHistory history;
    const std::vector<Note> one{Note{60, 0, 240}};
    const std::vector<Note> two{Note{60, 0, 240}, Note{64, 240, 240, 90}};
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.undo(project).has_value());

    edit(project, history, one);
    edit(project, history, two);
    REQUIRE(history.canUndo());

    CHECK(history.undo(project) == NoteHistory::Target{0, 0});
    CHECK(*project.notes(0, 0) == one);
    CHECK(history.undo(project).has_value());
    CHECK(project.notes(0, 0)->empty());
    CHECK_FALSE(history.canUndo());

    CHECK(history.redo(project).has_value());
    CHECK(history.redo(project).has_value());
    CHECK(*project.notes(0, 0) == two);
    CHECK_FALSE(history.canRedo());
}

TEST_CASE("A new edit drops what could be redone; an edit that changes nothing is not kept", "[history]")
{
    Project project;
    NoteHistory history;
    edit(project, history, {Note{60, 0, 240}});
    REQUIRE(history.undo(project).has_value());
    REQUIRE(history.canRedo());
    edit(project, history, {Note{72, 0, 240}});
    CHECK_FALSE(history.canRedo());

    history.clear();
    edit(project, history, *project.notes(0, 0));
    CHECK_FALSE(history.canUndo());
}

TEST_CASE("The history is dropped when the notes changed behind its back", "[history]")
{
    Project project;
    NoteHistory history;
    edit(project, history, {Note{60, 0, 240}});
    edit(project, history, {Note{60, 0, 240}, Note{62, 240, 240}});
    project.transposePattern(0, 1); // not through the history

    const std::vector<Note> now = *project.notes(0, 0);
    CHECK_FALSE(history.undo(project).has_value());
    CHECK(*project.notes(0, 0) == now); // left as they are
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.canRedo());
}

TEST_CASE("Only the newest edits are kept", "[history]")
{
    Project project;
    NoteHistory history;
    for (int i = 0; i < static_cast<int>(NoteHistory::kMaxEdits) + 5; ++i)
        edit(project, history, {Note{60, i * 240, 240}});
    std::size_t undone = 0;
    while (history.undo(project).has_value())
        ++undone;
    CHECK(undone == NoteHistory::kMaxEdits);
}

TEST_CASE("Replacing all notes clamps them and keeps their order", "[project]")
{
    Project project;
    project.setNotes(0, 0, {Note{200, -5, 0, 500}, Note{60, 480, 240, 0}});
    REQUIRE(project.notes(0, 0)->size() == 2);
    CHECK(project.notes(0, 0)->at(0) == Note{kHighestKey, 0, kMinNoteLength, kMaxVelocity});
    CHECK(project.notes(0, 0)->at(1) == Note{60, 480, 240, kMinVelocity});
    project.setNotes(9, 0, {Note{}}); // no such pattern: ignored
}

TEST_CASE("NoteHistory records and undoes/redoes MIDI control curve edits alongside notes", "[history]")
{
    Project project;
    NoteHistory history;
    const std::vector<ControlPoint> c1{ControlPoint{0, ControlKind::PitchBend, 0, -0.5, 0}};
    const std::vector<ControlPoint> c2{
        ControlPoint{0, ControlKind::PitchBend, 0, -0.5, 0},
        ControlPoint{240, ControlKind::Controller, 1, 0.75, 0},
    };

    const auto before1 = *project.controls(0, 0);
    project.setControls(0, 0, c1);
    history.recordControls({0, 0}, before1, *project.controls(0, 0));

    const auto before2 = *project.controls(0, 0);
    project.setControls(0, 0, c2);
    history.recordControls({0, 0}, before2, *project.controls(0, 0));

    REQUIRE(history.canUndo());
    CHECK(history.undo(project) == NoteHistory::Target{0, 0});
    CHECK(*project.controls(0, 0) == c1);
    CHECK(history.undo(project) == NoteHistory::Target{0, 0});
    CHECK(project.controls(0, 0)->empty());
    CHECK(history.redo(project) == NoteHistory::Target{0, 0});
    CHECK(*project.controls(0, 0) == c1);
    CHECK(history.redo(project) == NoteHistory::Target{0, 0});
    CHECK(*project.controls(0, 0) == c2);
}
