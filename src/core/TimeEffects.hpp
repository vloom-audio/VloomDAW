// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

/// What the editors of the built-in time and modulation effects draw (delay,
/// reverb, chorus, flanger, phaser, tremolo), worked out from parameter
/// values the way the effects in vloom-dsp work them out. Pure maths: no Qt,
/// not for the audio thread.
namespace vloomdaw {

// --- Delay ("vloom.delay") ---------------------------------------------------

/// What shapes the delay's echoes.
struct EchoSettings {
    double leftSeconds = 0.375; ///< dsp::Delay::delaySeconds(false)
    double rightSeconds = 0.375; ///< dsp::Delay::delaySeconds(true)
    bool pingPong = false; ///< Mode: Ping-pong
    double feedback = 0.35; ///< 0 .. 1
    double width = 1.0; ///< 0 (mono echoes) .. 1
};

/// One echo of a sound that is the same on both sides (a centred hit): when
/// it is heard and how loud on each side, relative to the first echo of the
/// delay line it comes from (before the dry/wet mix and the cut filters).
struct EchoTap {
    double seconds = 0.0;
    double left = 0.0;
    double right = 0.0;
};

/// The echoes up to `untilSeconds`, earliest first, while they are louder
/// than `floor` and at most `maxTaps` of them. Stereo: each side echoes
/// itself; ping-pong: the first echo is on the left, then they bounce, each
/// `feedback` quieter. Width narrows each echo towards the middle, as
/// dsp::Delay does (mid/side).
[[nodiscard]] std::vector<EchoTap> echoTaps(
    const EchoSettings& settings, double untilSeconds, double floor = 0.001, std::size_t maxTaps = 64);

/// The index of the note in `beats` (lengths in beats, one per note value)
/// whose length at `bpm` is nearest `seconds` (nearest as a ratio: 1/8 and
/// 1/4 are as far from 3/16). 0 for an empty list.
[[nodiscard]] int nearestNote(double seconds, double bpm, std::span<const double> beats) noexcept;

// --- Reverb ("vloom.reverb") ---------------------------------------------------

/// The reverb's line length factor for Size (0 .. 100 %): 0.3 .. 2.0, spread
/// exponentially (dsp Reverb.cpp).
[[nodiscard]] double reverbScale(double sizePercent) noexcept;

/// One of the first trips round the reverb's eight delay lines.
struct ReverbReflection {
    double seconds = 0.0; ///< after the dry sound (pre-delay included)
    double level = 0.0; ///< 0 .. 1
};

/// The first reflections: each of the reverb's eight lines once round, after
/// the pre-delay, as long as Size makes them and turned down by one trip's
/// loss for Decay. Diffusion smears them into the tail, so they read lower.
[[nodiscard]] std::vector<ReverbReflection> reverbReflections(
    double preDelaySeconds, double sizePercent, double decaySeconds, double diffusionPercent);

/// When the reverb's tail starts after the dry sound: the pre-delay and the
/// shortest line.
[[nodiscard]] double reverbTailStart(double preDelaySeconds, double sizePercent) noexcept;

/// The tail's level `seconds` after it starts when it falls by 60 dB in
/// `decaySeconds` (1 at the start; 1 throughout while frozen).
[[nodiscard]] double reverbLevel(double seconds, double decaySeconds, bool frozen = false) noexcept;

/// The decay time (60 dB) at `hz`: Damping's low-pass in the loop takes
/// more off high frequencies on every trip, so they die sooner than
/// `decaySeconds`. The trip is the lines' average length at Size.
[[nodiscard]] double reverbDecayAt(
    double hz, double decaySeconds, double dampingPercent, double sizePercent, double sampleRate) noexcept;

// --- LFOs (chorus, flanger, phaser, tremolo) -------------------------------------

/// The shapes of the effects' LFOs (dsp::detail::LfoShape; Tremolo's Shape).
enum class LfoWave { Sine, Triangle, Square };

/// The LFO's value at `phase` (cycles): -1 .. 1, 0 at phase 0 and peak at
/// a quarter, as dsp::detail::lfoValue().
[[nodiscard]] double lfoWave(LfoWave wave, double phase) noexcept;

/// How many seconds a display of an LFO at `rateHz` shows: a round length
/// (0.1, 0.2, 0.25, 0.5, 1, 2, 4, 5, 10 ... 500 s) that holds at least two
/// cycles, so the curve keeps its look as the rate turns.
[[nodiscard]] double lfoWindowSeconds(double rateHz) noexcept;

/// A round step for the ticks of an axis `span` long with at most
/// `maxTicks` ticks: 1, 2 or 5 times a power of ten.
[[nodiscard]] double tickStep(double span, int maxTicks) noexcept;

/// The delay of chorus voice `voice` (0 .. 3) at LFO `phase` (cycles), in ms:
/// from `delayMs` up to `delayMs` + depth x 8 ms (dsp::Chorus).
[[nodiscard]] double chorusDelayMs(double delayMs, double depth, int voice, double phase) noexcept;

/// The flanger's gain at `hz`, in dB, with its delay held at `delayMs`:
/// the input mixed (`mix`, 0 .. 1) with a comb of `feedback` (-0.95 .. 0.95).
[[nodiscard]] double flangerResponseDb(
    double hz, double delayMs, double feedback, double mix, double sampleRate) noexcept;

/// The lowest notch of the flanger's comb at `delayMs`: half a cycle
/// across the delay.
[[nodiscard]] double flangerNotchHz(double delayMs) noexcept;

/// The phaser's gain at `hz`, in dB, with its all-passes held at
/// `centreHz`: `stages` first-order all-passes, `feedback` (-0.95 .. 0.95)
/// from their output back to their input, mixed (`mix`) with the input.
[[nodiscard]] double phaserResponseDb(
    double hz, double centreHz, int stages, double feedback, double mix, double sampleRate) noexcept;

/// The lowest and highest frequency the phaser sweeps to around `centreHz`
/// at `depth` (0 .. 1: up to two octaves either way), kept between 20 Hz and
/// 20 kHz (and under 0.45 of the sample rate).
[[nodiscard]] std::pair<double, double> phaserSweepHz(
    double centreHz, double depth, double sampleRate) noexcept;

/// The tremolo's gain on the left and the right (0 .. 1) at LFO `phase`
/// (cycles): Tremolo dips each side by up to `depth` (0 .. 1), the right
/// `stereoPhase` cycles on; auto-pan moves the sound between the sides
/// without boosting either.
[[nodiscard]] std::pair<double, double> tremoloGains(
    bool autoPan, LfoWave wave, double depth, double stereoPhase, double phase) noexcept;

} // namespace vloomdaw
