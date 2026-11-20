// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/ParameterScale.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vloomdaw {

namespace {

constexpr double kPowerExponent = 3.0;

bool canLog(double minValue, double maxValue) noexcept { return minValue > 0.0 && maxValue > minValue; }

/// `format` with `value`, its "-" made a real minus sign.
std::string printed(const char* format, double value)
{
    char text[48];
    std::snprintf(text, sizeof(text), format, value);
    std::string result = text;
    if (!result.empty() && result.front() == '-')
        result.replace(0, 1, "\xE2\x88\x92"); // U+2212
    return result;
}

/// `value` rounded to `decimals`, so a value that rounds to zero prints
/// "0.0" rather than "-0.0".
double rounded(double value, int decimals) noexcept
{
    const double scale = std::pow(10.0, decimals);
    const double result = std::round(value * scale) / scale;
    return result == 0.0 ? 0.0 : result;
}

} // namespace

ParameterScale defaultScale(std::string_view unit, double minValue, double maxValue) noexcept
{
    const bool time = unit == "ms" || unit == "s";
    if ((time || unit == "Hz" || unit == ":1") && canLog(minValue, maxValue))
        return ParameterScale::Log;
    if (time && minValue == 0.0 && maxValue > 0.0)
        return ParameterScale::Power;
    return ParameterScale::Linear;
}

double positionOf(ParameterScale scale, double minValue, double maxValue, double value) noexcept
{
    if (!(maxValue > minValue) || std::isnan(value))
        return 0.0;
    value = std::clamp(value, minValue, maxValue);
    double position = 0.0;
    switch (scale) {
    case ParameterScale::Log:
        if (canLog(minValue, maxValue)) {
            position = std::log(value / minValue) / std::log(maxValue / minValue);
            break;
        }
        [[fallthrough]];
    case ParameterScale::Linear:
        position = (value - minValue) / (maxValue - minValue);
        break;
    case ParameterScale::Power:
        position = std::cbrt((value - minValue) / (maxValue - minValue));
        break;
    }
    return std::clamp(position, 0.0, 1.0);
}

double valueAt(ParameterScale scale, double minValue, double maxValue, double position, bool stepped) noexcept
{
    if (!(maxValue > minValue))
        return minValue;
    position = std::isnan(position) ? 0.0 : std::clamp(position, 0.0, 1.0);
    double value = minValue;
    switch (scale) {
    case ParameterScale::Log:
        if (canLog(minValue, maxValue)) {
            value = minValue * std::pow(maxValue / minValue, position);
            break;
        }
        [[fallthrough]];
    case ParameterScale::Linear:
        value = minValue + position * (maxValue - minValue);
        break;
    case ParameterScale::Power:
        value = minValue + std::pow(position, kPowerExponent) * (maxValue - minValue);
        break;
    }
    if (stepped)
        value = std::round(value);
    return std::clamp(value, minValue, maxValue);
}

std::optional<double> zeroPosition(ParameterScale scale, double minValue, double maxValue) noexcept
{
    if (!(minValue < 0.0 && maxValue > 0.0))
        return std::nullopt;
    return positionOf(scale, minValue, maxValue, 0.0);
}

double dragPosition(double start, double pixels, bool fine) noexcept
{
    const double moved = pixels / kDragPixels * (fine ? kFineFactor : 1.0);
    return std::clamp(start + moved, 0.0, 1.0);
}

double wheelPosition(double start, double notches, bool fine) noexcept
{
    return std::clamp(start + notches * kWheelStep * (fine ? kFineFactor : 1.0), 0.0, 1.0);
}

double stepValue(double value, int steps, double minValue, double maxValue) noexcept
{
    if (!(maxValue >= minValue))
        return minValue;
    return std::clamp(std::round(value) + steps, minValue, maxValue);
}

std::string formatParameterValue(std::string_view unit, double value)
{
    if (std::isnan(value))
        return "\xE2\x80\x93"; // an en dash: no value
    if (unit == "dB") {
        const double db = rounded(value, 1);
        return printed(db > 0.0 ? "+%.1f dB" : "%.1f dB", db);
    }
    if (unit == ":1")
        return printed(value >= 10.0 ? "%.0f : 1" : "%.1f : 1", rounded(value, 1));
    if (unit == "ms") {
        const double ms = std::abs(value);
        if (ms >= 1000.0)
            return printed("%.2f s", rounded(value / 1000.0, 2));
        if (ms >= 100.0)
            return printed("%.0f ms", rounded(value, 0));
        if (ms >= 1.0)
            return printed("%.1f ms", rounded(value, 1));
        return printed("%.2f ms", rounded(value, 2));
    }
    if (unit == "s")
        return printed(std::abs(value) >= 10.0 ? "%.1f s" : "%.2f s", rounded(value, 2));
    if (unit == "Hz") {
        const double hz = std::abs(value);
        if (hz >= 1000.0)
            return printed("%.2f kHz", rounded(value / 1000.0, 2));
        if (hz >= 10.0)
            return printed("%.0f Hz", rounded(value, 0));
        return printed("%.2f Hz", rounded(value, 2));
    }
    if (unit == "%")
        return printed("%.0f %%", rounded(value, 0));
    const bool whole = std::abs(value - std::round(value)) < 1e-9;
    std::string number = printed(whole ? "%.0f" : "%.2f", rounded(value, whole ? 0 : 2));
    if (!unit.empty()) {
        number += ' ';
        number += unit;
    }
    return number;
}

} // namespace vloomdaw
