// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <cstddef>
#include <optional>

class QMimeData;

namespace vloomdaw {

/// A plugin dragged from the Plugins panel: its index into the plugin
/// store's list. Drops onto mixer inserts (effects) and the channel rack
/// (instruments) read it back.
[[nodiscard]] QMimeData* pluginMimeData(std::size_t index);
/// The plugin index a drag carries, if it is a plugin drag from this app.
[[nodiscard]] std::optional<std::size_t> droppedPlugin(const QMimeData* data);

} // namespace vloomdaw
