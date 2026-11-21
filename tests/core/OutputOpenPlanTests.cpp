// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/OutputOpenPlan.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

using namespace vloomdaw;

namespace {

void checkSteps(const std::vector<OutputOpenStep>& steps, const std::vector<OutputOpenStep>& expected)
{
    REQUIRE(steps.size() == expected.size());
    for (std::size_t i = 0; i < steps.size(); ++i) {
        CAPTURE(i);
        CHECK(steps[i].exclusive == expected[i].exclusive);
        CHECK(steps[i].channels == expected[i].channels);
    }
}

} // namespace

TEST_CASE("Shared mode opens the routed channels, then stereo", "[core][audio]")
{
    checkSteps(outputOpenSteps(false, 2), {{false, 2}});
    checkSteps(outputOpenSteps(false, 6), {{false, 6}, {false, 2}});
}

TEST_CASE("Exclusive mode tries every exclusive open before falling back to shared", "[core][audio]")
{
    checkSteps(outputOpenSteps(true, 2), {{true, 2}, {false, 2}});
    checkSteps(outputOpenSteps(true, 4), {{true, 4}, {true, 2}, {false, 4}, {false, 2}});
}

TEST_CASE("Fewer than two wanted channels still opens stereo", "[core][audio]")
{
    checkSteps(outputOpenSteps(false, 0), {{false, 2}});
    checkSteps(outputOpenSteps(true, 1), {{true, 2}, {false, 2}});
}
