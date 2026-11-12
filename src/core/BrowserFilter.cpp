// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/BrowserFilter.hpp"

#include <algorithm>
#include <cstddef>

namespace vloomdaw {

namespace {

/// ASCII-only folding: file names in other scripts compare as they are.
constexpr char lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool equalsIgnoringCase(std::string_view a, std::string_view b) noexcept
{
    return a.size() == b.size()
        && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return lower(x) == lower(y); });
}

constexpr bool isWildcard(char c) noexcept
{
    return c == '*' || c == '?' || c == '[' || c == ']' || c == '\\';
}

/// Does `text` contain the search term, wildcards skipped, ignoring case?
bool containsTerm(std::string_view text, std::string_view term) noexcept
{
    // The term, wildcards skipped, must appear in one piece somewhere in `text`.
    std::size_t length = 0;
    for (const char c : term)
        length += isWildcard(c) ? 0 : 1;
    if (length == 0)
        return true;
    if (length > text.size())
        return false;
    for (std::size_t start = 0; start + length <= text.size(); ++start) {
        std::size_t t = start;
        bool all = true;
        for (const char c : term) {
            if (isWildcard(c))
                continue;
            if (lower(text[t]) != lower(c)) {
                all = false;
                break;
            }
            ++t;
        }
        if (all)
            return true;
    }
    return false;
}

} // namespace

bool isAudioFile(std::string_view fileName) noexcept
{
    const auto dot = fileName.rfind('.');
    if (dot == std::string_view::npos || dot == 0)
        return false;
    const std::string_view extension = fileName.substr(dot + 1);
    return std::any_of(kAudioFileExtensions.begin(), kAudioFileExtensions.end(),
        [extension](std::string_view known) { return equalsIgnoringCase(extension, known); });
}

bool matchesSearch(std::string_view fileName, std::string_view search) noexcept
{
    const auto first = search.find_first_not_of(' ');
    if (first == std::string_view::npos)
        return true;
    const std::string_view term = search.substr(first, search.find_last_not_of(' ') - first + 1);
    const auto dot = fileName.rfind('.');
    const std::string_view stem = dot == std::string_view::npos ? fileName : fileName.substr(0, dot);
    return containsTerm(stem, term);
}

} // namespace vloomdaw
