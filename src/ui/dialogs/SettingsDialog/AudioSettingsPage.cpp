// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/dialogs/SettingsDialog/AudioSettingsPage.hpp"

#include "core/AudioFormat.hpp"
#include "ui/dialogs/SettingsDialog/SettingsCard.hpp"
#include "ui/widgets/DeviceSelector/DeviceSelector.hpp"

#include <QComboBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>

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
    devices_ = new DeviceSelector;
    connect(devices_, &DeviceSelector::deviceSelected, this, &AudioSettingsPage::deviceSelected);

    sampleRate_ = new QComboBox;
    for (const int rate : kSampleRates)
        sampleRate_->addItem(tr("%1 Hz").arg(rate), rate);
    connect(sampleRate_, &QComboBox::currentIndexChanged, this, &AudioSettingsPage::emitFormat);

    bufferSize_ = new QComboBox;
    for (const int frames : kBufferSizes)
        bufferSize_->addItem(tr("%1 samples").arg(frames), frames);
    connect(bufferSize_, &QComboBox::currentIndexChanged, this, &AudioSettingsPage::emitFormat);

    // Status: a pill that says whether the device runs.
    status_ = new QLabel;
    status_->setObjectName(QStringLiteral("statusPill"));
    status_->setAlignment(Qt::AlignCenter);

    auto* output = new SettingsCard(tr("Output"), this);
    output->addRow(tr("Device"), tr("Where VloomDAW plays. Also in the status bar: click the device name."),
        devices_);
    output->addRow(tr("Status"), QString(), status_);

    auto* format = new SettingsCard(tr("Format"), this);
    format->addRow(tr("Sample rate"), tr("Use your interface's rate; 48000 Hz suits most."), sampleRate_);
    latency_ = format->addRow(tr("Buffer size"), QString(), bufferSize_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    layout->addWidget(output);
    layout->addWidget(format);
    layout->addStretch();

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

void AudioSettingsPage::setStatus(const QString& text, bool running)
{
    status_->setText(text);
    // Green when running, red when not ([running="true"] in Theme).
    status_->setProperty("running", running);
    status_->style()->unpolish(status_);
    status_->style()->polish(status_);
}

void AudioSettingsPage::showLatency()
{
    const double ms = bufferLatencyMs(bufferSize_->currentData().toInt(), sampleRate_->currentData().toInt());
    latency_->setText(
        tr("%1 ms of delay per buffer: smaller reacts faster, larger is safer against crackles.").arg(ms, 0, 'f', 1));
    latency_->show();
}

void AudioSettingsPage::emitFormat()
{
    showLatency();
    emit formatChanged(sampleRate_->currentData().toInt(), bufferSize_->currentData().toInt());
}

} // namespace vloomdaw
