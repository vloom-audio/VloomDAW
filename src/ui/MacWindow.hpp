// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

class QWidget;

namespace vloomdaw {

// macOS-only window tweaks that Qt has no API for (MacWindow.mm).
// Do not call these on other platforms.

/// Moves the window buttons (close / minimise / zoom) of `window` so they sit
/// vertically centred in a bar `barHeight` pixels tall at the top of the
/// window, starting `leftInset` pixels from the left edge.
///
/// It adjusts AppKit's title bar views directly. macOS resets their layout on
/// resize and state changes, so call it again after those. If the view layout
/// is not what it expects, it does nothing.
void placeMacWindowButtons(QWidget* window, int barHeight, int leftInset);

/// Turns off macOS native full screen (its own Space, window buttons hidden)
/// for `window`. The green window button then zooms the window instead.
void disableMacNativeFullScreen(QWidget* window);

/// Auto-hides the menu bar and the Dock while `on` is true, so a window can
/// cover the whole screen and still keep its window buttons.
void setMacMenuBarAndDockHidden(bool on);

} // namespace vloomdaw
