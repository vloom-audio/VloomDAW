// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/MixerAudio.hpp"
#include "core/Decibels.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace vloomdaw;
using Catch::Approx;

TEST_CASE("The engine gets each insert's gain with mute and solo, and its pan", "[audio]")
{
    MixerModel mixer(4);
    mixer.setFader(1, 0.5);
    mixer.setPan(1, 0.25);
    mixer.setMuted(2, true);
    auto state = engineMixerState(mixer);
    REQUIRE(state->inserts.size() == 4); // inserts 1..4, not the master
    CHECK(state->inserts[0].gain == Approx(faderToGain(0.5)));
    CHECK(state->inserts[0].pan == Approx(0.25f));
    CHECK(state->inserts[1].gain == 0.0f); // muted
    CHECK(state->inserts[2].gain == Approx(1.0f)); // unity at the default fader

    mixer.setSoloed(3, true); // solo silences the other inserts
    state = engineMixerState(mixer);
    CHECK(state->inserts[0].gain == 0.0f);
    CHECK(state->inserts[2].gain == Approx(1.0f));
    CHECK(state->inserts[3].gain == 0.0f);
}
