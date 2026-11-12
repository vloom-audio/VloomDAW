// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace vloomdaw {

/// One mixer track's settings.
struct MixerTrack {
    std::string name;
    double fader = 0.8; ///< 0..1, see faderToGain(); 0.8 is unity
    double pan = 0.0; ///< -1 left .. +1 right
    bool muted = false;
    bool soloed = false;
};

/// The mixer: track 0 is the master, tracks 1..N are inserts, as in FL
/// Studio. Holds settings only; the engine renders the audio.
///
/// Solo works on inserts: while any insert is soloed, the other inserts are
/// silent. The master is never silenced by solo, only by its own mute.
class MixerModel {
public:
    static constexpr std::size_t kMaster = 0;

    /// A master plus `insertCount` inserts named "Insert 1".. "Insert N".
    explicit MixerModel(std::size_t insertCount);

    [[nodiscard]] std::size_t size() const { return tracks_.size(); }
    [[nodiscard]] const MixerTrack& track(std::size_t index) const { return tracks_.at(index); }

    // Setters clamp their value and ignore out-of-range indices and NaN.
    void setFader(std::size_t index, double position);
    void setPan(std::size_t index, double pan);
    void setMuted(std::size_t index, bool muted);
    void setSoloed(std::size_t index, bool soloed);

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
