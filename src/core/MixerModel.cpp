// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/MixerModel.hpp"

#include "core/Decibels.hpp"

#include <algorithm>
#include <cmath>

namespace vloomdaw {

MixerModel::MixerModel(std::size_t insertCount)
{
    tracks_.resize(insertCount + 1);
    tracks_[kMaster].name = "Master";
    for (std::size_t i = 1; i < tracks_.size(); ++i)
        tracks_[i].name = "Insert " + std::to_string(i);
}

void MixerModel::setFader(std::size_t index, double position)
{
    if (valid(index) && !std::isnan(position))
        tracks_[index].fader = std::clamp(position, 0.0, 1.0);
}

void MixerModel::setPan(std::size_t index, double pan)
{
    if (valid(index) && !std::isnan(pan))
        tracks_[index].pan = std::clamp(pan, -1.0, 1.0);
}

void MixerModel::setMuted(std::size_t index, bool muted)
{
    if (valid(index))
        tracks_[index].muted = muted;
}

void MixerModel::setSoloed(std::size_t index, bool soloed)
{
    // Solo only means something on inserts.
    if (valid(index) && index != kMaster)
        tracks_[index].soloed = soloed;
}

bool MixerModel::anySoloed() const
{
    return std::any_of(tracks_.begin() + 1, tracks_.end(), [](const MixerTrack& t) { return t.soloed; });
}

double MixerModel::effectiveGain(std::size_t index) const
{
    if (!valid(index))
        return 0.0;
    const MixerTrack& t = tracks_[index];
    if (t.muted)
        return 0.0;
    if (index != kMaster && anySoloed() && !t.soloed)
        return 0.0;
    return faderToGain(t.fader);
}

} // namespace vloomdaw
