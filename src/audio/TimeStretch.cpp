// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/TimeStretch.hpp"

#include <vloom/engine/sample/SampleBuffer.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <vector>

namespace vloomdaw {

std::shared_ptr<const vloom::SampleBuffer> timeStretch(const vloom::SampleBuffer& sample, double ratio)
{
    if (!(ratio >= 0.25 && ratio <= 4.0) || sample.empty() || !(sample.sampleRate() > 0.0))
        return nullptr;
    const std::int64_t inFrames = sample.frames();
    const double outLength = std::round(static_cast<double>(inFrames) * ratio);
    // Signalsmith Stretch counts in int.
    if (inFrames > std::numeric_limits<int>::max() || outLength > std::numeric_limits<int>::max())
        return nullptr;
    const auto outFrames = static_cast<std::int64_t>(outLength);
    auto out = std::make_shared<vloom::SampleBuffer>(sample.channels(), outFrames, sample.sampleRate());

    std::vector<const float*> inputs;
    std::vector<float*> outputs;
    for (int ch = 0; ch < sample.channels(); ++ch) {
        inputs.push_back(sample.channel(ch));
        outputs.push_back(out->channel(ch));
    }
    // A fixed seed: the same sample and ratio always give the same result.
    signalsmith::stretch::SignalsmithStretch<float> stretch(0x766c6f6fL);
    stretch.presetDefault(sample.channels(), static_cast<float>(sample.sampleRate()));
    if (!stretch.exact(
            inputs.data(), static_cast<int>(inFrames), outputs.data(), static_cast<int>(outFrames)))
        return nullptr; // too short to stretch
    return out;
}

} // namespace vloomdaw
