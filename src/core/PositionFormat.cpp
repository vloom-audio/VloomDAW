// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/PositionFormat.hpp"

#include <vloom/engine/time/MusicalTime.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vloomdaw {

std::string formatPosition(double ticks, int beatsPerBar)
{
    const auto bbt = vloom::toBarBeatTick(ticks, beatsPerBar);
    char buffer[32];
    std::snprintf(
        buffer, sizeof(buffer), "%03lld.%d.%03d", static_cast<long long>(bbt.bar), bbt.beat, bbt.tick);
    return buffer;
}

double ticksToSeconds(double ticks, double bpm)
{
    if (bpm <= 0.0)
        return 0.0;
    return ticks / vloom::kPpq * 60.0 / bpm;
}

std::string formatTime(double seconds)
{
    // Truncate to whole milliseconds so 59.9996 s shows 0:59.999, not 0:60.000.
    const auto totalMs = static_cast<long long>(std::floor(std::max(0.0, seconds) * 1000.0));
    const long long minutes = totalMs / 60000;
    const int secs = static_cast<int>((totalMs / 1000) % 60);
    const int ms = static_cast<int>(totalMs % 1000);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%lld:%02d.%03d", minutes, secs, ms);
    return buffer;
}

} // namespace vloomdaw
