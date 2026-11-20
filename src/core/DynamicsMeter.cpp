// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/DynamicsMeter.hpp"

#include <vloom/dsp/effects/Compressor.hpp>

#include <algorithm>
#include <cmath>

namespace vloomdaw {

double peakDb(std::span<const float> samples) noexcept
{
    float peak = 0.0f;
    for (const float sample : samples)
        if (std::isfinite(sample))
            peak = std::max(peak, std::abs(sample));
    if (peak <= 0.0f)
        return -std::numeric_limits<double>::infinity();
    return 20.0 * std::log10(static_cast<double>(peak));
}

LevelHistory::LevelHistory(std::size_t capacity)
    : levels_(std::max<std::size_t>(capacity, 1), kNoLevel)
{
}

void LevelHistory::push(double level) noexcept
{
    levels_[next_] = level;
    next_ = (next_ + 1) % levels_.size();
    size_ = std::min(size_ + 1, levels_.size());
}

void LevelHistory::clear() noexcept
{
    std::fill(levels_.begin(), levels_.end(), kNoLevel);
    next_ = 0;
    size_ = 0;
}

double LevelHistory::at(std::size_t index) const noexcept
{
    if (index >= size_)
        return kNoLevel;
    const std::size_t oldest = (next_ + levels_.size() - size_) % levels_.size();
    return levels_[(oldest + index) % levels_.size()];
}

double LevelHistory::newest() const noexcept { return size_ == 0 ? kNoLevel : at(size_ - 1); }

double compressorMakeupDb(
    double thresholdDb, double ratio, double kneeDb, double makeupDb, bool autoMakeup) noexcept
{
    double db = makeupDb;
    if (autoMakeup)
        db -= 0.5 * vloom::dsp::Compressor::staticGainDb(0.0, thresholdDb, ratio, kneeDb);
    return db;
}

double compressorOutputDb(double inputDb, double thresholdDb, double ratio, double kneeDb, double makeupDb,
    bool autoMakeup) noexcept
{
    return inputDb + vloom::dsp::Compressor::staticGainDb(inputDb, thresholdDb, ratio, kneeDb)
        + compressorMakeupDb(thresholdDb, ratio, kneeDb, makeupDb, autoMakeup);
}

double inputFromOutputDb(double outputDb, double reductionDb, double addedGainDb) noexcept
{
    if (!std::isfinite(outputDb))
        return kNoLevel;
    const double reduction = std::isfinite(reductionDb) ? std::max(reductionDb, 0.0) : 0.0;
    return outputDb + reduction - (std::isfinite(addedGainDb) ? addedGainDb : 0.0);
}

} // namespace vloomdaw
