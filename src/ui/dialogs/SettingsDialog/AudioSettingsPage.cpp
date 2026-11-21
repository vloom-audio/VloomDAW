// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/dialogs/SettingsDialog/AudioSettingsPage.hpp"

#include "core/AudioFormat.hpp"
#include "core/AudioIoSetup.hpp"
#include "core/Decibels.hpp"
#include "ui/Theme.hpp"
#include "ui/dialogs/SettingsDialog/SettingsCard.hpp"
#include "ui/widgets/DeviceSelector/DeviceSelector.hpp"
#include "ui/widgets/ToggleSwitch/ToggleSwitch.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace vloomdaw {

namespace {

constexpr double kInMeterFloorDb = -60.0;
constexpr double kInMeterTopDb = 6.0;
constexpr double kInMeterFallDbPerSec = 24.0;
constexpr double kInMeterHoldSec = 1.5;

double toInMeterScale(float peak)
{
    if (!(peak > 0.0f))
        return 0.0;
    const double db = gainToDb(peak);
    if (!std::isfinite(db))
        return 0.0;
    return std::clamp((db - kInMeterFloorDb) / (kInMeterTopDb - kInMeterFloorDb), 0.0, 1.0);
}

class HorizontalInputMeter final : public QWidget {
public:
    explicit HorizontalInputMeter(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(20);
        setMinimumWidth(220);
        setToolTip(QObject::tr("Live microphone / audio input level (speak to test your selected device)"));
    }

    void addPeaks(float left, float right)
    {
        feed(0, left);
        feed(1, right);
    }

    void tick(double dt)
    {
        const double fall = kInMeterFallDbPerSec * std::max(0.0, dt) / (kInMeterTopDb - kInMeterFloorDb);
        bool changed = false;
        for (int ch = 0; ch < 2; ++ch) {
            const double prev = level_[ch];
            level_[ch] = std::max(0.0, level_[ch] - fall);
            holdAge_[ch] += dt;
            if (holdAge_[ch] > kInMeterHoldSec && hold_[ch] > level_[ch]) {
                hold_[ch] = level_[ch];
                changed = true;
            }
            if (std::abs(level_[ch] - prev) > 0.001)
                changed = true;
        }
        if (changed)
            update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const Theme::Colors& c = Theme::colors();
        QPainter p(this);
        const int labelW = 54;
        const int barW = std::max(20, width() - labelW - 6);
        const int laneH = 6;
        const int y0 = (height() - (laneH * 2 + 2)) / 2;

        const int warnX = static_cast<int>(
            (((-6.0 - kInMeterFloorDb) / (kInMeterTopDb - kInMeterFloorDb)) * static_cast<double>(barW)));
        const int unityX = static_cast<int>(
            (((0.0 - kInMeterFloorDb) / (kInMeterTopDb - kInMeterFloorDb)) * static_cast<double>(barW)));

        for (int ch = 0; ch < 2; ++ch) {
            const int y = y0 + ch * (laneH + 2);
            p.fillRect(0, y, barW, laneH, c.display);
            const int lit = static_cast<int>(std::lround(std::clamp(level_[ch], 0.0, 1.0) * barW));
            const auto seg = [&](int from, int to, const QColor& col) {
                const int x1 = std::max(0, from);
                const int x2 = std::min(lit, to);
                if (x2 > x1)
                    p.fillRect(x1, y, x2 - x1, laneH, col);
            };
            seg(0, warnX, c.displayText);
            seg(warnX, unityX, c.accent);
            seg(unityX, barW, c.danger);
            if (hold_[ch] > 0.002) {
                const int hx
                    = std::clamp(static_cast<int>(std::lround(hold_[ch] * barW)), 0, std::max(0, barW - 1));
                p.fillRect(hx, y, 1, laneH, c.text);
            }
            p.fillRect(unityX, y, 1, laneH, c.border);
        }

        const double maxHold = std::max(hold_[0], hold_[1]);
        QString dbStr = QStringLiteral("-∞ dB");
        if (maxHold > 0.002) {
            const double db = kInMeterFloorDb + maxHold * (kInMeterTopDb - kInMeterFloorDb);
            dbStr = QStringLiteral("%1 dB").arg(db, 0, 'f', 1);
        }
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() > 0 ? f.pointSizeF() * 0.82 : 8.0);
        p.setFont(f);
        p.setPen(maxHold > 0.002 ? c.text : c.textDim);
        p.drawText(QRect(barW + 6, 0, labelW, height()), Qt::AlignRight | Qt::AlignVCenter, dbStr);
    }

private:
    void feed(int ch, float peak)
    {
        const double sc = toInMeterScale(peak);
        if (sc > level_[ch]) {
            level_[ch] = sc;
            update();
        }
        if (sc >= hold_[ch] && sc > 0.0) {
            hold_[ch] = sc;
            holdAge_[ch] = 0.0;
        }
    }

    double level_[2]{0.0, 0.0};
    double hold_[2]{0.0, 0.0};
    double holdAge_[2]{0.0, 0.0};
};

void selectData(QComboBox* combo, int value)
{
    const QSignalBlocker block(combo);
    const int index = combo->findData(value);
    if (index >= 0)
        combo->setCurrentIndex(index);
}

void updateStatusPill(QLabel* pill, const QString& text, bool running)
{
    pill->setText(text);
    pill->setProperty("running", running);
    pill->style()->unpolish(pill);
    pill->style()->polish(pill);
}

} // namespace

AudioSettingsPage::AudioSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    driver_ = new QComboBox;
    driver_->setMinimumWidth(220);
    driver_->addItem(QString::fromLatin1(nativeAudioDriverName()), 0);

    deviceMode_ = new QComboBox;
    deviceMode_->setMinimumWidth(220);
    deviceMode_->addItem(tr("Separate Output & Input Devices"), false);
    deviceMode_->addItem(tr("Unified Audio Interface (Sync Input)"), true);
    connect(deviceMode_, &QComboBox::currentIndexChanged, this,
        [this] { emit unifiedDeviceModeChanged(deviceMode_->currentData().toBool()); });

    ioSetupBtn_ = new QPushButton(tr("Audio I/O Setup…"));
    ioSetupBtn_->setObjectName(QStringLiteral("accentButton"));
    ioSetupBtn_->setIcon(Theme::icon(QStringLiteral("audio-io")));
    ioSetupBtn_->setAutoDefault(false);
    ioSetupBtn_->setFocusPolicy(Qt::NoFocus);
    ioSetupBtn_->setToolTip(
        tr("Open the Input & Output Channel Routing Matrix (Mono / Stereo buses, L/R channel mapping)"));
    connect(ioSetupBtn_, &QPushButton::clicked, this, &AudioSettingsPage::audioIoSetupRequested);

    ioSummary_ = new QLabel;
    ioSummary_->setObjectName(QStringLiteral("settingsRowDescription"));
    ioSummary_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto* ioRowWidget = new QWidget;
    auto* ioRowLayout = new QHBoxLayout(ioRowWidget);
    ioRowLayout->setContentsMargins(0, 0, 0, 0);
    ioRowLayout->setSpacing(10);
    ioRowLayout->addWidget(ioSummary_, 1);
    ioRowLayout->addWidget(ioSetupBtn_);

    devices_ = new DeviceSelector;
    connect(devices_, &DeviceSelector::deviceSelected, this, &AudioSettingsPage::deviceSelected);

    inputDevices_ = new QComboBox;
    inputDevices_->setMinimumWidth(220);
    connect(inputDevices_, &QComboBox::currentIndexChanged, this,
        [this] { emit inputDeviceSelected(inputDevices_->currentData().toInt()); });

    midiDevices_ = new QComboBox;
    midiDevices_->setMinimumWidth(190);
    connect(midiDevices_, &QComboBox::currentIndexChanged, this,
        [this] { emit midiDeviceSelected(midiDevices_->currentData().toString()); });

    refreshMidiBtn_ = new QPushButton(tr("Refresh"));
    refreshMidiBtn_->setAutoDefault(false);
    refreshMidiBtn_->setFocusPolicy(Qt::NoFocus);
    connect(refreshMidiBtn_, &QPushButton::clicked, this, &AudioSettingsPage::refreshMidiRequested);

    auto* midiRowWidget = new QWidget;
    auto* midiRowLayout = new QHBoxLayout(midiRowWidget);
    midiRowLayout->setContentsMargins(0, 0, 0, 0);
    midiRowLayout->setSpacing(6);
    midiRowLayout->addWidget(midiDevices_, 1);
    midiRowLayout->addWidget(refreshMidiBtn_);

    sampleRate_ = new QComboBox;
    for (const int rate : kSampleRates)
        sampleRate_->addItem(tr("%1 Hz").arg(rate), rate);
    connect(sampleRate_, &QComboBox::currentIndexChanged, this, &AudioSettingsPage::emitFormat);

    bufferSize_ = new QComboBox;
    for (const int frames : kBufferSizes)
        bufferSize_->addItem(tr("%1 samples").arg(frames), frames);
    connect(bufferSize_, &QComboBox::currentIndexChanged, this, &AudioSettingsPage::emitFormat);

    status_ = new QLabel;
    status_->setObjectName(QStringLiteral("statusPill"));
    status_->setAlignment(Qt::AlignCenter);

    inputStatus_ = new QLabel;
    inputStatus_->setObjectName(QStringLiteral("statusPill"));
    inputStatus_->setAlignment(Qt::AlignCenter);

    inputMeter_ = new HorizontalInputMeter(this);

    midiStatus_ = new QLabel;
    midiStatus_->setObjectName(QStringLiteral("statusPill"));
    midiStatus_->setAlignment(Qt::AlignCenter);

    auto* driverCard = new SettingsCard(tr("Driver & Channel I/O"), this);
    driverCard->addRow(
        tr("Audio driver"), tr("Low-latency system audio backend active on this platform."), driver_);
    driverCard->addRow(tr("Device routing mode"),
        tr("Choose independent playback/capture devices or lock them to one audio interface."), deviceMode_);
    driverCard->addRow(tr("Channel routing"),
        tr("Configure physical Mono / Stereo input & output buses and live meters."), ioRowWidget);

    auto* output = new SettingsCard(tr("Output"), this);
    output->addRow(
        tr("Device"), tr("Where VloomDAW plays. Also in the status bar: click the device name."), devices_);
    output->addRow(tr("Status"), QString(), status_);

    auto* input = new SettingsCard(tr("Input & Recording"), this);
    input->addRow(tr("Audio input"),
        tr("Microphone or audio interface inputs for live monitoring and Song recording."), inputDevices_);
    input->addRow(tr("Input level"), tr("Live signal from the active capture device."), inputMeter_);
    input->addRow(tr("Status"), QString(), inputStatus_);

    auto* midi = new SettingsCard(tr("MIDI Input"), this);
    midi->addRow(tr("Controller port"),
        tr("Hardware MIDI keyboard or pad controller: plays the selected channel live and records notes."),
        midiRowWidget);
    midi->addRow(tr("Status"), QString(), midiStatus_);

    auto* format = new SettingsCard(tr("Format"), this);
    format->addRow(tr("Sample rate"), tr("Use your interface's rate; 48000 Hz suits most."), sampleRate_);
    latency_ = format->addRow(tr("Buffer size"), QString(), bufferSize_);
#if defined(Q_OS_WIN)
    exclusive_ = new ToggleSwitch;
    exclusive_->setAccessibleName(tr("Exclusive mode (WASAPI)"));
    connect(exclusive_, &ToggleSwitch::toggled, this, &AudioSettingsPage::exclusiveChanged);
    format->addRow(tr("Exclusive mode (WASAPI)"),
        tr("Lower latency, but other apps can't play sound while VloomDAW uses the device. "
           "Windows' own mixer and limiter are bypassed, so sound over 0 dB clips instead of "
           "being squeezed."),
        exclusive_);
#endif

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addWidget(driverCard);
    layout->addWidget(output);
    layout->addWidget(input);
    layout->addWidget(midi);
    layout->addWidget(format);
    layout->addStretch();

    setUnifiedDeviceMode(false);
    setIoRoutingSummary(tr("IN 0 + 1 (Stereo)  →  Main (Stereo)"));
    setInputDevices({}, kInputDisabled);
    setMidiDevices({}, QString());
    setInputStatus(tr("Disabled"), false);
    setMidiStatus(tr("No MIDI controller selected"), false);
    setFormat(kDefaultSampleRate, kDefaultBufferFrames);
}

void AudioSettingsPage::setUnifiedDeviceMode(bool unified)
{
    const QSignalBlocker block(deviceMode_);
    const int idx = deviceMode_->findData(unified);
    deviceMode_->setCurrentIndex(idx >= 0 ? idx : 0);
}

void AudioSettingsPage::setIoRoutingSummary(const QString& summary)
{
    if (ioSummary_ != nullptr)
        ioSummary_->setText(summary);
}

void AudioSettingsPage::setDevices(const QStringList& names, int currentIndex)
{
    devices_->setDevices(names, currentIndex);
}

void AudioSettingsPage::setInputDevices(const QStringList& names, int selectedCode)
{
    const QSignalBlocker block(inputDevices_);
    inputDevices_->clear();
    inputDevices_->addItem(tr("None (Disabled)"), kInputDisabled);
    if (!names.isEmpty()) {
        inputDevices_->addItem(tr("System Default"), kInputSystemDefault);
        for (int i = 0; i < names.size(); ++i)
            inputDevices_->addItem(names.at(i), i);
    }
    const int found = inputDevices_->findData(selectedCode);
    inputDevices_->setCurrentIndex(found >= 0 ? found : 0);
}

void AudioSettingsPage::setMidiDevices(const QStringList& names, const QString& activeName)
{
    const QSignalBlocker block(midiDevices_);
    midiDevices_->clear();
    midiDevices_->addItem(tr("None"), QString());
    for (const QString& name : names)
        midiDevices_->addItem(name, name);
    int idx = activeName.isEmpty() ? 0 : midiDevices_->findData(activeName);
    if (idx < 0 && !activeName.isEmpty()) {
        midiDevices_->addItem(tr("%1 (disconnected)").arg(activeName), activeName);
        idx = midiDevices_->count() - 1;
    }
    midiDevices_->setCurrentIndex(std::max(0, idx));
}

void AudioSettingsPage::setFormat(int sampleRate, int bufferFrames)
{
    selectData(sampleRate_, sampleRate);
    selectData(bufferSize_, bufferFrames);
    showLatency();
}

void AudioSettingsPage::setExclusive(bool exclusive)
{
    if (exclusive_ == nullptr)
        return;
    const QSignalBlocker block(exclusive_);
    exclusive_->setChecked(exclusive);
}

QString AudioSettingsPage::runningStatusText(int sampleRate, int requestedRate, bool exclusive)
{
    QString text = tr("Running at %1 Hz").arg(sampleRate);
    if (sampleRate != requestedRate)
        text += tr(" (device does not support %1 Hz)").arg(requestedRate);
    if (exclusive)
        text += tr("  ·  Exclusive");
    return text;
}

QString AudioSettingsPage::exclusiveFallbackText(const QString& device, const QString& reason)
{
    QString why = reason.trimmed();
    while (why.endsWith(QLatin1Char('.')))
        why.chop(1);
    if (why.isEmpty())
        why = tr("the device refused");
    const QString name = device.isEmpty() ? tr("the device") : QStringLiteral("\u201C%1\u201D").arg(device);
    return tr("Couldn't open %1 in exclusive mode: %2. Playing through shared mode.").arg(name, why);
}

void AudioSettingsPage::setStatus(const QString& text, bool running)
{
    updateStatusPill(status_, text, running);
}

void AudioSettingsPage::setInputStatus(const QString& text, bool running)
{
    updateStatusPill(inputStatus_, text, running);
}

void AudioSettingsPage::setMidiStatus(const QString& text, bool running)
{
    updateStatusPill(midiStatus_, text, running);
}

void AudioSettingsPage::addInputPeaks(float left, float right)
{
    if (inputMeter_ != nullptr)
        static_cast<HorizontalInputMeter*>(inputMeter_)->addPeaks(left, right);
}

void AudioSettingsPage::tickInputMeter(double dt)
{
    if (inputMeter_ != nullptr)
        static_cast<HorizontalInputMeter*>(inputMeter_)->tick(dt);
}

void AudioSettingsPage::showLatency()
{
    const double ms = bufferLatencyMs(bufferSize_->currentData().toInt(), sampleRate_->currentData().toInt());
    latency_->setText(
        tr("%1 ms of delay per buffer: smaller reacts faster, larger is safer against crackles.")
            .arg(ms, 0, 'f', 1));
    latency_->show();
}

void AudioSettingsPage::emitFormat()
{
    showLatency();
    emit formatChanged(sampleRate_->currentData().toInt(), bufferSize_->currentData().toInt());
}

} // namespace vloomdaw
