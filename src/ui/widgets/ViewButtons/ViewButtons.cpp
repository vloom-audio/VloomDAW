// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/ViewButtons/ViewButtons.hpp"

#include "ui/Theme.hpp"
#include "ui/widgets/ToolGroup/ToolGroup.hpp"

#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>

namespace vloomdaw {

ViewButtons::ViewButtons(QSize iconSize, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("viewButtons"));

    // FL Studio's keys for its windows.
    QAction* playlist
        = makeViewAction(QStringLiteral("playlist"), tr("Playlist"), QKeySequence(Qt::Key_F5), View::Playlist);
    QAction* channelRack = makeViewAction(
        QStringLiteral("channel-rack"), tr("Channel rack"), QKeySequence(Qt::Key_F6), View::ChannelRack);
    QAction* pianoRoll
        = makeViewAction(QStringLiteral("piano-roll"), tr("Piano roll"), QKeySequence(Qt::Key_F7), View::PianoRoll);
    QAction* mixer = makeViewAction(QStringLiteral("mixer"), tr("Mixer"), QKeySequence(Qt::Key_F9), View::Mixer);
    QAction* browser = makeViewAction(
        QStringLiteral("folder"), tr("Browser"), QKeySequence(Qt::ALT | Qt::Key_F8), View::Browser);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(makeToolGroup(this, {playlist, channelRack, pianoRoll, mixer, browser}, iconSize));
}

QAction* ViewButtons::makeViewAction(
    const QString& iconName, const QString& text, const QKeySequence& shortcut, View view)
{
    auto* action = new QAction(Theme::icon(iconName), text, this);
    action->setShortcut(shortcut);
    action->setToolTip(QStringLiteral("%1 (%2)").arg(text, shortcut.toString(QKeySequence::NativeText)));
    // The light shows whether the view is open. MainWindow answers
    // viewRequested() by calling setViewOpen(), so the light matches the
    // panel, not the click.
    action->setCheckable(true);
    connect(action, &QAction::triggered, this, [this, action, view] {
        action->setChecked(!action->isChecked()); // undo the automatic toggle
        emit viewRequested(view);
    });
    actions_.insert(view, action);
    return action;
}

void ViewButtons::setViewOpen(View view, bool open)
{
    if (QAction* action = actions_.value(view))
        action->setChecked(open);
}

} // namespace vloomdaw
