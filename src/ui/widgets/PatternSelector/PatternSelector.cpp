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

    menu_ = new QMenu(this);
    group_ = new QActionGroup(menu_);
    separator_ = menu_->addSeparator();
    menu_->addAction(tr("Add Pattern"), this, &PatternSelector::addRequested);
    deleteAction_ = menu_->addAction(tr("Delete Pattern"), this, &PatternSelector::deleteRequested);

    button_ = new QToolButton(this);
    button_->setObjectName(QStringLiteral("patternButton"));
    button_->setMenu(menu_);
    button_->setPopupMode(QToolButton::InstantPopup);
    button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button_->setFocusPolicy(Qt::NoFocus);
    // Wide enough for "Pattern 999", so the bar does not shift as numbers
    // grow; longer names are elided (the tool tip has them in full).
    button_->setFixedWidth(
        QFontMetrics(button_->font()).horizontalAdvance(tr("Pattern %1").arg(kMaxPatterns)) + 34);
    layout->addWidget(button_);

    const auto addIconButton = [&](const QString& icon, const QString& tip, auto signal) {
        auto* b = new QToolButton(this);
        b->setIcon(Theme::icon(icon)); // themed; fades when disabled
        b->setIconSize(iconSize);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(tip);
        b->setAccessibleName(tip); // icon only: screen readers need a name
        connect(b, &QToolButton::clicked, this, signal);
        layout->addWidget(b);
        return b;
    };
    addIconButton(QStringLiteral("plus"), tr("Add a pattern"), &PatternSelector::addRequested);
    deleteButton_ = addIconButton(
        QStringLiteral("minus"), tr("Delete the current pattern"), &PatternSelector::deleteRequested);

    setPatterns(names_, 0);
}

void PatternSelector::setPatterns(const QStringList& names, int current)
{
    names_ = names.isEmpty() ? QStringList{QStringLiteral("Pattern 1")} : names;
    current_ = std::clamp(current, 0, static_cast<int>(names_.size()) - 1);

    // Rebuilt every time: renames and moves change any row. Old actions go
    // with deleteLater, as this can run inside one's triggered() signal.
    for (QAction* old : group_->actions()) {
        group_->removeAction(old);
        menu_->removeAction(old);
        old->deleteLater();
    }
    for (int i = 0; i < names_.size(); ++i) {
        auto* action = new QAction(names_[i], group_);
        action->setCheckable(true);
        menu_->insertAction(separator_, action);
        connect(action, &QAction::triggered, this, [this, i] {
            if (i != current_)
                emit patternPicked(i);
        });
    }
    sync();
}

void PatternSelector::sync()
{
    const QString name = names_[current_];
    button_->setText(button_->fontMetrics().elidedText(name, Qt::ElideRight, button_->width() - 34));
    button_->setToolTip(tr("Current pattern: %1").arg(name));
    const auto actions = group_->actions();
    if (current_ < actions.size())
        actions[current_]->setChecked(true);
    const bool canDelete = names_.size() > 1;
    deleteButton_->setEnabled(canDelete);
    deleteAction_->setEnabled(canDelete);
    deleteAction_->setText(tr("Delete %1").arg(name));
}

} // namespace vloomdaw
