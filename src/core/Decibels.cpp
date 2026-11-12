// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/Decibels.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace vloomdaw {

double gainToDb(double gain)
{
    if (!(gain > kSilentGain)) // also catches NaN
        return -std::numeric_limits<double>::infinity();
    return 20.0 * std::log10(gain);
}

double dbToGain(double db)
{
    if (std::isnan(db) || db == -std::numeric_limits<double>::infinity())
        return 0.0;
    return std::pow(10.0, db / 20.0);
}

std::string formatDb(double gain)
{
    const double db = gainToDb(gain);
    if (std::isinf(db))
        return "-inf dB";
    // Round first, so -0.04 prints as "0.0", not "-0.0".
    const double rounded = std::round(db * 10.0) / 10.0;
    char text[32];
    std::snprintf(text, sizeof(text), rounded > 0.0 ? "+%.1f dB" : "%.1f dB", rounded == 0.0 ? 0.0 : rounded);
    return text;
}

double faderToGain(double position)
{
    if (std::isnan(position))
        return 1.0;
    const double x = std::clamp(position, 0.0, 1.0) / kUnityFaderPosition;
    return x * x * x;
}

double gainToFader(double gain)
{
    if (!(gain > 0.0))
        return 0.0;
    return std::clamp(std::cbrt(gain) * kUnityFaderPosition, 0.0, 1.0);
}

std::pair<double, double> balanceGains(double pan)
{
    if (std::isnan(pan))
        pan = 0.0;
    pan = std::clamp(pan, -1.0, 1.0);
    return {std::min(1.0, 1.0 - pan), std::min(1.0, 1.0 + pan)};
}

} // namespace vloomdaw
