// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/DeviceSelector/DeviceSelector.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QSignalBlocker>

namespace vloomdaw {

DeviceSelector::DeviceSelector(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    combo_ = new QComboBox(this);
    combo_->setToolTip(tr("Audio output device"));
    combo_->setMinimumContentsLength(24);
    combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    layout->addWidget(combo_);

    connect(combo_, &QComboBox::currentIndexChanged, this, &DeviceSelector::deviceSelected);
}

void DeviceSelector::setDevices(const QStringList& names, int currentIndex)
{
    const QSignalBlocker block(combo_);
    combo_->clear();
    combo_->addItems(names);
    if (currentIndex >= 0 && currentIndex < combo_->count())
        combo_->setCurrentIndex(currentIndex);
}

int DeviceSelector::currentIndex() const { return combo_->currentIndex(); }

} // namespace vloomdaw
