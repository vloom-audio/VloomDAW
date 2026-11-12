// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/ThemePresets.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace vloomdaw {

namespace {

constexpr Rgb hex(std::uint32_t v)
{
    return {
        static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 8), static_cast<std::uint8_t>(v)};
}

// Each palette steps from the darkest surface (rail) to the lightest (raised).
constexpr std::array kPalettes{
    PaletteSpec{"midnight", "Midnight", hex(0x212121), hex(0x181818), hex(0x2a2a2a), hex(0x121212),
        hex(0x191919), hex(0x333333), hex(0x3a3a3a), hex(0xe6e9ee), hex(0x8b95a3), hex(0x141414),
        hex(0x5eead4)},
    PaletteSpec{"graphite", "Graphite", hex(0x2b2d31), hex(0x232428), hex(0x34363c), hex(0x1e1f22),
        hex(0x25272b), hex(0x3d4047), hex(0x474a52), hex(0xeceef2), hex(0xa3abb8), hex(0x1b1c1f),
        hex(0x7dd3fc)},
    PaletteSpec{"deep-black", "Deep Black", hex(0x0b0b0b), hex(0x050505), hex(0x141414), hex(0x000000),
        hex(0x080808), hex(0x1f1f1f), hex(0x262626), hex(0xe6e9ee), hex(0x8b95a3), hex(0x000000),
        hex(0x5eead4)},
    PaletteSpec{"slate", "Slate", hex(0x1b2130), hex(0x151a26), hex(0x222a3b), hex(0x11151f), hex(0x171d2a),
        hex(0x2b3448), hex(0x344057), hex(0xe8ecf4), hex(0x9aa6bd), hex(0x0f131c), hex(0x93c5fd)},
    PaletteSpec{"forest", "Forest", hex(0x1a221d), hex(0x141a16), hex(0x212b25), hex(0x0f1511), hex(0x161d18),
        hex(0x2a362f), hex(0x334239), hex(0xe6efe9), hex(0x98aa9f), hex(0x0d120f), hex(0x86efac)},
    PaletteSpec{"plum", "Plum", hex(0x231b2a), hex(0x1c1522), hex(0x2c2234), hex(0x150f1a), hex(0x1e1724),
        hex(0x372b41), hex(0x42344e), hex(0xefe8f4), hex(0xab9cba), hex(0x130e17), hex(0xf0abfc)},
    PaletteSpec{"ember", "Ember", hex(0x251e1a), hex(0x1d1714), hex(0x2f2621), hex(0x17120f), hex(0x201a16),
        hex(0x3a2f29), hex(0x463831), hex(0xf2ebe6), hex(0xb3a49a), hex(0x15100d), hex(0xfdba74)},
};

constexpr std::array kAccents{
    AccentSpec{"orange", "Orange", hex(0xff8a3d)},
    AccentSpec{"teal", "Teal", hex(0x2dd4bf)},
    AccentSpec{"violet", "Violet", hex(0xa78bfa)},
    AccentSpec{"blue", "Blue", hex(0x60a5fa)},
    AccentSpec{"pink", "Pink", hex(0xf472b6)},
    AccentSpec{"green", "Green", hex(0x4ade80)},
    AccentSpec{"yellow", "Yellow", hex(0xfacc15)},
};

double channel(std::uint8_t c)
{
    const double s = c / 255.0;
    return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
}

} // namespace

std::span<const PaletteSpec> palettes() { return kPalettes; }
std::span<const AccentSpec> accents() { return kAccents; }

const PaletteSpec& paletteById(std::string_view id)
{
    const auto it
        = std::find_if(kPalettes.begin(), kPalettes.end(), [id](const PaletteSpec& p) { return p.id == id; });
    return it != kPalettes.end() ? *it : kPalettes.front();
}

const AccentSpec& accentById(std::string_view id)
{
    const auto it
        = std::find_if(kAccents.begin(), kAccents.end(), [id](const AccentSpec& a) { return a.id == id; });
    return it != kAccents.end() ? *it : kAccents.front();
}

double relativeLuminance(Rgb color)
{
    return 0.2126 * channel(color.r) + 0.7152 * channel(color.g) + 0.0722 * channel(color.b);
}

double contrastRatio(Rgb a, Rgb b)
{
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

std::optional<Rgb> parseHex(std::string_view text)
{
    if (text.size() != 7 || text[0] != '#')
        return std::nullopt;
    std::uint32_t value = 0;
    for (char c : text.substr(1)) {
        int digit = 0;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else
            return std::nullopt;
        value = value * 16 + static_cast<std::uint32_t>(digit);
    }
    return hex(value);
}

std::string toHex(Rgb color)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out = "#";
    for (std::uint8_t c : {color.r, color.g, color.b}) {
        out += kDigits[c >> 4];
        out += kDigits[c & 0x0f];
    }
    return out;
}

// ---------------------------------------------------------------- tuning

namespace {

struct Hsl {
    double h = 0.0; ///< degrees, 0 .. 360
    double s = 0.0; ///< 0 .. 1
    double l = 0.0; ///< 0 .. 1
};

Hsl toHsl(Rgb c)
{
    const double r = c.r / 255.0;
    const double g = c.g / 255.0;
    const double b = c.b / 255.0;
    const double maxc = std::max({r, g, b});
    const double minc = std::min({r, g, b});
    Hsl out;
    out.l = (maxc + minc) / 2.0;
    const double d = maxc - minc;
    if (d <= 0.0)
        return out; // grey: hue and saturation 0
    out.s = d / (1.0 - std::abs(2.0 * out.l - 1.0));
    if (maxc == r)
        out.h = 60.0 * std::fmod((g - b) / d, 6.0);
    else if (maxc == g)
        out.h = 60.0 * ((b - r) / d + 2.0);
    else
        out.h = 60.0 * ((r - g) / d + 4.0);
    if (out.h < 0.0)
        out.h += 360.0;
    return out;
}

Rgb toRgb(Hsl c)
{
    const double h = std::fmod(std::fmod(c.h, 360.0) + 360.0, 360.0);
    const double s = std::clamp(c.s, 0.0, 1.0);
    const double l = std::clamp(c.l, 0.0, 1.0);
    const double chroma = (1.0 - std::abs(2.0 * l - 1.0)) * s;
    const double x = chroma * (1.0 - std::abs(std::fmod(h / 60.0, 2.0) - 1.0));
    const double m = l - chroma / 2.0;
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    switch (static_cast<int>(h / 60.0)) {
    case 0:
        r = chroma;
        g = x;
        break;
    case 1:
        r = x;
        g = chroma;
        break;
    case 2:
        g = chroma;
        b = x;
        break;
    case 3:
        g = x;
        b = chroma;
        break;
    case 4:
        r = x;
        b = chroma;
        break;
    default:
        r = chroma;
        b = x;
        break;
    }
    const auto byte = [m](double v) {
        return static_cast<std::uint8_t>(std::lround(std::clamp(v + m, 0.0, 1.0) * 255.0));
    };
    return {byte(r), byte(g), byte(b)};
}

double finiteOr(double value, double fallback) { return std::isfinite(value) ? value : fallback; }

ThemeAdjustments clamped(ThemeAdjustments a)
{
    a.hue = std::clamp(finiteOr(a.hue, 0.0), -180.0, 180.0);
    a.saturation = std::clamp(finiteOr(a.saturation, 1.0), 0.0, 2.0);
    a.brightness = std::clamp(finiteOr(a.brightness, 0.0), -0.3, 0.3);
    a.contrast = std::clamp(finiteOr(a.contrast, 1.0), 0.5, 2.0);
    a.text = std::clamp(finiteOr(a.text, 0.0), -0.3, 0.3);
    return a;
}

} // namespace

ResolvedTheme resolveTheme(const PaletteSpec& palette, const AccentSpec& accent, ThemeAdjustments adjustments,
    const ThemeOverrides& overrides)
{
    const ThemeAdjustments a = clamped(adjustments);
    const bool identity = a == ThemeAdjustments{};
    const double pivot = toHsl(palette.window).l;

    // Surfaces: hue turn, saturation, contrast around the window shade, then
    // brightness. With the defaults the colours come back unchanged.
    const auto surface = [&](Rgb c) {
        if (identity)
            return c;
        Hsl h = toHsl(c);
        h.h += a.hue;
        h.s *= a.saturation;
        h.l = pivot + (h.l - pivot) * a.contrast + a.brightness;
        return toRgb(h);
    };
    const auto text = [&](Rgb c) {
        if (identity)
            return c;
        Hsl h = toHsl(c);
        h.h += a.hue;
        h.s *= a.saturation;
        h.l += a.text;
        return toRgb(h);
    };

    ResolvedTheme t{};
    t.window = surface(palette.window);
    t.base = surface(palette.base);
    t.panel = surface(palette.panel);
    t.rail = surface(palette.rail);
    t.sidebar = surface(palette.sidebar);
    t.raised = surface(palette.raised);
    t.border = surface(palette.border);
    t.display = surface(palette.display);
    t.text = overrides.text.value_or(text(palette.text));
    t.textDim = text(palette.textDim);
    t.displayText = overrides.lcd.value_or(palette.displayText);
    t.accent = overrides.selection.value_or(accent.color);
    t.playing = overrides.playing.value_or(hex(0x4ade80));
    t.record = overrides.record.value_or(hex(0xe81123));
    return t;
}

ResolvedTheme resolveTheme(const ThemeChoice& choice)
{
    return resolveTheme(
        paletteById(choice.paletteId), accentById(choice.accentId), choice.adjustments, choice.overrides);
}

bool hasLowContrast(const ResolvedTheme& t)
{
    constexpr double kAa = 4.5;
    for (Rgb surface : {t.window, t.panel, t.sidebar})
        if (contrastRatio(t.text, surface) < kAa || contrastRatio(t.textDim, surface) < kAa)
            return true;
    return contrastRatio(t.displayText, t.display) < kAa;
}

} // namespace vloomdaw
