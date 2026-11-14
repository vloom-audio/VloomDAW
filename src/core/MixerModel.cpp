// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/MixerModel.hpp"

#include "core/Decibels.hpp"
#include "core/Names.hpp"

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

std::optional<std::size_t> MixerModel::addInsert()
{
    if (insertCount() >= kMaxInserts)
        return std::nullopt;
    MixerTrack& added = tracks_.emplace_back();
    added.name = "Insert " + std::to_string(tracks_.size() - 1);
    return tracks_.size() - 1;
}

bool MixerModel::removeInsert(std::size_t index)
{
    if (index == kMaster || !valid(index))
        return false;
    tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(index));
    for (std::size_t i = index; i < tracks_.size(); ++i)
        if (!tracks_[i].named)
            tracks_[i].name = "Insert " + std::to_string(i);
    return true;
}

std::size_t MixerModel::moveInsert(std::size_t from, std::size_t to)
{
    if (from == kMaster || !valid(from))
        return from;
    to = std::clamp<std::size_t>(to, 1, tracks_.size() - 1);
    if (from == to)
        return to;
    MixerTrack moved = std::move(tracks_[from]);
    tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(from));
    tracks_.insert(tracks_.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
    for (std::size_t i = 1; i < tracks_.size(); ++i)
        if (!tracks_[i].named)
            tracks_[i].name = "Insert " + std::to_string(i);
    return to;
}

bool MixerModel::addEffect(std::size_t index, PluginSlot effect)
{
    if (!valid(index) || tracks_[index].effects.size() >= kMaxEffects)
        return false;
    tracks_[index].effects.push_back(std::move(effect));
    return true;
}

bool MixerModel::removeEffect(std::size_t index, std::size_t slot)
{
    if (!valid(index) || slot >= tracks_[index].effects.size())
        return false;
    auto& effects = tracks_[index].effects;
    effects.erase(effects.begin() + static_cast<std::ptrdiff_t>(slot));
    return true;
}

void MixerModel::setEffectBypassed(std::size_t index, std::size_t slot, bool bypassed)
{
    if (valid(index) && slot < tracks_[index].effects.size())
        tracks_[index].effects[slot].bypassed = bypassed;
}

void MixerModel::setName(std::size_t index, const std::string& name)
{
    if (index == kMaster || !valid(index))
        return;
    const std::string trimmed = cleanName(name, kMaxNameLength);
    MixerTrack& t = tracks_[index];
    t.named = !trimmed.empty();
    t.name = t.named ? trimmed : "Insert " + std::to_string(index);
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

void MixerModel::setColor(std::size_t index, std::optional<std::uint32_t> color)
{
    if (valid(index))
        tracks_[index].color = color ? std::optional<std::uint32_t>(*color & 0xffffffu) : std::nullopt;
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
