// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/NoteHistory.hpp"

#include <utility>

namespace vloomdaw {

void NoteHistory::record(Target target, std::vector<Note> before, std::vector<Note> after)
{
    if (before == after)
        return;
    undone_.clear();
    if (done_.size() >= kMaxEdits)
        done_.erase(done_.begin());
    done_.push_back(Edit{target, std::move(before), std::move(after)});
}

std::optional<NoteHistory::Target> NoteHistory::undo(Project& project)
{
    return step(project, done_, undone_, false);
}

std::optional<NoteHistory::Target> NoteHistory::redo(Project& project)
{
    return step(project, undone_, done_, true);
}

void NoteHistory::clear()
{
    done_.clear();
    undone_.clear();
}

std::optional<NoteHistory::Target> NoteHistory::step(
    Project& project, std::vector<Edit>& from, std::vector<Edit>& to, bool forward)
{
    if (from.empty())
        return std::nullopt;
    Edit edit = std::move(from.back());
    from.pop_back();
    const std::vector<Note>& expected = forward ? edit.before : edit.after;
    const std::vector<Note>* now = project.notes(edit.target.pattern, edit.target.channel);
    if (now == nullptr || *now != expected) {
        clear(); // the project changed under the history
        return std::nullopt;
    }
    project.setNotes(edit.target.pattern, edit.target.channel, forward ? edit.after : edit.before);
    const Target target = edit.target;
    to.push_back(std::move(edit));
    return target;
}

} // namespace vloomdaw
