// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QStringList>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;

namespace vloomdaw {

class DeviceSelector;

/// Settings → Audio: output device, input device, MIDI input port, sample rate, buffer size.
/// A pure view: emits what the user picked; changes apply immediately.
class AudioSettingsPage : public QWidget {
    Q_OBJECT

public:
    static constexpr int kInputDisabled = -2;
    static constexpr int kInputSystemDefault = -1;

    explicit AudioSettingsPage(QWidget* parent = nullptr);

    /// The setters below never emit signals.
    void setDevices(const QStringList& names, int currentIndex);
    void setInputDevices(const QStringList& names, int selectedCode);
    void setMidiDevices(const QStringList& names, const QString& activeName);
    void setFormat(int sampleRate, int bufferFrames);
    void setUnifiedDeviceMode(bool unified);
    void setIoRoutingSummary(const QString& summary);
    /// What the device is actually doing, e.g. "Running at 48000 Hz", and
    /// whether it runs (colours the status pill).
    void setStatus(const QString& text, bool running);
    void setInputStatus(const QString& text, bool running);
    void setMidiStatus(const QString& text, bool running);
    void addInputPeaks(float left, float right);
    void tickInputMeter(double dt);

signals:
    void deviceSelected(int index);
    void inputDeviceSelected(int code); ///< `kInputDisabled`, `kInputSystemDefault`, or `0..N-1`
    void midiDeviceSelected(const QString& portName); ///< empty = None
    void refreshMidiRequested();
    void formatChanged(int sampleRate, int bufferFrames);
    void unifiedDeviceModeChanged(bool unified);
    void audioIoSetupRequested();

private:
    void emitFormat();
    void showLatency();

    QComboBox* driver_ = nullptr;
    QComboBox* deviceMode_ = nullptr;
    QPushButton* ioSetupBtn_ = nullptr;
    QLabel* ioSummary_ = nullptr;
    DeviceSelector* devices_ = nullptr;
    QComboBox* inputDevices_ = nullptr;
    QComboBox* midiDevices_ = nullptr;
    QPushButton* refreshMidiBtn_ = nullptr;
    QComboBox* sampleRate_ = nullptr;
    QComboBox* bufferSize_ = nullptr;
    QLabel* latency_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* inputStatus_ = nullptr;
    QWidget* inputMeter_ = nullptr;
    QLabel* midiStatus_ = nullptr;
};

} // namespace vloomdaw
