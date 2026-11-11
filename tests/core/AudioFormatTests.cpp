// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/AudioFormat.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace vloomdaw;

TEST_CASE("Buffer latency is frames over sample rate", "[core][audio]")
{
    CHECK(bufferLatencyMs(480, 48000) == Catch::Approx(10.0));
    CHECK(bufferLatencyMs(256, 48000) == Catch::Approx(5.333).margin(0.001));
    CHECK(bufferLatencyMs(256, 0) == 0.0);
    CHECK(bufferLatencyMs(0, 48000) == 0.0);
}

TEST_CASE("Saved formats outside the offered lists fall back to defaults", "[core][audio]")
{
    CHECK(validSampleRate(96000) == 96000);
    CHECK(validSampleRate(12345) == kDefaultSampleRate);
    CHECK(validBufferFrames(64) == 64);
    CHECK(validBufferFrames(100) == kDefaultBufferFrames);
}
