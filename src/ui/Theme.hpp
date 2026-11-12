// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/ThemePresets.hpp"

#include <QColor>
#include <QIcon>
#include <QString>

#include <cstddef>

class QApplication;

namespace vloomdaw {

/// The application's colour palette and style sheet.
///
/// Every colour used by the UI lives here; widgets pick up their look through
/// object names (`setObjectName`) and the style sheet built by apply(), never
/// by hard-coding colours.
class Theme {
public:
    struct Colors {
        QColor window; ///< main window background
        QColor base; ///< input fields, lists
        QColor panel; ///< tool bars, status bar
        QColor rail; ///< activity bar (icon strip at the window edge)
        QColor sidebar; ///< side panels next to the activity bar
        QColor raised; ///< hovered buttons
        QColor border; ///< separators and outlines
        QColor text; ///< primary text
        QColor textDim; ///< secondary text, captions
        QColor accent; ///< selection, focus, checked toggles
        QColor playing; ///< "transport is running" highlight
        QColor danger; ///< hovered close button, record button and its armed state
        QColor display; ///< LCD display background
        QColor displayText; ///< LCD display digits
        QColor displayUnlit; ///< LCD display unlit "8" segments behind the digits
    };

    /// The colours in use (the default palette until select() is called).
    [[nodiscard]] static const Colors& colors();
    /// Uses the colours of `choice` (palette, accent, tuning, overrides; see
    /// core/ThemePresets). Call apply() afterwards to restyle the app.
    static void select(const ThemeChoice& choice);

    /// Colour of pattern `index` in the playlist and channel rack, from a
    /// small set of hues that read well on the dark background.
    [[nodiscard]] static QColor patternColor(std::size_t index);

    /// The icon `name` from resources/icons ("folder" for folder.svg), drawn
    /// in the current theme's colours: the SVGs are authored in the default
    /// palette and those colours are swapped at paint time (text #e6e9ee,
    /// dim text #8b95a3, accent #ff8a3d, record #e81123). The icon follows
    /// later theme changes by itself; disabled icons are faded.
    [[nodiscard]] static QIcon icon(const QString& name);
    /// Increases on every select(); themed icons re-render when it changes.
    [[nodiscard]] static unsigned generation();

    /// Installs the Fusion style, the palette and the style sheet on `app`.
    /// Safe to call again: every widget is restyled with the current colours.
    static void apply(QApplication& app);
};

} // namespace vloomdaw
