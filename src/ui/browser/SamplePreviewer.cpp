// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/browser/SamplePreviewer.hpp"

#include "ui/AudioDecoder.hpp"

#include <vloom/engine/Engine.hpp>
#include <vloom/engine/sample/SampleLoader.hpp>

#include <QThread>

#include <algorithm>
#include <cmath>
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

namespace {

/// The largest absolute sample of each of `points` slices of `sample`, all
/// channels: a waveform to draw.
std::vector<float> waveformOf(const vloom::SampleBuffer& sample, std::size_t points)
{
    const std::int64_t frames = sample.frames();
    std::vector<float> out;
    if (frames <= 0)
        return out;
    const auto count
        = static_cast<std::size_t>(std::min<std::int64_t>(frames, static_cast<std::int64_t>(points)));
    out.assign(count, 0.0f);
    for (int ch = 0; ch < sample.channels(); ++ch) {
        const float* data = sample.channel(ch);
        for (std::size_t p = 0; p < count; ++p) {
            const auto from = static_cast<std::int64_t>(p) * frames / static_cast<std::int64_t>(count);
            const auto to = static_cast<std::int64_t>(p + 1) * frames / static_cast<std::int64_t>(count);
            float peak = out[p];
            for (std::int64_t i = from; i < to; ++i)
                peak = std::max(peak, std::abs(data[i]));
            out[p] = std::min(peak, 1.0f);
        }
    }
    return out;
}

} // namespace

void SamplePreviewer::play(const QString& path) { fetch(path, true); }

void SamplePreviewer::show(const QString& path) { fetch(path, false); }

void SamplePreviewer::fetch(const QString& path, bool andPlay)
{
    const std::uint64_t request = andPlay ? request_.fetch_add(1) + 1 : 0;
    const std::uint64_t showRequest = showRequest_.fetch_add(1) + 1;
    if (andPlay)
        requested_ = path;
    if (const Entry* entry = cached(path)) {
        const Entry found = *entry; // start() may not keep the list as it is
        if (andPlay)
            start(path, found.sample);
        emit loaded(
            path, found.waveform, static_cast<double>(found.sample->frames()) / found.sample->sampleRate());
        return;
    }
    const double rate = sampleRate_;
    QMetaObject::invokeMethod(
        loader_,
        [this, request, showRequest, andPlay, path, rate] {
            // Worker thread. `this` outlives it: the destructor joins the thread.
            const bool playWanted = andPlay && request == request_;
            if (!playWanted && showRequest != showRequest_)
                return; // newer requests came in while this one waited
            std::string error;
            Entry entry{path, nullptr, nullptr};
            try {
                // MP3 and the other non-WAV types: their decoded copy.
                QString decodeError;
                const QString source = playableWav(path, rate, &decodeError);
                if (source.isEmpty())
                    error = decodeError.toStdString();
                else
                    entry.sample = vloom::loadSample(source.toStdString(), rate, &error);
                if (entry.sample != nullptr)
                    entry.waveform = std::make_shared<const std::vector<float>>(
                        waveformOf(*entry.sample, kWaveformPoints));
            } catch (const std::exception&) {
                entry.sample = nullptr;
                error = "not enough memory to load it";
            }
            QMetaObject::invokeMethod(
                this,
                [this, request, andPlay, entry, error] {
                    if (entry.sample == nullptr || entry.waveform == nullptr) {
                        if (andPlay && request == request_)
                            stop(); // the previous file must not go on as if it were this one
                        emit failed(entry.path, QString::fromStdString(error));
                        return;
                    }
                    remember(entry);
                    if (andPlay && request == request_)
                        start(entry.path, entry.sample);
                    emit loaded(entry.path, entry.waveform,
                        static_cast<double>(entry.sample->frames()) / entry.sample->sampleRate());
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void SamplePreviewer::start(const QString& path, const std::shared_ptr<const vloom::SampleBuffer>& sample)
{
    requested_ = path;
    started_.start();
    engine_.previewSample(sample);
}

void SamplePreviewer::stop()
{
    ++request_; // loads still on their way do not start
    requested_.clear();
    engine_.previewSample(nullptr);
}

QString SamplePreviewer::playingPath() const { return engine_.isPreviewPlaying() ? requested_ : QString(); }

double SamplePreviewer::playingSeconds() const
{
    if (!engine_.isPreviewPlaying() || requested_.isEmpty() || !started_.isValid())
        return -1.0;
    return static_cast<double>(started_.nsecsElapsed()) / 1e9;
}

const SamplePreviewer::Entry* SamplePreviewer::cached(const QString& path)
{
    for (auto it = cache_.begin(); it != cache_.end(); ++it) {
        if (it->path == path) {
            cache_.splice(cache_.begin(), cache_, it); // most recent first
            return &cache_.front();
        }
    }
    return nullptr;
}

void SamplePreviewer::remember(Entry entry)
{
    if (cached(entry.path) != nullptr)
        return;
    cache_.push_front(std::move(entry));
    while (cache_.size() > kCacheSize)
        cache_.pop_back();
}

} // namespace vloomdaw
