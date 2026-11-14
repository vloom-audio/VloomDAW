// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/Project.hpp"

#include "core/Names.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace vloomdaw {

Project::Project()
{
    for (const char* name : {"Kick", "Clap", "Hat", "Snare"})
        channels_.push_back(Channel{name, false, {}});
    addPattern();
    for (int t = 0; t < kDefaultPlaylistTracks; ++t)
        addTrack();
}

// ---------------------------------------------------------------- channels

std::size_t Project::addChannel(const std::string& name)
{
    channels_.push_back(Channel{name, false, {}});
    for (Pattern& p : patterns_) {
        ChannelPattern& added = p.channels.emplace_back();
        added.steps.assign(static_cast<std::size_t>(p.steps), false);
    }
    return channels_.size() - 1;
}

std::optional<std::size_t> Project::channelWithSample(const std::string& path) const
{
    for (std::size_t c = 0; c < channels_.size(); ++c)
        if (!path.empty() && channels_[c].samplePath == path)
            return c;
    return std::nullopt;
}

void Project::setChannelMuted(std::size_t channel, bool muted)
{
    if (channel < channels_.size())
        channels_[channel].muted = muted;
}

void Project::setChannelColor(std::size_t channel, std::optional<std::uint32_t> color)
{
    if (channel < channels_.size())
        channels_[channel].color = color ? std::optional<std::uint32_t>(*color & 0xffffffu) : std::nullopt;
}

void Project::setChannelInstrument(std::size_t channel, std::optional<PluginSlot> instrument)
{
    if (channel < channels_.size())
        channels_[channel].instrument = std::move(instrument);
}

void Project::setChannelName(std::size_t channel, const std::string& name)
{
    if (channel < channels_.size())
        channels_[channel].name = name;
}

void Project::setChannelSample(std::size_t channel, const std::string& path)
{
    if (channel < channels_.size())
        channels_[channel].samplePath = path;
}

void Project::setChannelVolume(std::size_t channel, double volume)
{
    if (channel < channels_.size() && !std::isnan(volume))
        channels_[channel].volume = std::clamp(volume, 0.0, 1.0);
}

void Project::setChannelPan(std::size_t channel, double pan)
{
    if (channel < channels_.size() && !std::isnan(pan))
        channels_[channel].pan = std::clamp(pan, -1.0, 1.0);
}

void Project::setChannelInsert(std::size_t channel, int insert)
{
    if (channel < channels_.size())
        channels_[channel].insert = std::max(insert, 0);
}

std::size_t Project::removeInsert(int insert)
{
    if (insert < 1)
        return 0;
    std::size_t toMaster = 0;
    for (Channel& c : channels_) {
        if (c.insert == insert) {
            c.insert = 0;
            ++toMaster;
        } else if (c.insert > insert) {
            --c.insert;
        }
    }
    return toMaster;
}

void Project::moveInsert(int from, int to)
{
    if (from < 1 || to < 1 || from == to)
        return;
    for (Channel& c : channels_)
        if (c.insert >= 1)
            c.insert = static_cast<int>(movedIndex(static_cast<std::size_t>(c.insert),
                static_cast<std::size_t>(from), static_cast<std::size_t>(to)));
}

// ---------------------------------------------------------------- patterns

bool Project::patternNameUsed(const std::string& name) const
{
    return std::any_of(patterns_.begin(), patterns_.end(), [&](const Pattern& p) { return p.name == name; });
}

Pattern Project::emptyPattern(std::string name) const
{
    Pattern p;
    p.name = std::move(name);
    p.channels.resize(channels_.size());
    return p;
}

std::size_t Project::insertPatternAt(std::size_t at, Pattern pattern)
{
    at = std::min(at, patterns_.size());
    patterns_.insert(patterns_.begin() + static_cast<std::ptrdiff_t>(at), std::move(pattern));
    for (Clip& c : clips_)
        if (c.pattern >= at)
            ++c.pattern;
    return at;
}

std::size_t Project::addPattern() { return insertPattern(patterns_.size()); }

std::size_t Project::insertPattern(std::size_t at)
{
    int number = 1;
    while (patternNameUsed("Pattern " + std::to_string(number)))
        ++number;
    return insertPatternAt(at, emptyPattern("Pattern " + std::to_string(number)));
}

std::optional<std::size_t> Project::clonePattern(std::size_t pattern)
{
    if (pattern >= patterns_.size())
        return std::nullopt;
    Pattern copy = patterns_[pattern];
    int number = 2;
    while (patternNameUsed(copy.name + " #" + std::to_string(number)))
        ++number;
    copy.name += " #" + std::to_string(number);
    return insertPatternAt(pattern + 1, std::move(copy));
}

void Project::removePattern(std::size_t pattern)
{
    if (pattern >= patterns_.size() || patterns_.size() <= 1)
        return;
    patterns_.erase(patterns_.begin() + static_cast<std::ptrdiff_t>(pattern));
    std::erase_if(clips_, [pattern](const Clip& c) { return c.pattern == pattern; });
    for (Clip& c : clips_)
        if (c.pattern > pattern)
            --c.pattern;
}

std::size_t Project::movePattern(std::size_t from, std::size_t to)
{
    if (from >= patterns_.size())
        return from;
    to = std::min(to, patterns_.size() - 1);
    if (from == to)
        return to;
    Pattern moved = std::move(patterns_[from]);
    patterns_.erase(patterns_.begin() + static_cast<std::ptrdiff_t>(from));
    patterns_.insert(patterns_.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
    // Every index between the two shifts by one towards `from`.
    for (Clip& c : clips_) {
        if (c.pattern == from)
            c.pattern = to;
        else if (from < to && c.pattern > from && c.pattern <= to)
            --c.pattern;
        else if (to < from && c.pattern >= to && c.pattern < from)
            ++c.pattern;
    }
    return to;
}

void Project::setPatternName(std::size_t pattern, const std::string& name)
{
    if (pattern >= patterns_.size()
        || std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isspace(c) != 0; }))
        return;
    patterns_[pattern].name = name;
}

bool Project::isPatternEmpty(std::size_t pattern) const
{
    if (pattern >= patterns_.size())
        return false;
    return std::all_of(
        patterns_[pattern].channels.begin(), patterns_[pattern].channels.end(), [](const ChannelPattern& cp) {
            return cp.notes.empty()
                && std::none_of(cp.steps.begin(), cp.steps.end(), [](bool on) { return on; });
        });
}

std::optional<std::size_t> Project::findEmptyPattern(std::size_t from) const
{
    for (std::size_t i = from; i < patterns_.size(); ++i)
        if (isPatternEmpty(i))
            return i;
    return std::nullopt;
}

void Project::transposePattern(std::size_t pattern, int semitones)
{
    if (pattern >= patterns_.size() || semitones == 0)
        return;
    int lowest = kHighestKey;
    int highest = kLowestKey;
    for (const ChannelPattern& cp : patterns_[pattern].channels)
        for (const Note& n : cp.notes) {
            lowest = std::min(lowest, n.key);
            highest = std::max(highest, n.key);
        }
    if (lowest > highest)
        return; // no notes
    // Shift the whole set as far as asked, but only as far as the range allows.
    const int shift = std::clamp(semitones, kLowestKey - lowest, kHighestKey - highest);
    for (ChannelPattern& cp : patterns_[pattern].channels)
        for (Note& n : cp.notes)
            n.key += shift;
}

std::vector<std::size_t> Project::splitPatternByChannel(std::size_t pattern)
{
    std::vector<std::size_t> created;
    if (pattern >= patterns_.size())
        return created;
    const Pattern source = patterns_[pattern];
    std::size_t at = pattern + 1;
    for (std::size_t ch = 0; ch < source.channels.size() && ch < channels_.size(); ++ch) {
        const ChannelPattern& cp = source.channels[ch];
        if (cp.notes.empty() && std::none_of(cp.steps.begin(), cp.steps.end(), [](bool on) { return on; }))
            continue;
        Pattern part = emptyPattern(source.name + " - " + channels_[ch].name);
        part.steps = source.steps; // same length as the original
        for (ChannelPattern& other : part.channels)
            other.steps.assign(static_cast<std::size_t>(source.steps), false);
        part.channels[ch] = cp;
        created.push_back(insertPatternAt(at++, std::move(part)));
    }
    return created;
}

int Project::stepCount(std::size_t pattern) const
{
    return pattern < patterns_.size() ? patterns_[pattern].steps : kStepsPerBar;
}

void Project::setStepCount(std::size_t pattern, int steps)
{
    if (pattern >= patterns_.size())
        return;
    steps = std::clamp(steps, 1, kMaxSteps);
    Pattern& p = patterns_[pattern];
    p.steps = steps;
    for (ChannelPattern& cp : p.channels)
        cp.steps.resize(static_cast<std::size_t>(steps), false);
}

bool Project::hasStepsFrom(std::size_t pattern, int from) const
{
    if (pattern >= patterns_.size())
        return false;
    const auto first = static_cast<std::size_t>(std::max(from, 0));
    return std::any_of(patterns_[pattern].channels.begin(), patterns_[pattern].channels.end(),
        [first](const ChannelPattern& cp) {
            return first < cp.steps.size()
                && std::find(cp.steps.begin() + static_cast<std::ptrdiff_t>(first), cp.steps.end(), true)
                != cp.steps.end();
        });
}

int Project::patternLength(std::size_t pattern) const
{
    if (pattern >= patterns_.size())
        return kTicksPerBar;
    int end = std::max(kTicksPerBar, patterns_[pattern].steps * kTicksPerStep);
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
    clip.track = std::clamp(clip.track, 0, static_cast<int>(tracks_.size()) - 1);
    clip.start = std::max(0, clip.start);
    clip.offset = std::max(0, clip.offset);
    clip.length = std::max(0, clip.length);
    return clip;
}

std::optional<std::size_t> Project::addTrack()
{
    if (tracks_.size() >= static_cast<std::size_t>(kMaxPlaylistTracks))
        return std::nullopt;
    PlaylistTrack track;
    track.name = "Track " + std::to_string(tracks_.size() + 1);
    tracks_.push_back(std::move(track));
    return tracks_.size() - 1;
}

void Project::removeTrack(std::size_t track)
{
    if (track >= tracks_.size() || tracks_.size() <= 1)
        return;
    tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(track));
    for (std::size_t t = track; t < tracks_.size(); ++t)
        if (!tracks_[t].named)
            tracks_[t].name = "Track " + std::to_string(t + 1);
    const int removed = static_cast<int>(track);
    std::erase_if(clips_, [removed](const Clip& c) { return c.track == removed; });
    for (Clip& c : clips_)
        if (c.track > removed)
            --c.track;
    std::erase_if(audioClips_, [removed](const AudioClip& c) { return c.track == removed; });
    for (AudioClip& c : audioClips_)
        if (c.track > removed)
            --c.track;
    normalizeTrackDepths(); // its sub-tracks move up a level where they must
}

std::size_t Project::moveTrack(std::size_t from, std::size_t to)
{
    if (from >= tracks_.size())
        return from;
    to = std::min(to, tracks_.size() - 1);
    if (from == to)
        return to;
    PlaylistTrack moved = std::move(tracks_[from]);
    tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(from));
    tracks_.insert(tracks_.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
    for (std::size_t t = 0; t < tracks_.size(); ++t)
        if (!tracks_[t].named)
            tracks_[t].name = "Track " + std::to_string(t + 1);
    const auto follow = [from, to](int& track) {
        track = static_cast<int>(movedIndex(static_cast<std::size_t>(track), from, to));
    };
    for (Clip& c : clips_)
        follow(c.track);
    for (AudioClip& c : audioClips_)
        follow(c.track);
    normalizeTrackDepths();
    return to;
}

void Project::setTrackName(std::size_t track, const std::string& name)
{
    if (track >= tracks_.size())
        return;
    const std::string clean = cleanName(name, kMaxTrackNameLength);
    PlaylistTrack& t = tracks_[track];
    t.named = !clean.empty();
    t.name = t.named ? clean : "Track " + std::to_string(track + 1);
}

void Project::setTrackMuted(std::size_t track, bool muted)
{
    if (track < tracks_.size())
        tracks_[track].muted = muted;
}

void Project::setTrackSolo(std::size_t track, bool solo)
{
    if (track < tracks_.size())
        tracks_[track].solo = solo;
}

bool Project::trackAudible(std::size_t track) const
{
    if (track >= tracks_.size())
        return false;
    // Walk up the group: a muted or solo track counts for its sub-tracks.
    bool solo = false;
    for (std::optional<std::size_t> t = track; t; t = parentTrack(*t)) {
        if (tracks_[*t].muted)
            return false;
        solo = solo || tracks_[*t].solo;
    }
    const bool anySolo
        = std::any_of(tracks_.begin(), tracks_.end(), [](const PlaylistTrack& each) { return each.solo; });
    return !anySolo || solo;
}

void Project::setTrackColor(std::size_t track, std::optional<std::uint32_t> color)
{
    if (track < tracks_.size())
        tracks_[track].color = color ? std::optional<std::uint32_t>(*color & 0xffffffu) : std::nullopt;
}

void Project::setTrackHeight(std::size_t track, int height)
{
    if (track < tracks_.size())
        tracks_[track].height = std::clamp(height, PlaylistTrack::kMinHeight, PlaylistTrack::kMaxHeight);
}

void Project::setTrackCollapsed(std::size_t track, bool collapsed)
{
    if (track < tracks_.size())
        tracks_[track].collapsed = collapsed;
}

bool Project::canIndentTrack(std::size_t track) const
{
    if (track == 0 || track >= tracks_.size())
        return false;
    // Deeper than the track above, it already is that track's sub-track.
    if (tracks_[track].depth > tracks_[track - 1].depth)
        return false;
    for (std::size_t t = track; t < subTreeEnd(track); ++t)
        if (tracks_[t].depth + 1 > PlaylistTrack::kMaxDepth)
            return false;
    return true;
}

bool Project::indentTrack(std::size_t track)
{
    if (!canIndentTrack(track))
        return false;
    const std::size_t end = subTreeEnd(track);
    for (std::size_t t = track; t < end; ++t)
        ++tracks_[t].depth;
    return true;
}

bool Project::outdentTrack(std::size_t track)
{
    if (track >= tracks_.size() || tracks_[track].depth == 0)
        return false;
    const std::size_t end = subTreeEnd(track);
    for (std::size_t t = track; t < end; ++t)
        --tracks_[t].depth;
    return true;
}

std::optional<std::size_t> Project::parentTrack(std::size_t track) const
{
    if (track >= tracks_.size())
        return std::nullopt;
    const int depth = tracks_[track].depth;
    for (std::size_t t = track; t-- > 0;)
        if (tracks_[t].depth < depth)
            return t;
    return std::nullopt;
}

bool Project::hasSubTracks(std::size_t track) const
{
    return track + 1 < tracks_.size() && tracks_[track + 1].depth > tracks_[track].depth;
}

std::size_t Project::subTreeEnd(std::size_t track) const
{
    if (track >= tracks_.size())
        return tracks_.size();
    std::size_t end = track + 1;
    while (end < tracks_.size() && tracks_[end].depth > tracks_[track].depth)
        ++end;
    return end;
}

bool Project::trackVisible(std::size_t track) const
{
    if (track >= tracks_.size())
        return false;
    for (std::optional<std::size_t> t = parentTrack(track); t; t = parentTrack(*t))
        if (tracks_[*t].collapsed)
            return false;
    return true;
}

std::optional<Project::GroupSwap> Project::groupSwap(std::size_t track, bool down) const
{
    if (track >= tracks_.size())
        return std::nullopt;
    const int depth = tracks_[track].depth;
    if (down) {
        const std::size_t middle = subTreeEnd(track);
        if (middle >= tracks_.size() || tracks_[middle].depth != depth)
            return std::nullopt;
        return GroupSwap{track, middle, subTreeEnd(middle)};
    }
    // Above: skip the sub-tracks of the group before this one.
    std::size_t above = track;
    while (above > 0 && tracks_[above - 1].depth > depth)
        --above;
    if (above == 0 || tracks_[above - 1].depth != depth)
        return std::nullopt;
    return GroupSwap{above - 1, track, subTreeEnd(track)};
}

std::optional<std::size_t> Project::moveTrackGroup(std::size_t track, bool down)
{
    const auto swap = groupSwap(track, down);
    if (!swap)
        return std::nullopt;
    std::vector<std::size_t> order(tracks_.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::rotate(order.begin() + static_cast<std::ptrdiff_t>(swap->first),
        order.begin() + static_cast<std::ptrdiff_t>(swap->middle),
        order.begin() + static_cast<std::ptrdiff_t>(swap->last));
    reorderTracks(order);
    return down ? swap->first + (swap->last - swap->middle) : swap->first;
}

void Project::reorderTracks(const std::vector<std::size_t>& order)
{
    std::vector<PlaylistTrack> tracks;
    std::vector<int> newIndex(tracks_.size());
    tracks.reserve(order.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        newIndex[order[i]] = static_cast<int>(i);
        tracks.push_back(std::move(tracks_[order[i]]));
    }
    tracks_ = std::move(tracks);
    for (std::size_t t = 0; t < tracks_.size(); ++t)
        if (!tracks_[t].named)
            tracks_[t].name = "Track " + std::to_string(t + 1);
    for (Clip& c : clips_)
        c.track = newIndex[static_cast<std::size_t>(c.track)];
    for (AudioClip& c : audioClips_)
        c.track = newIndex[static_cast<std::size_t>(c.track)];
}

void Project::normalizeTrackDepths()
{
    // The outline stays valid: the first track is at the top level and no
    // track is more than one level deeper than the track above it.
    for (std::size_t t = 0; t < tracks_.size(); ++t) {
        const int most = t == 0 ? 0 : tracks_[t - 1].depth + 1;
        tracks_[t].depth = std::clamp(tracks_[t].depth, 0, std::min(most, PlaylistTrack::kMaxDepth));
    }
}

std::size_t Project::clipsOnTrack(std::size_t track) const
{
    const auto on = [track](const auto& c) { return c.track == static_cast<int>(track); };
    return static_cast<std::size_t>(std::count_if(clips_.begin(), clips_.end(), on))
        + static_cast<std::size_t>(std::count_if(audioClips_.begin(), audioClips_.end(), on));
}

AudioClip Project::clamped(AudioClip clip) const
{
    clip.track = std::clamp(clip.track, 0, static_cast<int>(tracks_.size()) - 1);
    clip.start = std::max(0, clip.start);
    clip.offset = std::max(0, clip.offset);
    clip.length = std::max(0, clip.length);
    // A tempo outside the range (or NaN) is no tempo: the clip is not fitted.
    if (!(clip.sourceBpm >= kMinSourceBpm && clip.sourceBpm <= kMaxSourceBpm))
        clip.sourceBpm = 0.0;
    return clip;
}

std::optional<std::size_t> Project::addAudioClip(AudioClip clip)
{
    if (clip.channel >= channels_.size())
        return std::nullopt;
    audioClips_.push_back(clamped(std::move(clip)));
    return audioClips_.size() - 1;
}

void Project::setAudioClip(std::size_t index, AudioClip clip)
{
    if (index < audioClips_.size() && clip.channel < channels_.size())
        audioClips_[index] = clamped(std::move(clip));
}

void Project::removeAudioClip(std::size_t index)
{
    if (index < audioClips_.size())
        audioClips_.erase(audioClips_.begin() + static_cast<std::ptrdiff_t>(index));
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
