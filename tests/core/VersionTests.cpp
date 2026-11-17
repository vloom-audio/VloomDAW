// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/Version.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace vloomdaw;

namespace {
Version v(const char* text)
{
    const auto parsed = parseVersion(text);
    REQUIRE(parsed);
    return *parsed;
}
} // namespace

TEST_CASE("Versions are read as semver", "[version]")
{
    const Version a = v("0.2.0");
    CHECK(a.major == 0);
    CHECK(a.minor == 2);
    CHECK(a.patch == 0);
    CHECK_FALSE(a.isPrerelease());
    CHECK(v("v1.10.3") == Version{1, 10, 3, {}});
    CHECK(v("0.3.0-rc.1").prerelease == "rc.1");
    CHECK(v("0.3.0-rc.1+build.7").toString() == "0.3.0-rc.1"); // build metadata ignored
    CHECK(v("0.2.0-dev.1a2b3c4").isPrerelease());

    for (const char* bad : {"", "v", "1", "1.2", "1.2.3.4", "01.2.3", "1.2.3-", "1.2.3-rc..1", "1.2.3-rc.01",
             "1.2.3+", "1.2.3-rc_1", "a.b.c", " 1.2.3", "1.2.3 ", "1.-2.3", "9999999999.0.0"})
        CHECK_FALSE(parseVersion(bad));
}

TEST_CASE("Versions are ordered by semver precedence", "[version]")
{
    // From semver.org, section 11.
    const std::vector<Version> ordered
        = {v("1.0.0-alpha"), v("1.0.0-alpha.1"), v("1.0.0-alpha.beta"), v("1.0.0-beta"), v("1.0.0-beta.2"),
            v("1.0.0-beta.11"), v("1.0.0-rc.1"), v("1.0.0"), v("1.0.1"), v("1.1.0"), v("1.10.0"), v("2.0.0")};
    for (std::size_t i = 0; i + 1 < ordered.size(); ++i) {
        INFO(ordered[i].toString() << " < " << ordered[i + 1].toString());
        CHECK(ordered[i] < ordered[i + 1]);
        CHECK(ordered[i + 1] > ordered[i]);
    }
    CHECK(v("1.0.0+a") == v("1.0.0+b"));
    CHECK(v("0.9.0") < v("0.10.0"));
}
