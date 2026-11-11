// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/dialogs/SettingsDialog/SettingsCard.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>

namespace vloomdaw {

SettingsCard::SettingsCard(const QString& title, QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("settingsCard"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);

    auto* heading = new QLabel(title, this);
    heading->setObjectName(QStringLiteral("settingsCardTitle"));
    layout->addWidget(heading);

    rows_ = new QVBoxLayout;
    rows_->setSpacing(0);
    layout->addLayout(rows_);
}

void SettingsCard::addSeparatorIfNeeded()
{
    if (count_++ == 0)
        return;
    auto* line = new QFrame(this);
    line->setObjectName(QStringLiteral("settingsSeparator"));
    line->setFixedHeight(1);
    rows_->addWidget(line);
}

QLabel* SettingsCard::addRow(const QString& name, const QString& description, QWidget* control)
{
    addSeparatorIfNeeded();

    auto* row = new QWidget(this);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 10, 0, 10);
    layout->setSpacing(16);

    auto* text = new QVBoxLayout;
    text->setSpacing(2);
    auto* nameLabel = new QLabel(name, row);
    nameLabel->setObjectName(QStringLiteral("settingsRowName"));
    text->addWidget(nameLabel);
    auto* descriptionLabel = new QLabel(description, row);
    descriptionLabel->setObjectName(QStringLiteral("settingsRowDescription"));
    descriptionLabel->setWordWrap(true);
    descriptionLabel->setVisible(!description.isEmpty());
    text->addWidget(descriptionLabel);
    layout->addLayout(text, 1);

    if (control != nullptr) {
        control->setParent(row);
        // Combo boxes and the like get a common width so the column lines up;
        // fixed-size controls (switches, swatches) keep their own.
        if (control->sizePolicy().horizontalPolicy() != QSizePolicy::Fixed)
            control->setMinimumWidth(std::max(control->minimumWidth(), 220));
        layout->addWidget(control, 0, Qt::AlignVCenter);
    }
    rows_->addWidget(row);
    return descriptionLabel;
}

void SettingsCard::addWide(QWidget* widget)
{
    addSeparatorIfNeeded();
    widget->setParent(this);
    rows_->addWidget(widget);
}

} // namespace vloomdaw
