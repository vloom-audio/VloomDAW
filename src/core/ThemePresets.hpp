// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace vloomdaw {

/// An sRGB colour without Qt, so the theme data can be tested in core.
struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    friend bool operator==(const Rgb&, const Rgb&) = default;
};

/// The surfaces and text colours of one theme (see ui/Theme for where each
/// is used). All presets are dark: the SVG icons are drawn for dark
/// backgrounds.
struct PaletteSpec {
    std::string_view id; ///< stored in the settings, e.g. "midnight"
    std::string_view name; ///< shown to the user, e.g. "Midnight"
    Rgb window;
    Rgb base;
    Rgb panel;
    Rgb rail;
    Rgb sidebar;
    Rgb raised;
    Rgb border;
    Rgb text;
    Rgb textDim;
    Rgb display;
    Rgb displayText;
};

struct AccentSpec {
    std::string_view id; ///< e.g. "orange"
    std::string_view name; ///< e.g. "Orange"
    Rgb color;
};

/// Every palette; the first is the default.
[[nodiscard]] std::span<const PaletteSpec> palettes();
/// Every accent colour; the first is the default.
[[nodiscard]] std::span<const AccentSpec> accents();

/// The palette / accent with `id`, or the default for an unknown id (an old
/// or hand-edited setting must not break the app).
[[nodiscard]] const PaletteSpec& paletteById(std::string_view id);
[[nodiscard]] const AccentSpec& accentById(std::string_view id);

/// WCAG 2 relative luminance (0 = black, 1 = white).
[[nodiscard]] double relativeLuminance(Rgb color);
/// WCAG 2 contrast ratio between two colours, 1 .. 21.
[[nodiscard]] double contrastRatio(Rgb a, Rgb b);

/// Parses "#rrggbb" (case-insensitive); empty for anything else.
[[nodiscard]] std::optional<Rgb> parseHex(std::string_view text);
/// "#rrggbb", lower case.
[[nodiscard]] std::string toHex(Rgb color);

// ---------------------------------------------------------------- tuning

/// User tuning on top of a palette, like the knobs of a theme editor. The
/// defaults change nothing.
struct ThemeAdjustments {
    double hue = 0.0; ///< degrees added to every surface's hue, -180 .. 180
    double saturation = 1.0; ///< surface saturation factor, 0 .. 2
    double brightness = 0.0; ///< added to surface lightness, -0.3 .. 0.3
    double contrast = 1.0; ///< spreads surfaces away from the window shade, 0.5 .. 2
    double text = 0.0; ///< added to text lightness, -0.3 .. 0.3

    friend bool operator==(const ThemeAdjustments&, const ThemeAdjustments&) = default;
};

/// Colours the user picked for one role, replacing the palette's / accent's.
struct ThemeOverrides {
    std::optional<Rgb> selection; ///< accent: selections, lit buttons, knobs
    std::optional<Rgb> playing; ///< play button, playheads, "running" states
    std::optional<Rgb> record; ///< record button, close hover, clip lights
    std::optional<Rgb> lcd; ///< LCD digits
    std::optional<Rgb> text; ///< body text (after the Text adjustment)
};

/// Every colour the UI uses, after tuning and overrides.
struct ResolvedTheme {
    Rgb window, base, panel, rail, sidebar, raised, border;
    Rgb text, textDim;
    Rgb display, displayText;
    Rgb accent, playing, record;
};

/// Everything the user chose for the theme; what the settings store.
struct ThemeChoice {
    std::string paletteId = "midnight";
    std::string accentId = "orange";
    ThemeAdjustments adjustments;
    ThemeOverrides overrides;
};

/// Applies `adjustments` to `palette`'s surfaces and text, then the overrides.
/// Values out of range are clamped; NaN counts as the default.
[[nodiscard]] ResolvedTheme resolveTheme(const PaletteSpec& palette, const AccentSpec& accent,
    ThemeAdjustments adjustments, const ThemeOverrides& overrides);

/// True when body or secondary text, or the LCD digits, fall below WCAG AA
/// (4.5 : 1) on their background: worth a warning in the theme editor.
/// resolveTheme() for a stored choice (unknown ids fall back to the defaults).
[[nodiscard]] ResolvedTheme resolveTheme(const ThemeChoice& choice);

[[nodiscard]] bool hasLowContrast(const ResolvedTheme& theme);

} // namespace vloomdaw
