// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QStringList>
#include <QWidget>

class QComboBox;

namespace vloomdaw {

/// Drop-down for picking the audio output device.
class DeviceSelector : public QWidget {
    Q_OBJECT

public:
    explicit DeviceSelector(QWidget* parent = nullptr);

    /// Replaces the list without emitting deviceSelected().
    void setDevices(const QStringList& names, int currentIndex);
    [[nodiscard]] int currentIndex() const;

signals:
    void deviceSelected(int index);

private:
    QComboBox* combo_ = nullptr;
};

} // namespace vloomdaw
