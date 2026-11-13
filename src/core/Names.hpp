// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <cstddef>
#include <string>

namespace vloomdaw {

/// A name the user typed (UTF-8), made safe to keep: surrounding white space
/// removed and cut to at most `maxBytes` without splitting a character.
/// Empty when it was blank.
[[nodiscard]] std::string cleanName(const std::string& name, std::size_t maxBytes);

/// Where index `i` ends up when the item at `from` moves to `to` (the items
/// between shift one place towards `from`).
[[nodiscard]] constexpr std::size_t movedIndex(std::size_t i, std::size_t from, std::size_t to)
{
    if (i == from)
        return to;
    if (from < to && i > from && i <= to)
        return i - 1;
    if (to < from && i >= to && i < from)
        return i + 1;
    return i;
}

} // namespace vloomdaw
