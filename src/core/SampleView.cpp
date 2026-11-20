// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/SampleView.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace vloomdaw {

namespace {

constexpr double kMinAttack = 0.0005; ///< dsp::EnvelopeShape::kMinAttackSeconds
constexpr double kMinFall = 0.002; ///< dsp::EnvelopeShape::kMinReleaseSeconds
constexpr double kFallRate = 5.0; ///< a decay or release falls by e^-5 (-43 dB) in its time

std::string printed(const char* format, double value)
{
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), format, value);
    return text.data();
}

} // namespace

std::vector<PeakColumn> samplePeaks(
    std::span<const float* const> channels, std::int64_t frames, std::size_t columns)
{
    std::vector<PeakColumn> out;
    if (frames <= 0 || columns == 0 || channels.empty())
        return out;
    const auto count
        = static_cast<std::size_t>(std::min<std::int64_t>(frames, static_cast<std::int64_t>(columns)));
    out.assign(count, PeakColumn{std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest()});
    for (const float* data : channels) {
        if (data == nullptr)
            continue;
        for (std::size_t c = 0; c < count; ++c) {
            const auto from = static_cast<std::int64_t>(c) * frames / static_cast<std::int64_t>(count);
            const auto to = static_cast<std::int64_t>(c + 1) * frames / static_cast<std::int64_t>(count);
            PeakColumn& column = out[c];
            for (std::int64_t i = from; i < to; ++i) {
                const float v = std::isfinite(data[i]) ? data[i] : 0.0f;
                column.low = std::min(column.low, v);
                column.high = std::max(column.high, v);
            }
        }
    }
    for (PeakColumn& column : out)
        if (column.low > column.high)
            column = PeakColumn{}; // no channel had data
    return out;
}

PeakColumn peakBetween(std::span<const PeakColumn> peaks, double from, double to) noexcept
{
    if (peaks.empty() || !std::isfinite(from) || !std::isfinite(to))
        return {};
    const auto size = static_cast<double>(peaks.size());
    const auto first = static_cast<std::size_t>(std::clamp(std::floor(from * size), 0.0, size - 1.0));
    const auto last = static_cast<std::size_t>(std::clamp(std::ceil(to * size), first + 1.0, size));
    PeakColumn column = peaks[first];
    for (std::size_t i = first + 1; i < last; ++i) {
        column.low = std::min(column.low, peaks[i].low);
        column.high = std::max(column.high, peaks[i].high);
    }
    return column;
}

double envelopeLevel(double seconds, double attack, double decay, double sustain) noexcept
{
    if (!(seconds > 0.0))
        return 0.0;
    attack = std::max(attack, kMinAttack);
    if (seconds < attack)
        return seconds / attack;
    sustain = std::clamp(sustain, 0.0, 1.0);
    return sustain + (1.0 - sustain) * std::exp(-kFallRate * (seconds - attack) / std::max(decay, kMinFall));
}

double releaseLevel(double level, double seconds, double release) noexcept
{
    if (!(seconds > 0.0))
        return level;
    return level * std::exp(-kFallRate * seconds / std::max(release, kMinFall));
}

double playbackSpeed(double semitones, double cents) noexcept
{
    return std::exp2((semitones + cents / 100.0) / 12.0);
}

std::string tickLabel(double seconds, double step)
{
    if (step < 0.1 - 1e-12)
        return printed("%.0f ms", seconds * 1000.0);
    return printed(seconds < 10.0 - 1e-9 || step < 1.0 - 1e-12 ? "%.1f s" : "%.0f s", seconds);
}

std::string panText(double percent)
{
    const long whole = std::lround(percent);
    if (whole == 0)
        return "C";
    return std::to_string(std::abs(whole)) + (whole < 0 ? " L" : " R");
}

} // namespace vloomdaw
