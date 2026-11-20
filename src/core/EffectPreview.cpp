// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/EffectPreview.hpp"

#include <vloom/dsp/Biquad.hpp>
#include <vloom/dsp/effects/Bitcrusher.hpp>
#include <vloom/dsp/effects/Filter.hpp>
#include <vloom/dsp/effects/Saturator.hpp>
#include <vloom/dsp/effects/TransientShaper.hpp>
#include <vloom/dsp/effects/Utility.hpp>

#include <cmath>
#include <numbers>

namespace vloomdaw {

namespace {

namespace dsp = vloom::dsp;

/// Setting `id`, or `fallback` when `settings` do not have it.
double setting(EffectSettings settings, std::uint32_t id, double fallback)
{
    return id < settings.size() && std::isfinite(settings[id]) ? settings[id] : fallback;
}

double gainOf(double db) { return std::pow(10.0, db / 20.0); }

double validRate(double sampleRate)
{
    return sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 48000.0;
}

/// `frames` of a sine of `amplitude`, `cycleFrames` a cycle, from zero.
std::vector<float> sine(std::size_t frames, double cycleFrames, double amplitude)
{
    std::vector<float> wave(frames);
    for (std::size_t i = 0; i < frames; ++i)
        wave[i] = static_cast<float>(
            amplitude * std::sin(2.0 * std::numbers::pi * static_cast<double>(i) / cycleFrames));
    return wave;
}

/// Runs `effect` over a mono `input` (fed to both sides) and gives the
/// left side back.
template <typename Effect> std::vector<float> processed(Effect& effect, const std::vector<float>& input)
{
    std::vector<float> left = input;
    std::vector<float> right = input;
    runEffect(effect, left, right);
    return left;
}

} // namespace

double saturatorCurve(EffectSettings settings, double x)
{
    using Saturator = dsp::Saturator;
    const int curve = static_cast<int>(std::lround(setting(settings, Saturator::kCurve, 0.0)));
    const double drive = gainOf(setting(settings, Saturator::kDrive, 6.0));
    const double output = gainOf(setting(settings, Saturator::kOutput, 0.0));
    const double mix = std::clamp(setting(settings, Saturator::kMix, 100.0), 0.0, 100.0) / 100.0;
    const double wet
        = static_cast<double>(Saturator::shape(
              static_cast<Saturator::Curve>(std::clamp(curve, 0, 3)), static_cast<float>(x * drive)))
        * output;
    return x + mix * (wet - x);
}

WavePreview saturatorPreview(EffectSettings settings, double sampleRate)
{
    const double rate = validRate(sampleRate);
    auto effect = preparedEffect<dsp::Saturator>(settings, rate);
    const double cycle = rate / 100.0;
    const auto latency = static_cast<std::size_t>(std::max(0, effect->latencyFrames()));
    // A quarter of a second lets the DC blocker and the tone settle; whole
    // cycles, so the view starts at a zero crossing.
    const auto before = static_cast<std::size_t>(std::ceil(0.25 * rate / cycle) * cycle);
    const auto view = static_cast<std::size_t>(std::lround(kPreviewCycles * cycle));
    const std::vector<float> input = sine(before + latency + view, cycle, 0.6);
    const std::vector<float> output = processed(*effect, input);
    WavePreview preview;
    preview.dry.assign(input.begin() + static_cast<std::ptrdiff_t>(before),
        input.begin() + static_cast<std::ptrdiff_t>(before + view));
    preview.wet.assign(output.begin() + static_cast<std::ptrdiff_t>(before + latency),
        output.begin() + static_cast<std::ptrdiff_t>(before + latency + view));
    return preview;
}

double crusherCycleFrames(double downsample)
{
    const double factor = std::isfinite(downsample) ? std::clamp(downsample, 1.0, 64.0) : 1.0;
    return std::max(96.0, 12.0 * factor);
}

WavePreview crusherPreview(EffectSettings settings, double sampleRate)
{
    using Bitcrusher = dsp::Bitcrusher;
    const double rate = validRate(sampleRate);
    auto effect = preparedEffect<Bitcrusher>(settings, rate);
    const double cycle = crusherCycleFrames(setting(settings, Bitcrusher::kDownsample, 1.0));
    // Whole cycles before the view (the anti-alias filter settles); at
    // least 2048 frames, past the glides.
    const double cycles = std::ceil(std::max(2048.0, 2.0 * cycle) / cycle);
    const auto view = static_cast<std::size_t>(std::lround(kPreviewCycles * cycle));
    const auto before = static_cast<std::size_t>(std::lround(cycles * cycle));
    const std::vector<float> input = sine(before + view, cycle, 0.8);
    const std::vector<float> output = processed(*effect, input);
    WavePreview preview;
    preview.dry.assign(input.begin() + static_cast<std::ptrdiff_t>(before), input.end());
    preview.wet.assign(output.begin() + static_cast<std::ptrdiff_t>(before), output.end());
    return preview;
}

WavePreview transientPreview(EffectSettings settings, double sampleRate, std::size_t points)
{
    const double rate = validRate(sampleRate);
    auto effect = preparedEffect<dsp::TransientShaper>(settings, rate);
    const auto hit = static_cast<std::size_t>(std::lround(kHitSeconds * rate));
    const double rise = 0.002 * rate;
    const double decay = kHitDecaySeconds * rate;
    constexpr double kLevel = 0.5;
    constexpr double kFloor = 0.01; // -46 dB under the hits: what rings between them

    // The level itself is the signal: the shaper follows the peak level.
    std::vector<float> input(hit * (kPreviewHits + 1));
    for (std::size_t i = 0; i < input.size(); ++i) {
        const auto p = static_cast<double>(i % hit);
        input[i] = static_cast<float>(kLevel * std::min(1.0, p / rise) * std::exp(-p / decay) + kFloor);
    }
    const std::vector<float> output = processed(*effect, input);

    // The peak of each of `points` stretches, after the first hit.
    WavePreview preview;
    points = std::max<std::size_t>(points, 1);
    preview.dry.assign(points, 0.0f);
    preview.wet.assign(points, 0.0f);
    const std::size_t view = hit * kPreviewHits;
    for (std::size_t i = 0; i < view; ++i) {
        const std::size_t point = std::min(points - 1, i * points / view);
        preview.dry[point] = std::max(preview.dry[point], std::abs(input[hit + i]));
        preview.wet[point] = std::max(preview.wet[point], std::abs(output[hit + i]));
    }
    return preview;
}

StereoMatrix utilityMatrix(EffectSettings settings, double sampleRate)
{
    using Utility = dsp::Utility;
    const double rate = validRate(sampleRate);
    // The DC blocker would take the steady test signal away: left out.
    std::vector<double> values(settings.begin(), settings.end());
    if (values.size() > Utility::kDcBlock)
        values[Utility::kDcBlock] = 0.0;
    // A steady signal on one side, then the other, past the fades.
    constexpr std::size_t kFrames = 2048;
    const auto response = [&](float left, float right) {
        auto effect = preparedEffect<Utility>(values, rate);
        std::vector<float> l(kFrames, left);
        std::vector<float> r(kFrames, right);
        runEffect(*effect, l, r);
        return std::array<double, 2>{static_cast<double>(l.back()), static_cast<double>(r.back())};
    };
    const std::array<double, 2> fromLeft = response(1.0f, 0.0f);
    const std::array<double, 2> fromRight = response(0.0f, 1.0f);
    return {.ll = fromLeft[0], .lr = fromRight[0], .rl = fromLeft[1], .rr = fromRight[1]};
}

std::vector<double> filterResponseDb(
    EffectSettings settings, double sampleRate, std::span<const double> frequencies)
{
    const auto effect = preparedEffect<dsp::Filter>(settings, validRate(sampleRate));
    std::vector<double> response;
    response.reserve(frequencies.size());
    for (const double hz : frequencies)
        response.push_back(effect->magnitudeDb(hz));
    return response;
}

double deEsserDetectionDb(double frequency, double sampleRate, double hz)
{
    const double rate = validRate(sampleRate);
    return dsp::biquad::highPass(frequency, rate).magnitudeDb(hz, rate);
}

double deEsserGainDb(double frequency, bool splitBand, double reductionDb, double sampleRate, double hz)
{
    const double reduction = std::isfinite(reductionDb) ? std::max(0.0, reductionDb) : 0.0;
    if (!splitBand)
        return -reduction;
    if (reduction <= 0.0)
        return 0.0;
    const double rate = validRate(sampleRate);
    return dsp::biquad::highShelf(frequency, rate, -reduction).magnitudeDb(hz, rate);
}

} // namespace vloomdaw
