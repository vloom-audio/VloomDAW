// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/ToolGroup/ToolGroup.hpp"

#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QToolButton>

namespace vloomdaw {

QFrame* makeToolGroup(QWidget* parent, std::initializer_list<QAction*> actions, QSize iconSize)
{
    auto* group = new QFrame(parent);
    group->setObjectName(QStringLiteral("toolGroup"));
    auto* layout = new QHBoxLayout(group);
    layout->setContentsMargins(3, 3, 3, 3);
    layout->setSpacing(2);
    for (QAction* action : actions) {
        auto* button = new QToolButton(group);
        button->setDefaultAction(action); // also makes the shortcut live
        button->setIconSize(iconSize);
        button->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(button);
    }
    return group;
}

QFrame* makePanelButtonGroup(const QList<QToolButton*>& buttons)
{
    auto* group = new QFrame;
    group->setObjectName(QStringLiteral("panelButtonGroup"));
    auto* layout = new QHBoxLayout(group);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    for (QToolButton* button : buttons) {
        button->setParent(group);
        button->setObjectName(QStringLiteral("panelSegment"));
        button->setProperty("iconOnly", button->toolButtonStyle() == Qt::ToolButtonIconOnly);
        layout->addWidget(button);
    }
    return group;
}

} // namespace vloomdaw
