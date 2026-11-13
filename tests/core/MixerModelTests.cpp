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

TEST_CASE("Inserts can be added up to the engine's limit", "[mixer]")
{
    MixerModel mixer(2);
    CHECK(mixer.insertCount() == 2);
    CHECK(mixer.addInsert() == std::optional<std::size_t>(3));
    CHECK(mixer.track(3).name == "Insert 3");
    while (mixer.addInsert()) { }
    CHECK(mixer.insertCount() == MixerModel::kMaxInserts);
}

TEST_CASE("Removing an insert renumbers the ones after it", "[mixer]")
{
    MixerModel mixer(3);
    mixer.setFader(3, 0.25);
    mixer.setSoloed(2, true);
    REQUIRE(mixer.removeInsert(2));
    CHECK(mixer.insertCount() == 2);
    CHECK(mixer.track(2).name == "Insert 2");
    CHECK(mixer.track(2).fader == Approx(0.25)); // the old Insert 3, settings kept
    CHECK_FALSE(mixer.anySoloed()); // the soloed one is gone

    CHECK_FALSE(mixer.removeInsert(MixerModel::kMaster));
    CHECK_FALSE(mixer.removeInsert(9));
    CHECK(mixer.removeInsert(1));
    CHECK(mixer.removeInsert(1));
    CHECK(mixer.insertCount() == 0);
}

TEST_CASE("Inserts can be named, and a blank name gives the number back", "[mixer]")
{
    MixerModel mixer(3);
    mixer.setName(2, "  Drums  ");
    CHECK(mixer.track(2).name == "Drums");
    CHECK(mixer.track(2).named);
    mixer.setName(MixerModel::kMaster, "Main"); // ignored
    CHECK(mixer.track(MixerModel::kMaster).name == "Master");
    mixer.setName(9, "x"); // out of range: ignored

    SECTION("a named insert keeps its name when an earlier one goes")
    {
        mixer.setName(3, "Bass");
        mixer.removeInsert(1);
        CHECK(mixer.track(1).name == "Drums");
        CHECK(mixer.track(2).name == "Bass");
    }
    SECTION("an unnamed one follows its number")
    {
        mixer.removeInsert(1);
        CHECK(mixer.track(2).name == "Insert 2");
    }
    SECTION("blank")
    {
        mixer.setName(2, " \t ");
        CHECK(mixer.track(2).name == "Insert 2");
        CHECK_FALSE(mixer.track(2).named);
    }
    SECTION("too long: cut at a character boundary")
    {
        std::string name(63, 'a');
        name += "\xE0\xB8\x81"; // a 3-byte character across the limit
        mixer.setName(1, name);
        CHECK(mixer.track(1).name == std::string(63, 'a'));
    }
}

TEST_CASE("Moving an insert takes its settings and name along", "[mixer]")
{
    MixerModel mixer(4);
    mixer.setFader(1, 0.3);
    mixer.setName(2, "Drums");
    CHECK(mixer.moveInsert(1, 3) == 3);
    CHECK(mixer.track(3).fader == Approx(0.3));
    CHECK(mixer.track(3).name == "Insert 3"); // unnamed: its new number
    CHECK(mixer.track(1).name == "Drums"); // was 2, moved down one
    CHECK(mixer.moveInsert(4, 99) == 4); // clamped to the last
    CHECK(mixer.moveInsert(2, 0) == 1); // never before the master
    CHECK(mixer.moveInsert(MixerModel::kMaster, 2) == MixerModel::kMaster);
    CHECK(mixer.track(MixerModel::kMaster).name == "Master");
}

TEST_CASE("A mixer track keeps its colour when inserts move", "[mixer]")
{
    MixerModel mixer(3);
    mixer.setColor(1, 0xa78bfau);
    mixer.setColor(0, 0x112233u); // the master too
    CHECK(mixer.track(0).color == 0x112233u);
    mixer.moveInsert(1, 3);
    CHECK_FALSE(mixer.track(1).color);
    CHECK(mixer.track(3).color == 0xa78bfau);
    mixer.setColor(3, std::nullopt);
    CHECK_FALSE(mixer.track(3).color);
    mixer.setColor(99, 0x111111u); // out of range: ignored
}
