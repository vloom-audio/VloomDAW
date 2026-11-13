// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/MixerAudio.hpp"

namespace vloomdaw {

static_assert(MixerModel::kMaxInserts == static_cast<std::size_t>(vloom::kMaxInserts));

std::unique_ptr<const vloom::MixerState> engineMixerState(const MixerModel& mixer)
{
    auto state = std::make_unique<vloom::MixerState>();
    for (std::size_t i = 1; i < mixer.size() && static_cast<int>(i) <= vloom::kMaxInserts; ++i)
        state->inserts.push_back(
            {static_cast<float>(mixer.effectiveGain(i)), static_cast<float>(mixer.track(i).pan)});
    return state;
}

} // namespace vloomdaw
