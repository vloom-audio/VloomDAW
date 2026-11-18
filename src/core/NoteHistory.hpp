// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/Project.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace vloomdaw {

/// Undo and redo for the piano roll: the edits made to the notes of a
/// channel in a pattern, newest last.
///
/// An edit keeps the notes before and after it. Patterns and channels are
/// named by position, and other commands move them or change notes without
/// telling the history (delete a pattern, transpose it, open a project), so
/// an edit is only taken back when the notes are still as it left them.
/// When they are not, the history no longer fits the project and is dropped.
class NoteHistory {
public:
    static constexpr std::size_t kMaxEdits = 200; ///< older ones are forgotten

    /// The notes an undo or redo changed.
    struct Target {
        std::size_t pattern = 0;
        std::size_t channel = 0;

        friend bool operator==(const Target&, const Target&) = default;
    };

    /// Records that the notes of `target` went from `before` to `after`.
    /// Nothing is recorded when they are the same. Clears what could be redone.
    void record(Target target, std::vector<Note> before, std::vector<Note> after);
    void recordControls(Target target, std::vector<ControlPoint> before, std::vector<ControlPoint> after);

    [[nodiscard]] bool canUndo() const { return !done_.empty(); }
    [[nodiscard]] bool canRedo() const { return !undone_.empty(); }

    /// Takes the newest edit back in `project`; where, or nullopt when there
    /// is none or the history no longer fits the project (it is then empty).
    std::optional<Target> undo(Project& project);
    /// Makes the edit last taken back again; as undo().
    std::optional<Target> redo(Project& project);

    void clear();

private:
    struct Edit {
        Target target;
        bool isControls = false;
        std::vector<Note> before{};
        std::vector<Note> after{};
        std::vector<ControlPoint> controlsBefore{};
        std::vector<ControlPoint> controlsAfter{};
    };

    /// Moves the newest edit of `from` to `to`: the notes must be as the edit
    /// left them (`before` when going `forward`, else `after`) and become the other ones.
    std::optional<Target> step(
        Project& project, std::vector<Edit>& from, std::vector<Edit>& to, bool forward);

    std::vector<Edit> done_;
    std::vector<Edit> undone_; ///< taken back, newest last
};

} // namespace vloomdaw
