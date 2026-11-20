// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/EffectPreview.hpp"

#include <vloom/dsp/effects/Bitcrusher.hpp>
#include <vloom/dsp/effects/Filter.hpp>
#include <vloom/dsp/effects/Saturator.hpp>
#include <vloom/dsp/effects/TransientShaper.hpp>
#include <vloom/dsp/effects/Utility.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using namespace vloomdaw;
using Catch::Approx;

namespace {

constexpr double kRate = 48000.0;

/// An effect's default settings, by id.
template <typename Effect> std::vector<double> defaults()
{
    std::vector<double> values;
    for (const auto& spec : Effect::parameters())
        values.push_back(spec.defaultValue);
    return values;
}

float peak(const std::vector<float>& wave)
{
    float most = 0.0f;
    for (const float sample : wave)
        most = std::max(most, std::abs(sample));
    return most;
}

} // namespace

TEST_CASE("The saturator's curve follows its drive, output and mix", "[effectpreview]")
{
    using Saturator = vloom::dsp::Saturator;
    std::vector<double> s = defaults<Saturator>();
    s[Saturator::kDrive] = 0.0;
    CHECK(saturatorCurve(s, 0.5) == Approx(std::tanh(0.5)).epsilon(1e-6));
    CHECK(saturatorCurve(s, -0.5) == Approx(-std::tanh(0.5)).epsilon(1e-6));
    s[Saturator::kDrive] = 20.0; // 10 x
    CHECK(saturatorCurve(s, 0.5) == Approx(std::tanh(5.0)).epsilon(1e-6));
    s[Saturator::kOutput] = 20.0 * std::log10(0.5);
    CHECK(saturatorCurve(s, 0.5) == Approx(0.5 * std::tanh(5.0)).epsilon(1e-6));
    s[Saturator::kMix] = 0.0;
    CHECK(saturatorCurve(s, 0.3) == Approx(0.3));
    s = defaults<Saturator>();
    s[Saturator::kCurve] = 3.0; // Hard
    s[Saturator::kDrive] = 12.0;
    CHECK(saturatorCurve(s, 0.9) == Approx(1.0));
    CHECK(saturatorCurve(s, -0.9) == Approx(-1.0));
}

TEST_CASE("The saturator's preview is the real effect on a sine", "[effectpreview]")
{
    using Saturator = vloom::dsp::Saturator;
    std::vector<double> s = defaults<Saturator>();
    s[Saturator::kDrive] = 0.0;
    s[Saturator::kMix] = 0.0; // the dry sound, lined up
    const WavePreview clean = saturatorPreview(s, kRate);
    REQUIRE(clean.dry.size() == static_cast<std::size_t>(kPreviewCycles * kRate / 100.0));
    REQUIRE(clean.wet.size() == clean.dry.size());
    CHECK(peak(clean.dry) == Approx(0.6).epsilon(1e-3));
    for (std::size_t i = 0; i < clean.dry.size(); i += 37)
        CHECK(clean.wet[i] == Approx(clean.dry[i]).margin(0.01));

    s[Saturator::kMix] = 100.0;
    s[Saturator::kDrive] = 12.0;
    const WavePreview driven = saturatorPreview(s, kRate);
    // tanh(4 x 0.6): flattened well below the 2.4 the drive alone would give.
    CHECK(peak(driven.wet) == Approx(std::tanh(2.4)).epsilon(0.05));
}

TEST_CASE("The bitcrusher's preview holds and rounds as the effect does", "[effectpreview]")
{
    using Bitcrusher = vloom::dsp::Bitcrusher;
    CHECK(crusherCycleFrames(1.0) == 96.0);
    CHECK(crusherCycleFrames(16.0) == 192.0);
    CHECK(crusherCycleFrames(1000.0) == 768.0);

    std::vector<double> s = defaults<Bitcrusher>();
    const WavePreview clean = crusherPreview(s, kRate);
    REQUIRE(clean.dry.size() == static_cast<std::size_t>(kPreviewCycles * 96.0));
    CHECK(clean.wet == clean.dry); // the defaults pass through bit for bit

    s[Bitcrusher::kBits] = 2.0; // steps of 0.5
    s[Bitcrusher::kDownsample] = 8.0;
    const WavePreview crushed = crusherPreview(s, kRate);
    REQUIRE(crushed.wet.size() == static_cast<std::size_t>(kPreviewCycles * 96.0));
    int changes = 0;
    for (std::size_t i = 0; i < crushed.wet.size(); ++i) {
        const double steps = static_cast<double>(crushed.wet[i]) / 0.5;
        CHECK(steps == Approx(std::round(steps)).margin(1e-6));
        if (i > 0 && crushed.wet[i] != crushed.wet[i - 1])
            ++changes;
    }
    // A new sample every 8 frames at most: 30 in 240 frames.
    CHECK(changes > 0);
    CHECK(changes <= 30);
}

TEST_CASE("The transient shaper's preview shapes the hits", "[effectpreview]")
{
    using Shaper = vloom::dsp::TransientShaper;
    std::vector<double> s = defaults<Shaper>();
    const WavePreview flat = transientPreview(s, kRate, 400);
    REQUIRE(flat.dry.size() == 400);
    REQUIRE(flat.wet.size() == 400);
    for (std::size_t i = 0; i < flat.dry.size(); ++i)
        CHECK(flat.wet[i] == Approx(flat.dry[i]).margin(1e-6));
    CHECK(peak(flat.dry) == Approx(0.5).epsilon(0.02));

    s[Shaper::kAttack] = 100.0;
    const WavePreview punchy = transientPreview(s, kRate, 400);
    CHECK(peak(punchy.wet) > 1.5 * peak(punchy.dry));

    s = defaults<Shaper>();
    s[Shaper::kSustain] = -100.0;
    const WavePreview tight = transientPreview(s, kRate, 400);
    // Late in a hit the tail is turned down; its start is not.
    const std::size_t late = 400 / kPreviewHits - 10;
    CHECK(tight.wet[late] < 0.5f * tight.dry[late]);
    CHECK(tight.wet[0] == Approx(tight.dry[0]).epsilon(0.1));
}

TEST_CASE("The utility's matrix is the effect's", "[effectpreview]")
{
    using Utility = vloom::dsp::Utility;
    std::vector<double> s = defaults<Utility>();
    StereoMatrix m = utilityMatrix(s, kRate);
    CHECK(m.ll == Approx(1.0));
    CHECK(m.lr == Approx(0.0).margin(1e-6));
    CHECK(m.rl == Approx(0.0).margin(1e-6));
    CHECK(m.rr == Approx(1.0));

    s[Utility::kSwap] = 1.0;
    s[Utility::kDcBlock] = 1.0; // left out: it would take the steady signal away
    m = utilityMatrix(s, kRate);
    CHECK(m.ll == Approx(0.0).margin(1e-6));
    CHECK(m.lr == Approx(1.0));
    CHECK(m.rl == Approx(1.0));

    s = defaults<Utility>();
    s[Utility::kMono] = 1.0;
    m = utilityMatrix(s, kRate);
    CHECK(m.ll == Approx(0.5));
    CHECK(m.lr == Approx(0.5));
    CHECK(m.rr == Approx(0.5));

    s = defaults<Utility>();
    s[Utility::kInvertLeft] = 1.0;
    s[Utility::kPan] = 100.0; // hard right: the left side is gone
    s[Utility::kGain] = -6.0;
    m = utilityMatrix(s, kRate);
    CHECK(m.ll == Approx(0.0).margin(1e-6));
    CHECK(m.rr == Approx(std::pow(10.0, -6.0 / 20.0)).epsilon(1e-4));
}

TEST_CASE("The filter's response is the effect's magnitude", "[effectpreview]")
{
    using Filter = vloom::dsp::Filter;
    std::vector<double> s = defaults<Filter>(); // low-pass at 1 kHz
    const std::array<double, 3> hz{20.0, 1000.0, 16000.0};
    std::vector<double> db = filterResponseDb(s, kRate, hz);
    REQUIRE(db.size() == 3);
    CHECK(db[0] == Approx(0.0).margin(0.1));
    CHECK(db[1] == Approx(-3.0).margin(0.1));
    CHECK(db[2] < -40.0);

    s[Filter::kType] = 1.0; // high-pass
    s[Filter::kResonance] = 100.0;
    db = filterResponseDb(s, kRate, hz);
    CHECK(db[0] < -40.0);
    CHECK(db[1] > 20.0); // a resonant peak at the cutoff
}

TEST_CASE("The de-esser's band and reduction", "[effectpreview]")
{
    CHECK(deEsserDetectionDb(6000.0, kRate, 6000.0) == Approx(-3.0).margin(0.1));
    CHECK(deEsserDetectionDb(6000.0, kRate, 300.0) < -40.0);
    CHECK(deEsserDetectionDb(6000.0, kRate, 18000.0) > -1.0);

    CHECK(deEsserGainDb(6000.0, true, 0.0, kRate, 12000.0) == 0.0);
    CHECK(deEsserGainDb(6000.0, false, 5.0, kRate, 100.0) == -5.0);
    CHECK(deEsserGainDb(6000.0, true, 6.0, kRate, 100.0) == Approx(0.0).margin(0.1));
    CHECK(deEsserGainDb(6000.0, true, 6.0, kRate, 20000.0) == Approx(-6.0).margin(0.5));
    CHECK(deEsserGainDb(6000.0, true, -3.0, kRate, 20000.0) == 0.0); // never a boost
}
