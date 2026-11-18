// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/AudioIoSetup.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace vloomdaw;

TEST_CASE("Default Audio I/O Setup matches Studio matrix layout", "[audio-io]")
{
    const AudioIoConfig def = defaultAudioIoConfig();
    REQUIRE(def.inputs.size() == 3);
    CHECK(def.inputs[0] == AudioIoBus{"IN 0", false, 0, 0});
    CHECK(def.inputs[1] == AudioIoBus{"IN 1", false, 1, 1});
    CHECK(def.inputs[2] == AudioIoBus{"IN 0 + 1", true, 0, 1});
    CHECK(def.activeInputBus == 2);

    REQUIRE(def.outputs.size() == 1);
    CHECK(def.outputs[0] == AudioIoBus{"Main", true, 0, 1});
    CHECK(def.activeOutputBus == 0);
}

TEST_CASE("Audio I/O Setup serialises, deserialises, and clamps invalid values", "[audio-io]")
{
    AudioIoConfig cfg;
    cfg.unifiedDevice = true;
    cfg.inputs = {
        AudioIoBus{"  Vocal Mic  ", false, 3, 7},
        AudioIoBus{"", true, -5, 99},
    };
    cfg.outputs = {
        AudioIoBus{"Headphones", true, 2, 3},
    };
    cfg.activeInputBus = 99;
    cfg.activeOutputBus = -4;

    const std::string text = serializeAudioIoConfig(cfg);
    const AudioIoConfig restored = deserializeAudioIoConfig(text);

    CHECK(restored.unifiedDevice);
    REQUIRE(restored.inputs.size() == 2);
    CHECK(restored.inputs[0] == AudioIoBus{"Vocal Mic", false, 3, 3});
    CHECK(restored.inputs[1] == AudioIoBus{"IN 2", true, 0, 7});
    CHECK(restored.activeInputBus == 1);

    REQUIRE(restored.outputs.size() == 1);
    CHECK(restored.outputs[0] == AudioIoBus{"Headphones", true, 2, 3});
    CHECK(restored.activeOutputBus == 0);

    CHECK(deserializeAudioIoConfig("{broken json") == defaultAudioIoConfig());
    CHECK(deserializeAudioIoConfig("") == defaultAudioIoConfig());

    // Multi-byte UTF-8 bus names (e.g. Thai) truncate on a clean code-point boundary and round-trip.
    AudioIoConfig utf8Cfg = defaultAudioIoConfig();
    utf8Cfg.inputs[0].name = "ไมโครโฟนห้องอัดเสียงหลักช่องสัญญาณที่หนึ่งและสองทดสอบความยาวเกินขีดจำกัด";
    const std::string utf8Json = serializeAudioIoConfig(utf8Cfg);
    const AudioIoConfig utf8Restored = deserializeAudioIoConfig(utf8Json);
    CHECK(!utf8Restored.inputs[0].name.empty());
    CHECK(utf8Restored.inputs[0].name.size() <= 48);
}
