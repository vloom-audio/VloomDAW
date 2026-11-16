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

std::vector<Note> slicedNotes(const std::vector<Note>& notes, int splitTick, int fromKey, int toKey)
{
    if (splitTick <= 0)
        return notes;
    const int lowKey = std::min(fromKey, toKey);
    const int highKey = std::max(fromKey, toKey);

    std::vector<Note> out;
    out.reserve(notes.size() + 8);
    std::size_t extraSplits = 0;
    const auto split = static_cast<std::int64_t>(splitTick);
    for (const Note& n : notes) {
        const auto start = static_cast<std::int64_t>(n.start);
        const auto end = start + static_cast<std::int64_t>(n.length);
        if (n.key >= lowKey && n.key <= highKey && split >= start + kMinNoteLength
            && split <= end - kMinNoteLength && notes.size() + extraSplits + 1 <= kMaxPatternNotes) {
            out.push_back(Project::clamped(Note{n.key, n.start, splitTick - n.start, n.velocity}));
            out.push_back(
                Project::clamped(Note{n.key, splitTick, static_cast<int>(end - split), n.velocity}));
            ++extraSplits;
        } else {
            out.push_back(n);
        }
    }
    return out;
}

std::vector<Note> legatoNotes(const std::vector<Note>& notes, const NoteSelection& selection)
{
    return changed(notes, selection, [&notes](Note note) {
        int nextStart = std::numeric_limits<int>::max();
        for (const Note& other : notes) {
            if (other.start > note.start && other.start < nextStart)
                nextStart = other.start;
        }
        if (nextStart != std::numeric_limits<int>::max() && nextStart - note.start >= kMinNoteLength)
            note.length = nextStart - note.start;
        return note;
    });
}

std::vector<Note> scaledVelocityNotes(
    const std::vector<Note>& notes, const NoteSelection& selection, double factor)
{
    if (!std::isfinite(factor) || factor <= 0.0)
        return notes;
    return changed(notes, selection, [factor](Note note) {
        const double raw = std::clamp(
            note.velocity * factor, static_cast<double>(kMinVelocity), static_cast<double>(kMaxVelocity));
        note.velocity = static_cast<int>(std::lround(raw));
        return note;
    });
}

std::vector<Note> snappedToScaleNotes(
    const std::vector<Note>& notes, const NoteSelection& selection, int root, Scale scale)
{
    return changed(notes, selection, [root, scale](Note note) {
        if (scaleContains(scale, root, note.key))
            return note;
        for (int dist = 1; dist <= 6; ++dist) {
            if (note.key - dist >= kLowestKey && scaleContains(scale, root, note.key - dist)) {
                note.key -= dist;
                return note;
            }
            if (note.key + dist <= kHighestKey && scaleContains(scale, root, note.key + dist)) {
                note.key += dist;
                return note;
            }
        }
        return note;
    });
}

} // namespace vloomdaw
