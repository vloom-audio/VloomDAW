// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/dialogs/SettingsDialog/GeneralSettingsPage.hpp"

#include "ui/dialogs/SettingsDialog/SettingsCard.hpp"
#include "ui/widgets/ToggleSwitch/ToggleSwitch.hpp"

#include <QComboBox>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace vloomdaw {

GeneralSettingsPage::GeneralSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    positionMode_ = new QComboBox;
    positionMode_->addItem(tr("Bars · beats · ticks"));
    positionMode_->addItem(tr("Minutes · seconds"));
    connect(
        positionMode_, &QComboBox::currentIndexChanged, this, &GeneralSettingsPage::startPositionModeChanged);

    metronome_ = new ToggleSwitch;
    metronome_->setAccessibleName(tr("Metronome on at startup"));
    connect(metronome_, &ToggleSwitch::toggled, this, &GeneralSettingsPage::metronomeOnStartupChanged);

    auto* startup = new SettingsCard(tr("When VloomDAW starts"), this);
    startup->addRow(tr("Position display"),
        tr("What the song position LCD shows first. Click the display to switch while you work."),
        positionMode_);
    startup->addRow(tr("Metronome"),
        tr("Start with the metronome clicking on every beat (Ctrl+M toggles it)."), metronome_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    layout->addWidget(startup);
    layout->addStretch();
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
