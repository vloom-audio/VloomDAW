// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/WindowButtons/WindowButtons.hpp"

#include "ui/Theme.hpp"

#include <QHBoxLayout>
#include <QIcon>
#include <QToolButton>

namespace vloomdaw {

namespace {

// Same proportions as the Windows caption buttons.
constexpr QSize kButtonSize{46, 30};
constexpr QSize kIconSize{20, 20};

QIcon icon(const QString& name) { return Theme::icon(name); }

} // namespace

WindowButtons::WindowButtons(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("windowButtons"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    const auto addButton = [&](const QString& iconName, const QString& tip) {
        auto* button = new QToolButton(this);
        button->setIcon(icon(iconName));
        button->setIconSize(kIconSize);
        button->setFixedSize(kButtonSize);
        button->setToolTip(tip);
        button->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(button);
        return button;
    };

    auto* minimize = addButton(QStringLiteral("window-minimize"), tr("Minimize"));
    maximize_ = addButton(QStringLiteral("window-maximize"), tr("Maximize"));
    auto* close = addButton(QStringLiteral("window-close"), tr("Close"));
    close->setObjectName(QStringLiteral("closeButton"));

    connect(minimize, &QToolButton::clicked, this, &WindowButtons::minimizeRequested);
    connect(maximize_, &QToolButton::clicked, this, &WindowButtons::maximizeToggled);
    connect(close, &QToolButton::clicked, this, &WindowButtons::closeRequested);
}

void WindowButtons::setMaximized(bool maximized)
{
    maximize_->setIcon(icon(maximized ? QStringLiteral("window-restore") : QStringLiteral("window-maximize")));
    maximize_->setToolTip(maximized ? tr("Restore Down") : tr("Maximize"));
}

} // namespace vloomdaw
