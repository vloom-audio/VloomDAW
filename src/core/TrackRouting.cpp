// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/TrackRouting.hpp"

#include <algorithm>
#include <array>

namespace vloomdaw {

namespace {

constexpr std::array<std::uint32_t, 20> kTrackPalette{{
    0xf87171u, // coral red
    0xfb7185u, // rose
    0xf472b6u, // pink
    0xe879f9u, // fuchsia
    0xc084fcu, // purple
    0xa78bfau, // violet
    0x818cf8u, // indigo
    0x60a5fau, // blue
    0x38bdf8u, // sky
    0x22d3eeu, // cyan
    0x2dd4bfu, // teal
    0x34d399u, // emerald
    0x4ade80u, // green
    0x84cc16u, // lime-green
    0xa3e635u, // lime
    0xfacc15u, // yellow
    0xfbbf24u, // amber
    0xf59e0bu, // gold
    0xfb923cu, // orange
    0xf97316u, // deep orange
}};

bool isInsertUntouched(const MixerTrack& t)
{
    return t.role == MixerRole::Standard && t.output == 0 && t.sends.empty() && !t.named && t.effects.empty()
        && !t.color.has_value() && !t.muted && !t.soloed && t.fader == 0.8 && t.pan == 0.0;
}

bool isInsertUsed(const Project& project, int insert)
{
    if (std::any_of(project.channels().begin(), project.channels().end(),
            [insert](const Channel& ch) { return ch.insert == insert; }))
        return true;
    return std::any_of(project.tracks().begin(), project.tracks().end(),
        [insert](const PlaylistTrack& pt) { return pt.insert == insert; });
}

bool isInsertUsedExcluding(const Project& project, int insert, std::optional<std::size_t> skipTrack,
    std::optional<std::size_t> skipChannel)
{
    for (std::size_t c = 0; c < project.channels().size(); ++c) {
        if (skipChannel && c == *skipChannel)
            continue;
        if (project.channels()[c].insert == insert)
            return true;
    }
    for (std::size_t t = 0; t < project.tracks().size(); ++t) {
        if (skipTrack && t == *skipTrack)
            continue;
        if (project.tracks()[t].insert == insert)
            return true;
    }
    return false;
}

/// True when `insert` (`>= 1`) belongs exclusively to `channel` and `channel`
/// is not placed on any playlist track other than `track`.
bool isDedicatedInsert(
    const Project& project, const MixerModel& mixer, std::size_t track, std::size_t channel, int insert)
{
    if (insert <= 0 || static_cast<std::size_t>(insert) >= mixer.size())
        return false;
    if (isInsertUsedExcluding(project, insert, track, channel))
        return false;
    const auto targetTrack = static_cast<int>(track);
    for (const AudioClip& clip : project.audioClips()) {
        if (clip.channel == channel && clip.track != targetTrack)
            return false;
    }
    return true;
}

/// True when an empty track's `pt.insert` (`>= 1`) is not shared by any other track or channel.
bool isDedicatedEmptyTrackInsert(
    const Project& project, const MixerModel& mixer, std::size_t track, int insert)
{
    if (insert <= 0 || static_cast<std::size_t>(insert) >= mixer.size())
        return false;
    return !isInsertUsedExcluding(project, insert, track, std::nullopt);
}

bool trackInSoloSubtree(const Project& project, std::size_t track)
{
    for (std::optional<std::size_t> t = track; t; t = project.parentTrack(*t)) {
        if (project.tracks()[*t].solo)
            return true;
    }
    return false;
}

bool isChannelUsedInAnyPattern(const Project& project, std::size_t channel)
{
    for (const Pattern& pat : project.patterns()) {
        if (channel >= pat.channels.size())
            continue;
        const ChannelPattern& cp = pat.channels[channel];
        if (!cp.notes.empty() || std::any_of(cp.steps.begin(), cp.steps.end(), [](bool on) { return on; })) {
            return true;
        }
    }
    return false;
}

} // namespace

std::uint32_t trackPaletteColor(std::size_t index) noexcept
{
    return kTrackPalette[index % kTrackPalette.size()];
}

std::size_t linkChannelToInsert(Project& project, MixerModel& mixer, std::size_t channel)
{
    if (channel >= project.channels().size())
        return 0;
    const Channel& ch = project.channels()[channel];
    if (ch.insert > 0) {
        const auto target = static_cast<std::size_t>(ch.insert);
        while (mixer.insertCount() < target && mixer.addInsert().has_value()) { }
        if (target < mixer.size()) {
            if (!mixer.track(target).named)
                mixer.setName(target, ch.name);
            if (!mixer.track(target).color && ch.color)
                mixer.setColor(target, ch.color);
            return target;
        }
        return 0;
    }

    std::size_t chosen = 0;
    for (std::size_t i = 1; i < mixer.size(); ++i) {
        if (!isInsertUsed(project, static_cast<int>(i)) && isInsertUntouched(mixer.track(i))) {
            chosen = i;
            break;
        }
    }
    if (chosen == 0) {
        if (const auto added = mixer.addInsert())
            chosen = *added;
        else
            return 0;
    }

    project.setChannelInsert(channel, static_cast<int>(chosen));
    mixer.setName(chosen, project.channels()[channel].name);
    if (project.channels()[channel].color)
        mixer.setColor(chosen, project.channels()[channel].color);
    return chosen;
}

void linkAllChannelsToInserts(Project& project, MixerModel& mixer)
{
    for (std::size_t c = 0; c < project.channels().size(); ++c) {
        if (project.channels()[c].insert != 0)
            continue;
        if (!project.channels()[c].color)
            project.setChannelColor(c, trackPaletteColor(c));
        linkChannelToInsert(project, mixer, c);
    }
}

void syncTrackColor(
    Project& project, MixerModel& mixer, std::size_t track, std::optional<std::uint32_t> color)
{
    project.setTrackColor(track, color);
    if (track >= project.tracks().size())
        return;

    const auto targetTrack = static_cast<int>(track);
    bool updatedInsert = false;
    for (std::size_t c = 0; c < project.channels().size(); ++c) {
        bool onTarget = false;
        bool onOther = false;
        for (const AudioClip& clip : project.audioClips()) {
            if (clip.channel != c)
                continue;
            if (clip.track == targetTrack)
                onTarget = true;
            else
                onOther = true;
        }
        if (!onTarget || onOther)
            continue;

        project.setChannelColor(c, color);
        const int ins = project.channels()[c].insert;
        if (ins <= 0 || static_cast<std::size_t>(ins) >= mixer.size())
            continue;
        if (!isInsertUsedExcluding(project, ins, track, c)) {
            mixer.setColor(static_cast<std::size_t>(ins), color);
            updatedInsert = true;
        }
    }

    if (!updatedInsert) {
        const int ins = trackMixerInsert(project, mixer, track);
        if (ins > 0 && static_cast<std::size_t>(ins) < mixer.size()) {
            const auto ch = trackPrimaryChannel(project, track);
            if (!isInsertUsedExcluding(project, ins, track, ch))
                mixer.setColor(static_cast<std::size_t>(ins), color);
        }
    }
}

void syncInsertColor(
    Project& project, MixerModel& mixer, std::size_t insert, std::optional<std::uint32_t> color)
{
    if (insert >= mixer.size())
        return;
    mixer.setColor(insert, color);
    if (insert == MixerModel::kMaster)
        return;

    const auto targetIns = static_cast<int>(insert);
    std::vector<std::size_t> matchingTracks;
    for (std::size_t t = 0; t < project.tracks().size(); ++t) {
        if (trackMixerInsert(project, mixer, t) == targetIns)
            matchingTracks.push_back(t);
    }
    if (matchingTracks.size() == 1)
        project.setTrackColor(matchingTracks.front(), color);

    std::vector<std::size_t> matchingChannels;
    for (std::size_t c = 0; c < project.channels().size(); ++c) {
        if (project.channels()[c].insert == targetIns)
            matchingChannels.push_back(c);
    }
    if (matchingChannels.size() == 1)
        project.setChannelColor(matchingChannels.front(), color);
}

void syncTrackName(Project& project, MixerModel& mixer, std::size_t track, const std::string& name)
{
    if (track >= project.tracks().size())
        return;
    project.setTrackName(track, name);
    const PlaylistTrack& pt = project.tracks()[track];
    const int ins = trackMixerInsert(project, mixer, track);
    if (ins <= 0 || static_cast<std::size_t>(ins) >= mixer.size())
        return;

    const auto ch = trackPrimaryChannel(project, track);
    if (!isInsertUsedExcluding(project, ins, track, ch))
        mixer.setName(static_cast<std::size_t>(ins), pt.named ? pt.name : std::string());
}

void syncInsertName(Project& project, MixerModel& mixer, std::size_t insert, const std::string& name)
{
    if (insert == MixerModel::kMaster || insert >= mixer.size())
        return;
    mixer.setName(insert, name);
    const MixerTrack& mt = mixer.track(insert);

    const auto targetIns = static_cast<int>(insert);
    std::vector<std::size_t> matchingTracks;
    for (std::size_t t = 0; t < project.tracks().size(); ++t) {
        if (trackMixerInsert(project, mixer, t) == targetIns)
            matchingTracks.push_back(t);
    }
    if (matchingTracks.size() == 1)
        project.setTrackName(matchingTracks.front(), mt.named ? mt.name : std::string());
}

void syncTrackMute(Project& project, MixerModel& mixer, std::size_t track, bool muted)
{
    if (track >= project.tracks().size())
        return;
    project.setTrackMuted(track, muted);

    const int ins = trackMixerInsert(project, mixer, track);
    if (ins <= 0 || static_cast<std::size_t>(ins) >= mixer.size())
        return;

    if (!muted) {
        mixer.setMuted(static_cast<std::size_t>(ins), false);
        return;
    }

    // Mute the insert only when every playlist track feeding it is muted.
    bool anyUnmutedOnInsert = false;
    for (std::size_t t = 0; t < project.tracks().size(); ++t) {
        if (trackMixerInsert(project, mixer, t) == ins && !project.tracks()[t].muted) {
            anyUnmutedOnInsert = true;
            break;
        }
    }
    if (!anyUnmutedOnInsert)
        mixer.setMuted(static_cast<std::size_t>(ins), true);
}

void syncInsertMute(Project& project, MixerModel& mixer, std::size_t insert, bool muted)
{
    if (insert >= mixer.size())
        return;
    mixer.setMuted(insert, muted);
    if (insert == MixerModel::kMaster)
        return;

    const auto targetIns = static_cast<int>(insert);
    for (std::size_t t = 0; t < project.tracks().size(); ++t) {
        if (trackMixerInsert(project, mixer, t) == targetIns)
            project.setTrackMuted(t, muted);
    }
}

void syncTrackSolo(Project& project, MixerModel& mixer, std::size_t track, bool solo)
{
    if (track >= project.tracks().size())
        return;
    project.setTrackSolo(track, solo);

    const bool anyTrackSolo = std::any_of(
        project.tracks().begin(), project.tracks().end(), [](const PlaylistTrack& pt) { return pt.solo; });

    for (std::size_t ins = 1; ins < mixer.size(); ++ins) {
        bool boundToAnyTrack = false;
        bool boundToSoloedSubtree = false;
        for (std::size_t t = 0; t < project.tracks().size(); ++t) {
            if (trackMixerInsert(project, mixer, t) == static_cast<int>(ins)) {
                boundToAnyTrack = true;
                if (anyTrackSolo && trackInSoloSubtree(project, t))
                    boundToSoloedSubtree = true;
            }
        }
        if (boundToAnyTrack)
            mixer.setSoloed(ins, anyTrackSolo && boundToSoloedSubtree);
    }
}

void syncInsertSolo(Project& project, MixerModel& mixer, std::size_t insert, bool solo)
{
    if (insert == MixerModel::kMaster || insert >= mixer.size())
        return;
    mixer.setSoloed(insert, solo);

    const auto targetIns = static_cast<int>(insert);
    for (std::size_t t = 0; t < project.tracks().size(); ++t) {
        if (trackMixerInsert(project, mixer, t) == targetIns)
            project.setTrackSolo(t, solo);
    }
}

bool trackEffectiveAudible(const Project& project, const MixerModel& mixer, std::size_t track)
{
    if (!project.trackAudible(track))
        return false;
    const int ins = trackMixerInsert(project, mixer, track);
    if (ins > 0 && static_cast<std::size_t>(ins) < mixer.size()) {
        if (!mixer.isTrackAudible(static_cast<std::size_t>(ins)))
            return false;
    }
    return true;
}

void pairTrackWithInsert(Project& project, MixerModel& mixer, std::size_t track, std::size_t insert)
{
    if (track >= project.tracks().size() || insert == MixerModel::kMaster || insert >= mixer.size())
        return;

    project.setTrackInsert(track, static_cast<int>(insert));
    const PlaylistTrack& pt = project.tracks()[track];
    const MixerTrack& mt = mixer.track(insert);

    if (pt.named)
        mixer.setName(insert, pt.name);
    else if (mt.named)
        project.setTrackName(track, mt.name);

    if (pt.color) {
        mixer.setColor(insert, pt.color);
    } else if (mt.color) {
        project.setTrackColor(track, mt.color);
    } else {
        const std::uint32_t c = trackPaletteColor(track);
        project.setTrackColor(track, c);
        mixer.setColor(insert, c);
    }

    if (pt.volume != 0.8)
        mixer.setFader(insert, pt.volume);
    else
        project.setTrackVolume(track, mt.fader);

    if (pt.pan != 0.0)
        mixer.setPan(insert, pt.pan);
    else
        project.setTrackPan(track, mt.pan);

    const bool muted = pt.muted || mt.muted;
    project.setTrackMuted(track, muted);
    mixer.setMuted(insert, muted);

    if (pt.solo || mt.soloed)
        syncTrackSolo(project, mixer, track, true);
}

std::size_t bindAudioToTrack(Project& project, MixerModel& mixer, std::size_t track,
    const std::string& samplePath, const std::string& stemName)
{
    while (project.tracks().size() <= track && project.addTrack().has_value()) { }
    if (track >= project.tracks().size())
        track = project.tracks().size() - 1;

    const bool wasEmpty = project.clipsOnTrack(track) == 0;
    const double presetVol = project.tracks()[track].volume;
    const double presetPan = project.tracks()[track].pan;

    const std::uint32_t color
        = project.tracks()[track].color ? *project.tracks()[track].color : trackPaletteColor(track);
    if (!project.tracks()[track].color)
        project.setTrackColor(track, color);
    if (!project.tracks()[track].named && wasEmpty && !stemName.empty())
        project.setTrackName(track, stemName);

    // Reuse an existing channel for this sample only if it is unused or already on `track`,
    // so independent stem tracks never collide onto the same channel/insert.
    std::optional<std::size_t> channel;
    if (const auto existing = project.channelWithSample(samplePath)) {
        const auto targetTrack = static_cast<int>(track);
        const bool alreadyOnTargetTrack
            = std::any_of(project.audioClips().begin(), project.audioClips().end(),
                [&](const AudioClip& c) { return c.channel == *existing && c.track == targetTrack; });
        const bool usedOnOtherTrack = std::any_of(project.audioClips().begin(), project.audioClips().end(),
            [&](const AudioClip& c) { return c.channel == *existing && c.track != targetTrack; });
        if (!usedOnOtherTrack && (alreadyOnTargetTrack || !isChannelUsedInAnyPattern(project, *existing)))
            channel = *existing;
    }

    // Also reuse an empty placeholder channel created earlier for `track`'s routing menu.
    if (!channel && wasEmpty) {
        const int linkedIns = project.tracks()[track].insert;
        if (linkedIns > 0) {
            for (std::size_t c = 0; c < project.channels().size(); ++c) {
                const Channel& ch = project.channels()[c];
                if (ch.insert == linkedIns && ch.samplePath.empty() && !ch.instrument.has_value()) {
                    const bool hasClips = std::any_of(project.audioClips().begin(),
                        project.audioClips().end(), [c](const AudioClip& ac) { return ac.channel == c; });
                    const bool sharedWithOtherTrack = isInsertUsedExcluding(project, linkedIns, track, c);
                    if (!hasClips && !sharedWithOtherTrack && !isChannelUsedInAnyPattern(project, c)) {
                        project.setChannelName(c, stemName.empty() ? project.tracks()[track].name : stemName);
                        project.setChannelSample(c, samplePath);
                        project.setChannelColor(c, color);
                        channel = c;
                        break;
                    }
                }
            }
        }
    }

    if (!channel) {
        const std::size_t added
            = project.addChannel(stemName.empty() ? project.tracks()[track].name : stemName);
        project.setChannelSample(added, samplePath);
        project.setChannelColor(added, color);
        channel = added;
    }

    // Choose the 1:1 Mixer Insert for this track without leaving pre-named strips orphaned.
    std::size_t chosen = 0;
    const int existingChIns = project.channels()[*channel].insert;
    const int existingTrkIns = project.tracks()[track].insert;

    if (existingChIns > 0) {
        const auto target = static_cast<std::size_t>(existingChIns);
        while (mixer.insertCount() < target && mixer.addInsert().has_value()) { }
        if (target < mixer.size())
            chosen = target;
    }
    if (chosen == 0 && existingTrkIns > 0
        && !isInsertUsedExcluding(project, existingTrkIns, track, *channel)) {
        const auto target = static_cast<std::size_t>(existingTrkIns);
        while (mixer.insertCount() < target && mixer.addInsert().has_value()) { }
        if (target < mixer.size())
            chosen = target;
    }
    if (chosen == 0) {
        const std::size_t positional = track + 1;
        if (positional < mixer.size() && mixer.track(positional).role == MixerRole::Standard
            && !isInsertUsedExcluding(project, static_cast<int>(positional), track, *channel)
            && mixer.track(positional).effects.empty() && mixer.track(positional).sends.empty()
            && mixer.track(positional).output == 0) {
            chosen = positional;
        }
    }
    if (chosen == 0) {
        for (std::size_t i = 1; i < mixer.size(); ++i) {
            if (!isInsertUsedExcluding(project, static_cast<int>(i), track, *channel)
                && isInsertUntouched(mixer.track(i))) {
                chosen = i;
                break;
            }
        }
    }
    if (chosen == 0) {
        if (const auto added = mixer.addInsert())
            chosen = *added;
    }

    if (chosen > 0 && chosen < mixer.size()) {
        project.setChannelInsert(*channel, static_cast<int>(chosen));
        project.setTrackInsert(track, static_cast<int>(chosen));

        const PlaylistTrack& pt = project.tracks()[track];
        mixer.setName(chosen, pt.named ? pt.name : stemName);
        mixer.setColor(chosen, color);

        if (wasEmpty) {
            if (presetVol != 0.8)
                mixer.setFader(chosen, presetVol);
            else
                project.setTrackVolume(track, mixer.track(chosen).fader);

            if (presetPan != 0.0)
                mixer.setPan(chosen, presetPan);
            else
                project.setTrackPan(track, mixer.track(chosen).pan);

            const bool muted = pt.muted || mixer.track(chosen).muted;
            project.setTrackMuted(track, muted);
            mixer.setMuted(chosen, muted);

            if (pt.solo || mixer.track(chosen).soloed)
                syncTrackSolo(project, mixer, track, true);
        }
    }

    return *channel;
}

std::optional<std::size_t> trackPrimaryChannel(const Project& project, std::size_t track)
{
    if (track >= project.tracks().size())
        return std::nullopt;
    const auto targetTrack = static_cast<int>(track);

    for (const AudioClip& clip : project.audioClips()) {
        if (clip.track == targetTrack && clip.channel < project.channels().size())
            return clip.channel;
    }
    for (const Clip& clip : project.clips()) {
        if (clip.track != targetTrack || clip.pattern >= project.patterns().size())
            continue;
        const Pattern& pat = project.patterns()[clip.pattern];
        for (std::size_t c = 0; c < pat.channels.size() && c < project.channels().size(); ++c) {
            const ChannelPattern& cp = pat.channels[c];
            const bool active = !cp.notes.empty()
                || std::any_of(cp.steps.begin(), cp.steps.end(), [](bool on) { return on; });
            if (active)
                return c;
        }
    }
    return std::nullopt;
}

int trackMixerInsert(const Project& project, const MixerModel& mixer, std::size_t track)
{
    if (track >= project.tracks().size())
        return -1;
    if (const auto ch = trackPrimaryChannel(project, track)) {
        const int ins = project.channels()[*ch].insert;
        return (ins >= 0 && static_cast<std::size_t>(ins) < mixer.size()) ? ins : 0;
    }
    const int ins = project.tracks()[track].insert;
    if (ins >= 0 && static_cast<std::size_t>(ins) < mixer.size())
        return ins;
    return -1;
}

double trackMixVolume(const Project& project, const MixerModel& mixer, std::size_t track)
{
    if (track >= project.tracks().size())
        return 0.8;
    const auto ch = trackPrimaryChannel(project, track);
    if (!ch) {
        const int ins = project.tracks()[track].insert;
        if (isDedicatedEmptyTrackInsert(project, mixer, track, ins))
            return mixer.track(static_cast<std::size_t>(ins)).fader;
        return project.tracks()[track].volume;
    }
    const int ins = project.channels()[*ch].insert;
    if (isDedicatedInsert(project, mixer, track, *ch, ins))
        return mixer.track(static_cast<std::size_t>(ins)).fader;
    return project.channels()[*ch].volume;
}

double trackMixPan(const Project& project, const MixerModel& mixer, std::size_t track)
{
    if (track >= project.tracks().size())
        return 0.0;
    const auto ch = trackPrimaryChannel(project, track);
    if (!ch) {
        const int ins = project.tracks()[track].insert;
        if (isDedicatedEmptyTrackInsert(project, mixer, track, ins))
            return mixer.track(static_cast<std::size_t>(ins)).pan;
        return project.tracks()[track].pan;
    }
    const int ins = project.channels()[*ch].insert;
    if (isDedicatedInsert(project, mixer, track, *ch, ins))
        return mixer.track(static_cast<std::size_t>(ins)).pan;
    return project.channels()[*ch].pan;
}

void setTrackMixVolume(Project& project, MixerModel& mixer, std::size_t track, double volume)
{
    if (track >= project.tracks().size())
        return;
    project.setTrackVolume(track, volume);
    const auto ch = trackPrimaryChannel(project, track);
    if (!ch) {
        const int ins = project.tracks()[track].insert;
        if (isDedicatedEmptyTrackInsert(project, mixer, track, ins))
            mixer.setFader(static_cast<std::size_t>(ins), volume);
        return;
    }
    const int ins = project.channels()[*ch].insert;
    if (isDedicatedInsert(project, mixer, track, *ch, ins))
        mixer.setFader(static_cast<std::size_t>(ins), volume);
    else
        project.setChannelVolume(*ch, volume);
}

void setTrackMixPan(Project& project, MixerModel& mixer, std::size_t track, double pan)
{
    if (track >= project.tracks().size())
        return;
    project.setTrackPan(track, pan);
    const auto ch = trackPrimaryChannel(project, track);
    if (!ch) {
        const int ins = project.tracks()[track].insert;
        if (isDedicatedEmptyTrackInsert(project, mixer, track, ins))
            mixer.setPan(static_cast<std::size_t>(ins), pan);
        return;
    }
    const int ins = project.channels()[*ch].insert;
    if (isDedicatedInsert(project, mixer, track, *ch, ins))
        mixer.setPan(static_cast<std::size_t>(ins), pan);
    else
        project.setChannelPan(*ch, pan);
}

int routeTrackToInsert(Project& project, MixerModel& mixer, std::size_t track, int insert, bool createIfEmpty)
{
    if (track >= project.tracks().size())
        return -1;
    auto ch = trackPrimaryChannel(project, track);
    const int clamped = std::clamp(insert, 0, static_cast<int>(MixerModel::kMaxInserts));

    if (!ch && !createIfEmpty)
        return -1;

    if (clamped > 0) {
        const auto target = static_cast<std::size_t>(clamped);
        while (mixer.insertCount() < target && mixer.addInsert().has_value()) { }
        if (target >= mixer.size())
            return ch ? project.channels()[*ch].insert : project.tracks()[track].insert;

        const PlaylistTrack& pt = project.tracks()[track];
        if (isInsertUntouched(mixer.track(target))) {
            mixer.setName(target, pt.named ? pt.name : (ch ? project.channels()[*ch].name : pt.name));
            const auto color = pt.color ? pt.color : (ch ? project.channels()[*ch].color : std::nullopt);
            if (color)
                mixer.setColor(target, color);
        }
    }

    project.setTrackInsert(track, clamped);
    if (ch)
        project.setChannelInsert(*ch, clamped);
    return clamped;
}

std::optional<std::size_t> insertPrimaryTrack(
    const Project& project, const MixerModel& mixer, std::size_t insert)
{
    if (insert == 0 || insert >= mixer.size())
        return std::nullopt;
    const auto target = static_cast<int>(insert);
    for (std::size_t t = 0; t < project.tracks().size(); ++t) {
        if (trackMixerInsert(project, mixer, t) == target)
            return t;
    }
    return std::nullopt;
}

std::optional<std::size_t> insertPrimaryChannel(const Project& project, std::size_t insert)
{
    if (insert == 0)
        return std::nullopt;
    const auto target = static_cast<int>(insert);
    for (std::size_t c = 0; c < project.channels().size(); ++c) {
        if (project.channels()[c].insert == target)
            return c;
    }
    return std::nullopt;
}

} // namespace vloomdaw
