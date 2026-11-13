// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/TempoDetector.hpp"
#include "audio/TimeStretch.hpp"

#include <vloom/engine/sample/SampleBuffer.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <numbers>
#include <random>

using namespace vloomdaw;
using Catch::Approx;

namespace {

constexpr double kRate = 44100.0;

/// A drum loop: a kick (low thump) on every beat and, with `hats`, a quiet
/// noise burst on every eighth note; `seconds` long at `bpm`.
vloom::SampleBuffer drums(double bpm, double seconds, bool hats = true)
{
    const auto frames = static_cast<std::int64_t>(std::llround(seconds * kRate));
    vloom::SampleBuffer b(2, frames, kRate);
    std::mt19937 noise(7);
    std::uniform_real_distribution<float> white(-1.0f, 1.0f);
    const double eighth = 30.0 / bpm; // seconds
    for (std::int64_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / kRate;
        const auto n = static_cast<long long>(std::floor(t / eighth));
        const double since = t - static_cast<double>(n) * eighth;
        float v = 0.0f;
        if (n % 2 == 0) // on the beat: a kick
            v += static_cast<float>(
                0.9 * std::exp(-since * 25.0) * std::sin(2.0 * std::numbers::pi * 60.0 * since));
        else if (hats) // off the beat: a hat
            v += 0.25f * static_cast<float>(std::exp(-since * 120.0)) * white(noise);
        b.channel(0)[i] = v;
        b.channel(1)[i] = v;
    }
    return b;
}

} // namespace

TEST_CASE("A loop's tempo is found and fitted to its whole bars", "[tempo]")
{
    for (const double bpm : {90.0, 120.0, 128.0, 146.0, 174.0}) {
        CAPTURE(bpm);
        const double seconds = 4 * 4 * 60.0 / bpm; // four bars
        const TempoGuess guess = detectTempo(drums(bpm, seconds));
        CHECK(guess.bpm == Approx(bpm).margin(0.01));
        CHECK(guess.confidence > 0.2);
    }
}

TEST_CASE("A two-bar loop, and one with a tail past its bars, are measured too", "[tempo]")
{
    // Two bars at 146: 3.3 s, the shortest loops come in.
    CHECK(detectTempo(drums(146.0, 2 * 4 * 60.0 / 146.0)).bpm == Approx(146.0).margin(0.01));
    // A loop with a tail (not a whole number of bars): rounded to the whole tempo.
    CHECK(detectTempo(drums(146.0, 4.5)).bpm == Approx(146.0).margin(0.01));
    // Two bars and a short tail: the bars are not fitted to the whole length.
    CHECK(detectTempo(drums(146.0, 2 * 4 * 60.0 / 146.0 + 0.012)).bpm == Approx(146.0).margin(0.01));
}

TEST_CASE("A song's tempo is found without bar fitting", "[tempo]")
{
    // 70 s: too long to be a loop, and not a whole number of bars.
    const TempoGuess guess = detectTempo(drums(128.0, 70.3));
    CHECK(guess.bpm == Approx(128.0).epsilon(0.01));
}

TEST_CASE("Half and double tempos are not mistaken for the beat", "[tempo]")
{
    // 90 BPM with hats on the eighths: 180 would also fit the attacks.
    CHECK(detectTempo(drums(90.0, 4 * 4 * 60.0 / 90.0)).bpm == Approx(90.0).margin(0.01));
    // Kicks only at 160: 80 also repeats, but every other beat would be empty.
    CHECK(detectTempo(drums(160.0, 4 * 4 * 60.0 / 160.0, false)).bpm == Approx(160.0).margin(0.01));
}

TEST_CASE("Silence and very short sounds have no tempo", "[tempo]")
{
    CHECK(detectTempo(vloom::SampleBuffer(1, 44100 * 4, kRate)).bpm == 0.0);
    CHECK(detectTempo(drums(120.0, 0.5)).bpm == 0.0);
    CHECK(detectTempo(vloom::SampleBuffer{}).bpm == 0.0);
}

TEST_CASE("Time-stretching changes the length and keeps the beat's tempo apart", "[tempo]")
{
    // A 146 BPM loop stretched for a 130 BPM song: longer by 146/130, and
    // heard at 130 afterwards; the channels and rate stay.
    const auto loop = drums(146.0, 4 * 4 * 60.0 / 146.0);
    const auto stretched = timeStretch(loop, 146.0 / 130.0);
    REQUIRE(stretched != nullptr);
    CHECK(stretched->channels() == 2);
    CHECK(stretched->sampleRate() == kRate);
    CHECK(stretched->frames() == std::llround(static_cast<double>(loop.frames()) * 146.0 / 130.0));
    CHECK(detectTempo(*stretched).bpm == Approx(130.0).margin(0.01));

    CHECK(timeStretch(loop, 10.0) == nullptr); // out of range
    CHECK(timeStretch(vloom::SampleBuffer{}, 1.5) == nullptr);
}
