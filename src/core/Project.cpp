// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/Project.hpp"

#include <algorithm>

namespace vloomdaw {

Project::Project()
{
    for (const char* name : {"Kick", "Clap", "Hat", "Snare"})
        channels_.push_back(Channel{name, false});
    addPattern();
}

// ---------------------------------------------------------------- channels

std::size_t Project::addChannel(const std::string& name)
{
    channels_.push_back(Channel{name, false});
    for (Pattern& p : patterns_)
        p.channels.emplace_back();
    return channels_.size() - 1;
}

void Project::setChannelMuted(std::size_t channel, bool muted)
{
    if (channel < channels_.size())
        channels_[channel].muted = muted;
}

// ---------------------------------------------------------------- patterns

std::size_t Project::addPattern()
{
    Pattern p;
    p.name = "Pattern " + std::to_string(patterns_.size() + 1);
    p.channels.resize(channels_.size());
    patterns_.push_back(std::move(p));
    return patterns_.size() - 1;
}

void Project::removePattern(std::size_t pattern)
{
    if (pattern >= patterns_.size() || patterns_.size() <= 1)
        return;
    patterns_.erase(patterns_.begin() + static_cast<std::ptrdiff_t>(pattern));
    // Names are numbers, as in the pattern selector: renumber.
    for (std::size_t i = 0; i < patterns_.size(); ++i)
        patterns_[i].name = "Pattern " + std::to_string(i + 1);

    std::erase_if(clips_, [pattern](const Clip& c) { return c.pattern == pattern; });
    for (Clip& c : clips_)
        if (c.pattern > pattern)
            --c.pattern;
}

int Project::patternLength(std::size_t pattern) const
{
    if (pattern >= patterns_.size())
        return kTicksPerBar;
    int end = kTicksPerBar; // steps fill one bar
    for (const ChannelPattern& cp : patterns_[pattern].channels)
        for (const Note& n : cp.notes)
            end = std::max(end, n.end());
    return (end + kTicksPerBar - 1) / kTicksPerBar * kTicksPerBar;
}

// ---------------------------------------------------------------- steps

ChannelPattern* Project::channelPattern(std::size_t pattern, std::size_t channel)
{
    if (pattern >= patterns_.size() || channel >= patterns_[pattern].channels.size())
        return nullptr;
    return &patterns_[pattern].channels[channel];
}

const ChannelPattern* Project::channelPattern(std::size_t pattern, std::size_t channel) const
{
    if (pattern >= patterns_.size() || channel >= patterns_[pattern].channels.size())
        return nullptr;
    return &patterns_[pattern].channels[channel];
}

bool Project::step(std::size_t pattern, std::size_t channel, int step) const
{
    const ChannelPattern* cp = channelPattern(pattern, channel);
    return cp != nullptr && step >= 0 && step < static_cast<int>(cp->steps.size())
        && cp->steps[static_cast<std::size_t>(step)];
}

void Project::setStep(std::size_t pattern, std::size_t channel, int step, bool on)
{
    ChannelPattern* cp = channelPattern(pattern, channel);
    if (cp != nullptr && step >= 0 && step < static_cast<int>(cp->steps.size()))
        cp->steps[static_cast<std::size_t>(step)] = on;
}

void Project::toggleStep(std::size_t pattern, std::size_t channel, int step)
{
    setStep(pattern, channel, step, !this->step(pattern, channel, step));
}

// ---------------------------------------------------------------- notes

Note Project::clamped(Note note)
{
    note.key = std::clamp(note.key, kLowestKey, kHighestKey);
    note.start = std::max(0, note.start);
    note.length = std::max(kMinNoteLength, note.length);
    return note;
}

const std::vector<Note>* Project::notes(std::size_t pattern, std::size_t channel) const
{
    const ChannelPattern* cp = channelPattern(pattern, channel);
    return cp != nullptr ? &cp->notes : nullptr;
}

std::optional<std::size_t> Project::addNote(std::size_t pattern, std::size_t channel, Note note)
{
    ChannelPattern* cp = channelPattern(pattern, channel);
    if (cp == nullptr)
        return std::nullopt;
    cp->notes.push_back(clamped(note));
    return cp->notes.size() - 1;
}

void Project::setNote(std::size_t pattern, std::size_t channel, std::size_t index, Note note)
{
    ChannelPattern* cp = channelPattern(pattern, channel);
    if (cp != nullptr && index < cp->notes.size())
        cp->notes[index] = clamped(note);
}

void Project::removeNote(std::size_t pattern, std::size_t channel, std::size_t index)
{
    ChannelPattern* cp = channelPattern(pattern, channel);
    if (cp != nullptr && index < cp->notes.size())
        cp->notes.erase(cp->notes.begin() + static_cast<std::ptrdiff_t>(index));
}

// ---------------------------------------------------------------- playlist

Clip Project::clamped(Clip clip) const
{
    clip.track = std::clamp(clip.track, 0, kPlaylistTracks - 1);
    clip.start = std::max(0, clip.start);
    return clip;
}

std::optional<std::size_t> Project::addClip(Clip clip)
{
    if (clip.pattern >= patterns_.size())
        return std::nullopt;
    clips_.push_back(clamped(clip));
    return clips_.size() - 1;
}

void Project::setClip(std::size_t index, Clip clip)
{
    if (index < clips_.size() && clip.pattern < patterns_.size())
        clips_[index] = clamped(clip);
}

void Project::removeClip(std::size_t index)
{
    if (index < clips_.size())
        clips_.erase(clips_.begin() + static_cast<std::ptrdiff_t>(index));
}

} // namespace vloomdaw
