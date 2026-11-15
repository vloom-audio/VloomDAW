// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/EqResponse.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

using namespace vloomdaw;
using Catch::Approx;
using Eq = vloom::dsp::Eq;

namespace {

constexpr double kRate = 48000.0;
constexpr double kFrequencies[] = {30.0, 100.0, 400.0, 1000.0, 3000.0, 10000.0, 16000.0};

} // namespace

TEST_CASE("The default equalizer is flat", "[eq]")
{
    const EqResponse response(kRate);
    for (double f : kFrequencies) {
        CHECK(response.totalDb(f) == Approx(0.0).margin(1e-9));
        for (std::size_t s = 0; s < kEqSections; ++s)
            CHECK(response.sectionDb(static_cast<EqSection>(s), f) == Approx(0.0).margin(1e-9));
    }
}

TEST_CASE("A bell reaches its gain at its frequency", "[eq]")
{
    EqResponse response(kRate);
    response.set(Eq::kHighMidFrequency, 3000.0);
    response.set(Eq::kHighMidGain, 5.0);
    CHECK(response.totalDb(3000.0) == Approx(5.0).margin(0.01));
    CHECK(response.sectionDb(EqSection::HighMid, 3000.0) == Approx(5.0).margin(0.01));
    CHECK(response.sectionDb(EqSection::LowMid, 3000.0) == Approx(0.0).margin(1e-9));
    CHECK(response.value(Eq::kHighMidGain) == 5.0);
}

TEST_CASE("The sections add up to the whole equalizer", "[eq]")
{
    EqResponse response(kRate);
    response.set(Eq::kLowCutOn, 1.0);
    response.set(Eq::kLowCutFrequency, 30.0);
    response.set(Eq::kLowShelfGain, 3.0);
    response.set(Eq::kLowMidGain, -4.0);
    response.set(Eq::kLowMidQ, 1.4);
    response.set(Eq::kHighMidGain, 5.0);
    response.set(Eq::kHighShelfGain, 2.0);
    for (double f : kFrequencies) {
        double sum = 0.0;
        for (std::size_t s = 0; s < kEqSections; ++s)
            sum += response.sectionDb(static_cast<EqSection>(s), f);
        CHECK(response.totalDb(f) == Approx(sum).margin(1e-6));
    }
}

TEST_CASE("A cut only shows while it is on", "[eq]")
{
    EqResponse response(kRate);
    response.set(Eq::kLowCutFrequency, 200.0);
    CHECK(response.sectionDb(EqSection::LowCut, 50.0) == Approx(0.0).margin(1e-9));
    response.set(Eq::kLowCutOn, 1.0);
    CHECK(response.sectionDb(EqSection::LowCut, 50.0) < -12.0);
    CHECK(response.sectionDb(EqSection::LowCut, 200.0) == Approx(-3.0).margin(0.1));
}

TEST_CASE("Values are clamped and bad ones ignored", "[eq]")
{
    EqResponse response(kRate);
    response.set(Eq::kLowMidGain, 99.0);
    CHECK(response.value(Eq::kLowMidGain) == Eq::kMaxGainDb);
    response.set(Eq::kLowMidGain, std::numeric_limits<double>::quiet_NaN());
    CHECK(response.value(Eq::kLowMidGain) == Eq::kMaxGainDb);
    response.set(static_cast<std::uint32_t>(Eq::kParameterCount), 1.0); // no such parameter
    CHECK(response.value(static_cast<std::uint32_t>(Eq::kParameterCount)) == 0.0);
}

TEST_CASE("Every parameter belongs to the section that lists it", "[eq]")
{
    for (std::size_t s = 0; s < kEqSections; ++s) {
        const auto section = static_cast<EqSection>(s);
        const EqSectionParameters ids = eqSectionParameters(section);
        CHECK(eqSectionOf(ids.frequency) == section);
        if (ids.gain)
            CHECK(eqSectionOf(*ids.gain) == section);
        if (ids.q)
            CHECK(eqSectionOf(*ids.q) == section);
        if (ids.on)
            CHECK(eqSectionOf(*ids.on) == section);
    }
}
