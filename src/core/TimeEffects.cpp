// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/TimeEffects.hpp"

#include <vloom/dsp/effects/detail/Modulation.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>

namespace vloomdaw {

namespace {

using Complex = std::complex<double>;

constexpr double kTwoPi = 2.0 * std::numbers::pi;

// Mirrors of the constants in vloom-dsp's sources, which the headers do not
// export. Keep them in step with src/effects/Reverb.cpp and Chorus.hpp.
constexpr std::array<double, 8> kReverbLineMs{29.71, 33.83, 37.97, 41.03, 44.69, 49.31, 53.87, 59.11};
constexpr double kReverbMinScale = 0.3; // Size 0 %
constexpr double kReverbMaxScale = 2.0; // Size 100 %
constexpr double kReverbDampingFromHz = 20000.0; // Damping 0 %
constexpr double kReverbDampingToHz = 1500.0; // Damping 100 %
constexpr std::array<double, 4> kChorusVoicePhases{0.0, 0.5, 1.0 / 6.0, 2.0 / 3.0};
constexpr double kChorusMaxSwingMs = 8.0;
constexpr double kPhaserMaxSweepOctaves = 2.0;
constexpr double kMinFrequency = 20.0;
constexpr double kMaxFrequency = 20000.0;

[[nodiscard]] double finiteOr(double value, double fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

[[nodiscard]] double toDb(double gain) noexcept { return 20.0 * std::log10(std::max(gain, 1.0e-6)); }

/// e^(-j w frames): a delay of `frames` at angular frequency `w` (radians per frame).
[[nodiscard]] Complex delayed(double w, double frames) noexcept { return std::polar(1.0, -w * frames); }

} // namespace

std::vector<EchoTap> echoTaps(
    const EchoSettings& settings, double untilSeconds, double floor, std::size_t maxTaps)
{
    std::vector<EchoTap> taps;
    const double feedback = std::clamp(finiteOr(settings.feedback, 0.0), 0.0, 1.0);
    const double width = std::clamp(finiteOr(settings.width, 1.0), 0.0, 1.0);
    const double left = finiteOr(settings.leftSeconds, 0.0);
    const double right = finiteOr(settings.rightSeconds, 0.0);
    if (!(left > 0.0) || !(right > 0.0) || maxTaps == 0)
        return taps;
    // An echo from the left line is heard (1 + width) / 2 on the left and the
    // rest on the right; one from the right line the other way round.
    const double near = 0.5 + 0.5 * width;
    const double far = 0.5 - 0.5 * width;
    const auto add = [&](double seconds, double level, bool fromLeft) {
        for (EchoTap& tap : taps) {
            if (std::abs(tap.seconds - seconds) < 1e-9) {
                tap.left += level * (fromLeft ? near : far);
                tap.right += level * (fromLeft ? far : near);
                return;
            }
        }
        taps.push_back(EchoTap{.seconds = seconds,
            .left = level * (fromLeft ? near : far),
            .right = level * (fromLeft ? far : near)});
    };

    if (settings.pingPong) {
        // In on the left, then across and back.
        double seconds = left;
        double level = 1.0;
        bool onLeft = true;
        while (seconds <= untilSeconds && level >= floor && taps.size() < maxTaps) {
            add(seconds, level, onLeft);
            seconds += onLeft ? right : left;
            onLeft = !onLeft;
            level *= feedback;
            if (level == 0.0)
                break;
        }
    } else {
        for (const bool fromLeft : {true, false}) {
            const double step = fromLeft ? left : right;
            double level = 1.0;
            for (int k = 1; k * step <= untilSeconds && level >= floor; ++k) {
                add(k * step, level, fromLeft);
                if (taps.size() >= 2 * maxTaps)
                    break;
                level *= feedback;
                if (level == 0.0)
                    break;
            }
        }
        std::sort(taps.begin(), taps.end(),
            [](const EchoTap& a, const EchoTap& b) { return a.seconds < b.seconds; });
        if (taps.size() > maxTaps)
            taps.resize(maxTaps);
    }
    return taps;
}

int nearestNote(double seconds, double bpm, std::span<const double> beats) noexcept
{
    if (beats.empty() || !(seconds > 0.0) || !(bpm > 0.0))
        return 0;
    const double wanted = std::log(seconds * bpm / 60.0); // in beats
    int best = 0;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < beats.size(); ++i) {
        if (!(beats[i] > 0.0))
            continue;
        const double distance = std::abs(std::log(beats[i]) - wanted);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = static_cast<int>(i);
        }
    }
    return best;
}

double reverbScale(double sizePercent) noexcept
{
    const double size = std::clamp(finiteOr(sizePercent, 50.0), 0.0, 100.0);
    return kReverbMinScale * std::pow(kReverbMaxScale / kReverbMinScale, size / 100.0);
}

std::vector<ReverbReflection> reverbReflections(
    double preDelaySeconds, double sizePercent, double decaySeconds, double diffusionPercent)
{
    const double scale = reverbScale(sizePercent);
    const double decay = std::max(finiteOr(decaySeconds, 2.0), 0.01);
    const double diffusion = std::clamp(finiteOr(diffusionPercent, 0.0), 0.0, 100.0) / 100.0;
    const double preDelay = std::max(finiteOr(preDelaySeconds, 0.0), 0.0);
    std::vector<ReverbReflection> reflections;
    reflections.reserve(kReverbLineMs.size());
    for (const double ms : kReverbLineMs) {
        const double trip = ms / 1000.0 * scale;
        // One trip's loss: -60 dB over the decay time (dsp Reverb::control()).
        const double loss = std::pow(10.0, -3.0 * trip / decay);
        reflections.push_back(
            ReverbReflection{.seconds = preDelay + trip, .level = loss * (1.0 - 0.5 * diffusion)});
    }
    return reflections;
}

double reverbTailStart(double preDelaySeconds, double sizePercent) noexcept
{
    return std::max(finiteOr(preDelaySeconds, 0.0), 0.0)
        + kReverbLineMs.front() / 1000.0 * reverbScale(sizePercent);
}

double reverbLevel(double seconds, double decaySeconds, bool frozen) noexcept
{
    if (frozen || !(seconds > 0.0))
        return 1.0;
    const double decay = std::max(finiteOr(decaySeconds, 2.0), 0.01);
    return std::pow(10.0, -3.0 * seconds / decay);
}

double reverbDecayAt(
    double hz, double decaySeconds, double dampingPercent, double sizePercent, double sampleRate) noexcept
{
    const double decay = std::max(finiteOr(decaySeconds, 2.0), 0.01);
    if (!(sampleRate > 0.0) || !(hz > 0.0))
        return decay;
    double average = 0.0;
    for (const double ms : kReverbLineMs)
        average += ms;
    const double trip
        = average / static_cast<double>(kReverbLineMs.size()) / 1000.0 * reverbScale(sizePercent);
    // The loop's one-pole low-pass (dsp Reverb: damped = out + a (damped - out)).
    const double damping = std::clamp(finiteOr(dampingPercent, 0.0), 0.0, 100.0) / 100.0;
    const double cutoff = kReverbDampingFromHz * std::pow(kReverbDampingToHz / kReverbDampingFromHz, damping);
    const double a = std::exp(-kTwoPi * cutoff / sampleRate);
    const double w = kTwoPi * std::min(hz, 0.499 * sampleRate) / sampleRate;
    const double gain = (1.0 - a) / std::abs(Complex(1.0, 0.0) - a * delayed(w, 1.0));
    // dB lost per second: the decay's 60 dB per decay time plus the filter's per trip.
    const double lossPerSecond = 60.0 / decay - toDb(gain) / trip;
    return lossPerSecond > 0.0 ? std::min(60.0 / lossPerSecond, decay) : decay;
}

double lfoWave(LfoWave wave, double phase) noexcept
{
    using vloom::dsp::detail::LfoShape;
    const LfoShape shape = wave == LfoWave::Triangle ? LfoShape::Triangle
        : wave == LfoWave::Square                    ? LfoShape::Square
                                                     : LfoShape::Sine;
    return static_cast<double>(vloom::dsp::detail::lfoValue(shape, finiteOr(phase, 0.0)));
}

double lfoWindowSeconds(double rateHz) noexcept
{
    constexpr std::array<double, 15> kWindows{
        0.1, 0.2, 0.25, 0.5, 1.0, 2.0, 4.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 250.0, 500.0};
    if (!(rateHz > 0.0) || !std::isfinite(rateHz))
        return kWindows.back();
    for (const double window : kWindows)
        if (rateHz * window >= 2.0 - 1e-9)
            return window;
    return kWindows.back();
}

double tickStep(double span, int maxTicks) noexcept
{
    if (!(span > 0.0) || !std::isfinite(span) || maxTicks < 1)
        return 1.0;
    const double rough = span / maxTicks;
    const double power = std::pow(10.0, std::floor(std::log10(rough)));
    for (const double factor : {1.0, 2.0, 5.0, 10.0})
        if (factor * power >= rough - 1e-12 * power)
            return factor * power;
    return 10.0 * power;
}

double chorusDelayMs(double delayMs, double depth, int voice, double phase) noexcept
{
    const double offset = kChorusVoicePhases[static_cast<std::size_t>(std::clamp(voice, 0, 3))];
    const double sweep = 0.5 + 0.5 * std::sin(kTwoPi * (finiteOr(phase, 0.0) + offset));
    return delayMs + std::clamp(finiteOr(depth, 0.0), 0.0, 1.0) * kChorusMaxSwingMs * sweep;
}

double flangerResponseDb(double hz, double delayMs, double feedback, double mix, double sampleRate) noexcept
{
    if (!(sampleRate > 0.0))
        return 0.0;
    const double w = kTwoPi * finiteOr(hz, 0.0) / sampleRate;
    const double frames = std::max(finiteOr(delayMs, 1.0), 0.0) * 0.001 * sampleRate;
    const double m = std::clamp(finiteOr(mix, 0.0), 0.0, 1.0);
    // line.push(x + fb wet), wet = line.read(D): wet = z^-D x / (1 - fb z^-D).
    const Complex z = delayed(w, frames);
    const Complex wet = z / (1.0 - finiteOr(feedback, 0.0) * z);
    return toDb(std::abs((1.0 - m) + m * wet));
}

double flangerNotchHz(double delayMs) noexcept { return 500.0 / std::max(finiteOr(delayMs, 1.0), 1e-3); }

double phaserResponseDb(
    double hz, double centreHz, int stages, double feedback, double mix, double sampleRate) noexcept
{
    if (!(sampleRate > 0.0))
        return 0.0;
    const double w = kTwoPi * finiteOr(hz, 0.0) / sampleRate;
    // dsp::detail::AllpassStage: y = a (x - y1) + x1, a for -90 degrees at the centre.
    const double t = std::tan(
        std::numbers::pi * std::clamp(finiteOr(centreHz, 1000.0), 1.0, 0.49 * sampleRate) / sampleRate);
    const double a = (t - 1.0) / (t + 1.0);
    const Complex z1 = delayed(w, 1.0);
    const Complex stage = (a + z1) / (1.0 + a * z1);
    const Complex chain = std::pow(stage, std::max(stages, 0));
    // The chain's last output goes back in a sample later: wet = A x / (1 - fb z^-1 A).
    const Complex wet = chain / (1.0 - finiteOr(feedback, 0.0) * z1 * chain);
    const double m = std::clamp(finiteOr(mix, 0.0), 0.0, 1.0);
    return toDb(std::abs((1.0 - m) + m * wet));
}

std::pair<double, double> phaserSweepHz(double centreHz, double depth, double sampleRate) noexcept
{
    const double lowest = std::log2(kMinFrequency);
    const double highest
        = std::log2(std::min(kMaxFrequency, 0.45 * (sampleRate > 0.0 ? sampleRate : 48000.0)));
    const double octave = std::log2(std::max(finiteOr(centreHz, 1000.0), 1.0));
    const double sweep = std::clamp(finiteOr(depth, 0.0), 0.0, 1.0) * kPhaserMaxSweepOctaves;
    return {std::exp2(std::clamp(octave - sweep, lowest, highest)),
        std::exp2(std::clamp(octave + sweep, lowest, highest))};
}

std::pair<double, double> tremoloGains(
    bool autoPan, LfoWave wave, double depth, double stereoPhase, double phase) noexcept
{
    const double d = std::clamp(finiteOr(depth, 0.0), 0.0, 1.0);
    const double left = lfoWave(wave, phase);
    if (autoPan) {
        const double pan = d * left;
        return {std::min(1.0, 1.0 - pan), std::min(1.0, 1.0 + pan)};
    }
    const double right = lfoWave(wave, phase + finiteOr(stereoPhase, 0.0));
    return {1.0 - d * (0.5 - 0.5 * left), 1.0 - d * (0.5 - 0.5 * right)};
}

} // namespace vloomdaw
