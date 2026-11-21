// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <vector>

namespace vloomdaw {

/// One try at opening the output device.
struct OutputOpenStep {
    bool exclusive = false; ///< WASAPI exclusive mode (Windows)
    int channels = 2;
};

/// The tries AudioController::openDevice() makes, in order, until one opens.
///
/// The channel count the routing needs comes first, then plain stereo. With
/// `exclusive`, every exclusive try comes before the shared ones: the engine
/// never falls back to shared mode itself, so the app does, and tells the
/// user why exclusive failed.
[[nodiscard]] std::vector<OutputOpenStep> outputOpenSteps(bool exclusive, int wantedChannels);

} // namespace vloomdaw
