// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/TapTempo.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using vloomdaw::TapTempo;

TEST_CASE("A single tap gives no tempo yet", "[core][tap]")
{
    TapTempo tap;
    CHECK_FALSE(tap.tap(10.0).has_value());
}

TEST_CASE("Taps half a second apart give 120 BPM", "[core][tap]")
{
    TapTempo tap;
    tap.tap(0.0);
    tap.tap(0.5);
    tap.tap(1.0);
    const auto bpm = tap.tap(1.5);
    REQUIRE(bpm.has_value());
    CHECK(*bpm == Catch::Approx(120.0));
}

TEST_CASE("Uneven taps are averaged", "[core][tap]")
{
    TapTempo tap;
    tap.tap(0.0);
    tap.tap(0.4);
    const auto bpm = tap.tap(1.0); // intervals 0.4 and 0.6 -> 0.5 s
    REQUIRE(bpm.has_value());
    CHECK(*bpm == Catch::Approx(120.0));
}

TEST_CASE("Only the last intervals count", "[core][tap]")
{
    TapTempo tap;
    // A slow start at 60 BPM, then steady taps at 120 BPM.
    double t = 0.0;
    tap.tap(t);
    t += 1.0;
    tap.tap(t);
    std::optional<double> bpm;
    for (std::size_t i = 0; i < TapTempo::kMaxTaps; ++i) {
        t += 0.5;
        bpm = tap.tap(t);
    }
    REQUIRE(bpm.has_value());
    CHECK(*bpm == Catch::Approx(120.0));
}

TEST_CASE("A long pause starts a new series", "[core][tap]")
{
    TapTempo tap;
    tap.tap(0.0);
    tap.tap(0.5);
    CHECK_FALSE(tap.tap(0.5 + TapTempo::kResetSeconds + 0.1).has_value());
}

TEST_CASE("A clock going backwards starts a new series", "[core][tap]")
{
    TapTempo tap;
    tap.tap(5.0);
    CHECK_FALSE(tap.tap(4.0).has_value());
    const auto bpm = tap.tap(5.0);
    REQUIRE(bpm.has_value());
    CHECK(*bpm == Catch::Approx(60.0));
}
