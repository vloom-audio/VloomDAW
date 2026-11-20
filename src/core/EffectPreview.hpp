// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace vloomdaw {

/// The pictures the built-in effects' editors draw from their settings,
/// worked out with the effects' own code (vloom::dsp) run offline on a test
/// signal, or with their own maths: what the effect does to a sine, a hit,
/// a stereo pair, a range of frequencies. No audio device, no made-up
/// shapes. Not for the audio thread.
///
/// `settings` are an effect's parameter values by id (0 .. n - 1), as an
/// editor holds them; missing ids keep their defaults.
using EffectSettings = std::span<const double>;

/// Frames an effect is run in at a time offline.
inline constexpr int kPreviewBlockFrames = 256;

/// Makes a `Effect` (a vloom::dsp effect), prepared for `sampleRate`, with
/// `settings` and its ramps at their targets.
template <typename Effect>
[[nodiscard]] std::unique_ptr<Effect> preparedEffect(EffectSettings settings, double sampleRate)
{
    auto effect = std::make_unique<Effect>();
    effect->prepare(sampleRate, kPreviewBlockFrames);
    for (std::size_t id = 0; id < settings.size(); ++id)
        effect->setParameter(static_cast<std::uint32_t>(id), settings[id]);
    effect->reset(); // glides jump to the settings
    return effect;
}

/// Runs `effect` over `left` and `right` (the same length) in place.
template <typename Effect> void runEffect(Effect& effect, std::span<float> left, std::span<float> right)
{
    const std::size_t frames = std::min(left.size(), right.size());
    for (std::size_t at = 0; at < frames; at += kPreviewBlockFrames) {
        const auto count = static_cast<int>(std::min<std::size_t>(kPreviewBlockFrames, frames - at));
        std::array<float*, 2> channels{left.data() + at, right.data() + at};
        effect.process(channels.data(), 2, count);
    }
}

/// A test signal before an effect (`dry`) and after it (`wet`), frame by
/// frame and lined up (the effect's latency taken out).
struct WavePreview {
    std::vector<float> dry;
    std::vector<float> wet;
};

// --- Saturator ------------------------------------------------------------

/// The saturator's static curve: the output for an input `x` (full scale
/// 1), with its drive, curve, output and mix (the tone tilt and the DC
/// blocker are filters, left out).
[[nodiscard]] double saturatorCurve(EffectSettings settings, double x);

/// Cycles of the sine a wave preview shows.
inline constexpr double kPreviewCycles = 2.5;

/// A 100 Hz sine at -4.4 dB (0.6) through the saturator: kPreviewCycles of
/// it, after the filters settled.
[[nodiscard]] WavePreview saturatorPreview(EffectSettings settings, double sampleRate);

// --- Bitcrusher -------------------------------------------------------------

/// Frames per cycle of the bitcrusher's preview sine: about 12 held steps a
/// cycle once it is downsampled, never fewer than 96 frames.
[[nodiscard]] double crusherCycleFrames(double downsample);

/// A sine at 0.8 through the bitcrusher, kPreviewCycles of it, slow enough
/// (crusherCycleFrames()) that the held steps show.
[[nodiscard]] WavePreview crusherPreview(EffectSettings settings, double sampleRate);

// --- Transient shaper -------------------------------------------------------

/// The hits of the transient shaper's preview: one every kHitSeconds, a 2 ms
/// rise then a decay of kHitDecaySeconds, over a quiet floor.
inline constexpr int kPreviewHits = 4;
inline constexpr double kHitSeconds = 0.5;
inline constexpr double kHitDecaySeconds = 0.12;

/// The level of the preview's hits (positive, at most 0.5) before the
/// shaper and after it, `points` peaks over kPreviewHits hits (a hit before
/// them settles the followers).
[[nodiscard]] WavePreview transientPreview(EffectSettings settings, double sampleRate, std::size_t points);

// --- Utility ----------------------------------------------------------------

/// What the utility does to a stereo pair, once its fades are over (the DC
/// blocker left out): out left = ll * in left + lr * in right, out right =
/// rl * in left + rr * in right.
struct StereoMatrix {
    double ll = 1.0;
    double lr = 0.0;
    double rl = 0.0;
    double rr = 1.0;
};

[[nodiscard]] StereoMatrix utilityMatrix(EffectSettings settings, double sampleRate);

// --- Filter -----------------------------------------------------------------

/// The filter's gain at each of `frequencies` (Hz), in dB, drive left out
/// (dsp::Filter::magnitudeDb()).
[[nodiscard]] std::vector<double> filterResponseDb(
    EffectSettings settings, double sampleRate, std::span<const double> frequencies);

// --- De-esser ---------------------------------------------------------------

/// What the de-esser's detector hears of `hz`: its 12 dB per octave
/// high-pass at `frequency`, in dB.
[[nodiscard]] double deEsserDetectionDb(double frequency, double sampleRate, double hz);

/// The de-esser's gain at `hz` while it takes `reductionDb` (>= 0) off:
/// the high shelf at `frequency` in split-band mode, all of it in wideband.
[[nodiscard]] double deEsserGainDb(
    double frequency, bool splitBand, double reductionDb, double sampleRate, double hz);

} // namespace vloomdaw
