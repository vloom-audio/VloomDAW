// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <Qt>

namespace vloomdaw {

/// Item data role of the browser's models: the file's path for a file,
/// nothing for a folder or a note row.
inline constexpr int kSampleFileRole = Qt::UserRole + 1;
/// The path of a file or a folder; nothing for a note row.
inline constexpr int kItemPathRole = Qt::UserRole + 2;

} // namespace vloomdaw
