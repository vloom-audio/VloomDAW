// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QObject>

#include <string>
#include <vector>

class QThread;

namespace vloomdaw {

/// Lists the audio output devices on a worker thread. Enumerating them takes
/// 50-200 ms on Windows; done on the UI thread every couple of seconds (while
/// waiting for an unplugged device to come back) it made the whole UI stutter.
///
/// Each scan uses its own vloom::AudioDevice, so it never touches the device
/// that is playing.
class DeviceScanner : public QObject {
    Q_OBJECT

public:
    explicit DeviceScanner(QObject* parent = nullptr);
    ~DeviceScanner() override;
    DeviceScanner(const DeviceScanner&) = delete;
    DeviceScanner& operator=(const DeviceScanner&) = delete;

    /// Starts a scan unless one is running; scanned() follows.
    void scan();
    /// Device names from the last finished scan (empty before the first).
    [[nodiscard]] const std::vector<std::string>& names() const noexcept { return names_; }

signals:
    void scanned();

private:
    std::vector<std::string> names_;
    bool busy_ = false;
    QThread* worker_ = nullptr; ///< joined in the destructor
    QObject* lister_ = nullptr; ///< lives on worker_
};

} // namespace vloomdaw
