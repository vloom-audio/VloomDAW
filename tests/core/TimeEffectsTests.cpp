// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/TimeEffects.hpp"

#include <vloom/dsp/NoteValue.hpp>
#include <vloom/dsp/effects/Delay.hpp>
#include <vloom/dsp/effects/Flanger.hpp>
#include <vloom/dsp/effects/Phaser.hpp>
#include <vloom/dsp/effects/Tremolo.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

using namespace vloomdaw;
using Catch::Approx;

namespace {

constexpr double kRate = 48000.0;

/// Runs `effect` over a stereo buffer of `frames`, `left` and `right` in and out.
template <typename Effect> void run(Effect& effect, std::vector<float>& left, std::vector<float>& right)
{
    std::array<float*, 2> channels{left.data(), right.data()};
    effect.process(channels.data(), 2, static_cast<int>(left.size()));
}

/// The steady gain (dB) of `effect` for a sine at `hz` on both sides: the
/// peak of the second half of four seconds.
template <typename Effect> double sineGainDb(Effect& effect, double hz)
{
    const auto frames = static_cast<std::size_t>(4.0 * kRate);
    std::vector<float> left(frames);
    for (std::size_t i = 0; i < frames; ++i)
        left[i] = static_cast<float>(
            0.5 * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kRate));
    std::vector<float> right = left;
    effect.reset();
    run(effect, left, right);
    float peak = 0.0f;
    for (std::size_t i = frames / 2; i < frames; ++i)
        peak = std::max(peak, std::abs(left[i]));
    return 20.0 * std::log10(static_cast<double>(peak) / 0.5);
}

} // namespace

TEST_CASE("Stereo echoes repeat on each side, each feedback quieter", "[timeeffects]")
{
    const auto taps = echoTaps(
        EchoSettings{
            .leftSeconds = 0.25, .rightSeconds = 0.25, .pingPong = false, .feedback = 0.5, .width = 1.0},
        1.0);
    REQUIRE(taps.size() == 4);
    for (std::size_t i = 0; i < taps.size(); ++i) {
        CHECK(taps[i].seconds == Approx(0.25 * static_cast<double>(i + 1)));
        CHECK(taps[i].left == Approx(std::pow(0.5, static_cast<double>(i))));
        CHECK(taps[i].right == Approx(taps[i].left));
    }
    // A longer right side: its echoes fall between the left's.
    const auto offset = echoTaps(
        EchoSettings{
            .leftSeconds = 0.2, .rightSeconds = 0.3, .pingPong = false, .feedback = 0.5, .width = 1.0},
        0.65);
    REQUIRE(offset.size() == 4); // 0.2 L, 0.3 R, 0.4 L, then 0.6 on both
    CHECK(offset[1].seconds == Approx(0.3));
    CHECK(offset[1].left == Approx(0.0));
    CHECK(offset[1].right == Approx(1.0));
    CHECK(offset[3].seconds == Approx(0.6));
    CHECK(offset[3].left == Approx(0.25));
    CHECK(offset[3].right == Approx(0.5));
    CHECK(std::is_sorted(offset.begin(), offset.end(),
        [](const EchoTap& a, const EchoTap& b) { return a.seconds < b.seconds; }));
}

TEST_CASE("Ping-pong echoes start on the left and bounce", "[timeeffects]")
{
    const auto taps = echoTaps(
        EchoSettings{
            .leftSeconds = 0.1, .rightSeconds = 0.2, .pingPong = true, .feedback = 0.5, .width = 0.5},
        1.0, 0.1);
    REQUIRE(taps.size() == 4); // 1, 0.5, 0.25, 0.125 (the next is under the floor)
    CHECK(taps[0].seconds == Approx(0.1));
    CHECK(taps[1].seconds == Approx(0.3));
    CHECK(taps[2].seconds == Approx(0.4));
    // Width 50 %: three quarters on the near side.
    CHECK(taps[0].left == Approx(0.75));
    CHECK(taps[0].right == Approx(0.25));
    CHECK(taps[1].left == Approx(0.125));
    CHECK(taps[1].right == Approx(0.375));
    CHECK(echoTaps(EchoSettings{.leftSeconds = 0.0}, 1.0).empty());
    CHECK(echoTaps(EchoSettings{.pingPong = true, .feedback = 1.0}, 100.0, 0.001, 8).size() == 8);
}

TEST_CASE("Echo taps are where the delay puts its echoes", "[timeeffects]")
{
    for (const bool pingPong : {false, true}) {
        vloom::dsp::Delay delay;
        delay.setParameter(vloom::dsp::Delay::kMode, pingPong ? 1.0 : 0.0);
        delay.setParameter(vloom::dsp::Delay::kSync, 0.0);
        delay.setParameter(vloom::dsp::Delay::kTime, 100.0);
        delay.setParameter(vloom::dsp::Delay::kStereoOffset, 20.0);
        delay.setParameter(vloom::dsp::Delay::kFeedback, 60.0);
        delay.setParameter(vloom::dsp::Delay::kLowCut, 20.0); // out of the path
        delay.setParameter(vloom::dsp::Delay::kHighCut, 20000.0);
        delay.setParameter(vloom::dsp::Delay::kWidth, 70.0);
        delay.setParameter(vloom::dsp::Delay::kMix, 100.0);
        delay.prepare(kRate, 512);
        const auto frames = static_cast<std::size_t>(kRate);
        std::vector<float> left(frames);
        std::vector<float> right(frames);
        left[0] = 1.0f;
        right[0] = 1.0f;
        run(delay, left, right);

        const auto taps = echoTaps(EchoSettings{.leftSeconds = delay.delaySeconds(false),
                                       .rightSeconds = delay.delaySeconds(true),
                                       .pingPong = pingPong,
                                       .feedback = 0.6,
                                       .width = 0.7},
            0.9);
        REQUIRE(taps.size() >= 4);
        for (const EchoTap& tap : taps) {
            const auto at = static_cast<std::size_t>(std::lround(tap.seconds * kRate));
            CHECK(static_cast<double>(left[at]) == Approx(tap.left).margin(1e-3));
            CHECK(static_cast<double>(right[at]) == Approx(tap.right).margin(1e-3));
        }
    }
}

TEST_CASE("The nearest note is nearest as a ratio", "[timeeffects]")
{
    const std::array<double, 3> beats{0.5, 0.75, 1.0}; // 1/8, 1/8 D, 1/4
    CHECK(nearestNote(0.25, 120.0, beats) == 0);
    CHECK(nearestNote(0.37, 120.0, beats) == 1);
    CHECK(nearestNote(0.6, 120.0, beats) == 2);
    CHECK(nearestNote(10.0, 120.0, beats) == 2);
    CHECK(nearestNote(0.5, 120.0, {}) == 0);
    CHECK(nearestNote(-1.0, 120.0, beats) == 0);
    // The delay's own notes: 375 ms at 120 BPM is a dotted eighth.
    std::vector<double> notes;
    for (int i = 0; i < vloom::dsp::Delay::kNoteCount; ++i)
        notes.push_back(vloom::dsp::Delay::noteBeats(i));
    CHECK(std::string(vloom::dsp::Delay::noteName(nearestNote(0.375, 120.0, notes))) == "1/8D");
}

TEST_CASE("The reverb's reflections and tail follow size and decay", "[timeeffects]")
{
    CHECK(reverbScale(0.0) == Approx(0.3));
    CHECK(reverbScale(100.0) == Approx(2.0));
    const auto small = reverbReflections(0.02, 0.0, 2.0, 0.0);
    const auto large = reverbReflections(0.02, 100.0, 2.0, 0.0);
    REQUIRE(small.size() == 8);
    CHECK(small.front().seconds == Approx(0.02 + 0.02971 * 0.3));
    CHECK(large.back().seconds > small.back().seconds);
    // A longer decay loses less per trip.
    CHECK(reverbReflections(0.0, 50.0, 10.0, 0.0).front().level
        > reverbReflections(0.0, 50.0, 1.0, 0.0).front().level);
    CHECK(reverbReflections(0.0, 50.0, 2.0, 100.0).front().level
        == Approx(0.5 * reverbReflections(0.0, 50.0, 2.0, 0.0).front().level));
    CHECK(reverbTailStart(0.01, 100.0) == Approx(0.01 + 0.02971 * 2.0));

    CHECK(reverbLevel(2.0, 2.0) == Approx(0.001));
    CHECK(reverbLevel(0.0, 2.0) == Approx(1.0));
    CHECK(reverbLevel(5.0, 2.0, true) == Approx(1.0));

    // Damping shortens the highs, never the lows.
    // Even at 0 % the loop's low-pass sits at 20 kHz and takes a little off.
    CHECK(reverbDecayAt(8000.0, 2.0, 0.0, 50.0, kRate) == Approx(1.48).epsilon(0.02));
    CHECK(reverbDecayAt(8000.0, 2.0, 100.0, 50.0, kRate) < 0.5);
    CHECK(reverbDecayAt(100.0, 2.0, 100.0, 50.0, kRate) == Approx(2.0).epsilon(0.02));
    CHECK(reverbDecayAt(8000.0, 2.0, 50.0, 50.0, kRate) < reverbDecayAt(4000.0, 2.0, 50.0, 50.0, kRate));
}

TEST_CASE("LFO displays show round windows of at least two cycles", "[timeeffects]")
{
    CHECK(lfoWindowSeconds(10.0) == Approx(0.2));
    CHECK(lfoWindowSeconds(4.0) == Approx(0.5));
    CHECK(lfoWindowSeconds(0.8) == Approx(4.0));
    CHECK(lfoWindowSeconds(0.01) == Approx(200.0));
    CHECK(lfoWindowSeconds(0.0) == Approx(500.0));
    CHECK(tickStep(4.0, 8) == Approx(0.5));
    CHECK(tickStep(1.0, 4) == Approx(0.5));
    CHECK(tickStep(3.0, 3) == Approx(1.0));
    CHECK(tickStep(0.3, 6) == Approx(0.05));
    CHECK(tickStep(0.0, 4) == Approx(1.0));
}

TEST_CASE("LFO waves are the effects' own", "[timeeffects]")
{
    CHECK(lfoWave(LfoWave::Sine, 0.25) == Approx(1.0));
    CHECK(lfoWave(LfoWave::Triangle, 0.125) == Approx(0.5));
    CHECK(lfoWave(LfoWave::Square, 0.75) == Approx(-1.0).margin(1e-6));
    CHECK(chorusDelayMs(10.0, 0.5, 0, 0.25) == Approx(14.0));
    CHECK(chorusDelayMs(10.0, 0.5, 1, 0.25) == Approx(10.0));
    CHECK(chorusDelayMs(10.0, 0.0, 2, 0.6) == Approx(10.0));
}

TEST_CASE("The flanger's response is the flanger's", "[timeeffects]")
{
    vloom::dsp::Flanger flanger;
    flanger.setParameter(vloom::dsp::Flanger::kDepth, 0.0); // holds the delay
    flanger.setParameter(vloom::dsp::Flanger::kDelay, 2.0);
    flanger.setParameter(vloom::dsp::Flanger::kFeedback, 60.0);
    flanger.setParameter(vloom::dsp::Flanger::kMix, 50.0);
    flanger.prepare(kRate, 512);
    for (const double hz : {150.0, 250.0, 400.0, 900.0, 1500.0})
        CHECK(sineGainDb(flanger, hz) == Approx(flangerResponseDb(hz, 2.0, 0.6, 0.5, kRate)).margin(0.3));
    CHECK(flangerNotchHz(2.0) == Approx(250.0));
    CHECK(flangerResponseDb(250.0, 2.0, 0.0, 0.5, kRate) < -40.0);
}

TEST_CASE("The phaser's response is the phaser's", "[timeeffects]")
{
    for (const int stages : {2, 6}) {
        vloom::dsp::Phaser phaser;
        phaser.setParameter(vloom::dsp::Phaser::kDepth, 0.0); // holds the all-passes at the centre
        phaser.setParameter(vloom::dsp::Phaser::kStages, stages);
        phaser.setParameter(vloom::dsp::Phaser::kFrequency, 800.0);
        phaser.setParameter(vloom::dsp::Phaser::kFeedback, -40.0);
        phaser.setParameter(vloom::dsp::Phaser::kMix, 50.0);
        phaser.prepare(kRate, 512);
        for (const double hz : {100.0, 300.0, 800.0, 2000.0, 6000.0})
            CHECK(sineGainDb(phaser, hz)
                == Approx(phaserResponseDb(hz, 800.0, stages, -0.4, 0.5, kRate)).margin(0.3));
    }
    const auto [low, high] = phaserSweepHz(800.0, 1.0, kRate);
    CHECK(low == Approx(200.0));
    CHECK(high == Approx(3200.0));
    CHECK(phaserSweepHz(100.0, 1.0, kRate).first == Approx(25.0));
    CHECK(phaserSweepHz(8000.0, 1.0, kRate).second == Approx(20000.0));
}

TEST_CASE("Tremolo gains are the tremolo's", "[timeeffects]")
{
    for (const bool autoPan : {false, true}) {
        vloom::dsp::Tremolo tremolo;
        tremolo.setParameter(vloom::dsp::Tremolo::kMode, autoPan ? 1.0 : 0.0);
        tremolo.setParameter(vloom::dsp::Tremolo::kRate, 4.0);
        tremolo.setParameter(vloom::dsp::Tremolo::kShape, 1.0); // triangle
        tremolo.setParameter(vloom::dsp::Tremolo::kDepth, 80.0);
        tremolo.setParameter(vloom::dsp::Tremolo::kStereoPhase, 90.0);
        tremolo.prepare(kRate, 512);
        const auto frames = static_cast<std::size_t>(kRate / 2.0);
        std::vector<float> left(frames, 1.0f);
        std::vector<float> right(frames, 1.0f);
        run(tremolo, left, right);
        for (const std::size_t at :
            {std::size_t{1000}, std::size_t{3000}, std::size_t{7000}, std::size_t{11000}}) {
            const double phase = 4.0 * static_cast<double>(at) / kRate;
            const auto [l, r] = tremoloGains(autoPan, LfoWave::Triangle, 0.8, 0.25, phase);
            CHECK(static_cast<double>(left[at]) == Approx(l).margin(1e-3));
            CHECK(static_cast<double>(right[at]) == Approx(r).margin(1e-3));
        }
    }
    // Auto-pan never boosts.
    const auto [l, r] = tremoloGains(true, LfoWave::Sine, 1.0, 0.0, 0.25);
    CHECK(l == Approx(0.0));
    CHECK(r == Approx(1.0));
}
