// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <cstdint>
#include <optional>

namespace vloomdaw {

/// Physical memory the current process uses right now (its resident set /
/// working set), in bytes. Empty if the platform does not report it.
[[nodiscard]] std::optional<std::uint64_t> processMemoryBytes();

} // namespace vloomdaw
