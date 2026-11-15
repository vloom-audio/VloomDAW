// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/Project.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace vloomdaw {

/// Which notes of a list an edit acts on: one flag per note. A list of
/// another size than the notes selects nothing past its end.
using NoteSelection = std::vector<bool>;

/// What the piano roll's tools do to a list of notes. Each returns the new
/// list; the notes keep their order, so a selection stays valid (except
/// after withoutNotes(), which says so).

[[nodiscard]] bool isSelected(const NoteSelection& selection, std::size_t index);
[[nodiscard]] bool anySelected(const NoteSelection& selection);
/// The selected notes, in order: what Copy puts on the clipboard.
[[nodiscard]] std::vector<Note> selectedNotes(const std::vector<Note>& notes, const NoteSelection& selection);
/// From the earliest start to the latest end of the selected notes; nullopt when there are none.
[[nodiscard]] std::optional<std::pair<int, int>> selectionSpan(
    const std::vector<Note>& notes, const NoteSelection& selection);

/// The selected notes moved by `ticks` and `keys`, together: when one would
/// leave the pattern's start or the key range, they all move less.
[[nodiscard]] std::vector<Note> movedNotes(
    const std::vector<Note>& notes, const NoteSelection& selection, int ticks, int keys);
/// The selected notes made `ticks` longer (or shorter, down to kMinNoteLength each).
[[nodiscard]] std::vector<Note> resizedNotes(
    const std::vector<Note>& notes, const NoteSelection& selection, int ticks);
/// The list without the selected notes. The selection no longer fits it.
[[nodiscard]] std::vector<Note> withoutNotes(const std::vector<Note>& notes, const NoteSelection& selection);
/// The selected notes with their starts on the nearest line of `grid` ticks; lengths stay.
[[nodiscard]] std::vector<Note> quantizedNotes(
    const std::vector<Note>& notes, const NoteSelection& selection, int grid);
/// The selected notes with `velocity` (clamped).
[[nodiscard]] std::vector<Note> withVelocity(
    const std::vector<Note>& notes, const NoteSelection& selection, int velocity);
/// `notes` followed by `pasted`, moved so that its earliest note starts at
/// tick `at`. Unchanged when that would be more than kMaxPatternNotes.
[[nodiscard]] std::vector<Note> withPasted(
    const std::vector<Note>& notes, const std::vector<Note>& pasted, int at);

} // namespace vloomdaw
