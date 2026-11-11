// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/BrowserFilter.hpp"

#include <catch2/catch_test_macros.hpp>

using vloomdaw::audioFileNameFilters;
using vloomdaw::kAudioFileExtensions;

TEST_CASE("No search shows every audio file type", "[core][browser]")
{
    const auto filters = audioFileNameFilters("");
    REQUIRE(filters.size() == kAudioFileExtensions.size());
    CHECK(filters.front() == "*.[wW][aA][vV]");
}

TEST_CASE("Search terms match anywhere in the name, ignoring case", "[core][browser]")
{
    CHECK(audioFileNameFilters("Kick").front() == "*[kK][iI][cC][kK]*.[wW][aA][vV]");
}

TEST_CASE("Digits and spaces are kept, surrounding spaces trimmed", "[core][browser]")
{
    CHECK(audioFileNameFilters("  808 a ").front() == "*808 [aA]*.[wW][aA][vV]");
}

TEST_CASE("Typed wildcards cannot change the pattern", "[core][browser]")
{
    CHECK(audioFileNameFilters("*[x]?").front() == "*[xX]*.[wW][aA][vV]");
    CHECK(audioFileNameFilters("**").front() == "*.[wW][aA][vV]");
}
