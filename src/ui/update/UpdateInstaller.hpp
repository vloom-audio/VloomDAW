// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QString>

namespace vloomdaw {

/// Puts a downloaded, checked update in place of this app (UpdateService).
///
/// - Windows: runs the Inno Setup installer silently. It waits for this
///   process to end, installs over it (same folder, same per-user or
///   all-users choice as before) and starts VloomDAW again.
/// - macOS: a small script waits for this process to end, copies the app
///   from the disk image over this VloomDAW.app and opens it.
/// - Linux: the AppImage replaces the one this app runs from ($APPIMAGE);
///   a script starts it once this process has ended.
///
/// Elsewhere (a distribution's package, an app folder that is not
/// writable) canInstall() is false: the user downloads it from the page.
namespace UpdateInstaller {

/// Whether this copy of VloomDAW can update itself in place.
[[nodiscard]] bool canInstall();

/// Starts the update; the caller must then quit the app at once (nothing
/// is replaced while it runs). On false nothing started, `error` says why.
[[nodiscard]] bool start(const QString& installer, QString* error);

} // namespace UpdateInstaller

} // namespace vloomdaw
