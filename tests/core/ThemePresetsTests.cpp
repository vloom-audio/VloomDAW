// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/ThemePresets.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <set>
#include <string_view>

using namespace vloomdaw;
using Catch::Approx;

TEST_CASE("Contrast ratio follows WCAG", "[theme]")
{
    CHECK(contrastRatio({0, 0, 0}, {255, 255, 255}) == Approx(21.0));
    CHECK(contrastRatio({255, 255, 255}, {0, 0, 0}) == Approx(21.0)); // order does not matter
    CHECK(contrastRatio({119, 119, 119}, {255, 255, 255}) == Approx(4.48).epsilon(0.01));
    CHECK(contrastRatio({40, 40, 40}, {40, 40, 40}) == Approx(1.0));
}

TEST_CASE("Every palette is readable", "[theme]")
{
    for (const PaletteSpec& p : palettes()) {
        CAPTURE(p.id);
        // Body text: WCAG AAA; secondary text: AA; LCD digits: AA.
        for (Rgb surface : {p.window, p.panel, p.sidebar, p.rail, p.base}) {
            CHECK(contrastRatio(p.text, surface) >= 7.0);
            CHECK(contrastRatio(p.textDim, surface) >= 4.5);
        }
        CHECK(contrastRatio(p.displayText, p.display) >= 4.5);
        // Surfaces step from the rail (darkest) to raised (lightest).
        CHECK(relativeLuminance(p.rail) <= relativeLuminance(p.window));
        CHECK(relativeLuminance(p.window) <= relativeLuminance(p.panel));
        CHECK(relativeLuminance(p.panel) <= relativeLuminance(p.raised));
    }
}

TEST_CASE("Every accent stands out on every palette", "[theme]")
{
    for (const AccentSpec& a : accents())
        for (const PaletteSpec& p : palettes()) {
            CAPTURE(a.id, p.id);
            // WCAG non-text contrast for UI components.
            CHECK(contrastRatio(a.color, p.window) >= 3.0);
            CHECK(contrastRatio(a.color, p.panel) >= 3.0);
        }
}

TEST_CASE("Ids are unique and unknown ids fall back to the default", "[theme]")
{
    std::set<std::string_view> ids;
    for (const PaletteSpec& p : palettes())
        CHECK(ids.insert(p.id).second);
    ids.clear();
    for (const AccentSpec& a : accents())
        CHECK(ids.insert(a.id).second);

    CHECK(paletteById("slate").id == "slate");
    CHECK(paletteById("no-such-theme").id == palettes().front().id);
    CHECK(accentById("").id == accents().front().id);
    CHECK(accentById("violet").id == "violet");
}

TEST_CASE("Hex colours parse and print", "[theme]")
{
    CHECK(parseHex("#ff8a3d") == Rgb{0xff, 0x8a, 0x3d});
    CHECK(parseHex("#FF8A3D") == Rgb{0xff, 0x8a, 0x3d});
    CHECK_FALSE(parseHex("ff8a3d").has_value());
    CHECK_FALSE(parseHex("#ff8a3").has_value());
    CHECK_FALSE(parseHex("#gg0000").has_value());
    CHECK_FALSE(parseHex("").has_value());
    CHECK(toHex(Rgb{0x05, 0xab, 0xff}) == "#05abff");
    CHECK(parseHex(toHex(Rgb{1, 2, 3})) == Rgb{1, 2, 3});
}

TEST_CASE("Default tuning leaves a palette unchanged", "[theme]")
{
    for (const PaletteSpec& p : palettes()) {
        CAPTURE(p.id);
        const ResolvedTheme t = resolveTheme(p, accents().front(), {}, {});
        CHECK(t.window == p.window);
        CHECK(t.panel == p.panel);
        CHECK(t.text == p.text);
        CHECK(t.textDim == p.textDim);
        CHECK(t.displayText == p.displayText);
        CHECK(t.accent == accents().front().color);
        CHECK_FALSE(hasLowContrast(t));
    }
}

TEST_CASE("A full hue turn changes almost nothing", "[theme]")
{
    const PaletteSpec& p = paletteById("slate");
    ThemeAdjustments a;
    a.hue = 180.0;
    const ResolvedTheme half = resolveTheme(p, accents().front(), a, {});
    a.hue = -180.0;
    const ResolvedTheme otherHalf = resolveTheme(p, accents().front(), a, {});
    // +180 and -180 are the same turn (within rounding).
    CHECK(std::abs(half.window.r - otherHalf.window.r) <= 1);
    CHECK(std::abs(half.window.b - otherHalf.window.b) <= 1);
    // And a turn really changes a tinted palette.
    CHECK_FALSE(half.window == p.window);
}

TEST_CASE("Brightness lightens surfaces, saturation 0 makes them grey", "[theme]")
{
    const PaletteSpec& p = paletteById("plum");
    ThemeAdjustments a;
    a.brightness = 0.1;
    CHECK(relativeLuminance(resolveTheme(p, accents().front(), a, {}).window) > relativeLuminance(p.window));
    a = {};
    a.saturation = 0.0;
    const Rgb grey = resolveTheme(p, accents().front(), a, {}).panel;
    CHECK(grey.r == grey.g);
    CHECK(grey.g == grey.b);
}

TEST_CASE("Out-of-range and NaN tuning is clamped", "[theme]")
{
    const PaletteSpec& p = palettes().front();
    ThemeAdjustments wild;
    wild.brightness = 99.0;
    ThemeAdjustments capped;
    capped.brightness = 0.3;
    CHECK(resolveTheme(p, accents().front(), wild, {}).window
        == resolveTheme(p, accents().front(), capped, {}).window);
    ThemeAdjustments nan;
    nan.contrast = std::numeric_limits<double>::quiet_NaN();
    CHECK(resolveTheme(p, accents().front(), nan, {}).window == p.window);
}

TEST_CASE("Overrides replace their role only", "[theme]")
{
    ThemeOverrides o;
    o.selection = Rgb{1, 2, 3};
    o.lcd = Rgb{4, 5, 6};
    const ResolvedTheme t = resolveTheme(palettes().front(), accents().front(), {}, o);
    CHECK(t.accent == Rgb{1, 2, 3});
    CHECK(t.displayText == Rgb{4, 5, 6});
    CHECK(t.text == palettes().front().text);
}

TEST_CASE("Unreadable tuning is flagged", "[theme]")
{
    ThemeAdjustments a;
    a.text = -0.3; // dark text
    a.brightness = 0.3; // on lighter surfaces
    CHECK(hasLowContrast(resolveTheme(palettes().front(), accents().front(), a, {})));
}
