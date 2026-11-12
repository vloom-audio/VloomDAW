// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/Decibels.hpp"
#include "core/MixerModel.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace vloomdaw;
using Catch::Approx;

TEST_CASE("A new mixer has a master and named inserts at unity", "[mixer]")
{
    const MixerModel mixer(4);
    REQUIRE(mixer.size() == 5);
    CHECK(mixer.track(MixerModel::kMaster).name == "Master");
    CHECK(mixer.track(1).name == "Insert 1");
    CHECK(mixer.track(4).name == "Insert 4");
    for (std::size_t i = 0; i < mixer.size(); ++i)
        CHECK(mixer.effectiveGain(i) == Approx(1.0));
}

TEST_CASE("Setters clamp and ignore bad input", "[mixer]")
{
    MixerModel mixer(2);
    mixer.setFader(1, 3.0);
    CHECK(mixer.track(1).fader == 1.0);
    mixer.setPan(1, -7.0);
    CHECK(mixer.track(1).pan == -1.0);
    mixer.setPan(1, std::numeric_limits<double>::quiet_NaN());
    CHECK(mixer.track(1).pan == -1.0);
    mixer.setFader(99, 0.1); // out of range: no effect, no throw
    CHECK(mixer.effectiveGain(99) == 0.0);
}

TEST_CASE("Mute silences a track", "[mixer]")
{
    MixerModel mixer(2);
    mixer.setMuted(2, true);
    CHECK(mixer.effectiveGain(2) == 0.0);
    CHECK(mixer.effectiveGain(1) == Approx(1.0));
    mixer.setMuted(MixerModel::kMaster, true);
    CHECK(mixer.effectiveGain(MixerModel::kMaster) == 0.0);
}

TEST_CASE("Solo silences the other inserts but never the master", "[mixer]")
{
    MixerModel mixer(3);
    mixer.setSoloed(2, true);
    CHECK(mixer.anySoloed());
    CHECK(mixer.effectiveGain(1) == 0.0);
    CHECK(mixer.effectiveGain(2) == Approx(1.0));
    CHECK(mixer.effectiveGain(3) == 0.0);
    CHECK(mixer.effectiveGain(MixerModel::kMaster) == Approx(1.0));

    mixer.setSoloed(MixerModel::kMaster, true); // ignored
    CHECK_FALSE(mixer.track(MixerModel::kMaster).soloed);

    mixer.setSoloed(2, false);
    CHECK_FALSE(mixer.anySoloed());
    CHECK(mixer.effectiveGain(1) == Approx(1.0));
}

TEST_CASE("A soloed but muted insert stays silent", "[mixer]")
{
    MixerModel mixer(2);
    mixer.setSoloed(1, true);
    mixer.setMuted(1, true);
    CHECK(mixer.effectiveGain(1) == 0.0);
    CHECK(mixer.effectiveGain(2) == 0.0);
}

TEST_CASE("Fader position sets the gain", "[mixer]")
{
    MixerModel mixer(1);
    mixer.setFader(1, 0.4);
    CHECK(mixer.effectiveGain(1) == Approx(faderToGain(0.4)));
}
