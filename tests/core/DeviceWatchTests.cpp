// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/DeviceWatch.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace vloomdaw;
using Action = DeviceWatch::Action;

namespace {
const auto present = [] { return true; };
const auto absent = [] { return false; };
} // namespace

TEST_CASE("A lost device falls back to the default at once", "[device]")
{
    DeviceWatch watch;
    watch.setPreferredIsDefault(false); // the user picked an interface
    watch.opened(true, 0);
    CHECK(watch.tick(true, 100, absent) == Action::None);
    CHECK(watch.tick(false, 116, absent) == Action::OpenDefault); // unplugged
    watch.opened(false, 120);
    CHECK(watch.onFallback());
}

TEST_CASE("On the fallback, the user's device is looked for every 2 s and switched back to", "[device]")
{
    DeviceWatch watch;
    watch.setPreferredIsDefault(false);
    watch.opened(false, 0); // already on the fallback
    int asked = 0;
    const auto count = [&] {
        ++asked;
        return false;
    };
    CHECK(watch.tick(true, 1000, count) == Action::None);
    CHECK(asked == 0); // not polled before 2 s
    CHECK(watch.tick(true, 2000, count) == Action::None);
    CHECK(asked == 1);
    CHECK(watch.tick(true, 2500, count) == Action::None);
    CHECK(asked == 1);
    CHECK(watch.tick(true, 4000, present) == Action::OpenPreferred); // plugged back in
    watch.opened(true, 4010);
    CHECK_FALSE(watch.onFallback());
    CHECK(watch.tick(true, 9000, present) == Action::None); // on it: no polling
}

TEST_CASE("Users of the system default just follow it", "[device]")
{
    DeviceWatch watch; // preferred = default
    watch.opened(true, 0);
    CHECK(watch.tick(false, 10, present) == Action::OpenDefault); // the default device went away
    watch.opened(false, 20); // "fallback" to the default is the preference itself
    CHECK_FALSE(watch.onFallback());
    CHECK(watch.tick(true, 5000, present) == Action::None);
}

TEST_CASE("With no output at all, opening is retried every 2 s", "[device]")
{
    DeviceWatch watch;
    watch.setPreferredIsDefault(false);
    watch.openFailed(0);
    CHECK(watch.tick(false, 1000, absent) == Action::None);
    CHECK(watch.tick(false, 2000, absent) == Action::OpenDefault);
    watch.openFailed(2000);
    CHECK(watch.tick(false, 4000, present) == Action::OpenPreferred); // the user's device first
}
