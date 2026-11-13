// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/browser/SamplePreviewer.hpp"

#include <vloom/engine/Engine.hpp>
#include <vloom/engine/sample/SampleLoader.hpp>

#include <QThread>

#include <exception>
#include <string>

namespace vloomdaw {

SamplePreviewer::SamplePreviewer(vloom::Engine& engine, QObject* parent)
    : QObject(parent)
    , engine_(engine)
{
    worker_ = new QThread(this);
    worker_->setObjectName(QStringLiteral("SamplePreview"));
    loader_ = new QObject; // no parent: it moves to the worker thread
    loader_->moveToThread(worker_);
    connect(worker_, &QThread::finished, loader_, &QObject::deleteLater);
    worker_->start(QThread::LowPriority);
}

SamplePreviewer::~SamplePreviewer()
{
    worker_->quit();
    worker_->wait(); // nothing reaches this object afterwards
}

void SamplePreviewer::setSampleRate(double sampleRate)
{
    if (sampleRate <= 0.0 || sampleRate == sampleRate_)
        return;
    sampleRate_ = sampleRate;
    cache_.clear(); // resampled for the old rate
    stop();
}

void SamplePreviewer::play(const QString& path)
{
    const std::uint64_t request = request_.fetch_add(1) + 1;
    requested_ = path;
    if (auto sample = cached(path)) {
        start(path, sample);
        return;
    }
    const std::string utf8 = path.toStdString();
    const double rate = sampleRate_;
    QMetaObject::invokeMethod(
        loader_,
        [this, request, path, utf8, rate] {
            // Worker thread. `this` outlives it: the destructor joins the thread.
            if (request != request_)
                return; // a newer request came in while this one waited
            std::string error;
            std::shared_ptr<const vloom::SampleBuffer> sample;
            try {
                sample = vloom::loadSample(utf8, rate, &error);
            } catch (const std::exception&) {
                error = "not enough memory to load it";
            }
            QMetaObject::invokeMethod(
                this,
                [this, request, path, sample, error] {
                    if (!sample) {
                        if (request == request_) {
                            // The previous file must not go on as if it were this one.
                            stop();
                            emit failed(path, QString::fromStdString(error));
                        }
                        return;
                    }
                    remember(path, sample);
                    if (request == request_)
                        start(path, sample);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void SamplePreviewer::start(const QString& path, const std::shared_ptr<const vloom::SampleBuffer>& sample)
{
    requested_ = path;
    engine_.previewSample(sample);
}

void SamplePreviewer::stop()
{
    ++request_; // loads still on their way do not start
    requested_.clear();
    engine_.previewSample(nullptr);
}

QString SamplePreviewer::playingPath() const { return engine_.isPreviewPlaying() ? requested_ : QString(); }

std::shared_ptr<const vloom::SampleBuffer> SamplePreviewer::cached(const QString& path)
{
    for (auto it = cache_.begin(); it != cache_.end(); ++it) {
        if (it->first == path) {
            cache_.splice(cache_.begin(), cache_, it); // most recent first
            return cache_.front().second;
        }
    }
    return nullptr;
}

void SamplePreviewer::remember(const QString& path, std::shared_ptr<const vloom::SampleBuffer> sample)
{
    if (cached(path))
        return;
    cache_.emplace_front(path, std::move(sample));
    while (cache_.size() > kCacheSize)
        cache_.pop_back();
}

} // namespace vloomdaw
