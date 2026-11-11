// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/BrowserFilter.hpp"

#include <cctype>

namespace vloomdaw {

namespace {

/// "Kick" -> "[kK][iI][cC][kK]"; other characters pass through.
std::string caseless(std::string_view text)
{
    std::string out;
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalpha(c) != 0) {
            out += '[';
            out += static_cast<char>(std::tolower(c));
            out += static_cast<char>(std::toupper(c));
            out += ']';
        } else {
            out += ch;
        }
    }
    return out;
}

} // namespace

std::vector<std::string> audioFileNameFilters(std::string_view search)
{
    std::string term;
    for (const char ch : search) {
        if (ch != '*' && ch != '?' && ch != '[' && ch != ']' && ch != '\\')
            term += ch;
    }
    // Trim surrounding spaces.
    const auto first = term.find_first_not_of(' ');
    term = first == std::string::npos ? std::string{}
                                      : term.substr(first, term.find_last_not_of(' ') - first + 1);

    const std::string name = term.empty() ? "*" : "*" + caseless(term) + "*";
    std::vector<std::string> filters;
    filters.reserve(kAudioFileExtensions.size());
    for (const auto ext : kAudioFileExtensions)
        filters.push_back(name + "." + caseless(ext));
    return filters;
}

} // namespace vloomdaw
