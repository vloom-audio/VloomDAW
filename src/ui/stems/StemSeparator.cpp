// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/stems/StemSeparator.hpp"

#include "audio/StemAudio.hpp"
#include "core/StemModels.hpp"
#include "ui/FilePaths.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <functional>
#include <string>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace vloomdaw {

namespace {

constexpr int kMaxThreads = 8;
// Where each step sits in the progress bar: the helper is nearly all of it.
constexpr double kPrepared = 0.03;
constexpr double kSeparated = 0.97;

/// Lower-case hex SHA-256 of the file; empty when it cannot be read or
/// `cancelled` is set meanwhile.
QByteArray sha256(const QString& path, const std::atomic<bool>& cancelled)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray block(1 << 20, Qt::Uninitialized);
    for (;;) {
        const qint64 got = file.read(block.data(), block.size());
        if (got < 0 || cancelled.load())
            return {};
        if (got == 0)
            break;
        hash.addData(QByteArrayView(block.constData(), got));
    }
    return hash.result().toHex();
}

} // namespace

StemSeparator::StemSeparator(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<QList<vloomdaw::StemFile>>();
}

StemSeparator::~StemSeparator()
{
    cancel();
    // The steps check their flag often, so this is short: at most one decode
    // of the audio. They use this object's files until they end.
    for (const QPointer<QThread>& worker : workers_)
        if (worker != nullptr)
            worker->wait();
}

QString StemSeparator::program()
{
#if defined(Q_OS_WIN)
    const QString name = QStringLiteral("vloomdaw-stems.exe");
#else
    const QString name = QStringLiteral("vloomdaw-stems");
#endif
    const QString path = QDir(QCoreApplication::applicationDirPath()).filePath(name);
    return QFileInfo::exists(path) ? path : QString();
}

QString StemSeparator::modelFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/models/stems");
}

QString StemSeparator::stemPath(const StemJob& job, const QString& stem)
{
    return QDir(job.outputFolder).filePath(QStringLiteral("%1 - %2.wav").arg(job.baseName, stem));
}

void StemSeparator::runOnWorker(std::function<void()> work, std::function<void()> then)
{
    // Nothing ever waits for the thread on the UI thread: it deletes itself
    // when its work is done, and `then` only runs if this object is still here.
    QThread* worker = QThread::create(std::move(work));
    connect(worker, &QThread::finished, this, std::move(then));
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    workers_.removeAll(QPointer<QThread>());
    workers_.push_back(worker);
    worker->start(QThread::LowPriority);
}

void StemSeparator::start(const StemJob& job)
{
    if (running_)
        return;
    const StemModel* model = findStemModel(job.modelId.toStdString());
    if (model == nullptr || program().isEmpty() || !stemSeparationSupported()) {
        emit failed(model == nullptr ? tr("The model is not known.")
                : program().isEmpty()
                ? tr("The stem separator is missing from this installation.")
                : tr("This processor is too old for the stem separator (it needs AVX2)."));
        return;
    }
    job_ = job;
    running_ = true;
    frames_ = 0;
    stems_ = 0;
    helperError_.clear();
    pending_.clear();
    work_ = std::make_shared<QTemporaryDir>();
    if (!work_->isValid() || !QDir(work_->path()).mkpath(QStringLiteral("out"))) {
        fail(tr("No room for temporary files."));
        return;
    }
    emit progress(0.0, tr("Reading the audio…"));
    const int run = ++run_;
    step_ = std::make_shared<Step>();
    // The step keeps the folder it works in alive, also once the job is given up.
    runOnWorker(
        [step = step_, folder = work_, job, model] {
            const QString work = folder->path();
            // The files are checked every time: the helper's reader trusts them.
            for (const StemModelFile& file : model->files) {
                const QString path = QDir(job.modelFolder).filePath(QString::fromLatin1(file.name));
                const QByteArray expected(file.sha256.data(), static_cast<qsizetype>(file.sha256.size()));
                if (sha256(path, step->cancelled) != expected) {
                    step->error = tr(
                        "The model's files are missing or damaged. Download it again in the model store.");
                    return;
                }
            }
            if (step->cancelled.load())
                return;
            std::string why;
            try {
                step->frames
                    = writeStemInput(job.source.toStdString(), toPath(work + QStringLiteral("/in.f32")),
                        (work + QStringLiteral("/in.wav")).toStdString(), &why);
            } catch (const std::exception&) {
                why = "not enough memory";
            }
            if (step->frames <= 0)
                step->error = tr("Could not read the audio: %1.").arg(QString::fromStdString(why));
        },
        [this, run, step = step_] { prepared(run, step->frames, step->error); });
}

void StemSeparator::prepared(int run, std::int64_t frames, const QString& error)
{
    if (run != run_ || !running_)
        return;
    if (!error.isEmpty()) {
        fail(error);
        return;
    }
    frames_ = frames;
    const StemModel* model = findStemModel(job_.modelId.toStdString());
    QStringList arguments{QStringLiteral("--in"),
        QDir::toNativeSeparators(work_->path() + QStringLiteral("/in.f32")), QStringLiteral("--out"),
        QDir::toNativeSeparators(work_->path() + QStringLiteral("/out"))};
    if (model->engine == StemModel::Engine::Onnx) {
        // ONNX Runtime: the graphics card for the models that can, when the
        // user lets it; it falls back to the processor by itself.
        arguments << QStringLiteral("--onnx") << QStringLiteral("--device")
                  << (model->gpu && job_.useGpu ? QStringLiteral("auto") : QStringLiteral("cpu"));
        arguments << QStringLiteral("--overlap") << QString::number(std::clamp(job_.overlapPercent, 25, 75));
        if (model->mdxFft != 0)
            arguments << QStringLiteral("--mdx-fft") << QString::number(model->mdxFft)
                      << QStringLiteral("--mdx-gain") << QString::number(model->mdxGain);
    } else {
        // demucs.cpp: one chunk of the song per core; hyper-threads do not help this kind of work.
        arguments << QStringLiteral("--threads")
                  << QString::number(std::clamp(QThread::idealThreadCount() / 2, 1, kMaxThreads));
        if (model->v3)
            arguments << QStringLiteral("--v3");
    }
    for (const StemModelFile& file : model->files)
        arguments << QStringLiteral("--model")
                  << QDir::toNativeSeparators(
                         QDir(job_.modelFolder).absoluteFilePath(QString::fromLatin1(file.name)));

    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    process_->setStandardErrorFile(QProcess::nullDevice());
#if defined(Q_OS_WIN)
    // Below the app's own priority: the song keeps playing while it works.
    process_->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* creation) {
        creation->flags |= BELOW_NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW;
    });
#endif
    connect(process_, &QProcess::readyReadStandardOutput, this, &StemSeparator::readOutput);
    connect(process_, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        helperFinished(status != QProcess::NormalExit, exitCode);
    });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError problem) {
        if (problem == QProcess::FailedToStart)
            helperFinished(true, -1);
    });
    stage_ = tr("Separating…");
    emit progress(kPrepared, stage_);
    process_->start(program(), arguments, QIODevice::ReadOnly);
}

void StemSeparator::readOutput()
{
    if (process_ == nullptr)
        return;
    pending_ += process_->readAllStandardOutput();
    if (pending_.size() > (1 << 20)) // a helper gone wrong: do not keep its output
        pending_.clear();
    for (qsizetype end = pending_.indexOf('\n'); end >= 0; end = pending_.indexOf('\n')) {
        const QList<QByteArray> fields = pending_.left(end).trimmed().split('\t');
        pending_.remove(0, end + 1);
        if (fields.size() < 3 || fields[0] != "@@vloom")
            continue; // the library's own chatter
        if (fields[1] == "progress") {
            const double done = std::clamp(fields[2].toDouble(), 0.0, 1.0);
            emit progress(kPrepared + (kSeparated - kPrepared) * done, stage_);
        } else if (fields[1] == "device") {
            // "gpu", "cpu", or "cpu: <why the card was given up>" (not for the user as it is).
            const bool gpu = fields[2] == "gpu";
            const bool gaveUp = fields[2].startsWith("cpu:");
            stage_ = gpu ? tr("Separating on the graphics card…") : tr("Separating on the processor…");
            emit deviceChanged(
                gpu, gaveUp ? tr("The graphics card could not run it; the processor took over.") : QString());
        } else if (fields[1] == "done") {
            stems_ = fields[2].toInt();
        } else if (fields[1] == "error") {
            helperError_ = QString::fromUtf8(fields[2]).left(200);
        }
    }
}

void StemSeparator::helperFinished(bool crashed, int exitCode)
{
    if (process_ == nullptr)
        return;
    readOutput();
    process_->deleteLater();
    process_ = nullptr;
    if (!running_)
        return;
    const StemModel* model = findStemModel(job_.modelId.toStdString());
    if (crashed || exitCode != 0 || stems_ != static_cast<int>(model->stems.size())) {
        fail(!helperError_.isEmpty() ? tr("The separator failed: %1.").arg(helperError_)
                : crashed ? tr("The separator stopped unexpectedly (it may have run out of memory).")
                          : tr("The separator failed."));
        return;
    }
    emit progress(kSeparated, tr("Writing the stems…"));
    const int run = run_;
    step_ = std::make_shared<Step>();
    runOnWorker(
        [step = step_, folder = work_, job = job_, frames = frames_, model] {
            const QString work = folder->path();
            if (!QDir().mkpath(job.outputFolder))
                step->error = tr("Could not create the folder for the stems.");
            for (std::size_t i = 0; i < model->stems.size() && step->error.isEmpty(); ++i) {
                if (step->cancelled.load())
                    step->error = tr("Cancelled.");
                else {
                    const QString stem = QString::fromLatin1(model->stems[i]);
                    const QString path = stemPath(job, stem);
                    std::string why;
                    bool ok = false;
                    try {
                        ok = writeStemWav(toPath(work + QStringLiteral("/out/%1.f32").arg(i)),
                            path.toStdString(), frames, &why);
                    } catch (const std::exception&) {
                        why = "not enough memory";
                    }
                    if (ok)
                        step->stems.push_back({stem, path});
                    else
                        step->error = tr("Could not write the stems: %1.").arg(QString::fromStdString(why));
                }
            }
            if (!step->error.isEmpty())
                for (const StemFile& written : step->stems)
                    QFile::remove(written.path); // no half a set left behind
        },
        [this, run, step = step_] { converted(run, step->stems, step->error); });
}

void StemSeparator::converted(int run, const QList<StemFile>& stems, const QString& error)
{
    if (run != run_ || !running_)
        return;
    if (!error.isEmpty()) {
        fail(error);
        return;
    }
    running_ = false;
    work_.reset();
    emit progress(1.0, QString());
    emit finished(stems);
}

void StemSeparator::fail(const QString& reason)
{
    running_ = false;
    step_.reset();
    work_.reset();
    emit failed(reason);
}

void StemSeparator::cancel()
{
    if (!running_)
        return;
    running_ = false;
    ++run_; // whatever is on its way belongs to no job now
    if (step_ != nullptr)
        step_->cancelled.store(true); // the step stops at its next check, by itself
    step_.reset();
    if (process_ != nullptr) {
        // Ended, not waited for: it goes away, and its folder with it, once
        // the system says it has stopped.
        QProcess* helper = process_;
        process_ = nullptr;
        helper->disconnect(this);
        helper->setParent(nullptr);
        connect(helper, &QProcess::finished, helper, [helper, folder = work_] {
            (void)folder; // kept until here
            helper->deleteLater();
        });
        connect(helper, &QProcess::errorOccurred, helper,
            [helper, folder = work_](QProcess::ProcessError problem) {
                (void)folder;
                if (problem == QProcess::FailedToStart)
                    helper->deleteLater();
            });
        if (helper->state() == QProcess::NotRunning)
            helper->deleteLater();
        else
            helper->kill();
    }
    work_.reset();
}

} // namespace vloomdaw
