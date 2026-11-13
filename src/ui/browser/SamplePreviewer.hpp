// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <utility>

class QThread;

namespace vloom {
class Engine;
class SampleBuffer;
} // namespace vloom

namespace vloomdaw {

/// Plays samples from the browser through the engine's preview voice
/// (Engine::previewSample()). Files are read and resampled on a worker
/// thread, so moving through a big folder with the arrow keys never stalls
/// the UI; the last few stay cached. Only the newest request plays.
class SamplePreviewer : public QObject {
    Q_OBJECT

public:
    /// `engine` must outlive the previewer.
    explicit SamplePreviewer(vloom::Engine& engine, QObject* parent = nullptr);
    ~SamplePreviewer() override;
    SamplePreviewer(const SamplePreviewer&) = delete;
    SamplePreviewer& operator=(const SamplePreviewer&) = delete;

    /// The device's rate: samples are resampled to it. A new rate empties the
    /// cache and stops the preview.
    void setSampleRate(double sampleRate);

    /// Plays `path` (UTF-8 file path) from its start, replacing the preview
    /// sounding.
    void play(const QString& path);
    /// Stops the preview (it fades out).
    void stop();
    /// The file asked for last, while it plays; empty otherwise.
    [[nodiscard]] QString playingPath() const;

signals:
    /// `path` could not be played; `error` says why.
    void failed(const QString& path, const QString& error);

private:
    void start(const QString& path, const std::shared_ptr<const vloom::SampleBuffer>& sample);
    [[nodiscard]] std::shared_ptr<const vloom::SampleBuffer> cached(const QString& path);
    void remember(const QString& path, std::shared_ptr<const vloom::SampleBuffer> sample);

    /// Most recently used first; small, so a linear search is fine.
    static constexpr std::size_t kCacheSize = 16;

    vloom::Engine& engine_;
    double sampleRate_ = 48000.0;
    /// The request whose result is wanted. Written on the UI thread; the
    /// worker reads it to skip loads that are no longer wanted.
    std::atomic<std::uint64_t> request_{0};
    QString requested_; ///< its path
    std::list<std::pair<QString, std::shared_ptr<const vloom::SampleBuffer>>> cache_;

    QThread* worker_ = nullptr; ///< loads files; joined in the destructor
    QObject* loader_ = nullptr; ///< lives on worker_
};

} // namespace vloomdaw
