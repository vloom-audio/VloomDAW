// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <compare>
#include <optional>
#include <string>
#include <string_view>

namespace vloomdaw {

/// A semantic version (semver.org 2.0): 0.2.0, 0.3.0-rc.1. Build metadata
/// ("+...") is read and ignored, as semver says.
struct Version {
    unsigned major = 0;
    unsigned minor = 0;
    unsigned patch = 0;
    std::string prerelease; ///< "rc.1" in 0.3.0-rc.1; empty for a release

    [[nodiscard]] bool isPrerelease() const { return !prerelease.empty(); }
    [[nodiscard]] std::string toString() const;

    /// Semver precedence: 0.3.0-rc.1 < 0.3.0-rc.2 < 0.3.0 < 0.3.1.
    friend std::strong_ordering operator<=>(const Version& a, const Version& b);
    friend bool operator==(const Version& a, const Version& b) { return (a <=> b) == 0; }
};

/// Reads "0.2.0", "v0.2.0" or "0.3.0-rc.1+build.5". Nothing for anything
/// else: missing parts, leading zeros, empty or invalid identifiers.
[[nodiscard]] std::optional<Version> parseVersion(std::string_view text);

} // namespace vloomdaw
