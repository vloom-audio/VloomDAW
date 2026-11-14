// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QStringList>

namespace vloomdaw::recent {

/// Most projects File > Open Recent lists.
inline constexpr int kMaxProjects = 10;

/// The recently opened or saved projects, newest first (QSettings).
[[nodiscard]] QStringList projects();
/// Puts `path` first (once).
void add(const QString& path);
void remove(const QString& path);
void clear();

} // namespace vloomdaw::recent
