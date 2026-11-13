// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <vector>

namespace vloomdaw {

/// One group of controls on the transport bar.
struct TransportItem {
    enum class Zone { Left, Centre, Right };

    int width = 0;
    int gapBefore = 0; ///< space before it, from the previous item of the same zone
    Zone zone = Zone::Left;
    /// 0: always on the bar. Otherwise it moves into the overflow menu when
    /// the bar is too narrow; higher ranks move first.
    int overflowRank = 0;
};

/// Where TransportBar puts its groups for one bar width.
struct TransportPlacement {
    std::vector<int> x; ///< left edge per item (meaningless when overflowed)
    std::vector<bool> overflowed; ///< per item: in the overflow menu instead of on the bar
    bool showMoreButton = false;
    int moreButtonX = 0;
};

/// Lays out the transport bar inside [`left`, `right`): left items from the
/// left edge, right items against the right edge, and the centre items (the
/// LCDs) in the middle of the bar when there is room, or between the two
/// sides when not. Items that do not fit move into the overflow menu by
/// rank, and the "more" button (`moreButtonWidth` wide, `zoneGap` from the
/// last right item) takes the right edge. Never overlaps items as long as
/// the rank-0 items fit.
[[nodiscard]] TransportPlacement layoutTransport(
    const std::vector<TransportItem>& items, int left, int right, int zoneGap, int moreButtonWidth);

} // namespace vloomdaw
