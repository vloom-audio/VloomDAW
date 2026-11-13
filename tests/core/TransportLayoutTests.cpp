// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/TransportLayout.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cstddef>
#include <vector>

using vloomdaw::layoutTransport;
using vloomdaw::TransportItem;
using Zone = TransportItem::Zone;

namespace {

constexpr int kGap = 10;
constexpr int kMore = 26;
/// Narrowest bar the always-on items fit: play 170 | tempo 146 + 4 + position
/// 142 | more 26, with two zone gaps between the three.
constexpr int kMinimumWidth = 170 + 292 + 26 + 2 * kGap;

// Roughly the real bar: play 170, patterns 171 | tempo 146, key 108, position
// 142 | meter 116, volume 30, pitch 30, tools 93, views 191.
std::vector<TransportItem> bar()
{
    return {
        {170, 0, Zone::Left, 0}, // play group
        {171, 10, Zone::Left, 1}, // pattern selector
        {146, 0, Zone::Centre, 0}, // tempo
        {108, 4, Zone::Centre, 2}, // key
        {142, 4, Zone::Centre, 0}, // position
        {116, 0, Zone::Right, 7}, // CPU / RAM
        {30, 10, Zone::Right, 3}, // volume
        {30, 2, Zone::Right, 6}, // pitch
        {93, 10, Zone::Right, 5}, // tap / metronome
        {191, 10, Zone::Right, 4}, // views
    };
}

/// Visible items never overlap each other or the more button, and stay inside the bar.
void checkNoOverlap(
    const std::vector<TransportItem>& items, const vloomdaw::TransportPlacement& p, int left, int right)
{
    struct Span {
        int a, b;
    };
    std::vector<Span> spans;
    for (std::size_t i = 0; i < items.size(); ++i)
        if (!p.overflowed[i])
            spans.push_back({p.x[i], p.x[i] + items[i].width});
    if (p.showMoreButton)
        spans.push_back({p.moreButtonX, p.moreButtonX + kMore});
    for (std::size_t i = 0; i < spans.size(); ++i) {
        CHECK(spans[i].a >= left);
        CHECK(spans[i].b <= right);
        for (std::size_t j = i + 1; j < spans.size(); ++j)
            CHECK((spans[i].b <= spans[j].a || spans[j].b <= spans[i].a));
    }
}

} // namespace

TEST_CASE("A wide bar shows everything and centres the displays", "[core][transport]")
{
    const auto items = bar();
    const auto p = layoutTransport(items, 0, 1800, kGap, kMore);
    CHECK_FALSE(p.showMoreButton);
    for (const bool o : p.overflowed)
        CHECK_FALSE(o);
    // The displays (tempo 146 + 4 + key 108 + 4 + position 142 = 404) in the middle.
    CHECK(p.x[2] == (1800 - 404) / 2);
    CHECK(p.x[0] == 0);
    CHECK(p.x[9] + 191 == 1800); // views against the right edge
    checkNoOverlap(items, p, 0, 1800);
}

TEST_CASE("Items move into the menu in rank order as the bar narrows", "[core][transport]")
{
    const auto items = bar();
    // Everything: 170+10+171 | 404 | 116+10+30+2+30+10+93+10+191 = 351 + 404 + 492 + 2 gaps = 1267
    CHECK_FALSE(layoutTransport(items, 0, 1267, kGap, kMore).showMoreButton);

    const auto p = layoutTransport(items, 0, 1266, kGap, kMore);
    REQUIRE(p.showMoreButton);
    CHECK(p.overflowed[5]); // the CPU / RAM meter goes first
    CHECK_FALSE(p.overflowed[7]);

    const auto narrower = layoutTransport(items, 0, 900, kGap, kMore);
    CHECK(narrower.overflowed[5]);
    CHECK(narrower.overflowed[7]); // pitch
    CHECK(narrower.overflowed[8]); // tools
    checkNoOverlap(items, narrower, 0, 900);
}

TEST_CASE("Always-on items never move into the menu", "[core][transport]")
{
    const auto items = bar();
    const auto p = layoutTransport(items, 0, 300, kGap, kMore);
    CHECK_FALSE(p.overflowed[0]); // play
    CHECK_FALSE(p.overflowed[2]); // tempo
    CHECK_FALSE(p.overflowed[4]); // position
    CHECK(p.overflowed[1]);
    CHECK(p.overflowed[3]);
    CHECK(p.showMoreButton);
}

TEST_CASE("Nothing overlaps at any width the always-on items fit", "[core][transport]")
{
    const auto items = bar();
    const int width = GENERATE(range(kMinimumWidth, 1900, 7));
    const int left = GENERATE(0, 80); // e.g. the macOS window buttons
    const auto p = layoutTransport(items, left, left + width, kGap, kMore);
    checkNoOverlap(items, p, left, left + width);
}

TEST_CASE("The displays move off centre rather than overlap a side", "[core][transport]")
{
    const auto items = bar();
    const auto p = layoutTransport(items, 0, 1100, kGap, kMore);
    // Centred would start at (1100 - 404) / 2 = 348, inside the left zone (ends at 351).
    CHECK(p.x[2] >= 351 + kGap);
    checkNoOverlap(items, p, 0, 1100);
}

TEST_CASE("Wider again brings every item back", "[core][transport]")
{
    const auto items = bar();
    const auto narrow = layoutTransport(items, 0, 700, kGap, kMore);
    CHECK(narrow.showMoreButton);
    const auto wide = layoutTransport(items, 0, 1700, kGap, kMore);
    CHECK_FALSE(wide.showMoreButton);
    for (const bool o : wide.overflowed)
        CHECK_FALSE(o);
}
