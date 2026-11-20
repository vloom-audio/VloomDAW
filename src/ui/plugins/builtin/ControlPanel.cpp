// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/ControlPanel.hpp"

#include "ui/plugins/builtin/EditorStyle.hpp"

#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QPainter>

namespace vloomdaw {

namespace {

namespace style = editor_style;

/// An upright rule between two groups of controls.
class Divider : public QWidget {
public:
    using QWidget::QWidget;
    [[nodiscard]] QSize sizeHint() const override { return {9, 56}; }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter p(this);
        p.fillRect(QRect(width() / 2, 0, 1, height()), style::strong());
    }
};

} // namespace

ControlPanel::ControlPanel(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("builtinControlPanel"));
    setFixedHeight(style::kPanelHeight);
    setAttribute(Qt::WA_TranslucentBackground); // the rounded corners show the display
    row_ = new QHBoxLayout(this);
    row_->setContentsMargins(14, 10, 14, 10);
    row_->setSpacing(4);
    row_->setSizeConstraint(QLayout::SetFixedSize);

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(28);
    shadow->setOffset(0, 10);
    shadow->setColor(QColor(0, 0, 0, 128));
    setGraphicsEffect(shadow);
}

void ControlPanel::add(QWidget* control)
{
    control->setParent(this);
    row_->addWidget(control, 0, Qt::AlignVCenter);
    control->show();
}

void ControlPanel::addSpacing(int pixels) { row_->addSpacing(pixels); }

void ControlPanel::addDivider()
{
    auto* divider = new Divider(this);
    divider->setFixedSize(divider->sizeHint());
    row_->addWidget(divider, 0, Qt::AlignVCenter);
}

void ControlPanel::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(style::strong(), 1.0));
    p.setBrush(style::panelFill());
    p.drawRoundedRect(
        QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), style::kPanelRadius, style::kPanelRadius);
}

FooterBar::FooterBar(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("builtinFooter"));
    setFixedHeight(style::kFooterHeight);
    row_ = new QHBoxLayout(this);
    row_->setContentsMargins(12, 1, 12, 0);
    row_->setSpacing(16);
    row_->addStretch(1);
}

void FooterBar::addLeft(QWidget* widget)
{
    widget->setParent(this);
    row_->insertWidget(left_++, widget, 0, Qt::AlignVCenter);
}

void FooterBar::addRight(QWidget* widget)
{
    widget->setParent(this);
    row_->addWidget(widget, 0, Qt::AlignVCenter);
}

void FooterBar::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.fillRect(rect(), style::background());
    p.fillRect(QRect(0, 0, width(), 1), style::faint());
}

} // namespace vloomdaw
