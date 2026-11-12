// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/PluginProber.hpp"

#include "core/PluginMetadata.hpp"
#include "ui/FilePaths.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>

namespace vloomdaw {

namespace {
constexpr qsizetype kMaxOutput = 1 << 20; ///< a chatty plugin cannot fill memory

} // namespace

PluginProber::PluginProber(QObject* parent)
    : QObject(parent)
    , process_(new QProcess(this))
    , timeout_(new QTimer(this))
{
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    process_->setStandardInputFile(QProcess::nullDevice());
    process_->setStandardErrorFile(QProcess::nullDevice());
    // Not the app's working directory (could be Downloads): nothing there is
    // looked at when a plugin loads its DLLs.
    process_->setWorkingDirectory(QDir::tempPath());
    connect(process_, &QProcess::readyReadStandardOutput, this, [this] {
        const QByteArray chunk = process_->readAllStandardOutput();
        if (output_.size() < kMaxOutput)
            output_.append(chunk.left(kMaxOutput - output_.size()));
    });
    connect(process_, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        timeout_->stop();
        output_.append(process_->readAllStandardOutput().left(kMaxOutput - output_.size()));
        QString failure;
        if (timedOut_)
            failure = tr("did not answer within %1 s").arg(kTimeoutMs / 1000);
        else if (status == QProcess::CrashExit)
            failure = tr("crashed while loading");
        else if (exitCode != 0 && !output_.contains("@@vloom\terror"))
            failure = tr("exited with code %1").arg(exitCode);
        moduleDone(output_, failure);
    });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            timeout_->stop();
            moduleDone({}, tr("the plugin probe could not start"));
        }
    });
    timeout_->setSingleShot(true);
    timeout_->setInterval(kTimeoutMs);
    connect(timeout_, &QTimer::timeout, this, [this] {
        timedOut_ = true;
        process_->kill(); // finished() follows
    });
}

PluginProber::~PluginProber()
{
    process_->disconnect(this);
    if (process_->state() != QProcess::NotRunning) {
        process_->kill();
        process_->waitForFinished(3000);
    }
}

QString PluginProber::probeProgram()
{
#if defined(Q_OS_WIN)
    const QString name = QStringLiteral("vloomdaw-plugin-probe.exe");
#else
    const QString name = QStringLiteral("vloomdaw-plugin-probe");
#endif
    const QString path = QDir(QCoreApplication::applicationDirPath()).filePath(name);
    return QFileInfo::exists(path) ? path : QString();
}

void PluginProber::start(std::vector<PluginInfo> modules, std::vector<PluginInfo> previous)
{
    modules_ = std::move(modules);
    previous_ = std::move(previous);
    results_.clear();
    index_ = 0;
    running_ = true;
    stopping_ = false;
    next();
}

void PluginProber::stop() { stopping_ = true; }

void PluginProber::next()
{
    const QString program = probeProgram();
    while (index_ < modules_.size()) {
        const PluginInfo& module = modules_[index_];
        emit progress(
            static_cast<int>(index_), static_cast<int>(modules_.size()), QString::fromStdString(module.name));

        // Unchanged since the last scan: reuse what it found (every plugin of the module).
        bool cached = false;
        for (const PluginInfo& old : previous_) {
            // Only successes are reused: a failure may have been transient
            // (antivirus, a slow licence check), so it is tried again.
            if (old.probe == PluginInfo::Probe::Ok && sameModule(old, module)) {
                results_.push_back(old);
                cached = true;
            }
        }
        if (cached) {
            ++index_;
            continue;
        }
        if (stopping_ || program.isEmpty()) {
            results_.push_back(module); // left unprobed
            ++index_;
            continue;
        }

        output_.clear();
        timedOut_ = false;
        const QString format
            = module.format == PluginFormat::Clap ? QStringLiteral("--clap") : QStringLiteral("--vst3");
        process_->start(program, {format, toQString(module.path)});
        timeout_->start();
        return; // continues in moduleDone()
    }
    running_ = false;
    emit progress(static_cast<int>(modules_.size()), static_cast<int>(modules_.size()), QString());
    emit finished(results_);
}

void PluginProber::moduleDone(const QByteArray& output, const QString& failure)
{
    if (index_ >= modules_.size())
        return;
    ProbeOutput probe
        = parseProbeOutput(std::string_view(output.constData(), static_cast<std::size_t>(output.size())));
    if (!failure.isEmpty()) {
        probe.ok = false;
        probe.error = failure.toStdString();
    }
    for (PluginInfo& info : applyProbe(modules_[index_], probe))
        results_.push_back(std::move(info));
    ++index_;
    // Let the event loop breathe between modules.
    QTimer::singleShot(0, this, &PluginProber::next);
}

} // namespace vloomdaw
