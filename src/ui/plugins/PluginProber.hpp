// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/PluginScanner.hpp"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <cstddef>
#include <vector>

class QProcess;
class QTimer;

namespace vloomdaw {

/// Probes plugin modules one at a time with vloomdaw-plugin-probe (a child
/// process), so a plugin that crashes or hangs while loading never takes the
/// app down: it is killed after a timeout and marked failed.
///
/// Modules unchanged since an earlier scan (same path, size and time) reuse
/// that scan's results instead of being loaded again.
class PluginProber : public QObject {
    Q_OBJECT

public:
    static constexpr int kTimeoutMs = 20000;

    explicit PluginProber(QObject* parent = nullptr);
    ~PluginProber() override;

    /// The probe executable next to the app's, or empty if it is missing.
    [[nodiscard]] static QString probeProgram();

    /// Probes `modules` (from scanPluginFolders()); `previous` is the last
    /// scan's result, for the cache. Emits progress() and then finished().
    void start(std::vector<PluginInfo> modules, std::vector<PluginInfo> previous);
    /// Stops after the module being probed; the rest stay unprobed.
    void stop();
    [[nodiscard]] bool isRunning() const noexcept { return running_; }

signals:
    void progress(int done, int total, const QString& current);
    void finished(const std::vector<PluginInfo>& plugins);

private:
    void next();
    void moduleDone(const QByteArray& output, const QString& failure);

    QProcess* process_ = nullptr;
    QTimer* timeout_ = nullptr;
    std::vector<PluginInfo> modules_;
    std::vector<PluginInfo> previous_;
    std::vector<PluginInfo> results_;
    std::size_t index_ = 0;
    QByteArray output_;
    bool running_ = false;
    bool stopping_ = false;
    bool timedOut_ = false;
};

} // namespace vloomdaw
