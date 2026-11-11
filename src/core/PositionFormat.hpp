// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <string>

namespace vloomdaw {

/// Formats a tick position as "BBB.b.TTT" (bar, beat, tick), e.g. "001.1.000".
std::string formatPosition(double ticks, int beatsPerBar);

/// Converts a tick position to seconds at a constant tempo. Returns 0 for a
/// non-positive tempo. (Exact until the engine gets a tempo map.)
double ticksToSeconds(double ticks, double bpm);

/// Formats seconds as "M:SS.mmm", e.g. "0:02.276" or "12:00.000".
/// Negative input shows as zero; milliseconds are truncated, never rounded up.
std::string formatTime(double seconds);

} // namespace vloomdaw
