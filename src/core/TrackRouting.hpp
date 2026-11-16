// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/MixerModel.hpp"
#include "core/Project.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace vloomdaw {

/// A vivid 0xRRGGBB track colour from a 20-step spectrum palette, cycling by `index`.
[[nodiscard]] std::uint32_t trackPaletteColor(std::size_t index) noexcept;

/// Links `channel` to its own mixer insert (1..MixerModel::kMaxInserts), which
/// outputs to Master: reuses the lowest free untouched insert or adds a new
/// one, naming and colouring it after the channel. Keeps an insert the channel
/// already has. Returns the 1-based insert index, or 0 (Master) if all 125
/// inserts are full.
std::size_t linkChannelToInsert(Project& project, MixerModel& mixer, std::size_t channel);

/// Gives every unrouted channel (`insert == 0`) a palette colour (when it has
/// none) and links it to a dedicated mixer insert outputting to Master.
void linkAllChannelsToInserts(Project& project, MixerModel& mixer);

/// Sets `track`'s colour in `project` and propagates it to any audio channel
/// whose playlist clips live exclusively on `track` (and to that channel's
/// dedicated mixer insert when no other channel shares it).
void syncTrackColor(
    Project& project, MixerModel& mixer, std::size_t track, std::optional<std::uint32_t> color);

/// Sets mixer `insert`'s colour and propagates it to any playlist track and
/// channel exclusively bound to `insert`.
void syncInsertColor(
    Project& project, MixerModel& mixer, std::size_t insert, std::optional<std::uint32_t> color);

/// Renames playlist `track` and mirrors the name to its dedicated mixer insert.
void syncTrackName(Project& project, MixerModel& mixer, std::size_t track, const std::string& name);

/// Renames mixer `insert` (`1..N`) and mirrors the name to its bound playlist track.
void syncInsertName(Project& project, MixerModel& mixer, std::size_t insert, const std::string& name);

/// Mutes or unmutes playlist `track` and keeps its dedicated mixer insert in sync.
void syncTrackMute(Project& project, MixerModel& mixer, std::size_t track, bool muted);

/// Mutes or unmutes mixer `insert` (`1..N`) and keeps bound playlist tracks in sync.
void syncInsertMute(Project& project, MixerModel& mixer, std::size_t insert, bool muted);

/// Solos or unsolos playlist `track` and mirrors solo state to mixer inserts
/// bound to playlist tracks (including sub-tracks of a soloed group).
void syncTrackSolo(Project& project, MixerModel& mixer, std::size_t track, bool solo);

/// Solos or unsolos mixer `insert` (`1..N`) and mirrors solo state to bound playlist tracks.
void syncInsertSolo(Project& project, MixerModel& mixer, std::size_t insert, bool solo);

/// Whether `track` is audible across both playlist mute/solo groups and its
/// routed mixer insert's mute/solo state.
[[nodiscard]] bool trackEffectiveAudible(const Project& project, const MixerModel& mixer, std::size_t track);

/// Pairs an empty/unlinked playlist `track` with mixer `insert` (`1..N`),
/// synchronising name, colour, mute, solo, volume and pan between them.
void pairTrackWithInsert(Project& project, MixerModel& mixer, std::size_t track, std::size_t insert);

/// Binds a dropped/imported audio stem (`samplePath`, `stemName`) to playlist
/// `track` and a 1:1 mixer insert without leaving pre-created/named track
/// inserts orphaned. Preserves a user-chosen track name on both track and
/// mixer strip; otherwise names both `stemName`. Returns the bound channel index.
std::size_t bindAudioToTrack(Project& project, MixerModel& mixer, std::size_t track,
    const std::string& samplePath, const std::string& stemName);

/// Primary channel playing on `track`: the first audio clip's channel on `track`,
/// or the first active channel in a pattern clip on `track`; nullopt if empty.
[[nodiscard]] std::optional<std::size_t> trackPrimaryChannel(const Project& project, std::size_t track);

/// Mixer insert index (`0` = Master, `1..N` = Insert) driven by `track`'s
/// primary channel, or `-1` when `track` has no channel yet.
[[nodiscard]] int trackMixerInsert(const Project& project, const MixerModel& mixer, std::size_t track);

/// Effective fader (`0..1`) and pan (`-1..+1`) displayed on `track`'s header:
/// reads the dedicated mixer insert when exclusive to `track`, else the primary
/// channel's trim when shared/Master, else `track`'s stored fallback.
[[nodiscard]] double trackMixVolume(const Project& project, const MixerModel& mixer, std::size_t track);
[[nodiscard]] double trackMixPan(const Project& project, const MixerModel& mixer, std::size_t track);

/// Adjusts `track`'s fader (`0..1`) or pan (`-1..+1`) from its playlist header,
/// updating its dedicated mixer insert (1:1 Studio One style), or its primary
/// channel when the insert is shared/Master, or `track`'s fallback when empty.
void setTrackMixVolume(Project& project, MixerModel& mixer, std::size_t track, double volume);
void setTrackMixPan(Project& project, MixerModel& mixer, std::size_t track, double pan);

/// Routes `track`'s primary channel (creating one if `createIfEmpty` is true)
/// to mixer `insert` (`0` = Master, `1..kMaxInserts` = Insert, growing `mixer`
/// if needed). Returns the assigned insert (`0..N`), or `-1` if `track` has no
/// channel and `createIfEmpty` is false.
int routeTrackToInsert(
    Project& project, MixerModel& mixer, std::size_t track, int insert, bool createIfEmpty = false);

/// First playlist track routed to mixer `insert` (`1..N`), or `nullopt` for Master / unused inserts.
[[nodiscard]] std::optional<std::size_t> insertPrimaryTrack(
    const Project& project, const MixerModel& mixer, std::size_t insert);

/// First channel routed to mixer `insert` (`1..N`), or `nullopt` for Master / unused inserts.
[[nodiscard]] std::optional<std::size_t> insertPrimaryChannel(const Project& project, std::size_t insert);

} // namespace vloomdaw
