// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/Version.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace vloomdaw {

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool isIdentifierChar(char c)
{
    return isDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
}

bool allDigits(std::string_view s) { return !s.empty() && std::all_of(s.begin(), s.end(), isDigit); }

/// A version number part: digits, no leading zero, fits in 32 bits.
std::optional<unsigned> parseNumber(std::string_view s)
{
    if (!allDigits(s) || (s.size() > 1 && s.front() == '0') || s.size() > 9)
        return std::nullopt;
    unsigned value = 0;
    for (const char c : s)
        value = value * 10 + static_cast<unsigned>(c - '0');
    return value;
}

std::vector<std::string_view> split(std::string_view s, char separator)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = s.find(separator, start);
        parts.push_back(
            s.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos)
            return parts;
        start = end + 1;
    }
}

/// Dot-separated identifiers, each non-empty and [0-9A-Za-z-].
bool validIdentifiers(std::string_view s, bool numericNoLeadingZero)
{
    for (const std::string_view id : split(s, '.')) {
        if (id.empty() || !std::all_of(id.begin(), id.end(), isIdentifierChar))
            return false;
        if (numericNoLeadingZero && allDigits(id) && id.size() > 1 && id.front() == '0')
            return false;
    }
    return true;
}

std::strong_ordering comparePrerelease(std::string_view a, std::string_view b)
{
    // A release is newer than any of its pre-releases.
    if (a.empty() || b.empty())
        return a.empty() <=> b.empty();
    const auto as = split(a, '.');
    const auto bs = split(b, '.');
    for (std::size_t i = 0; i < std::min(as.size(), bs.size()); ++i) {
        const bool an = allDigits(as[i]);
        const bool bn = allDigits(bs[i]);
        if (an && bn) {
            // Numerically: by length first (no leading zeros), then digits.
            if (as[i].size() != bs[i].size())
                return as[i].size() <=> bs[i].size();
            if (const auto c = as[i].compare(bs[i]); c != 0)
                return c <=> 0;
        } else if (an != bn) {
            return an ? std::strong_ordering::less : std::strong_ordering::greater;
        } else if (const auto c = as[i].compare(bs[i]); c != 0) {
            return c <=> 0;
        }
    }
    return as.size() <=> bs.size();
}

} // namespace

std::string Version::toString() const
{
    std::string s = std::to_string(major) + '.' + std::to_string(minor) + '.' + std::to_string(patch);
    if (!prerelease.empty())
        s += '-' + prerelease;
    return s;
}

std::strong_ordering operator<=>(const Version& a, const Version& b)
{
    if (const auto c = a.major <=> b.major; c != 0)
        return c;
    if (const auto c = a.minor <=> b.minor; c != 0)
        return c;
    if (const auto c = a.patch <=> b.patch; c != 0)
        return c;
    return comparePrerelease(a.prerelease, b.prerelease);
}

std::optional<Version> parseVersion(std::string_view text)
{
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V'))
        text.remove_prefix(1);
    if (const std::size_t plus = text.find('+'); plus != std::string_view::npos) {
        if (!validIdentifiers(text.substr(plus + 1), false))
            return std::nullopt;
        text = text.substr(0, plus);
    }
    std::string_view pre;
    if (const std::size_t dash = text.find('-'); dash != std::string_view::npos) {
        pre = text.substr(dash + 1);
        if (!validIdentifiers(pre, true))
            return std::nullopt;
        text = text.substr(0, dash);
    }
    const auto parts = split(text, '.');
    if (parts.size() != 3)
        return std::nullopt;
    const auto major = parseNumber(parts[0]);
    const auto minor = parseNumber(parts[1]);
    const auto patch = parseNumber(parts[2]);
    if (!major || !minor || !patch)
        return std::nullopt;
    return Version{*major, *minor, *patch, std::string(pre)};
}

} // namespace vloomdaw
