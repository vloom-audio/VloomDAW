// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/PluginSlot.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vloomdaw {

/// One mixer track's settings.
struct MixerTrack {
    std::string name;
    bool named = false; ///< the user named it; otherwise "Insert N", following its number
    double fader = 0.8; ///< 0..1, see faderToGain(); 0.8 is unity
    double pan = 0.0; ///< -1 left .. +1 right
    bool muted = false;
    bool soloed = false;
    std::optional<std::uint32_t> color{}; ///< 0xRRGGBB chosen by the user; none: no colour
    /// The track's effect chain, in processing order (the master's runs on
    /// the mix of everything, before its volume and pan).
    std::vector<PluginSlot> effects{};
};

/// The mixer: track 0 is the master, tracks 1..N are inserts, as in FL
/// Studio. Holds settings only; the engine renders the audio.
///
/// Solo works on inserts: while any insert is soloed, the other inserts are
/// silent. The master is never silenced by solo, only by its own mute.
class MixerModel {
public:
    static constexpr std::size_t kMaster = 0;
    /// Most inserts: as many as the engine routes (vloom::kMaxInserts).
    static constexpr std::size_t kMaxInserts = 125;
    /// Most effects on one insert.
    static constexpr std::size_t kMaxEffects = 10;
    /// Longest insert name, in bytes (UTF-8).
    static constexpr std::size_t kMaxNameLength = 64;

    /// A master plus `insertCount` inserts named "Insert 1".. "Insert N".
    explicit MixerModel(std::size_t insertCount);

    [[nodiscard]] std::size_t size() const { return tracks_.size(); }
    [[nodiscard]] const MixerTrack& track(std::size_t index) const { return tracks_.at(index); }
    [[nodiscard]] std::size_t insertCount() const { return tracks_.size() - 1; }

    /// Appends an insert named "Insert N"; its index, or nullopt when there
    /// are kMaxInserts already.
    std::optional<std::size_t> addInsert();
    /// Removes insert `index` (never the master); the inserts after it move
    /// down one and are named after their new number. False if out of range.
    bool removeInsert(std::size_t index);
    /// Names insert `index` (UTF-8; trimmed, cut to kMaxNameLength without
    /// splitting a character). A blank name gives back "Insert N". The master
    /// keeps its name.
    void setName(std::size_t index, const std::string& name);
    /// Moves insert `from` to position `to` (both clamped to the inserts;
    /// never the master). Settings go with it; unnamed inserts take their new
    /// number. Returns the new position.
    std::size_t moveInsert(std::size_t from, std::size_t to);

    // Setters clamp their value and ignore out-of-range indices and NaN.
    void setFader(std::size_t index, double position);
    void setPan(std::size_t index, double pan);
    void setMuted(std::size_t index, bool muted);
    void setSoloed(std::size_t index, bool soloed);
    /// The track's colour, 0xRRGGBB (higher bits dropped); nullopt: none.
    void setColor(std::size_t index, std::optional<std::uint32_t> color);

    /// Appends `effect` to track `index`'s chain (the master's too). False
    /// for an index out of range or a full chain (kMaxEffects).
    bool addEffect(std::size_t index, PluginSlot effect);
    /// Removes effect `slot` of insert `index`; false if there is none.
    bool removeEffect(std::size_t index, std::size_t slot);
    /// Bypasses (or un-bypasses) effect `slot` of insert `index`.
    void setEffectBypassed(std::size_t index, std::size_t slot, bool bypassed);

    /// True while any insert is soloed.
    [[nodiscard]] bool anySoloed() const;
    /// The gain track `index` contributes: fader gain, or 0 when muted or
    /// silenced by another insert's solo.
    [[nodiscard]] double effectiveGain(std::size_t index) const;

private:
    [[nodiscard]] bool valid(std::size_t index) const { return index < tracks_.size(); }

    std::vector<MixerTrack> tracks_;
};

} // namespace vloomdaw
