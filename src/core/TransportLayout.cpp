// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/TransportLayout.hpp"

#include <algorithm>
#include <cstddef>

namespace vloomdaw {

namespace {

using Zone = TransportItem::Zone;

/// Width of one zone's visible items with the gaps between them (the first
/// visible item's gap does not count).
int zoneWidth(const std::vector<TransportItem>& items, const std::vector<bool>& overflowed, Zone zone)
{
    int width = 0;
    bool first = true;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].zone != zone || overflowed[i])
            continue;
        width += (first ? 0 : items[i].gapBefore) + items[i].width;
        first = false;
    }
    return width;
}

bool anyVisible(const std::vector<TransportItem>& items, const std::vector<bool>& overflowed, Zone zone)
{
    for (std::size_t i = 0; i < items.size(); ++i)
        if (items[i].zone == zone && !overflowed[i])
            return true;
    return false;
}

} // namespace

TransportPlacement layoutTransport(
    const std::vector<TransportItem>& items, int left, int right, int zoneGap, int moreButtonWidth)
{
    TransportPlacement placement;
    placement.x.assign(items.size(), 0);
    placement.overflowed.assign(items.size(), false);
    const int available = std::max(0, right - left);

    const auto needed = [&] {
        int total = 0;
        int zones = 0;
        for (const Zone zone : {Zone::Left, Zone::Centre, Zone::Right}) {
            if (!anyVisible(items, placement.overflowed, zone))
                continue;
            total += zoneWidth(items, placement.overflowed, zone);
            ++zones;
        }
        const bool more = std::find(placement.overflowed.begin(), placement.overflowed.end(), true)
            != placement.overflowed.end();
        if (more)
            ++zones; // the button counts as a zone at the right end
        total += std::max(0, zones - 1) * zoneGap + (more ? moreButtonWidth : 0);
        return total;
    };

    // Move the highest-ranked items into the menu until the rest fits.
    while (needed() > available) {
        std::size_t pick = items.size();
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (placement.overflowed[i] || items[i].overflowRank == 0)
                continue;
            if (pick == items.size() || items[i].overflowRank > items[pick].overflowRank)
                pick = i;
        }
        if (pick == items.size())
            break; // only always-on items left: they overlap the right edge
        placement.overflowed[pick] = true;
    }
    placement.showMoreButton = std::find(placement.overflowed.begin(), placement.overflowed.end(), true)
        != placement.overflowed.end();

    // Left zone, from the left edge.
    int leftEnd = left;
    {
        bool first = true;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (items[i].zone != Zone::Left || placement.overflowed[i])
                continue;
            leftEnd += first ? 0 : items[i].gapBefore;
            placement.x[i] = leftEnd;
            leftEnd += items[i].width;
            first = false;
        }
    }

    // The more button and the right zone, from the right edge.
    int rightStart = right;
    if (placement.showMoreButton) {
        rightStart -= moreButtonWidth;
        placement.moreButtonX = rightStart;
    }
    {
        const int rightWidth = zoneWidth(items, placement.overflowed, Zone::Right);
        if (rightWidth > 0) {
            if (placement.showMoreButton)
                rightStart -= zoneGap;
            rightStart -= rightWidth;
            int x = rightStart;
            bool first = true;
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (items[i].zone != Zone::Right || placement.overflowed[i])
                    continue;
                x += first ? 0 : items[i].gapBefore;
                placement.x[i] = x;
                x += items[i].width;
                first = false;
            }
        }
    }

    // Centre zone: in the middle of the whole bar if the sides allow it.
    const int centreWidth = zoneWidth(items, placement.overflowed, Zone::Centre);
    if (centreWidth > 0) {
        const int lowest = leftEnd > left ? leftEnd + zoneGap : left;
        const int highest = (rightStart < right ? rightStart - zoneGap : right) - centreWidth;
        const int ideal = left + (available - centreWidth) / 2;
        int x = highest >= lowest ? std::clamp(ideal, lowest, highest) : lowest;
        bool first = true;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (items[i].zone != Zone::Centre || placement.overflowed[i])
                continue;
            x += first ? 0 : items[i].gapBefore;
            placement.x[i] = x;
            x += items[i].width;
            first = false;
        }
    }
    return placement;
}

} // namespace vloomdaw
