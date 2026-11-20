// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/EditorStyle.hpp"

#include "ui/Theme.hpp"

namespace vloomdaw::editor_style {

namespace {

/// `from` moved `amount` (0 .. 1) of the way to `to`.
QColor mix(const QColor& from, const QColor& to, double amount)
{
    const auto channel = [amount](int a, int b) { return static_cast<int>(a + (b - a) * amount + 0.5); };
    return QColor(channel(from.red(), to.red()), channel(from.green(), to.green()),
        channel(from.blue(), to.blue()), channel(from.alpha(), to.alpha()));
}

} // namespace

QColor background() { return Theme::colors().base; }
QColor text() { return Theme::colors().text; }
QColor muted() { return Theme::colors().textDim; }
QColor accent() { return Theme::colors().accent; }
QColor grid() { return mix(Theme::colors().base, Theme::colors().panel, 0.67); }
QColor faint() { return Theme::colors().panel; }
QColor line() { return Theme::colors().border; }
QColor strong() { return mix(Theme::colors().border, Theme::colors().text, 0.09); }

QColor panelFill()
{
    QColor fill = Theme::colors().panel;
    fill.setAlphaF(0.94f);
    return fill;
}

QColor level() { return Theme::patternColor(1); }

QFont font(const QFont& base, int pixels, QFont::Weight weight, bool tabular)
{
    QFont result = base;
    result.setPixelSize(pixels);
    result.setWeight(weight);
    if (tabular)
        result.setFeature(QFont::Tag("tnum"), 1);
    return result;
}

} // namespace vloomdaw::editor_style
