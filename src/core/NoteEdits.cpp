// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/NoteEdits.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vloomdaw {

namespace {

constexpr int kLatestStart = std::numeric_limits<int>::max() / 4; ///< far past any song; keeps sums in range

/// `notes` with `change` applied to each selected one.
template <typename Change>
std::vector<Note> changed(const std::vector<Note>& notes, const NoteSelection& selection, Change change)
{
    std::vector<Note> out = notes;
    for (std::size_t i = 0; i < out.size(); ++i)
        if (isSelected(selection, i))
            out[i] = Project::clamped(change(out[i]));
    return out;
}

} // namespace

bool isSelected(const NoteSelection& selection, std::size_t index)
{
    return index < selection.size() && selection[index];
}

bool anySelected(const NoteSelection& selection)
{
    return std::find(selection.begin(), selection.end(), true) != selection.end();
}

std::vector<Note> selectedNotes(const std::vector<Note>& notes, const NoteSelection& selection)
{
    std::vector<Note> out;
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (isSelected(selection, i))
            out.push_back(notes[i]);
    return out;
}

std::optional<std::pair<int, int>> selectionSpan(
    const std::vector<Note>& notes, const NoteSelection& selection)
{
    std::optional<std::pair<int, int>> span;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (!isSelected(selection, i))
            continue;
        if (!span)
            span = std::pair{notes[i].start, notes[i].end()};
        span->first = std::min(span->first, notes[i].start);
        span->second = std::max(span->second, notes[i].end());
    }
    return span;
}

std::vector<Note> movedNotes(
    const std::vector<Note>& notes, const NoteSelection& selection, int ticks, int keys)
{
    // How far the group can go before one of its notes meets an edge.
    int earliest = kLatestStart;
    int latest = 0;
    int lowest = kHighestKey;
    int highest = kLowestKey;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (!isSelected(selection, i))
            continue;
        earliest = std::min(earliest, notes[i].start);
        latest = std::max(latest, notes[i].start);
        lowest = std::min(lowest, notes[i].key);
        highest = std::max(highest, notes[i].key);
    }
    if (lowest > highest)
        return notes; // nothing selected
    ticks = std::clamp(ticks, -earliest, std::max(-earliest, kLatestStart - latest));
    keys = std::clamp(keys, kLowestKey - lowest, kHighestKey - highest);
    return changed(notes, selection, [ticks, keys](Note note) {
        note.start += ticks;
        note.key += keys;
        return note;
    });
}

std::vector<Note> resizedNotes(const std::vector<Note>& notes, const NoteSelection& selection, int ticks)
{
    return changed(notes, selection, [ticks](Note note) {
        const long long length = static_cast<long long>(note.length) + ticks;
        note.length = static_cast<int>(std::clamp<long long>(length, kMinNoteLength, kLatestStart));
        return note;
    });
}

std::vector<Note> withoutNotes(const std::vector<Note>& notes, const NoteSelection& selection)
{
    std::vector<Note> out;
    out.reserve(notes.size());
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (!isSelected(selection, i))
            out.push_back(notes[i]);
    return out;
}

std::vector<Note> quantizedNotes(const std::vector<Note>& notes, const NoteSelection& selection, int grid)
{
    if (grid <= 0)
        return notes;
    return changed(notes, selection, [grid](Note note) {
        note.start = static_cast<int>(std::lround(static_cast<double>(note.start) / grid)) * grid;
        return note;
    });
}

std::vector<Note> withVelocity(const std::vector<Note>& notes, const NoteSelection& selection, int velocity)
{
    return changed(notes, selection, [velocity](Note note) {
        note.velocity = velocity;
        return note;
    });
}

std::vector<Note> withPasted(const std::vector<Note>& notes, const std::vector<Note>& pasted, int at)
{
    if (pasted.empty() || notes.size() + pasted.size() > kMaxPatternNotes)
        return notes;
    int earliest = pasted.front().start;
    for (const Note& note : pasted)
        earliest = std::min(earliest, note.start);
    std::vector<Note> out = notes;
    out.reserve(notes.size() + pasted.size());
    for (Note note : pasted) {
        note.start = std::clamp(note.start - earliest, 0, kLatestStart) + std::clamp(at, 0, kLatestStart);
        out.push_back(Project::clamped(note));
    }
    return out;
}

} // namespace vloomdaw
