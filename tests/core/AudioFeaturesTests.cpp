// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/AudioFeatures.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <vector>

using namespace vloomdaw::motion;
using Catch::Approx;

namespace {

/// `frames` of a sine at `amplitude`, starting at `phase` (advanced).
std::vector<float> sine(std::size_t frames, double amplitude, double& phase, double hz = 440.0)
{
    std::vector<float> out(frames);
    for (float& sample : out) {
        sample = static_cast<float>(amplitude * std::sin(phase));
        phase += 2.0 * std::numbers::pi * hz / 48000.0;
    }
    return out;
}

} // namespace

TEST_CASE("Smoothing does not depend on the frame rate", "[motion]")
{
    // One second at 30 fps or at 120 fps ends in the same place.
    double slow = 0.0;
    for (int i = 0; i < 30; ++i)
        slow = approach(slow, 1.0, 1.0 / 30.0, 0.5);
    double fast = 0.0;
    for (int i = 0; i < 120; ++i)
        fast = approach(fast, 1.0, 1.0 / 120.0, 0.5);
    CHECK(slow == Approx(fast).epsilon(1e-9));
    CHECK(slow == Approx(1.0 - std::exp(-2.0)).epsilon(1e-9));
}

TEST_CASE("Smoothing edge cases", "[motion]")
{
    CHECK(smoothingFactor(0.0, 0.1) == 0.0);
    CHECK(smoothingFactor(-1.0, 0.1) == 0.0);
    CHECK(smoothingFactor(0.016, 0.0) == 1.0);
    CHECK(approach(2.0, 5.0, 0.1, 0.0) == 5.0);
}

TEST_CASE("Decay factor follows the decay time", "[motion]")
{
    // 60 dB in 2 s: after 2 s, one thousandth, whatever the steps.
    double level = 1.0;
    for (int i = 0; i < 120; ++i)
        level *= decayFactor(1.0 / 60.0, 2.0, 60.0);
    CHECK(level == Approx(0.001).epsilon(1e-6));
    CHECK(decayFactor(0.0, 2.0, 60.0) == 1.0);
    CHECK(decayFactor(0.1, 0.0, 60.0) == 0.0);
}

TEST_CASE("Normalised level", "[motion]")
{
    CHECK(normalisedLevel(0.0) == 1.0);
    CHECK(normalisedLevel(-30.0) == Approx(0.5));
    CHECK(normalisedLevel(-60.0) == 0.0);
    CHECK(normalisedLevel(-90.0) == 0.0);
    CHECK(normalisedLevel(6.0) == 1.0);
    CHECK(normalisedLevel(-std::numeric_limits<double>::infinity()) == 0.0);
    CHECK(normalisedLevel(std::numeric_limits<double>::quiet_NaN()) == 0.0);
}

TEST_CASE("Envelope follower attacks fast and releases slowly", "[motion]")
{
    EnvelopeFollower env(0.01, 0.5);
    const double dt = 1.0 / 60.0;
    env.step(1.0, dt);
    CHECK(env.value() > 0.8); // 16 ms is well over a 10 ms attack
    for (int i = 0; i < 10; ++i)
        env.step(1.0, dt);
    CHECK(env.value() == Approx(1.0).margin(1e-6));
    env.step(0.0, dt);
    CHECK(env.value() > 0.95); // a 500 ms release barely moves in a frame
    for (int i = 0; i < 60; ++i)
        env.step(0.0, dt); // one second more
    CHECK(env.value() == Approx(std::exp(-61.0 * dt / 0.5)).epsilon(1e-6));
    env.step(std::numeric_limits<double>::quiet_NaN(), dt);
    CHECK(std::isfinite(env.value()));
    env.reset();
    CHECK(env.value() == 0.0);
}

TEST_CASE("Onset detector finds hits, not steady sound", "[motion]")
{
    OnsetDetector onsets;
    const double dt = 1.0 / 60.0;
    int found = 0;
    // Silence, then a hit to -6 dB: one onset.
    for (int i = 0; i < 30; ++i)
        found += onsets.step(-std::numeric_limits<double>::infinity(), dt) ? 1 : 0;
    CHECK(found == 0);
    found += onsets.step(-6.0, dt) ? 1 : 0;
    CHECK(found == 1);
    CHECK(onsets.strength() == Approx(1.0));
    // The same level held: no more onsets.
    for (int i = 0; i < 120; ++i)
        found += onsets.step(-6.0, dt) ? 1 : 0;
    CHECK(found == 1);
}

TEST_CASE("Onset detector holds off between onsets and ignores quiet sound", "[motion]")
{
    OnsetDetector onsets(
        OnsetDetector::Settings{.riseDb = 6.0, .floorDb = -50.0, .holdSeconds = 0.1, .averageSeconds = 0.25});
    const double dt = 1.0 / 60.0;
    (void)onsets.step(-100.0, dt);
    CHECK(onsets.step(-10.0, dt));
    // Down and up again within the hold: not a second onset.
    CHECK_FALSE(onsets.step(-80.0, dt));
    CHECK_FALSE(onsets.step(-10.0, dt));

    OnsetDetector quiet;
    (void)quiet.step(-120.0, dt);
    CHECK_FALSE(quiet.step(-55.0, dt)); // under the floor
}

TEST_CASE("Onset detector catches hits every half second", "[motion]")
{
    OnsetDetector onsets;
    const double dt = 1.0 / 60.0;
    int found = 0;
    for (int frame = 0; frame < 240; ++frame) { // four seconds
        const double t = frame * dt;
        const double level = 0.5 * std::exp(-std::fmod(t, 0.5) / 0.05);
        found += onsets.step(20.0 * std::log10(std::max(level, 1e-9)), dt) ? 1 : 0;
    }
    CHECK(found == 8);
}

namespace {

struct Spark {
    double x = 0.0;
};

} // namespace

TEST_CASE("Particle pool reuses dead slots and then the oldest", "[motion]")
{
    ParticlePool<Spark, 4> pool;
    CHECK(pool.alive() == 0);
    for (int i = 0; i < 4; ++i)
        pool.spawn(Spark{.x = static_cast<double>(i)}, 1.0 + i);
    CHECK(pool.alive() == 4);
    // Half a second on: the first (life 1 s) is nearest its end.
    pool.advance(0.5);
    pool.spawn(Spark{.x = 10.0}, 1.0);
    CHECK(pool.alive() == 4);
    bool replaced = true;
    pool.forEach([&](const auto& each) { replaced = replaced && each.state.x != 0.0; });
    CHECK(replaced);

    // Moving: the move runs only for the living.
    const std::size_t alive = pool.advance(1.6, [](auto& each, double dt) { each.state.x += dt; });
    CHECK(alive == 2); // lives 3 and 4 s; the others ended
    pool.forEach([](const auto& each) {
        CHECK(each.progress() > 0.0);
        CHECK(each.progress() < 1.0);
    });
    pool.clear();
    CHECK(pool.alive() == 0);
}

TEST_CASE("Particle pool never grows", "[motion]")
{
    ParticlePool<Spark, 8> pool;
    for (int i = 0; i < 100; ++i)
        pool.spawn(Spark{}, 5.0);
    CHECK(pool.alive() == 8);
    CHECK(ParticlePool<Spark, 8>::capacity() == 8);
}

TEST_CASE("Audio features follow a sine and settle after it", "[motion]")
{
    AudioFeatures features;
    CHECK(features.settled());
    CHECK(features.liveliness() == 0.0);
    double phase = 0.0;
    const double dt = 1.0 / 60.0;
    int onsets = 0;
    for (int frame = 0; frame < 60; ++frame) {
        const std::vector<float> block = sine(800, 0.5, phase);
        features.update(AudioInput{.mono = block, .running = true}, dt);
        onsets += features.onset() ? 1 : 0;
    }
    CHECK(onsets == 1); // its start
    CHECK(features.peakDb() == Approx(-6.02).margin(0.1));
    CHECK(features.rmsDb() == Approx(-9.03).margin(0.1));
    CHECK(features.level() == Approx(normalisedLevel(-6.02)).margin(0.01));
    CHECK(features.envelope() == Approx(normalisedLevel(-6.02)).margin(0.02));
    CHECK(features.liveliness() == 1.0);
    CHECK_FALSE(features.settled());
    CHECK(std::abs(features.waveform().back()) <= 0.5F);

    // The sound stops: it settles in kSettleSeconds.
    int frames = 0;
    while (!features.settled() && frames < 1000) {
        features.update(AudioInput{}, dt);
        ++frames;
    }
    CHECK(frames * dt == Approx(AudioFeatures::kSettleSeconds).margin(dt * 1.5));
    CHECK(features.liveliness() == 0.0);
    CHECK(features.level() < 0.01);
    CHECK(features.waveform().front() == 0.0F);
}

TEST_CASE("Audio features: pulse, reduction and silence", "[motion]")
{
    AudioFeatures features;
    const double dt = 1.0 / 60.0;
    double phase = 0.0;
    const std::vector<float> quiet(800, 0.0F);
    features.update(AudioInput{.mono = quiet, .running = true}, dt);
    CHECK(features.silentSeconds() > 0.0); // digital silence is silence
    const std::vector<float> loud = sine(800, 0.9, phase);
    features.update(AudioInput{.mono = loud, .running = true, .reductionDb = 6.0}, dt);
    CHECK(features.onset());
    CHECK(features.pulse() > 0.5);
    CHECK(features.silentSeconds() == 0.0);
    for (int i = 0; i < 30; ++i)
        features.update(AudioInput{.mono = loud, .running = true, .reductionDb = 6.0}, dt);
    CHECK(features.pulse() < 0.05);
    CHECK(features.reductionDb() == Approx(6.0).margin(0.01));
    features.reset();
    CHECK(features.reductionDb() == 0.0);
    CHECK(features.settled());
}

TEST_CASE("Audio features: a coarse spectrum finds the tone", "[motion]")
{
    AudioFeatures features;
    features.enableSpectrum(48000.0, 24);
    REQUIRE(features.bands().size() == 24);
    double phase = 0.0;
    for (int frame = 0; frame < 10; ++frame) {
        const std::vector<float> block = sine(800, 0.5, phase, 1000.0);
        features.update(AudioInput{.mono = block, .running = true}, 1.0 / 60.0);
    }
    // The loudest band holds 1 kHz: 30 Hz .. 16 kHz in 24 log bands.
    std::size_t loudest = 0;
    for (std::size_t i = 1; i < features.bands().size(); ++i)
        if (features.bands()[i] > features.bands()[loudest])
            loudest = i;
    const double width = std::log(16000.0 / 30.0) / 24.0;
    const double low = 30.0 * std::exp(width * static_cast<double>(loudest));
    CHECK(low <= 1000.0);
    CHECK(low * std::exp(width) >= 1000.0);
    CHECK(features.bands()[0] < features.bands()[loudest]);
}
