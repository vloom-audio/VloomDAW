// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

class QProcess;
class QTemporaryDir;
class QThread;

namespace vloomdaw {

/// What to separate, and where the stems go.
struct StemJob {
    QString source; ///< the audio file
    QString outputFolder; ///< for the stems' WAV files; created when missing
    QString baseName; ///< the stems are named "<baseName> - <stem>.wav"
    QString modelId; ///< core/StemModels
    QString modelFolder; ///< where the model's files are
};

/// One stem of a finished job.
struct StemFile {
    QString stem; ///< "vocals", "drums"...
    QString path; ///< its WAV file
};

/// Separates an audio file into stems with a model from the model store.
///
/// The work is done by the vloomdaw-stems helper in a process of its own (it
/// takes minutes, and cancelling ends the process). Before that the model's
/// files are checked against their SHA-256 and the audio is decoded; after
/// it the stems are written as WAV files. Those two steps run on a worker
/// thread. One job at a time.
///
/// Nothing here makes the UI thread wait: cancel() tells the running step to
/// stop and ends the helper, and both finish by themselves.
class StemSeparator : public QObject {
    Q_OBJECT

public:
    explicit StemSeparator(QObject* parent = nullptr);
    ~StemSeparator() override;

    /// The helper next to the app's executable; empty when it is missing.
    [[nodiscard]] static QString program();
    /// Where downloaded models are kept.
    [[nodiscard]] static QString modelFolder();
    /// The file of stem `stem` of `job`.
    [[nodiscard]] static QString stemPath(const StemJob& job, const QString& stem);

    [[nodiscard]] bool isRunning() const { return running_; }
    /// Starts `job`; finished() or failed() follows. Does nothing while one runs.
    void start(const StemJob& job);
    /// Gives the running job up: neither finished() nor failed() follows.
    void cancel();

signals:
    void progress(double fraction, const QString& stage);
    void finished(const QList<vloomdaw::StemFile>& stems);
    void failed(const QString& reason);

private:
    void prepared(int run, std::int64_t frames, const QString& error);
    void readOutput();
    void helperFinished(bool crashed, int exitCode);
    void converted(int run, const QList<StemFile>& stems, const QString& error);
    void fail(const QString& reason);
    /// What a step on the worker thread is told and gives back. The thread
    /// writes it; the UI thread reads it once the thread has finished.
    struct Step {
        std::atomic<bool> cancelled{false}; ///< set by the UI thread: stop at the next check
        QString error;
        std::int64_t frames = 0;
        QList<StemFile> stems;
    };
    /// Runs `work` on a thread of its own, then `then` on this thread (not
    /// when this object is gone by then).
    void runOnWorker(std::function<void()> work, std::function<void()> then);

    StemJob job_;
    /// The helper's input and output. Shared with the step or helper using
    /// it, so it outlives a job that was given up until they have stopped.
    std::shared_ptr<QTemporaryDir> work_;
    QProcess* process_ = nullptr;
    std::shared_ptr<Step> step_; ///< the step running now, if any
    QList<QPointer<QThread>> workers_; ///< threads that may still be running
    QByteArray pending_; ///< helper output not yet a whole line
    QString helperError_;
    std::int64_t frames_ = 0;
    int stems_ = 0; ///< how many the helper says it wrote
    int run_ = 0; ///< counts jobs: results of an earlier one are dropped
    bool running_ = false;
};

} // namespace vloomdaw
