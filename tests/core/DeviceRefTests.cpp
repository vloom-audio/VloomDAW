// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/DeviceRef.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace vloomdaw;

namespace {
const std::vector<std::string> kDevices{"Monitor", "Speakers", "Monitor", "Interface"};
}

TEST_CASE("Unique names are found again by name", "[core][device]")
{
    const DeviceRef ref = deviceRefAt(kDevices, 1);
    CHECK(ref == DeviceRef{"Speakers", 1});
    CHECK(findDevice(kDevices, ref) == 1);
}

TEST_CASE("Devices sharing a name keep their own identity", "[core][device]")
{
    const DeviceRef second = deviceRefAt(kDevices, 2);
    CHECK(second == DeviceRef{"Monitor", 2});
    CHECK(findDevice(kDevices, second) == 2);
    CHECK(findDevice(kDevices, deviceRefAt(kDevices, 0)) == 0);
}

TEST_CASE("A device is found after others are plugged in before it", "[core][device]")
{
    const DeviceRef interface = deviceRefAt(kDevices, 3);
    const std::vector<std::string> later{"USB Mic", "Monitor", "Speakers", "Monitor", "Interface"};
    CHECK(findDevice(later, interface) == 4);
}

TEST_CASE("Missing devices and bad indices fall back to the default", "[core][device]")
{
    CHECK(findDevice(kDevices, DeviceRef{"Unplugged", 1}) == -1);
    CHECK(findDevice(kDevices, DeviceRef{"Monitor", 3}) == -1);
    CHECK(deviceRefAt(kDevices, -1).isDefault());
    CHECK(deviceRefAt(kDevices, 99).isDefault());
    CHECK(findDevice(kDevices, DeviceRef{}) == -1);
}
