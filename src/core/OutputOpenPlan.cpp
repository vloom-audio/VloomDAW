// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/OutputOpenPlan.hpp"

#include <algorithm>

namespace vloomdaw {

std::vector<OutputOpenStep> outputOpenSteps(bool exclusive, int wantedChannels)
{
    const int wanted = std::max(2, wantedChannels);
    std::vector<OutputOpenStep> steps;
    const auto addMode = [&](bool exclusiveMode) {
        steps.push_back({exclusiveMode, wanted});
        if (wanted != 2)
            steps.push_back({exclusiveMode, 2});
    };
    if (exclusive)
        addMode(true);
    addMode(false);
    return steps;
}

} // namespace vloomdaw
