// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "audio/PluginHost.hpp"
#include "core/MixerModel.hpp"

#include <vloom/engine/mixer/MixerState.hpp>

#include <memory>

namespace vloomdaw {

/// The mixer's inserts as the engine plays them: per insert its fader gain
/// with mute and solo folded in (MixerModel::effectiveGain()), and its pan.
/// The master is not part of it: the engine's master gain and pan carry it.
/// Each insert's effects, and the master's (MixerState::masterEffects), are
/// the instances `plugins` finds for their PluginSlots (ones not loaded are
/// left out), bypassed as the slots say.
[[nodiscard]] std::unique_ptr<const vloom::MixerState> engineMixerState(
    const MixerModel& mixer, const PluginResolver& plugins = {});

} // namespace vloomdaw
