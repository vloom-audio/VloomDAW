// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/UndoHistory.hpp"

#include <algorithm>
#include <utility>

namespace vloomdaw {

UndoHistory::UndoHistory(std::size_t maxSteps)
    : maxSteps_(std::max<std::size_t>(1, maxSteps))
{
}

void UndoHistory::push(
    UndoSnapshot beforeState, std::string label, std::string coalesceKey, std::int64_t nowMs)
{
    redoStack_.clear();

    if (!coalesceKey.empty() && !undoStack_.empty()) {
        Entry& last = undoStack_.back();
        if (last.coalesceKey == coalesceKey && nowMs >= last.timestampMs) {
            const std::uint64_t elapsed
                = static_cast<std::uint64_t>(nowMs) - static_cast<std::uint64_t>(last.timestampMs);
            if (elapsed <= static_cast<std::uint64_t>(kCoalesceWindowMs)) {
                last.timestampMs = nowMs;
                if (!label.empty())
                    last.label = std::move(label);
                return;
            }
        }
    }

    if (undoStack_.size() >= maxSteps_)
        undoStack_.erase(undoStack_.begin());

    undoStack_.push_back(Entry{
        std::move(beforeState),
        std::move(label),
        std::move(coalesceKey),
        nowMs,
    });
}

void UndoHistory::breakCoalescing() noexcept
{
    if (!undoStack_.empty())
        undoStack_.back().coalesceKey.clear();
}

std::string_view UndoHistory::undoLabel() const noexcept
{
    return undoStack_.empty() ? std::string_view{} : std::string_view{undoStack_.back().label};
}

std::string_view UndoHistory::redoLabel() const noexcept
{
    return redoStack_.empty() ? std::string_view{} : std::string_view{redoStack_.back().label};
}

std::optional<UndoSnapshot> UndoHistory::undo(UndoSnapshot currentState)
{
    if (undoStack_.empty())
        return std::nullopt;

    Entry entry = std::move(undoStack_.back());
    undoStack_.pop_back();

    if (redoStack_.size() >= maxSteps_)
        redoStack_.erase(redoStack_.begin());
    redoStack_.push_back(Entry{std::move(currentState), std::move(entry.label), {}, 0});

    breakCoalescing();
    return std::move(entry.snapshot);
}

std::optional<UndoSnapshot> UndoHistory::redo(UndoSnapshot currentState)
{
    if (redoStack_.empty())
        return std::nullopt;

    Entry entry = std::move(redoStack_.back());
    redoStack_.pop_back();

    if (undoStack_.size() >= maxSteps_)
        undoStack_.erase(undoStack_.begin());
    undoStack_.push_back(Entry{std::move(currentState), std::move(entry.label), {}, 0});

    return std::move(entry.snapshot);
}

void UndoHistory::clear() noexcept
{
    undoStack_.clear();
    redoStack_.clear();
}

} // namespace vloomdaw
