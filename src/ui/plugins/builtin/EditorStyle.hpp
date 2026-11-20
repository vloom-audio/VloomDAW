// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QColor>
#include <QFont>

namespace vloomdaw::editor_style {

/// The look the built-in plugins' editors share (the approved designs:
/// window #181818, text #e6e9ee, muted #8b95a3, lines #2a2a2a / #3a3a3a /
/// #4a4a4a, the theme's accent), taken from Theme so other themes work too.
/// Read at paint time: a theme switch shows at the next repaint.

[[nodiscard]] QColor background(); ///< the window and the display (#181818)
[[nodiscard]] QColor text(); ///< #e6e9ee
[[nodiscard]] QColor muted(); ///< labels, captions (#8b95a3)
[[nodiscard]] QColor accent(); ///< the theme's accent
[[nodiscard]] QColor grid(); ///< grid lines on a display (#242424)
[[nodiscard]] QColor faint(); ///< header and footer rules, meter wells (#2a2a2a)
[[nodiscard]] QColor line(); ///< borders of buttons (#3a3a3a)
[[nodiscard]] QColor strong(); ///< the panel's border, a knob's track, a pill that is off (#4a4a4a)
[[nodiscard]] QColor panelFill(); ///< the floating panel, 94 % opaque (rgba(42,42,42,0.94))
[[nodiscard]] QColor level(); ///< level meters (teal, #5eead4)

/// `base` at `pixels` px, `weight` and tabular numbers (values that change
/// keep their width).
[[nodiscard]] QFont font(
    const QFont& base, int pixels, QFont::Weight weight = QFont::Normal, bool tabular = false);

/// Heights and sizes from the designs, in px.
inline constexpr int kHeaderHeight = 40;
inline constexpr int kFooterHeight = 32;
inline constexpr int kPanelHeight = 100;
inline constexpr int kPanelRadius = 12;
inline constexpr int kPanelMargin = 16; ///< the panel to the bottom of the display
inline constexpr int kKnobDial = 48;
inline constexpr int kKnobCell = 72; ///< a knob's width with its label and value

} // namespace vloomdaw::editor_style
