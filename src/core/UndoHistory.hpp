// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/MixerModel.hpp"
#include "core/Project.hpp"
#include "core/ProjectFile.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vloomdaw {

/// Complete undoable song + mixer + selection state (Qt-free).
struct UndoSnapshot {
    Project project;
    MixerModel mixer{0};
    SongSettings song{};
    std::size_t currentPattern = 0;
    std::size_t currentChannel = 0;
    int selectedTrack = 0;
    std::size_t selectedMixerTrack = 0;
};

/// Bounded undo/redo stack with time-windowed gesture coalescing.
///
/// Call `push(beforeState, label, coalesceKey, nowMs)` immediately *before*
/// mutating state. Consecutive pushes with the same non-empty `coalesceKey`
/// within `kCoalesceWindowMs` keep the initial `beforeState` so continuous
/// fader, knob, note and clip drags undo in a single step.
class UndoHistory {
public:
    static constexpr std::size_t kDefaultMaxSteps = 100;
    static constexpr std::int64_t kCoalesceWindowMs = 650;

    explicit UndoHistory(std::size_t maxSteps = kDefaultMaxSteps);

    /// Records `beforeState` prior to an edit; clears the redo stack.
    void push(
        UndoSnapshot beforeState, std::string label, std::string coalesceKey = {}, std::int64_t nowMs = 0);

    /// Ends any active gesture coalescing window so the next push starts a fresh step.
    void breakCoalescing() noexcept;

    [[nodiscard]] bool canUndo() const noexcept { return !undoStack_.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redoStack_.empty(); }
    [[nodiscard]] std::size_t undoCount() const noexcept { return undoStack_.size(); }
    [[nodiscard]] std::size_t redoCount() const noexcept { return redoStack_.size(); }

    [[nodiscard]] std::string_view undoLabel() const noexcept;
    [[nodiscard]] std::string_view redoLabel() const noexcept;

    /// Pops the latest undo entry, pushes `currentState` onto redo, and returns the restored state.
    [[nodiscard]] std::optional<UndoSnapshot> undo(UndoSnapshot currentState);

    /// Pops the latest redo entry, pushes `currentState` onto undo, and returns the restored state.
    [[nodiscard]] std::optional<UndoSnapshot> redo(UndoSnapshot currentState);

    void clear() noexcept;

    /// Visits every `PluginSlot` stored across undo and redo snapshots so the
    /// host can snapshot `savedState` before unloading unused plugin instances.
    template <typename Fn> void forEachPluginSlot(Fn&& fn)
    {
        const auto visitSnapshot = [&](UndoSnapshot& snap) {
            for (std::size_t c = 0; c < snap.project.channels().size(); ++c) {
                if (auto inst = snap.project.channels()[c].instrument) {
                    fn(*inst);
                    snap.project.setChannelInstrument(c, std::move(inst));
                }
                const auto& noteFx = snap.project.channels()[c].noteEffects;
                for (std::size_t ne = 0; ne < noteFx.size(); ++ne) {
                    PluginSlot copy = noteFx[ne];
                    fn(copy);
                    snap.project.replaceChannelNoteEffect(c, ne, std::move(copy));
                }
            }
            for (std::size_t m = 0; m < snap.mixer.size(); ++m) {
                const auto& effects = snap.mixer.track(m).effects;
                for (std::size_t s = 0; s < effects.size(); ++s) {
                    PluginSlot copy = effects[s];
                    fn(copy);
                    snap.mixer.replaceEffect(m, s, std::move(copy));
                }
            }
        };
        for (Entry& e : undoStack_)
            visitSnapshot(e.snapshot);
        for (Entry& e : redoStack_)
            visitSnapshot(e.snapshot);
    }

private:
    struct Entry {
        UndoSnapshot snapshot;
        std::string label;
        std::string coalesceKey;
        std::int64_t timestampMs = 0;
    };

    std::size_t maxSteps_ = kDefaultMaxSteps;
    std::vector<Entry> undoStack_;
    std::vector<Entry> redoStack_;
};

} // namespace vloomdaw
