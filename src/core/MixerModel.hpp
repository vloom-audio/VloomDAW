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

/// Channel role in the Studio One-style console.
enum class MixerRole : std::uint8_t {
    Standard = 0, ///< 1:1 Audio / Instrument channel insert
    Bus = 1, ///< Submix group bus channel
    Fx = 2, ///< Effect return channel (fed via Sends)
    Aux = 3, ///< Auxiliary routing / stem channel
};

/// One post-FX send from a mixer channel to another mixer channel (typically FX / Bus / Aux).
struct MixerSend {
    int target = 0; ///< target mixer track `1 .. kMaxInserts`
    double level = 0.8; ///< fader-taper position `0..1` (`0.8` = `0.0 dB` unity via `faderToGain()`)
    bool muted = false;
    bool preFader = false;

    friend bool operator==(const MixerSend&, const MixerSend&) = default;
};

/// One mixer track's settings.
struct MixerTrack {
    std::string name;
    bool named = false; ///< the user named it; otherwise role-based default ("Insert N", "Bus N", ...)
    MixerRole role = MixerRole::Standard;
    double fader = 0.8; ///< 0..1, see faderToGain(); 0.8 is unity
    double pan = 0.0; ///< -1 left .. +1 right
    bool muted = false;
    bool soloed = false;
    bool mono = false; ///< true = fold stereo L+R to mono (0.5 * (L + R))
    int output = 0; ///< destination mixer track: `0` = Master (Main), `1..kMaxInserts` = Bus/Aux/Insert
    std::optional<std::uint32_t> color{}; ///< 0xRRGGBB chosen by the user; none: no colour
    /// The track's effect chain, in processing order (the master's runs on
    /// the mix of everything, before its volume and pan).
    std::vector<PluginSlot> effects{};
    /// Post-effect sends to other mixer channels (`FX`, `Bus`, `Aux`).
    std::vector<MixerSend> sends{};
};

/// The mixer: track 0 is the master (`Main`), tracks 1..N are inserts / buses /
/// FX returns / aux channels. Holds settings only; the engine renders the audio.
///
/// Solo is bus- & send-aware: when any channel is soloed, channels feeding a
/// soloed bus and downstream buses / FX returns receiving signal from an
/// audible channel remain audible automatically.
class MixerModel {
public:
    static constexpr std::size_t kMaster = 0;
    /// Most inserts: as many as the engine routes (vloom::kMaxInserts).
    static constexpr std::size_t kMaxInserts = 125;
    /// Most effects on one insert: the slots of the mixer's FX rack. A project
    /// file with more opens with the first ones and a warning.
    static constexpr std::size_t kMaxEffects = 9;
    /// Most sends on one mixer track (`<= vloom::kMaxSends`).
    static constexpr std::size_t kMaxSends = 8;
    /// Longest insert name, in bytes (UTF-8).
    static constexpr std::size_t kMaxNameLength = 64;

    /// A master plus `insertCount` standard inserts named "Insert 1".. "Insert N".
    explicit MixerModel(std::size_t insertCount);

    [[nodiscard]] std::size_t size() const { return tracks_.size(); }
    [[nodiscard]] const MixerTrack& track(std::size_t index) const { return tracks_.at(index); }
    [[nodiscard]] std::size_t insertCount() const { return tracks_.size() - 1; }

    /// Appends a standard insert named "Insert N"; its index, or nullopt when
    /// there are kMaxInserts already.
    std::optional<std::size_t> addInsert();
    /// Appends a channel with `role` (`Standard`, `Bus`, `Fx`, `Aux`) and an
    /// optional `customName` (auto-numbered "Bus 1", "FX 1", "Aux 1" when empty).
    std::optional<std::size_t> addChannel(MixerRole role, const std::string& customName = {});
    /// Removes insert `index` (never the master); updates `output` and `sends`
    /// references on all remaining tracks. False if out of range.
    bool removeInsert(std::size_t index);
    /// Names insert `index` (UTF-8; trimmed, cut to kMaxNameLength without
    /// splitting a character). A blank name gives back its default role name.
    void setName(std::size_t index, const std::string& name);
    /// Moves insert `from` to position `to` (both clamped to the inserts;
    /// never the master), keeping `output` and `sends` targets pointing at the
    /// same strips. Returns the new position.
    std::size_t moveInsert(std::size_t from, std::size_t to);

    void setRole(std::size_t index, MixerRole role);

    // Setters clamp their value and ignore out-of-range indices and NaN.
    void setFader(std::size_t index, double position);
    void setPan(std::size_t index, double pan);
    void setMuted(std::size_t index, bool muted);
    void setSoloed(std::size_t index, bool soloed);
    void setMono(std::size_t index, bool mono);
    /// The track's colour, 0xRRGGBB (higher bits dropped); nullopt: none.
    void setColor(std::size_t index, std::optional<std::uint32_t> color);

    // ---- output & send routing
    /// True when routing `index` (`>= 1`) to `target` (`0` = Master, `1..insertCount()`)
    /// is valid and introduces no routing cycle through `output` or `sends`.
    [[nodiscard]] bool canRouteOutput(std::size_t index, int target) const;
    bool setOutput(std::size_t index, int target);

    /// True when adding a send from `index` (`>= 1`) to `target` (`1..insertCount()`)
    /// is valid, within `kMaxSends`, non-duplicate, and cycle-free.
    [[nodiscard]] bool canAddSend(std::size_t index, int target) const;
    bool addSend(std::size_t index, int target, double level = 0.8, bool preFader = false);
    bool removeSend(std::size_t index, std::size_t sendSlot);
    void setSendLevel(std::size_t index, std::size_t sendSlot, double level);
    void setSendMuted(std::size_t index, std::size_t sendSlot, bool muted);
    void setSendPreFader(std::size_t index, std::size_t sendSlot, bool preFader);

    /// Appends `effect` to track `index`'s chain (the master's too). False
    /// for an index out of range or a full chain (kMaxEffects).
    bool addEffect(std::size_t index, PluginSlot effect);
    /// Replaces effect `slot` of track `index`; false if out of range.
    bool replaceEffect(std::size_t index, std::size_t slot, PluginSlot effect);
    /// Moves effect `from` to position `to` on track `index`; false if out of range.
    bool moveEffect(std::size_t index, std::size_t from, std::size_t to);
    /// Removes effect `slot` of insert `index`; false if there is none.
    bool removeEffect(std::size_t index, std::size_t slot);
    /// Bypasses (or un-bypasses) effect `slot` of insert `index`.
    void setEffectBypassed(std::size_t index, std::size_t slot, bool bypassed);

    /// True while any insert is soloed.
    [[nodiscard]] bool anySoloed() const;
    /// Whether `index` is audible after mute, solo, upstream soloed-bus feed,
    /// and downstream bus/FX solo-safe propagation.
    [[nodiscard]] bool isTrackAudible(std::size_t index) const;
    /// The gain track `index` contributes: fader gain, or 0 when not audible.
    [[nodiscard]] double effectiveGain(std::size_t index) const;

private:
    [[nodiscard]] bool valid(std::size_t index) const { return index < tracks_.size(); }
    [[nodiscard]] bool reaches(std::size_t from, std::size_t target) const;
    [[nodiscard]] static std::string defaultNameFor(MixerRole role, std::size_t number);
    void refreshDefaultNames();

    std::vector<MixerTrack> tracks_;
};

} // namespace vloomdaw
