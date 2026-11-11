// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/PatternSelector/PatternSelector.hpp"

#include "ui/Theme.hpp"

#include <QActionGroup>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QToolButton>

#include <algorithm>

namespace vloomdaw {

namespace {

constexpr int kMaxPatterns = 999;

} // namespace

PatternSelector::PatternSelector(QSize iconSize, QWidget* parent)
    : QFrame(parent)
{
    // Styled as a tool group, so it is as tall as the transport buttons.
    setObjectName(QStringLiteral("toolGroup"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(3, 3, 3, 3);
    layout->setSpacing(2);

    // Pattern actions are only ever appended, and only the last one is ever
    // removed (never from inside its own triggered() signal).
    menu_ = new QMenu(this);
    group_ = new QActionGroup(menu_);
    separator_ = menu_->addSeparator();
    menu_->addAction(tr("Add Pattern"), this, &PatternSelector::addPattern);
    deleteAction_ = menu_->addAction(tr("Delete Pattern"), this, &PatternSelector::deleteCurrentPattern);

    button_ = new QToolButton(this);
    button_->setObjectName(QStringLiteral("patternButton"));
    button_->setMenu(menu_);
    button_->setPopupMode(QToolButton::InstantPopup);
    button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button_->setFocusPolicy(Qt::NoFocus);
    button_->setToolTip(tr("Current pattern"));
    // Wide enough for "Pattern 999", so the bar does not shift as numbers grow.
    button_->setMinimumWidth(
        QFontMetrics(button_->font()).horizontalAdvance(tr("Pattern %1").arg(kMaxPatterns)) + 34);
    layout->addWidget(button_);

    const auto addIconButton = [&](const QString& icon, const QString& tip, auto slot) {
        auto* b = new QToolButton(this);
        b->setIcon(Theme::icon(icon)); // themed; fades when disabled
        b->setIconSize(iconSize);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(tip);
        b->setAccessibleName(tip); // icon only: screen readers need a name
        connect(b, &QToolButton::clicked, this, slot);
        layout->addWidget(b);
        return b;
    };
    addIconButton(QStringLiteral("plus"), tr("Add a pattern"), &PatternSelector::addPattern);
    deleteButton_ = addIconButton(
        QStringLiteral("minus"), tr("Delete the current pattern"), &PatternSelector::deleteCurrentPattern);

    appendAction(1);
    sync();
}

void PatternSelector::appendAction(int number)
{
    auto* action = new QAction(tr("Pattern %1").arg(number), group_);
    action->setCheckable(true);
    menu_->insertAction(separator_, action);
    connect(action, &QAction::triggered, this, [this, number] {
        if (number == current_)
            return;
        current_ = number;
        sync();
        emit currentChanged(number);
    });
}

void PatternSelector::setCurrent(int number)
{
    number = std::clamp(number, 1, count_);
    if (number == current_)
        return;
    current_ = number;
    sync();
}

void PatternSelector::addPattern()
{
    if (count_ >= kMaxPatterns)
        return;
    ++count_;
    appendAction(count_);
    current_ = count_;
    sync();
    emit currentChanged(current_);
}

void PatternSelector::deleteCurrentPattern()
{
    if (count_ <= 1)
        return;
    const int deleted = current_;
    // Names are just numbers, so dropping the last action renumbers the rest.
    QAction* last = group_->actions().constLast();
    group_->removeAction(last);
    menu_->removeAction(last);
    last->deleteLater();
    --count_;
    current_ = std::max(1, deleted - 1);
    sync();
    emit patternDeleted(deleted);
    emit currentChanged(current_);
}

void PatternSelector::reset()
{
    // Drop every pattern but the first (deleteLater: never inside a signal
    // of the action being removed).
    const auto actions = group_->actions();
    for (qsizetype i = 1; i < actions.size(); ++i) {
        group_->removeAction(actions[i]);
        menu_->removeAction(actions[i]);
        actions[i]->deleteLater();
    }
    count_ = 1;
    current_ = 1;
    sync();
}

void PatternSelector::sync()
{
    button_->setText(tr("Pattern %1").arg(current_));
    const auto actions = group_->actions();
    if (current_ >= 1 && current_ <= actions.size())
        actions[current_ - 1]->setChecked(true);
    deleteButton_->setEnabled(count_ > 1);
    deleteAction_->setEnabled(count_ > 1);
    deleteAction_->setText(tr("Delete Pattern %1").arg(current_));
}

} // namespace vloomdaw
