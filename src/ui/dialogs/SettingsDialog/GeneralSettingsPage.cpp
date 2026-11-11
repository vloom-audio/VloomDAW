// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/dialogs/SettingsDialog/GeneralSettingsPage.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QSignalBlocker>

namespace vloomdaw {

GeneralSettingsPage::GeneralSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    positionMode_ = new QComboBox(this);
    positionMode_->addItem(tr("Bars · beats · ticks"));
    positionMode_->addItem(tr("Minutes · seconds"));
    connect(
        positionMode_, &QComboBox::currentIndexChanged, this, &GeneralSettingsPage::startPositionModeChanged);

    metronome_ = new QCheckBox(tr("Turn the metronome on"), this);
    connect(metronome_, &QCheckBox::toggled, this, &GeneralSettingsPage::metronomeOnStartupChanged);

    auto* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setVerticalSpacing(12);
    form->addRow(tr("Position display shows"), positionMode_);
    form->addRow(tr("On startup"), metronome_);
}

void GeneralSettingsPage::setStartPositionMode(int mode)
{
    const QSignalBlocker block(positionMode_);
    positionMode_->setCurrentIndex(mode == 1 ? 1 : 0);
}

void GeneralSettingsPage::setMetronomeOnStartup(bool on)
{
    const QSignalBlocker block(metronome_);
    metronome_->setChecked(on);
}

} // namespace vloomdaw
