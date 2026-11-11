// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/ProcessMemory.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using vloomdaw::processMemoryBytes;

TEST_CASE("The running process reports the memory it uses", "[memory]")
{
    const auto bytes = processMemoryBytes();
    REQUIRE(bytes.has_value());
    // More than nothing, less than an absurd 64 GB for a test binary.
    CHECK(*bytes > 1024U * 1024U);
    CHECK(*bytes < 64ULL * 1024U * 1024U * 1024U);
}

TEST_CASE("Touching more memory raises the reported figure", "[memory]")
{
    const auto before = processMemoryBytes();
    REQUIRE(before.has_value());

    // 64 MB written page by page, so the OS has to back it with real memory.
    constexpr std::size_t kSize = 64U * 1024U * 1024U;
    std::vector<unsigned char> block(kSize);
    for (std::size_t i = 0; i < kSize; i += 4096)
        block[i] = static_cast<unsigned char>(i);

    const auto after = processMemoryBytes();
    REQUIRE(after.has_value());
    CHECK(*after > *before + kSize / 2);
    CHECK(block[4096] == static_cast<unsigned char>(4096));
}
