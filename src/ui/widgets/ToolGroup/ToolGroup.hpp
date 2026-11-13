// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QList>
#include <QSize>

#include <initializer_list>

class QAction;
class QFrame;
class QToolButton;
class QWidget;

namespace vloomdaw {

/// A rounded group of tool buttons for a tool bar, one button per action.
/// The buttons use the actions as their default action, which also makes the
/// actions' shortcuts live. Styled by "QFrame#toolGroup" in Theme.
[[nodiscard]] QFrame* makeToolGroup(QWidget* parent, std::initializer_list<QAction*> actions, QSize iconSize);

/// Puts panel header buttons side by side in one box as tall as a lone
/// panel header button (e.g. "+ Track" and "−"); takes the buttons over.
/// Styled by "QFrame#panelButtonGroup" in Theme.
[[nodiscard]] QFrame* makePanelButtonGroup(const QList<QToolButton*>& buttons);

} // namespace vloomdaw
