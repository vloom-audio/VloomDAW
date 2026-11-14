// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/MixerAudio.hpp"
#include "core/Decibels.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>

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

namespace {

/// An effect that does nothing; enough to be published.
class NullEffect : public vloom::AudioProcessor {
public:
    [[nodiscard]] vloom::PluginInfo info() const override { return {}; }
    bool activate(double /*sampleRate*/, int /*maxBlockFrames*/) override { return true; }
    void process(const vloom::ProcessContext& /*context*/) noexcept override { }
};

} // namespace

TEST_CASE("An insert's loaded effects reach the engine, in order and bypassed as set", "[audio]")
{
    auto first = std::make_shared<vloom::PluginInstance>(std::make_unique<NullEffect>());
    auto second = std::make_shared<vloom::PluginInstance>(std::make_unique<NullEffect>());
    const PluginResolver plugins = [&](std::uint64_t handle) -> std::shared_ptr<vloom::PluginInstance> {
        if (handle == 1)
            return first;
        if (handle == 2)
            return second;
        return nullptr;
    };
    MixerModel mixer(2);
    PluginSlot slot;
    slot.instance = 2;
    slot.bypassed = true;
    mixer.addEffect(2, slot);
    slot.instance = 9; // not loaded: left out
    slot.bypassed = false;
    mixer.addEffect(2, slot);
    slot.instance = 1;
    mixer.addEffect(2, slot);

    slot.instance = 2;
    mixer.addEffect(MixerModel::kMaster, slot); // the master's chain

    const auto state = engineMixerState(mixer, plugins);
    CHECK(state->inserts[0].effects.empty());
    REQUIRE(state->masterEffects.size() == 1);
    CHECK(state->masterEffects[0] == second);
    REQUIRE(state->inserts[1].effects.size() == 2);
    CHECK(state->inserts[1].effects[0] == second);
    CHECK(state->inserts[1].effects[1] == first);
    CHECK(second->isBypassed());
    CHECK_FALSE(first->isBypassed());

    CHECK(engineMixerState(mixer)->inserts[1].effects.empty()); // no resolver: no effects
}
