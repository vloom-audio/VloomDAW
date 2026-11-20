// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/SampleParts.hpp"

#include "core/ProjectFile.hpp"
#include "ui/AudioDecoder.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"

#include <vloom/engine/plugin/AudioProcessor.hpp>
#include <vloom/engine/sample/WavReader.hpp>

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QThread>

#include <algorithm>
#include <exception>
#include <tuple>
#include <utility>

namespace vloomdaw {

namespace {

/// Reads `path` as the engine does and takes its peaks. Worker thread.
std::shared_ptr<const SampleWaveforms::Waveform> readWaveform(const QString& path)
{
    // A project may name any path: a network one would make Windows
    // connect to that host, so none is read (as MainWindow's resolver).
    if (path.isEmpty() || isNetworkPath(path.toStdString()))
        return nullptr;
    try {
        const QString wav = playableWav(path, 0.0); // the file's own rate, as the engine loads it
        if (wav.isEmpty())
            return nullptr;
        const vloom::WavReadResult read = vloom::readWavFile(wav.toStdString());
        if (!read.buffer || read.buffer->empty())
            return nullptr;
        const vloom::SampleBuffer& buffer = *read.buffer;
        std::vector<const float*> channels;
        for (int channel = 0; channel < buffer.channels(); ++channel)
            channels.push_back(buffer.channel(channel));
        auto waveform = std::make_shared<SampleWaveforms::Waveform>();
        waveform->peaks = samplePeaks(channels, buffer.frames(), SampleWaveforms::kColumns);
        waveform->seconds = buffer.seconds();
        return waveform;
    } catch (const std::exception&) {
        return nullptr; // out of memory: no picture, the sample still plays
    }
}

} // namespace

SampleWaveforms::SampleWaveforms(int count, QObject* parent)
    : QObject(parent)
    , slots_(static_cast<std::size_t>(std::max(count, 0)))
{
    worker_ = new QThread(this);
    worker_->setObjectName(QStringLiteral("SampleWaveforms"));
    reader_ = new QObject; // no parent: it moves to the worker
    reader_->moveToThread(worker_);
    connect(worker_, &QThread::finished, reader_, &QObject::deleteLater);
    worker_->start(QThread::LowPriority);
}

SampleWaveforms::~SampleWaveforms()
{
    worker_->quit();
    worker_->wait(); // nothing reaches this object afterwards
}

void SampleWaveforms::update(int slot, const vloom::SampleSlotInfo& info)
{
    if (slot < 0 || static_cast<std::size_t>(slot) >= slots_.size())
        return;
    Slot& each = slots_[static_cast<std::size_t>(slot)];
    const std::string path = info.loaded() ? info.path : std::string();
    if (path == each.path && info.frames == each.frames)
        return;
    each.path = path;
    each.frames = info.frames;
    const bool had = each.waveform != nullptr;
    each.waveform = nullptr;
    each.request = 0;
    if (path.empty()) {
        if (had)
            emit changed(slot);
        return;
    }
    const std::uint64_t request = ++requests_;
    each.request = request;
    emit changed(slot); // loading
    const QString file = QString::fromStdString(path);
    QMetaObject::invokeMethod(
        reader_,
        [this, slot, request, file] {
            // Worker thread. `this` outlives it: the destructor joins the thread.
            std::shared_ptr<const Waveform> read = readWaveform(file);
            QMetaObject::invokeMethod(
                this,
                [this, slot, request, waveform = std::move(read)] {
                    Slot& current = slots_[static_cast<std::size_t>(slot)];
                    if (current.request != request)
                        return; // the slot has another file by now
                    current.request = 0;
                    current.waveform = waveform;
                    emit changed(slot);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

std::shared_ptr<const SampleWaveforms::Waveform> SampleWaveforms::waveform(int slot) const
{
    return slot >= 0 && static_cast<std::size_t>(slot) < slots_.size()
        ? slots_[static_cast<std::size_t>(slot)].waveform
        : nullptr;
}

bool SampleWaveforms::loading(int slot) const
{
    return slot >= 0 && static_cast<std::size_t>(slot) < slots_.size()
        && slots_[static_cast<std::size_t>(slot)].request != 0;
}

void paintWaveform(QPainter& p, const QRectF& area, std::span<const PeakColumn> peaks, bool reversed,
    const QColor& colour, double spacing, double width)
{
    if (peaks.empty() || area.width() <= 0.0 || spacing <= 0.0)
        return;
    const double middle = area.center().y();
    const double half = area.height() / 2.0;
    QPainterPath strokes;
    for (double x = 0.0; x < area.width(); x += spacing) {
        double from = x / area.width();
        double to = std::min(1.0, (x + spacing) / area.width());
        if (reversed)
            std::tie(from, to) = std::pair{1.0 - to, 1.0 - from};
        const PeakColumn column = peakBetween(peaks, from, to);
        const double high = std::clamp(static_cast<double>(column.high), -1.0, 1.0);
        const double low = std::clamp(static_cast<double>(column.low), -1.0, 1.0);
        // At least a hair: silence shows as a line along the middle.
        const double top = std::min(middle - high * half, middle - 0.5);
        const double bottom = std::max(middle - low * half, middle + 0.5);
        strokes.moveTo(area.left() + x + spacing / 2.0, top);
        strokes.lineTo(area.left() + x + spacing / 2.0, bottom);
    }
    p.save();
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(colour, width, Qt::SolidLine, Qt::FlatCap));
    p.drawPath(strokes);
    p.restore();
}

SmallButton::SmallButton(const QString& text, QWidget* parent)
    : QAbstractButton(parent)
{
    setText(text);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover); // repainted as the mouse comes and goes
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize SmallButton::sizeHint() const
{
    const QFontMetrics metrics(editor_style::font(font(), 11, QFont::Bold));
    return {std::max(28, metrics.horizontalAdvance(text()) + 16), 24};
}

void SmallButton::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool lit = isDown() || underMouse() || hasFocus();
    p.setPen(QPen(lit ? editor_style::strong() : editor_style::line(), 1.0));
    p.setBrush(isDown() ? editor_style::faint() : QColor(Qt::transparent));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    p.setFont(editor_style::font(font(), 11, QFont::Bold));
    p.setPen(isEnabled() ? editor_style::text() : editor_style::muted());
    p.drawText(rect(), Qt::AlignCenter, text());
}

} // namespace vloomdaw
