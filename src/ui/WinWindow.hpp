// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QPoint>
#include <QtGlobal>

#include <functional>

class QWidget;

namespace vloomdaw {

// Windows-only window tweaks that Qt has no API for (WinWindow.cpp).
// Do not call these on other platforms.

/// Removes the native title bar of `window` while keeping the resize border,
/// Aero Snap, the minimize / maximize animations and the taskbar behaviour
/// that a plain Qt::FramelessWindowHint window loses.
///
/// Call it once, before the window is shown, and route the window's
/// nativeEvent() through handleWindowsTitleBarMessage().
void removeWindowsTitleBar(QWidget* window);

/// Answers the non-client messages for a window set up with
/// removeWindowsTitleBar(): the whole window becomes client area, its edges
/// resize it, and points where `isCaption` (window-local, logical pixels)
/// returns true move it like a title bar. Returns true when it handled
/// `message` and set `*result`.
bool handleWindowsTitleBarMessage(
    QWidget* window, void* message, qintptr* result, const std::function<bool(QPoint)>& isCaption);

} // namespace vloomdaw
