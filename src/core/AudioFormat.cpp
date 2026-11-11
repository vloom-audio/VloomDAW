// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/AudioFormat.hpp"

#include <algorithm>

namespace vloomdaw {

double bufferLatencyMs(int bufferFrames, int sampleRate)
{
    if (sampleRate <= 0 || bufferFrames <= 0)
        return 0.0;
    return 1000.0 * bufferFrames / sampleRate;
}

int validSampleRate(int value)
{
    return std::ranges::find(kSampleRates, value) != kSampleRates.end() ? value : kDefaultSampleRate;
}

int validBufferFrames(int value)
{
    return std::ranges::find(kBufferSizes, value) != kBufferSizes.end() ? value : kDefaultBufferFrames;
}

} // namespace vloomdaw
