// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <array>
#include <cstddef>
#include <optional>

namespace vloomdaw {

/// Turns a series of taps into a tempo: the average of the last few intervals.
///
/// A pause longer than kResetSeconds (or a clock going backwards) starts a new
/// series, so the user can simply tap again after a break.
class TapTempo {
public:
    static constexpr double kResetSeconds = 2.0;
    static constexpr std::size_t kMaxTaps = 5; ///< averages up to 4 intervals

    /// Registers a tap at `seconds` (any monotonic clock). Returns the tempo
    /// in BPM once there are at least two taps in the current series.
    std::optional<double> tap(double seconds);

    void reset() noexcept { count_ = 0; }

private:
    std::array<double, kMaxTaps> taps_{}; ///< oldest first
    std::size_t count_ = 0;
};

} // namespace vloomdaw
