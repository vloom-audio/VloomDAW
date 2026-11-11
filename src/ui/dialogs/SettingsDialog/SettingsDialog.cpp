// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/dialogs/SettingsDialog/SettingsDialog.hpp"

#include "ui/dialogs/SettingsDialog/AudioSettingsPage.hpp"
#include "ui/dialogs/SettingsDialog/GeneralSettingsPage.hpp"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace vloomdaw {

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(tr("Settings"));
    setMinimumSize(620, 380);

    nav_ = new QListWidget(this);
    nav_->setObjectName(QStringLiteral("settingsNav"));
    nav_->setFixedWidth(160);
    nav_->setFrameShape(QFrame::NoFrame);
    nav_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    pages_ = new QStackedWidget(this);
    connect(nav_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);

    // Order must match the Page enum.
    general_ = new GeneralSettingsPage(this);
    addPage(tr("General"), general_);
    audio_ = new AudioSettingsPage(this);
    addPage(tr("Audio"), audio_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto* content = new QVBoxLayout;
    content->setContentsMargins(24, 20, 20, 16);
    content->addWidget(pages_, 1);
    content->addWidget(buttons);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(nav_);
    layout->addLayout(content, 1);

    nav_->setCurrentRow(0);
}

void SettingsDialog::addPage(const QString& title, QWidget* page)
{
    // Each page gets a heading above its own content.
    auto* holder = new QWidget(this);
    auto* layout = new QVBoxLayout(holder);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    auto* heading = new QLabel(title, holder);
    heading->setObjectName(QStringLiteral("settingsHeading"));
    layout->addWidget(heading);
    layout->addWidget(page);
    layout->addStretch();

    pages_->addWidget(holder);
    nav_->addItem(title);
}

void SettingsDialog::showPage(Page page)
{
    nav_->setCurrentRow(static_cast<int>(page));
    show();
    raise();
    activateWindow();
}

} // namespace vloomdaw
