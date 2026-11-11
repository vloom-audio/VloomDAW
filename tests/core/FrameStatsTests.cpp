// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/FrameStats.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace vloomdaw;
using Catch::Approx;

TEST_CASE("No samples give zeros", "[framestats]")
{
    FrameStats stats;
    const auto s = stats.snapshot(10.0);
    CHECK(s.fps == 0.0);
    CHECK(s.paintAvgMs == 0.0);
    CHECK(s.tickMaxMs == 0.0);
}

TEST_CASE("FPS counts the frames of the last second", "[framestats]")
{
    FrameStats stats;
    for (int i = 0; i < 120; ++i) // 60 fps for two seconds
        stats.addFrame(i / 60.0, 2.0);
    const auto s = stats.snapshot(119 / 60.0);
    CHECK(s.fps == Approx(60.0).margin(1.0));
    CHECK(s.paintAvgMs == Approx(2.0));
}

TEST_CASE("Paint time average and maximum", "[framestats]")
{
    FrameStats stats;
    stats.addFrame(0.1, 1.0);
    stats.addFrame(0.2, 3.0);
    stats.addFrame(0.3, 8.0);
    const auto s = stats.snapshot(0.5);
    CHECK(s.paintAvgMs == Approx(4.0));
    CHECK(s.paintMaxMs == 8.0);
}

TEST_CASE("Old frames fall out of the window", "[framestats]")
{
    FrameStats stats;
    stats.addFrame(0.0, 50.0);
    stats.addFrame(1.5, 1.0);
    const auto s = stats.snapshot(2.0);
    CHECK(s.fps == 1.0);
    CHECK(s.paintMaxMs == 1.0);
}

TEST_CASE("Tick spacing shows a late tick", "[framestats]")
{
    FrameStats stats;
    stats.addTick(0.000);
    stats.addTick(0.033);
    stats.addTick(0.066);
    stats.addTick(0.166); // 100 ms late tick: the UI thread was busy
    const auto s = stats.snapshot(0.2);
    CHECK(s.tickMaxMs == Approx(100.0));
    CHECK(s.tickAvgMs == Approx(166.0 / 3.0));
}

TEST_CASE("Headroom estimates the reachable frame rate", "[framestats]")
{
    FrameStats stats;
    for (int i = 0; i < 10; ++i) {
        stats.addTick(i * 0.016, 0.5); // 0.5 ms of work per tick
        stats.addFrame(i * 0.016 + 0.001, 1.5); // 1.5 ms of paint per frame
    }
    const auto s = stats.snapshot(0.2);
    CHECK(s.workAvgMs == Approx(0.5));
    CHECK(s.headroomFps == Approx(500.0)); // 1000 / (1.5 + 0.5)
}

TEST_CASE("No frames means no headroom figure", "[framestats]")
{
    FrameStats stats;
    stats.addTick(0.0, 1.0);
    CHECK(stats.snapshot(0.5).headroomFps == 0.0);
}

TEST_CASE("NaN and negative inputs are ignored or clamped", "[framestats]")
{
    FrameStats stats;
    stats.addFrame(std::numeric_limits<double>::quiet_NaN(), 1.0);
    stats.addFrame(0.5, std::numeric_limits<double>::quiet_NaN());
    stats.addFrame(0.6, -5.0);
    stats.addTick(std::numeric_limits<double>::quiet_NaN());
    const auto s = stats.snapshot(1.0);
    CHECK(s.fps == 1.0);
    CHECK(s.paintMaxMs == 0.0);
}
