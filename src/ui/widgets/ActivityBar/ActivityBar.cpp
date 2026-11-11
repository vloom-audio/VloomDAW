// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/ActivityBar/ActivityBar.hpp"

#include <QAction>
#include <QToolButton>
#include <QVBoxLayout>

namespace vloomdaw {

namespace {
constexpr int kButtonSize = 36;
constexpr int kIconSize = 20;
} // namespace

ActivityBar::ActivityBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("activityBar"));
    setAttribute(Qt::WA_StyledBackground);
    setFixedWidth(kButtonSize + 12);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 8, 6, 8);
    layout->setSpacing(0);
    top_ = new QVBoxLayout;
    top_->setSpacing(4);
    bottom_ = new QVBoxLayout;
    bottom_->setSpacing(4);
    layout->addLayout(top_);
    layout->addStretch();
    layout->addLayout(bottom_);
}

int ActivityBar::addPage(const QIcon& icon, const QString& toolTip)
{
    const auto index = static_cast<int>(pages_.size());
    auto* button = new QToolButton(this);
    button->setIcon(icon);
    button->setIconSize(QSize(kIconSize, kIconSize));
    button->setFixedSize(kButtonSize, kButtonSize);
    button->setToolTip(toolTip);
    button->setCheckable(true);
    button->setFocusPolicy(Qt::NoFocus);
    connect(button, &QToolButton::clicked, this, [this, index] {
        setCurrent(current_ == index ? -1 : index);
        emit currentChanged(current_);
    });
    pages_.append(button);
    top_->addWidget(button);
    syncButtons();
    return index;
}

void ActivityBar::addBottomAction(QAction* action)
{
    auto* button = new QToolButton(this);
    button->setDefaultAction(action);
    button->setIconSize(QSize(kIconSize, kIconSize));
    button->setFixedSize(kButtonSize, kButtonSize);
    button->setFocusPolicy(Qt::NoFocus);
    bottom_->addWidget(button);
}

void ActivityBar::setCurrent(int index)
{
    current_ = index >= 0 && index < pages_.size() ? index : -1;
    syncButtons();
}

void ActivityBar::syncButtons()
{
    for (qsizetype i = 0; i < pages_.size(); ++i)
        pages_[i]->setChecked(i == current_);
}

} // namespace vloomdaw
