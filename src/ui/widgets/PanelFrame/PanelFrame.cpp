// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/PanelFrame/PanelFrame.hpp"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <algorithm>

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
    headerBox->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    header_ = new QHBoxLayout(headerBox);
    header_->setContentsMargins(12, 6, 8, 6);
    header_->setSpacing(7);

    auto* titleLabel = new QLabel(title, headerBox);
    titleLabel->setObjectName(QStringLiteral("panelTitle"));
    header_->addWidget(titleLabel);
    context_ = new QLabel(headerBox);
    context_->setObjectName(QStringLiteral("panelContext"));
    context_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    header_->addWidget(context_, 1);
    layout->addWidget(headerBox);

    body->setParent(this);
    layout->addWidget(body, 1);
}

void PanelFrame::setContext(const QString& text)
{
    fullContext_ = text;
    context_->setToolTip(text);
    updateElidedContext();
}

void PanelFrame::addHeaderWidget(QWidget* widget)
{
    header_->addWidget(widget);
    updateElidedContext();
}

void PanelFrame::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateElidedContext();
}

void PanelFrame::updateElidedContext()
{
    if (context_ == nullptr)
        return;
    const int avail = std::max(0, context_->width());
    if (avail <= 8 || fullContext_.isEmpty()) {
        context_->clear();
        return;
    }
    const QFontMetrics fm(context_->font());
    context_->setText(fm.elidedText(fullContext_, Qt::ElideRight, avail));
}

} // namespace vloomdaw
