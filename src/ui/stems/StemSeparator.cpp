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

/// Lower-case hex SHA-256 of the file, empty when it cannot be read.
QByteArray sha256(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file) ? hash.result().toHex() : QByteArray();
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
    joinWorker();
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

void StemSeparator::runOnWorker(std::function<void()> work)
{
    joinWorker(); // an abandoned job's step may still be finishing
    worker_ = QThread::create(std::move(work));
    worker_->setParent(this);
    worker_->start(QThread::LowPriority);
}

void StemSeparator::joinWorker()
{
    if (worker_ == nullptr)
        return;
    worker_->wait();
    delete worker_;
    worker_ = nullptr;
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
    joinWorker();
    job_ = job;
    running_ = true;
    frames_ = 0;
    stems_ = 0;
    helperError_.clear();
    pending_.clear();
    work_ = std::make_unique<QTemporaryDir>();
    if (!work_->isValid() || !QDir(work_->path()).mkpath(QStringLiteral("out"))) {
        fail(tr("No room for temporary files."));
        return;
    }
    emit progress(0.0, tr("Reading the audio…"));
    const int run = ++run_;
    const QString work = work_->path();
    runOnWorker([this, run, job, work, model] {
        QString error;
        std::int64_t frames = 0;
        // The files are checked every time: the helper's reader trusts them.
        for (const StemModelFile& file : model->files) {
            const QString path = QDir(job.modelFolder).filePath(QString::fromLatin1(file.name));
            if (sha256(path) != QByteArray(file.sha256.data(), static_cast<qsizetype>(file.sha256.size()))) {
                error = tr("The model's files are missing or damaged. Download it again in the model store.");
                break;
            }
        }
        if (error.isEmpty()) {
            std::string why;
            try {
                frames = writeStemInput(job.source.toStdString(), toPath(work + QStringLiteral("/in.f32")),
                    (work + QStringLiteral("/in.wav")).toStdString(), &why);
            } catch (const std::exception&) {
                why = "not enough memory";
            }
            if (frames <= 0)
                error = tr("Could not read the audio: %1.").arg(QString::fromStdString(why));
        }
        QMetaObject::invokeMethod(
            this, [this, run, frames, error] { prepared(run, frames, error); }, Qt::QueuedConnection);
    });
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
        QDir::toNativeSeparators(work_->path() + QStringLiteral("/out")),
        // One chunk of the song per core; hyper-threads do not help this kind of work.
        QStringLiteral("--threads"),
        QString::number(std::clamp(QThread::idealThreadCount() / 2, 1, kMaxThreads))};
    if (model->v3)
        arguments << QStringLiteral("--v3");
    for (const StemModelFile& file : model->files)
        arguments << QStringLiteral("--model")
                  << QDir::toNativeSeparators(
                         QDir(job_.modelFolder).absoluteFilePath(QString::fromLatin1(file.name)));

    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    process_->setStandardErrorFile(QProcess::nullDevice());
#if defined(Q_OS_WIN)
    // Below the app's own priority: the song keeps playing while it works.
    process_->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
        arguments->flags |= BELOW_NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW;
    });
#endif
    connect(process_, &QProcess::readyReadStandardOutput, this, &StemSeparator::readOutput);
    connect(process_, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        helperFinished(status != QProcess::NormalExit, exitCode);
    });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            helperFinished(true, -1);
    });
    emit progress(kPrepared, tr("Separating…"));
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
            emit progress(kPrepared + (kSeparated - kPrepared) * done, tr("Separating…"));
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
    const StemJob job = job_;
    const QString work = work_->path();
    const std::int64_t frames = frames_;
    runOnWorker([this, run, job, work, frames, model] {
        QString error;
        QList<StemFile> stems;
        if (!QDir().mkpath(job.outputFolder))
            error = tr("Could not create the folder for the stems.");
        for (std::size_t i = 0; i < model->stems.size() && error.isEmpty(); ++i) {
            const QString stem = QString::fromLatin1(model->stems[i]);
            const QString path = stemPath(job, stem);
            std::string why;
            bool ok = false;
            try {
                ok = writeStemWav(
                    toPath(work + QStringLiteral("/out/%1.f32").arg(i)), path.toStdString(), frames, &why);
            } catch (const std::exception&) {
                why = "not enough memory";
            }
            if (ok)
                stems.push_back({stem, path});
            else
                error = tr("Could not write the stems: %1.").arg(QString::fromStdString(why));
        }
        if (!error.isEmpty())
            for (const StemFile& written : stems)
                QFile::remove(written.path); // no half a set left behind
        QMetaObject::invokeMethod(
            this, [this, run, stems, error] { converted(run, stems, error); }, Qt::QueuedConnection);
    });
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
    joinWorker();
    work_.reset();
    emit failed(reason);
}

void StemSeparator::cancel()
{
    if (!running_)
        return;
    running_ = false;
    ++run_; // whatever is on its way belongs to no job now
    if (process_ != nullptr) {
        process_->disconnect(this);
        process_->kill();
        process_->waitForFinished(3000);
        process_->deleteLater();
        process_ = nullptr;
    }
    joinWorker();
    work_.reset();
}

} // namespace vloomdaw
