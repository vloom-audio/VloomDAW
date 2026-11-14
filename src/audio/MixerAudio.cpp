// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/MixerAudio.hpp"

namespace vloomdaw {

static_assert(MixerModel::kMaxInserts == static_cast<std::size_t>(vloom::kMaxInserts));

std::unique_ptr<const vloom::MixerState> engineMixerState(
    const MixerModel& mixer, const PluginResolver& plugins)
{
    auto state = std::make_unique<vloom::MixerState>();
    if (plugins)
        for (const PluginSlot& slot : mixer.track(MixerModel::kMaster).effects)
            if (auto effect = plugins(slot.instance)) {
                effect->setBypassed(slot.bypassed);
                state->masterEffects.push_back(std::move(effect));
            }
    for (std::size_t i = 1; i < mixer.size() && static_cast<int>(i) <= vloom::kMaxInserts; ++i) {
        vloom::InsertState insert;
        insert.gain = static_cast<float>(mixer.effectiveGain(i));
        insert.pan = static_cast<float>(mixer.track(i).pan);
        if (plugins)
            for (const PluginSlot& slot : mixer.track(i).effects)
                if (auto effect = plugins(slot.instance)) {
                    effect->setBypassed(slot.bypassed);
                    insert.effects.push_back(std::move(effect));
                }
        state->inserts.push_back(std::move(insert));
    }
    return state;
}

} // namespace vloomdaw
