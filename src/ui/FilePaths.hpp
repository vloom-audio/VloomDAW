// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QString>

#include <filesystem>

namespace vloomdaw {

/// A file system path as a QString without losing characters: through
/// wide strings on Windows (non-ASCII names), through Qt's file name codec on
/// POSIX, where names are bytes that may not be valid UTF-8 and
/// path::wstring() would throw.
[[nodiscard]] QString toQString(const std::filesystem::path& path);

/// The inverse of toQString().
[[nodiscard]] std::filesystem::path toPath(const QString& path);

} // namespace vloomdaw
