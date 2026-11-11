// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QSize>

#include <initializer_list>

class QAction;
class QFrame;
class QWidget;

namespace vloomdaw {

/// A rounded group of tool buttons for a tool bar, one button per action.
/// The buttons use the actions as their default action, which also makes the
/// actions' shortcuts live. Styled by "QFrame#toolGroup" in Theme.
[[nodiscard]] QFrame* makeToolGroup(QWidget* parent, std::initializer_list<QAction*> actions, QSize iconSize);

} // namespace vloomdaw
