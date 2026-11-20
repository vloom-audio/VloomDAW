// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace vloomdaw {

/// How a control's travel (a position, 0 .. 1) spreads over a parameter's
/// range: the knobs and draggable readouts of the built-in plugins' editors.
enum class ParameterScale {
    Linear,
    /// Equal turns multiply the value: frequencies, times, ratios. Needs a
    /// range above zero; any other range is spread linearly.
    Log,
    /// Fine at the low end and coarse at the top (the cube of the position):
    /// times that start at zero, such as a gate's hold.
    Power,
};

/// The scale that suits a parameter with `unit` and this range: Log for
/// "Hz", "ms", "s" and ":1" above zero, Power for "ms" and "s" from zero,
/// Linear otherwise.
[[nodiscard]] ParameterScale defaultScale(std::string_view unit, double minValue, double maxValue) noexcept;

/// Where `value` sits on a control (0 .. 1, kept in range).
[[nodiscard]] double positionOf(
    ParameterScale scale, double minValue, double maxValue, double value) noexcept;

/// The value at `position` (kept in 0 .. 1); `stepped`: rounded to a whole number.
[[nodiscard]] double valueAt(
    ParameterScale scale, double minValue, double maxValue, double position, bool stepped) noexcept;

/// Where zero sits on a control whose range spans it (a gain of -12 .. +24
/// dB, a pan), so its arc can grow from there; nullopt for a range on one
/// side of zero.
[[nodiscard]] std::optional<double> zeroPosition(
    ParameterScale scale, double minValue, double maxValue) noexcept;

/// Pixels of vertical drag that cover a control's whole travel.
inline constexpr double kDragPixels = 200.0;
/// The share of travel one wheel notch moves.
inline constexpr double kWheelStep = 0.02;
/// What Shift does to a drag or a notch.
inline constexpr double kFineFactor = 0.1;

/// The position after dragging `pixels` up (negative: down) from `start`.
[[nodiscard]] double dragPosition(double start, double pixels, bool fine) noexcept;
/// The position after `notches` of the wheel (negative: down) from `start`.
[[nodiscard]] double wheelPosition(double start, double notches, bool fine) noexcept;

/// A stepped value moved by `steps` whole values, kept in range: a choice
/// or a count turned one notch at a time.
[[nodiscard]] double stepValue(double value, int steps, double minValue, double maxValue) noexcept;

/// How a value reads on a built-in editor's control, as in the designs:
/// "-18.0 dB" (with a real minus sign), "+3.0 dB", "4.0 : 1", "10.0 ms",
/// "120 ms", "1.20 s", "80 Hz", "6.00 kHz", "100 %". UTF-8.
[[nodiscard]] std::string formatParameterValue(std::string_view unit, double value);

} // namespace vloomdaw
