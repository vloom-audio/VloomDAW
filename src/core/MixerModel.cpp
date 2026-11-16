// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/MixerModel.hpp"

#include "core/Decibels.hpp"
#include "core/Names.hpp"

#include <algorithm>
#include <cmath>

namespace vloomdaw {

namespace {

std::uint32_t defaultRoleColor(MixerRole role) noexcept
{
    switch (role) {
    case MixerRole::Bus:
        return 0x38bdf8u; // sky blue
    case MixerRole::Fx:
        return 0xa78bfau; // studio violet
    case MixerRole::Aux:
        return 0x2dd4bfu; // studio teal
    case MixerRole::Standard:
        break;
    }
    return 0u;
}

} // namespace

std::string MixerModel::defaultNameFor(MixerRole role, std::size_t number)
{
    switch (role) {
    case MixerRole::Bus:
        return "Bus " + std::to_string(number);
    case MixerRole::Fx:
        return "FX " + std::to_string(number);
    case MixerRole::Aux:
        return "Aux " + std::to_string(number);
    case MixerRole::Standard:
        break;
    }
    return "Insert " + std::to_string(number);
}

void MixerModel::refreshDefaultNames()
{
    std::size_t busOrdinal = 0;
    std::size_t fxOrdinal = 0;
    std::size_t auxOrdinal = 0;
    for (std::size_t i = 1; i < tracks_.size(); ++i) {
        std::size_t ord = i;
        switch (tracks_[i].role) {
        case MixerRole::Bus:
            ord = ++busOrdinal;
            break;
        case MixerRole::Fx:
            ord = ++fxOrdinal;
            break;
        case MixerRole::Aux:
            ord = ++auxOrdinal;
            break;
        case MixerRole::Standard:
            ord = i;
            break;
        }
        if (!tracks_[i].named)
            tracks_[i].name = defaultNameFor(tracks_[i].role, ord);
    }
}

MixerModel::MixerModel(std::size_t insertCount)
{
    tracks_.resize(insertCount + 1);
    tracks_[kMaster].name = "Master";
    refreshDefaultNames();
}

std::optional<std::size_t> MixerModel::addInsert() { return addChannel(MixerRole::Standard); }

std::optional<std::size_t> MixerModel::addChannel(MixerRole role, const std::string& customName)
{
    if (insertCount() >= kMaxInserts)
        return std::nullopt;
    MixerTrack& added = tracks_.emplace_back();
    added.role = role;
    const std::uint32_t roleCol = defaultRoleColor(role);
    if (roleCol != 0u)
        added.color = roleCol;

    const std::string trimmed = cleanName(customName, kMaxNameLength);
    if (!trimmed.empty()) {
        added.named = true;
        added.name = trimmed;
    } else if (role != MixerRole::Standard) {
        // Number Bus / FX / Aux channels cleanly ("Bus 1", "FX 1", "Aux 1") and
        // mark them named so adding/deleting earlier audio tracks keeps "FX 1" stable.
        for (std::size_t n = 1; n <= kMaxInserts + 1; ++n) {
            const std::string candidate = defaultNameFor(role, n);
            const bool taken = std::any_of(tracks_.begin() + 1, tracks_.end() - 1,
                [&candidate](const MixerTrack& t) { return t.name == candidate; });
            if (!taken) {
                added.named = true;
                added.name = candidate;
                break;
            }
        }
    }
    refreshDefaultNames();
    return tracks_.size() - 1;
}

bool MixerModel::removeInsert(std::size_t index)
{
    if (index == kMaster || !valid(index))
        return false;
    const int removed = static_cast<int>(index);
    tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(index));

    // Keep output & send targets pointing to the right mixer tracks.
    for (std::size_t i = 1; i < tracks_.size(); ++i) {
        MixerTrack& t = tracks_[i];
        if (t.output == removed)
            t.output = 0;
        else if (t.output > removed)
            --t.output;

        std::erase_if(t.sends, [removed](const MixerSend& s) { return s.target == removed; });
        for (MixerSend& s : t.sends) {
            if (s.target > removed)
                --s.target;
        }
    }
    refreshDefaultNames();
    return true;
}

std::size_t MixerModel::moveInsert(std::size_t from, std::size_t to)
{
    if (from == kMaster || !valid(from))
        return from;
    to = std::clamp<std::size_t>(to, 1, tracks_.size() - 1);
    if (from == to)
        return to;

    const auto remapIndex = [from, to](int target) -> int {
        if (target <= 0)
            return 0;
        const auto cur = static_cast<std::size_t>(target);
        if (cur == from)
            return static_cast<int>(to);
        if (from < to && cur > from && cur <= to)
            return target - 1;
        if (to < from && cur >= to && cur < from)
            return target + 1;
        return target;
    };

    MixerTrack moved = std::move(tracks_[from]);
    tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(from));
    tracks_.insert(tracks_.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));

    for (std::size_t i = 1; i < tracks_.size(); ++i) {
        tracks_[i].output = remapIndex(tracks_[i].output);
        for (MixerSend& s : tracks_[i].sends)
            s.target = remapIndex(s.target);
    }
    refreshDefaultNames();
    return to;
}

void MixerModel::setRole(std::size_t index, MixerRole role)
{
    if (index == kMaster || !valid(index))
        return;
    tracks_[index].role = role;
    refreshDefaultNames();
}

bool MixerModel::reaches(std::size_t from, std::size_t target) const
{
    if (from == target)
        return true;
    std::vector<bool> visited(tracks_.size(), false);
    std::vector<std::size_t> stack{from};
    visited[from] = true;
    while (!stack.empty()) {
        const std::size_t cur = stack.back();
        stack.pop_back();
        if (cur == kMaster || cur >= tracks_.size())
            continue;
        const MixerTrack& t = tracks_[cur];
        const auto visit = [&](int dst) {
            if (dst <= 0 || static_cast<std::size_t>(dst) >= tracks_.size())
                return false;
            const auto next = static_cast<std::size_t>(dst);
            if (next == target)
                return true;
            if (!visited[next]) {
                visited[next] = true;
                stack.push_back(next);
            }
            return false;
        };
        if (visit(t.output))
            return true;
        for (const MixerSend& s : t.sends) {
            if (visit(s.target))
                return true;
        }
    }
    return false;
}

bool MixerModel::canRouteOutput(std::size_t index, int target) const
{
    if (index == kMaster || !valid(index))
        return false;
    if (target == 0)
        return true;
    if (target < 0 || static_cast<std::size_t>(target) >= tracks_.size())
        return false;
    const auto dst = static_cast<std::size_t>(target);
    return dst != index && !reaches(dst, index);
}

bool MixerModel::setOutput(std::size_t index, int target)
{
    if (!canRouteOutput(index, target))
        return false;
    tracks_[index].output = target;
    return true;
}

bool MixerModel::canAddSend(std::size_t index, int target) const
{
    if (index == kMaster || !valid(index))
        return false;
    if (target <= 0 || static_cast<std::size_t>(target) >= tracks_.size())
        return false;
    const auto dst = static_cast<std::size_t>(target);
    if (dst == index || tracks_[index].sends.size() >= kMaxSends)
        return false;
    const bool exists = std::any_of(tracks_[index].sends.begin(), tracks_[index].sends.end(),
        [target](const MixerSend& s) { return s.target == target; });
    if (exists)
        return false;
    return !reaches(dst, index);
}

bool MixerModel::addSend(std::size_t index, int target, double level, bool preFader)
{
    if (!canAddSend(index, target))
        return false;
    const double safeLevel = std::isfinite(level) ? std::clamp(level, 0.0, 1.0) : 0.8;
    tracks_[index].sends.push_back(MixerSend{target, safeLevel, false, preFader});
    return true;
}

bool MixerModel::removeSend(std::size_t index, std::size_t sendSlot)
{
    if (index == kMaster || !valid(index) || sendSlot >= tracks_[index].sends.size())
        return false;
    tracks_[index].sends.erase(tracks_[index].sends.begin() + static_cast<std::ptrdiff_t>(sendSlot));
    return true;
}

void MixerModel::setSendLevel(std::size_t index, std::size_t sendSlot, double level)
{
    if (index != kMaster && valid(index) && sendSlot < tracks_[index].sends.size() && std::isfinite(level))
        tracks_[index].sends[sendSlot].level = std::clamp(level, 0.0, 1.0);
}

void MixerModel::setSendMuted(std::size_t index, std::size_t sendSlot, bool muted)
{
    if (index != kMaster && valid(index) && sendSlot < tracks_[index].sends.size())
        tracks_[index].sends[sendSlot].muted = muted;
}

void MixerModel::setSendPreFader(std::size_t index, std::size_t sendSlot, bool preFader)
{
    if (index != kMaster && valid(index) && sendSlot < tracks_[index].sends.size())
        tracks_[index].sends[sendSlot].preFader = preFader;
}

bool MixerModel::addEffect(std::size_t index, PluginSlot effect)
{
    if (!valid(index) || tracks_[index].effects.size() >= kMaxEffects)
        return false;
    tracks_[index].effects.push_back(std::move(effect));
    return true;
}

bool MixerModel::replaceEffect(std::size_t index, std::size_t slot, PluginSlot effect)
{
    if (!valid(index) || slot >= tracks_[index].effects.size())
        return false;
    tracks_[index].effects[slot] = std::move(effect);
    return true;
}

bool MixerModel::moveEffect(std::size_t index, std::size_t from, std::size_t to)
{
    if (!valid(index))
        return false;
    auto& effects = tracks_[index].effects;
    if (from >= effects.size() || to >= effects.size() || from == to)
        return false;
    auto moved = std::move(effects[from]);
    effects.erase(effects.begin() + static_cast<std::ptrdiff_t>(from));
    effects.insert(effects.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
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
    if (t.named)
        t.name = trimmed;
    refreshDefaultNames();
}

void MixerModel::setFader(std::size_t index, double position)
{
    if (valid(index) && std::isfinite(position))
        tracks_[index].fader = std::clamp(position, 0.0, 1.0);
}

void MixerModel::setPan(std::size_t index, double pan)
{
    if (valid(index) && std::isfinite(pan))
        tracks_[index].pan = std::clamp(pan, -1.0, 1.0);
}

void MixerModel::setMuted(std::size_t index, bool muted)
{
    if (valid(index))
        tracks_[index].muted = muted;
}

void MixerModel::setMono(std::size_t index, bool mono)
{
    if (valid(index))
        tracks_[index].mono = mono;
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

bool MixerModel::isTrackAudible(std::size_t index) const
{
    if (!valid(index) || tracks_[index].muted)
        return false;
    if (index == kMaster || !anySoloed())
        return true;

    std::vector<bool> audible(tracks_.size(), false);
    for (std::size_t i = 1; i < tracks_.size(); ++i) {
        if (!tracks_[i].muted && tracks_[i].soloed)
            audible[i] = true;
    }

    // 1) Upstream propagation: if a Bus/Aux is soloed, unmuted channels whose
    //    `output` routes into it stay audible so the soloed Bus receives its mix.
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 1; i < tracks_.size(); ++i) {
            if (audible[i] || tracks_[i].muted)
                continue;
            const int out = tracks_[i].output;
            if (out > 0 && static_cast<std::size_t>(out) < tracks_.size()
                && audible[static_cast<std::size_t>(out)]) {
                audible[i] = true;
                changed = true;
            }
        }
    }

    // 2) Downstream solo-safe propagation: any unmuted Bus/FX/Aux receiving
    //    `output` or an active `send` from an audible channel stays audible too.
    changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 1; i < tracks_.size(); ++i) {
            if (!audible[i])
                continue;
            const int out = tracks_[i].output;
            if (out > 0 && static_cast<std::size_t>(out) < tracks_.size()) {
                const auto dst = static_cast<std::size_t>(out);
                if (!audible[dst] && !tracks_[dst].muted) {
                    audible[dst] = true;
                    changed = true;
                }
            }
            for (const MixerSend& s : tracks_[i].sends) {
                if (s.muted || s.target <= 0 || static_cast<std::size_t>(s.target) >= tracks_.size())
                    continue;
                const auto dst = static_cast<std::size_t>(s.target);
                if (!audible[dst] && !tracks_[dst].muted) {
                    audible[dst] = true;
                    changed = true;
                }
            }
        }
    }

    return audible[index];
}

double MixerModel::effectiveGain(std::size_t index) const
{
    if (!isTrackAudible(index))
        return 0.0;
    return faderToGain(tracks_[index].fader);
}

} // namespace vloomdaw
