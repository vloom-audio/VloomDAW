// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/TapTempo.hpp"

#include <algorithm>

namespace vloomdaw {

std::optional<double> TapTempo::tap(double seconds)
{
    if (count_ > 0) {
        const double gap = seconds - taps_[count_ - 1];
        if (gap <= 0.0 || gap > kResetSeconds)
            reset();
    }

    if (count_ == kMaxTaps) {
        std::shift_left(taps_.begin(), taps_.end(), 1);
        --count_;
    }
    taps_[count_++] = seconds;

    if (count_ < 2)
        return std::nullopt;
    const double averageInterval = (taps_[count_ - 1] - taps_[0]) / static_cast<double>(count_ - 1);
    return 60.0 / averageInterval;
}

} // namespace vloomdaw
