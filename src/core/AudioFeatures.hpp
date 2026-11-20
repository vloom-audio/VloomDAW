// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/SpectrumAnalyzer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <vector>

/// What the animated displays of the built-in editors know of the sound,
/// worked out once a frame from the plugin's scope tap: levels, an envelope,
/// onsets, a short waveform and, if asked for, a coarse spectrum. Pure logic
/// (no Qt), unit tested; docs/builtin-editors.md, "Animation", says how an
/// editor uses it. Every time is in seconds and every smoothing takes the
/// real frame time `dt`, so the motion is the same at any frame rate.
namespace vloomdaw::motion {

/// The share of the way to its target a value smoothed with time constant
/// `tau` moves in `dt`: 1 - e^(-dt / tau). 1 for tau <= 0 (no smoothing),
/// 0 for dt <= 0.
[[nodiscard]] double smoothingFactor(double dt, double tau) noexcept;

/// `current` moved towards `target` by smoothingFactor(dt, tau).
[[nodiscard]] double approach(double current, double target, double dt, double tau) noexcept;

/// What is left of something that fades by `db` decibels every `seconds`
/// after `dt`: 10^(-db / 20 * dt / seconds). A reverb tail falling 60 dB in
/// its decay time keeps decayFactor(dt, decay, 60) of its level a frame.
[[nodiscard]] double decayFactor(double dt, double seconds, double db) noexcept;

/// A level in dB (full scale) as 0 .. 1 over the `floorDb` .. 0 dB range a
/// display shows: 0 at or below the floor (and for -inf or NaN), 1 at 0 dB.
[[nodiscard]] double normalisedLevel(double db, double floorDb = -60.0) noexcept;

/// Follows a level: rises with the attack time and falls with the release
/// time (both time constants, s). What makes a glow breathe with the sound.
class EnvelopeFollower {
public:
    EnvelopeFollower(double attackSeconds = 0.01, double releaseSeconds = 0.25) noexcept;

    void setTimes(double attackSeconds, double releaseSeconds) noexcept;
    /// One frame of `dt` towards `target`; returns the new value.
    double step(double target, double dt) noexcept;
    void reset(double value = 0.0) noexcept { value_ = value; }
    [[nodiscard]] double value() const noexcept { return value_; }

private:
    double attack_;
    double release_;
    double value_ = 0.0;
};

/// Finds onsets (a hit, a note's attack) in a level read once a frame: the
/// level jumps clearly above where it has been lately. A slow average of the
/// level (in dB, starting from silence) is the reference; an onset is the
/// level rising `riseDb` above it, above `floorDb`, at least `holdSeconds`
/// after the last.
class OnsetDetector {
public:
    struct Settings {
        double riseDb = 6.0; ///< above the recent level
        double floorDb = -50.0; ///< quieter than this is never an onset
        double holdSeconds = 0.08; ///< the least time between two onsets
        double averageSeconds = 0.25; ///< time constant of the recent level
    };

    OnsetDetector() noexcept;
    explicit OnsetDetector(Settings settings) noexcept;

    /// One frame: the level now (dB full scale; -inf or NaN for silence).
    /// True if it is an onset.
    bool step(double levelDb, double dt) noexcept;
    void reset() noexcept;
    /// How strong the last onset was: its rise above the recent level, as
    /// 0 .. 1 (1 at riseDb * 3 or more).
    [[nodiscard]] double strength() const noexcept { return strength_; }

private:
    Settings settings_;
    double averageDb_;
    double sinceOnset_ = std::numeric_limits<double>::infinity();
    double strength_ = 0.0;
};

/// A fixed number of short-lived things (sparks, pulses): spawning reuses
/// the slot of a dead one or, when all are alive, the oldest; nothing is
/// allocated after construction. `T` is the caller's per-particle state.
template <typename T, std::size_t N> class ParticlePool {
public:
    struct Particle {
        T state{};
        double age = 0.0; ///< s since it was spawned
        double life = 0.0; ///< s it lives; dead once age >= life
        [[nodiscard]] bool alive() const noexcept { return age < life; }
        /// 0 when spawned .. 1 when it dies.
        [[nodiscard]] double progress() const noexcept
        {
            return life > 0.0 ? std::min(1.0, age / life) : 1.0;
        }
    };

    static constexpr std::size_t capacity() noexcept { return N; }

    /// A particle living `life` seconds with `state`; returns it.
    Particle& spawn(const T& state, double life) noexcept
    {
        Particle* slot = &particles_[0];
        for (Particle& each : particles_) {
            if (!each.alive()) {
                slot = &each;
                break;
            }
            if (each.age / std::max(each.life, 1e-9) > slot->age / std::max(slot->life, 1e-9))
                slot = &each; // the one nearest its end
        }
        slot->state = state;
        slot->age = 0.0;
        slot->life = std::max(0.0, life);
        return *slot;
    }

    /// Ages every particle by `dt`; `move(Particle&, dt)` runs for each one
    /// still alive. Returns how many are alive.
    template <typename Move> std::size_t advance(double dt, Move&& move)
    {
        std::size_t alive = 0;
        for (Particle& each : particles_) {
            if (!each.alive())
                continue;
            each.age += dt;
            if (each.alive()) {
                move(each, dt);
                ++alive;
            }
        }
        return alive;
    }
    std::size_t advance(double dt)
    {
        return advance(dt, [](Particle&, double) { });
    }

    /// `visit(const Particle&)` for each one alive.
    template <typename Visit> void forEach(Visit&& visit) const
    {
        for (const Particle& each : particles_)
            if (each.alive())
                visit(each);
    }

    [[nodiscard]] std::size_t alive() const noexcept
    {
        std::size_t count = 0;
        for (const Particle& each : particles_)
            count += each.alive() ? 1U : 0U;
        return count;
    }
    void clear() noexcept
    {
        for (Particle& each : particles_)
            each.life = 0.0;
    }

private:
    std::array<Particle, N> particles_{};
};

/// One frame of the plugin's sound, as the editor's shell reads it. Only
/// `mono` and `running` come from the engine today (PluginInstance's scope);
/// the rest are for what it may report later (per-instance input and output
/// meters, a stereo scope) and stay empty until then.
struct AudioInput {
    /// The newest samples since the last frame, oldest first (left and right
    /// averaged): what the plugin put out.
    std::span<const float> mono;
    /// Audio went through since the last frame.
    bool running = false;
    /// The gain reduction it reports (dB, >= 0); NaN for none.
    double reductionDb = std::numeric_limits<double>::quiet_NaN();
    /// Later: the left and right output on their own (empty: unknown).
    std::span<const float> left;
    std::span<const float> right;
    /// Later: the input's peak (dB full scale); nullopt: unknown.
    std::optional<double> inputPeakDb;
};

/// The features an animated display draws from, updated once a frame.
class AudioFeatures {
public:
    /// Samples the waveform keeps (and the spectrum analyses).
    static constexpr std::size_t kWaveFrames = 2048;
    /// No sound for this long: everything has settled and the clock may idle.
    static constexpr double kSettleSeconds = 2.0;
    /// Quieter than this counts as silence (dB full scale).
    static constexpr double kSilenceDb = -80.0;

    AudioFeatures();
    ~AudioFeatures();
    AudioFeatures(const AudioFeatures&) = delete;
    AudioFeatures& operator=(const AudioFeatures&) = delete;

    /// Also works out a coarse spectrum of `bands` bands (log spaced, 30 Hz
    /// .. 16 kHz) with SpectrumAnalyzer at `sampleRate`; 0 bands: none.
    void enableSpectrum(double sampleRate, std::size_t bands);

    /// One frame of `dt` seconds.
    void update(const AudioInput& input, double dt);
    /// Forgets the sound: silent and settled.
    void reset();

    /// The peak and RMS of the last frame's samples (dB full scale; -inf: silent).
    [[nodiscard]] double peakDb() const noexcept { return peakDb_; }
    [[nodiscard]] double rmsDb() const noexcept { return rmsDb_; }
    /// The peak, smoothed (fast attack, 0.3 s release), as 0 .. 1 over
    /// -60 .. 0 dB: what a glow's brightness follows.
    [[nodiscard]] double level() const noexcept { return level_.value(); }
    /// The RMS through a slower follower (0.05 s attack, 0.6 s release), as
    /// 0 .. 1 over -60 .. 0 dB: what a size or a spread follows.
    [[nodiscard]] double envelope() const noexcept { return envelope_.value(); }
    /// An onset this frame, and how strong (0 .. 1).
    [[nodiscard]] bool onset() const noexcept { return onset_; }
    [[nodiscard]] double onsetStrength() const noexcept { return onsets_.strength(); }
    /// 1 at an onset (times its strength), falling to 0 in about 0.4 s.
    [[nodiscard]] double pulse() const noexcept { return pulse_; }
    /// The gain reduction (dB, >= 0), smoothed; 0 for a plugin without one.
    [[nodiscard]] double reductionDb() const noexcept { return reduction_; }
    /// The newest kWaveFrames samples, oldest first (zeros before the first).
    [[nodiscard]] std::span<const float> waveform() const noexcept { return wave_; }
    /// The coarse spectrum: each band 0 .. 1 over -90 .. 0 dB (empty unless enabled).
    [[nodiscard]] std::span<const double> bands() const noexcept { return bands_; }

    /// Audio went through in the last frame.
    [[nodiscard]] bool running() const noexcept { return running_; }
    /// Seconds since the last frame with sound (0 while it plays).
    [[nodiscard]] double silentSeconds() const noexcept { return silent_; }
    /// 1 while sound plays, easing to 0 over kSettleSeconds after it stops:
    /// multiply what moves by it so it comes to rest in time.
    [[nodiscard]] double liveliness() const noexcept;
    /// Silent for kSettleSeconds: nothing should move any more.
    [[nodiscard]] bool settled() const noexcept { return silent_ >= kSettleSeconds; }

private:
    double peakDb_ = -std::numeric_limits<double>::infinity();
    double rmsDb_ = -std::numeric_limits<double>::infinity();
    EnvelopeFollower level_{0.005, 0.3};
    EnvelopeFollower envelope_{0.05, 0.6};
    OnsetDetector onsets_;
    bool onset_ = false;
    double pulse_ = 0.0;
    double reduction_ = 0.0;
    std::vector<float> wave_;
    bool running_ = false;
    double silent_ = kSettleSeconds;

    std::unique_ptr<SpectrumAnalyzer> spectrum_;
    std::vector<double> bandEdges_; ///< Hz, bands + 1
    std::vector<double> bands_;
};

} // namespace vloomdaw::motion
