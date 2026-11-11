// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/ActivityBar/ActivityBar.hpp"

#include <QAction>
#include <QEvent>
#include <QLabel>
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

    // Name of the hovered icon, shown at once beside it (a tool tip would wait
    // and appear under the cursor, over the panel being opened).
    hoverLabel_ = new QLabel(this, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    hoverLabel_->setObjectName(QStringLiteral("activityHint"));
    hoverLabel_->setAttribute(Qt::WA_ShowWithoutActivating);
    hoverLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    hoverLabel_->setAttribute(Qt::WA_TranslucentBackground); // rounded corners
    hoverLabel_->hide();
}

void ActivityBar::watchButton(QToolButton* button) { button->installEventFilter(this); }

bool ActivityBar::eventFilter(QObject* watched, QEvent* event)
{
    auto* button = qobject_cast<QToolButton*>(watched);
    if (button == nullptr)
        return QWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::ToolTip:
        return true; // the hover label replaces it
    case QEvent::Enter:
        showHoverLabel(button);
        break;
    case QEvent::Leave:
    case QEvent::MouseButtonPress:
    case QEvent::Hide:
        hoverLabel_->hide();
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

void ActivityBar::showHoverLabel(QToolButton* button)
{
    // A button with a default action takes its tool tip from the action.
    const QString text = button->toolTip();
    if (text.isEmpty())
        return;
    hoverLabel_->setText(text);
    hoverLabel_->adjustSize();
    constexpr int kGap = 8;
    const QPoint anchor = button->mapToGlobal(QPoint(button->width() + kGap, button->height() / 2));
    hoverLabel_->move(anchor.x(), anchor.y() - hoverLabel_->height() / 2);
    hoverLabel_->show();
}

int ActivityBar::addPage(const QIcon& icon, const QString& toolTip)
{
    const auto index = static_cast<int>(pages_.size());
    auto* button = new QToolButton(this);
    button->setIcon(icon);
    button->setIconSize(QSize(kIconSize, kIconSize));
    button->setFixedSize(kButtonSize, kButtonSize);
    button->setToolTip(toolTip);
    button->setAccessibleName(toolTip);
    button->setCheckable(true);
    button->setFocusPolicy(Qt::NoFocus);
    connect(button, &QToolButton::clicked, this, [this, index] {
        setCurrent(current_ == index ? -1 : index);
        emit currentChanged(current_);
    });
    watchButton(button);
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
    watchButton(button);
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
