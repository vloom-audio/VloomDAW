// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/TempoDetector.hpp"

#include <vloom/engine/sample/SampleBuffer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vloomdaw {

namespace {

constexpr double kFramesPerSecond = 200.0; ///< onset envelope rate: 5 ms steps
constexpr double kMinBpm = 70.0;
constexpr double kMaxBpm = 180.0;
constexpr double kMaxSeconds = 120.0; ///< a song's first two minutes are enough
constexpr double kMaxLoopSeconds = 60.0; ///< longer: a song, not a loop to fit in bars

/// How much louder each envelope step is than the one before (attacks):
/// log energy per step, rising part only, with its slow drift removed.
std::vector<double> onsetEnvelope(const vloom::SampleBuffer& sample)
{
    const double rate = sample.sampleRate();
    const auto hop = std::max<std::int64_t>(1, std::llround(rate / kFramesPerSecond));
    const std::int64_t frames
        = std::min<std::int64_t>(sample.frames(), static_cast<std::int64_t>(kMaxSeconds * rate));
    const std::int64_t steps = frames / hop;
    std::vector<double> energy(static_cast<std::size_t>(std::max<std::int64_t>(steps, 0)), 0.0);
    for (int ch = 0; ch < sample.channels(); ++ch) {
        const float* data = sample.channel(ch);
        for (std::int64_t s = 0; s < steps; ++s) {
            double sum = 0.0;
            for (std::int64_t i = s * hop; i < (s + 1) * hop; ++i)
                sum += static_cast<double>(data[i]) * static_cast<double>(data[i]);
            energy[static_cast<std::size_t>(s)] += sum;
        }
    }
    std::vector<double> onset(energy.size(), 0.0);
    for (std::size_t s = 1; s < energy.size(); ++s)
        onset[s] = std::max(0.0, std::log(energy[s] + 1e-9) - std::log(energy[s - 1] + 1e-9));
    // Remove the local average (about half a second), so only sharp attacks count.
    constexpr auto kHalfWindow = static_cast<std::size_t>(kFramesPerSecond / 4.0);
    std::vector<double> sums(onset.size() + 1, 0.0);
    for (std::size_t s = 0; s < onset.size(); ++s)
        sums[s + 1] = sums[s] + onset[s];
    std::vector<double> out(onset.size(), 0.0);
    for (std::size_t s = 0; s < onset.size(); ++s) {
        const std::size_t from = s > kHalfWindow ? s - kHalfWindow : 0;
        const std::size_t to = std::min(onset.size(), s + kHalfWindow + 1);
        const double mean = (sums[to] - sums[from]) / static_cast<double>(to - from);
        out[s] = std::max(0.0, onset[s] - mean);
    }
    return out;
}

/// Autocorrelation of `x` at `lag`, normalised by the number of products.
double correlation(const std::vector<double>& x, std::size_t lag)
{
    if (lag >= x.size())
        return 0.0;
    double sum = 0.0;
    for (std::size_t i = lag; i < x.size(); ++i)
        sum += x[i] * x[i - lag];
    return sum / static_cast<double>(x.size() - lag);
}

} // namespace

TempoGuess detectTempo(const vloom::SampleBuffer& sample)
{
    if (sample.empty() || !(sample.sampleRate() > 0.0))
        return {};
    const std::vector<double> onset = onsetEnvelope(sample);
    const auto minLag = static_cast<std::size_t>(std::floor(60.0 * kFramesPerSecond / kMaxBpm));
    const auto maxLag = static_cast<std::size_t>(std::ceil(60.0 * kFramesPerSecond / kMinBpm));
    // At least two beats at the slowest tempo, or there is nothing to compare.
    if (onset.size() < 2 * maxLag + 2)
        return {};

    // A beat repeats at its own spacing and at whole multiples of it (the bar
    // too): scoring those together keeps half and double tempos apart. A
    // short loop (two bars) only has room for twice the spacing; every
    // spacing is then scored the same way, so none gets an extra term.
    const bool longEnough = onset.size() >= 4 * maxLag + 2;
    std::vector<double> ac((longEnough ? 4 : 2) * maxLag + 2, 0.0);
    for (std::size_t lag = 0; lag < ac.size(); ++lag)
        ac[lag] = correlation(onset, lag);
    if (!(ac[0] > 0.0))
        return {}; // silence, or no attacks at all
    const auto score = [&](std::size_t lag) {
        return ac[lag] + 0.5 * ac[2 * lag] + (longEnough ? 0.25 * ac[4 * lag] : 0.0);
    };
    std::size_t best = minLag;
    for (std::size_t lag = minLag; lag <= maxLag; ++lag)
        if (score(lag) > score(best))
            best = lag;

    // Between steps: the peak of a parabola through the best and its neighbours.
    double lag = static_cast<double>(best);
    if (best > minLag && best < maxLag) {
        const double a = score(best - 1);
        const double b = score(best);
        const double c = score(best + 1);
        const double curve = a - 2.0 * b + c;
        if (curve < 0.0)
            lag += std::clamp(0.5 * (a - c) / curve, -0.5, 0.5);
    }
    double bpm = 60.0 * kFramesPerSecond / lag;

    // How far the beat stands out from the other spacings.
    double mean = 0.0;
    for (std::size_t l = minLag; l <= maxLag; ++l)
        mean += score(l);
    mean /= static_cast<double>(maxLag - minLag + 1);
    const double confidence
        = std::clamp((score(best) - mean) / ((longEnough ? 1.75 : 1.5) * ac[0]), 0.0, 1.0);

    // A loop is a whole number of bars (of four beats): the tempo that makes
    // them fit exactly, when it is what was heard within the measuring
    // precision. A loop with a tail past its bars is a little longer than
    // that: it keeps the tempo heard.
    const double seconds = static_cast<double>(sample.frames()) / sample.sampleRate();
    if (seconds <= kMaxLoopSeconds) {
        double fitted = 0.0;
        for (int bars = 1; bars <= 64; bars *= 2) {
            const double candidate = bars * 4.0 * 60.0 / seconds;
            if (std::abs(candidate - bpm) / bpm < 0.005
                && (fitted == 0.0 || std::abs(candidate - bpm) < std::abs(fitted - bpm)))
                fitted = candidate;
        }
        if (fitted > 0.0)
            bpm = fitted;
    }
    // Loops are nearly always made at a whole tempo: one heard within the
    // measuring precision of it is taken as that tempo.
    if (std::abs(bpm - std::round(bpm)) < 0.35)
        bpm = std::round(bpm);
    return {std::round(bpm * 100.0) / 100.0, confidence};
}

} // namespace vloomdaw
