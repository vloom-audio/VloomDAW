// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/dialogs/SettingsDialog/AudioSettingsPage.hpp"

#include "core/AudioFormat.hpp"
#include "ui/widgets/DeviceSelector/DeviceSelector.hpp"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>

namespace vloomdaw {

namespace {

void selectData(QComboBox* combo, int value)
{
    const QSignalBlocker block(combo);
    const int index = combo->findData(value);
    if (index >= 0)
        combo->setCurrentIndex(index);
}

} // namespace

AudioSettingsPage::AudioSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    devices_ = new DeviceSelector(this);
    connect(devices_, &DeviceSelector::deviceSelected, this, &AudioSettingsPage::deviceSelected);

    sampleRate_ = new QComboBox(this);
    for (const int rate : kSampleRates)
        sampleRate_->addItem(tr("%1 Hz").arg(rate), rate);
    connect(sampleRate_, &QComboBox::currentIndexChanged, this, &AudioSettingsPage::emitFormat);

    bufferSize_ = new QComboBox(this);
    for (const int frames : kBufferSizes)
        bufferSize_->addItem(tr("%1 samples").arg(frames), frames);
    connect(bufferSize_, &QComboBox::currentIndexChanged, this, &AudioSettingsPage::emitFormat);

    latency_ = new QLabel(this);
    latency_->setObjectName(QStringLiteral("dialogHint"));
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("dialogHint"));

    auto* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setVerticalSpacing(12);
    form->addRow(tr("Output device"), devices_);
    form->addRow(tr("Sample rate"), sampleRate_);
    form->addRow(tr("Buffer size"), bufferSize_);
    form->addRow(tr("Latency"), latency_);
    form->addRow(tr("Status"), status_);

    setFormat(kDefaultSampleRate, kDefaultBufferFrames);
}

void AudioSettingsPage::setDevices(const QStringList& names, int currentIndex)
{
    devices_->setDevices(names, currentIndex);
}

void AudioSettingsPage::setFormat(int sampleRate, int bufferFrames)
{
    selectData(sampleRate_, sampleRate);
    selectData(bufferSize_, bufferFrames);
    showLatency();
}

void AudioSettingsPage::setStatus(const QString& text) { status_->setText(text); }

void AudioSettingsPage::showLatency()
{
    const double ms = bufferLatencyMs(bufferSize_->currentData().toInt(), sampleRate_->currentData().toInt());
    latency_->setText(tr("%1 ms per buffer").arg(ms, 0, 'f', 1));
}

void AudioSettingsPage::emitFormat()
{
    showLatency();
    emit formatChanged(sampleRate_->currentData().toInt(), bufferSize_->currentData().toInt());
}

} // namespace vloomdaw
