// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QStringList>
#include <QWidget>

class QComboBox;
class QLabel;

namespace vloomdaw {

class DeviceSelector;

/// Settings → Audio: output device, sample rate, buffer size.
/// A pure view: emits what the user picked; changes apply immediately.
class AudioSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit AudioSettingsPage(QWidget* parent = nullptr);

    /// The setters below never emit signals.
    void setDevices(const QStringList& names, int currentIndex);
    void setFormat(int sampleRate, int bufferFrames);
    /// What the device is actually doing, e.g. "Running at 48000 Hz", and
    /// whether it runs (colours the status pill).
    void setStatus(const QString& text, bool running);

signals:
    void deviceSelected(int index);
    void formatChanged(int sampleRate, int bufferFrames);

private:
    void emitFormat();
    void showLatency();

    DeviceSelector* devices_ = nullptr;
    QComboBox* sampleRate_ = nullptr;
    QComboBox* bufferSize_ = nullptr;
    QLabel* latency_ = nullptr;
    QLabel* status_ = nullptr;
};

} // namespace vloomdaw
