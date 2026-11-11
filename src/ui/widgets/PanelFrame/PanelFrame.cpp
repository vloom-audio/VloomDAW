// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/PanelFrame/PanelFrame.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace vloomdaw {

PanelFrame::PanelFrame(const QString& title, QWidget* body, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("panelFrame"));
    setAttribute(Qt::WA_StyledBackground);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* headerBox = new QWidget(this);
    headerBox->setObjectName(QStringLiteral("panelHeader"));
    headerBox->setAttribute(Qt::WA_StyledBackground);
    header_ = new QHBoxLayout(headerBox);
    header_->setContentsMargins(12, 6, 8, 6);
    header_->setSpacing(10);

    auto* titleLabel = new QLabel(title, headerBox);
    titleLabel->setObjectName(QStringLiteral("panelTitle"));
    header_->addWidget(titleLabel);
    context_ = new QLabel(headerBox);
    context_->setObjectName(QStringLiteral("panelContext"));
    header_->addWidget(context_);
    header_->addStretch();
    layout->addWidget(headerBox);

    body->setParent(this);
    layout->addWidget(body, 1);
}

void PanelFrame::setContext(const QString& text) { context_->setText(text); }

void PanelFrame::addHeaderWidget(QWidget* widget) { header_->addWidget(widget); }

} // namespace vloomdaw
