// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QColor>

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
        QColor raised; ///< hovered buttons
        QColor border; ///< separators and outlines
        QColor text; ///< primary text
        QColor textDim; ///< secondary text, captions
        QColor accent; ///< selection, focus, checked toggles
        QColor playing; ///< "transport is running" highlight
        QColor display; ///< LCD display background
        QColor displayText; ///< LCD display digits
        QColor displayUnlit; ///< LCD display unlit "8" segments behind the digits
    };

    /// The default dark palette.
    [[nodiscard]] static const Colors& colors();

    /// Installs the Fusion style, the palette and the style sheet on `app`.
    static void apply(QApplication& app);
};

} // namespace vloomdaw
