// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/Names.hpp"

#include <algorithm>
#include <cctype>

namespace vloomdaw {

std::string cleanName(const std::string& name, std::size_t maxBytes)
{
    const auto space = [](unsigned char c) { return std::isspace(c) != 0; };
    const auto first = std::find_if_not(name.begin(), name.end(), space);
    const auto last = std::find_if_not(name.rbegin(), name.rend(), space).base();
    std::string trimmed = first < last ? std::string(first, last) : std::string();
    if (trimmed.size() > maxBytes) {
        std::size_t cut = maxBytes;
        // Back up over UTF-8 continuation bytes (10xxxxxx) to a character start.
        while (cut > 0 && (static_cast<unsigned char>(trimmed[cut]) & 0xC0) == 0x80)
            --cut;
        trimmed.resize(cut);
    }
    return trimmed;
}

} // namespace vloomdaw
