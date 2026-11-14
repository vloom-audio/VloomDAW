// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <vector>

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
///
/// It also hands out each file's waveform (loaded()), for the browser's
/// preview strip, whether the file is played or only shown.
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
    /// Loads `path` for its waveform (loaded()) without playing it; does not
    /// touch the preview sounding.
    void show(const QString& path);
    /// Stops the preview (it fades out).
    void stop();
    /// The file asked for last, while it plays; empty otherwise.
    [[nodiscard]] QString playingPath() const;
    /// Seconds since the preview sounding started; < 0 when none sounds.
    [[nodiscard]] double playingSeconds() const;

    /// Points in a waveform (loaded()): the largest absolute sample of each
    /// slice of the file, all channels.
    static constexpr std::size_t kWaveformPoints = 1024;

signals:
    /// `path` could not be played; `error` says why.
    void failed(const QString& path, const QString& error);
    /// `path` is loaded: its waveform (kWaveformPoints values in 0..1, or
    /// fewer for a very short file) and length.
    void loaded(const QString& path, std::shared_ptr<const std::vector<float>> waveform, double seconds);

private:
    struct Entry {
        QString path{};
        std::shared_ptr<const vloom::SampleBuffer> sample{};
        std::shared_ptr<const std::vector<float>> waveform{};
    };

    /// Loads `path` (from the cache, or on the worker), then emits loaded();
    /// plays it too with `andPlay` while it is still the newest play request.
    void fetch(const QString& path, bool andPlay);
    void start(const QString& path, const std::shared_ptr<const vloom::SampleBuffer>& sample);
    [[nodiscard]] const Entry* cached(const QString& path);
    void remember(Entry entry);

    /// Most recently used first; small, so a linear search is fine.
    static constexpr std::size_t kCacheSize = 16;

    vloom::Engine& engine_;
    double sampleRate_ = 48000.0;
    /// The play request whose result is wanted. Written on the UI thread; the
    /// worker reads it to skip loads that are no longer wanted.
    std::atomic<std::uint64_t> request_{0};
    /// The same for show(): only the newest file shown is worth loading.
    std::atomic<std::uint64_t> showRequest_{0};
    QString requested_; ///< the play request's path
    QElapsedTimer started_; ///< when the preview sounding started
    std::list<Entry> cache_;

    QThread* worker_ = nullptr; ///< loads files; joined in the destructor
    QObject* loader_ = nullptr; ///< lives on worker_
};

} // namespace vloomdaw
