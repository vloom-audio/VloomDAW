// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/UndoHistory.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace vloomdaw;

TEST_CASE("UndoHistory restores project and mixer snapshots across multi-step undo and redo", "[undo]")
{
    UndoHistory history(5);
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.canRedo());

    UndoSnapshot state;
    state.project.setTrackName(0, "Drums");
    state.mixer = MixerModel(1);
    state.mixer.setName(1, "Drums");

    // Step 1: mute Track 1 & Insert 1
    history.push(state, "Mute Track", {}, 100);
    state.project.setTrackMuted(0, true);
    state.mixer.setMuted(1, true);

    // Step 2: add a clip
    history.push(state, "Add Clip", {}, 200);
    state.project.addClip(Clip{0, 0, 0});

    REQUIRE(history.canUndo());
    CHECK(history.undoCount() == 2);
    CHECK(history.undoLabel() == "Add Clip");

    // Undo Step 2
    auto undone = history.undo(state);
    REQUIRE(undone.has_value());
    state = std::move(*undone);
    CHECK(state.project.clips().empty());
    CHECK(state.project.tracks()[0].muted);
    CHECK(history.canRedo());
    CHECK(history.redoLabel() == "Add Clip");

    // Undo Step 1
    undone = history.undo(state);
    REQUIRE(undone.has_value());
    state = std::move(*undone);
    CHECK_FALSE(state.project.tracks()[0].muted);
    CHECK_FALSE(state.mixer.track(1).muted);
    CHECK_FALSE(history.canUndo());

    // Redo Step 1 & Step 2
    auto redone = history.redo(state);
    REQUIRE(redone.has_value());
    state = std::move(*redone);
    CHECK(state.project.tracks()[0].muted);

    redone = history.redo(state);
    REQUIRE(redone.has_value());
    state = std::move(*redone);
    CHECK(state.project.clips().size() == 1);
    CHECK_FALSE(history.canRedo());
}

TEST_CASE("UndoHistory coalesces continuous fader and drag gestures into a single undo step", "[undo]")
{
    UndoHistory history(10);
    UndoSnapshot state;
    state.mixer = MixerModel(1);
    state.mixer.setFader(1, 0.8);

    // Simulate 5 mouse-move events within 650 ms window
    for (int i = 1; i <= 5; ++i) {
        history.push(state, "Mixer Fader", "mixer.fader.1", 1000 + i * 50);
        state.mixer.setFader(1, 0.8 - 0.1 * i);
    }

    CHECK(history.undoCount() == 1);
    REQUIRE(state.mixer.track(1).fader == Catch::Approx(0.3));

    // One Ctrl+Z jumps straight back to 0.8 before the drag started!
    auto restored = history.undo(state);
    REQUIRE(restored.has_value());
    CHECK(restored->mixer.track(1).fader == Catch::Approx(0.8));

    // And one Redo jumps straight to the final 0.3!
    auto redone = history.redo(*restored);
    REQUIRE(redone.has_value());
    CHECK(redone->mixer.track(1).fader == Catch::Approx(0.3));
}

TEST_CASE("UndoHistory enforces bounded capacity and visits stored PluginSlots", "[undo]")
{
    UndoHistory history(3);
    UndoSnapshot state;
    state.mixer = MixerModel(1);

    PluginSlot eq{PluginFormat::Builtin, {}, "vloom.eq", "EQ", false, 42, {}};
    state.mixer.addEffect(1, eq);

    for (int i = 0; i < 6; ++i) {
        history.push(state, "Step " + std::to_string(i), {}, i * 2000);
        state.song.tempo = 120.0 + i;
    }
    CHECK(history.undoCount() == 3);

    // Stamp savedState on every stored PluginSlot whose instance == 42
    history.forEachPluginSlot([](PluginSlot& slot) {
        if (slot.instance == 42)
            slot.savedState = {1, 2, 3, 4};
    });

    auto restored = history.undo(state);
    REQUIRE(restored.has_value());
    REQUIRE(restored->mixer.track(1).effects.size() == 1);
    CHECK(restored->mixer.track(1).effects[0].savedState == std::vector<std::uint8_t>{1, 2, 3, 4});
}

TEST_CASE("UndoHistory timestamp coalescing never overflows signed int64 across extreme timestamps", "[undo]")
{
    UndoHistory history(10);
    UndoSnapshot s1;
    s1.song.tempo = 100.0;
    history.push(s1, "Tempo", "tempo", std::numeric_limits<std::int64_t>::min());

    UndoSnapshot s2;
    s2.song.tempo = 140.0;
    history.push(s2, "Tempo", "tempo", 100);

    // Extreme timestamps are far outside the 450 ms window and must NOT coalesce due to int64 overflow.
    CHECK(history.undoCount() == 2);
}
