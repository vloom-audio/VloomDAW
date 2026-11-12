// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/MixerModel.hpp"

#include <vloom/engine/mixer/MixerState.hpp>

#include <memory>

namespace vloomdaw {

/// The mixer's inserts as the engine plays them: per insert its fader gain
/// with mute and solo folded in (MixerModel::effectiveGain()), and its pan.
/// The master is not part of it: the engine's master gain and pan carry it.
[[nodiscard]] std::unique_ptr<const vloom::MixerState> engineMixerState(const MixerModel& mixer);

} // namespace vloomdaw
