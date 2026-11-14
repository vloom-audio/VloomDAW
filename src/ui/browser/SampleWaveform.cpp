// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/browser/SampleWaveform.hpp"

#include "ui/Theme.hpp"

#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace vloomdaw {

namespace {

constexpr int kWaveHeight = 48;

/// "0:03.2", "4:01.0": minutes, seconds and tenths.
QString lengthText(double seconds)
{
    const auto tenths = static_cast<long long>(std::llround(std::max(0.0, seconds) * 10.0));
    return QStringLiteral("%1:%2.%3")
        .arg(tenths / 600)
        .arg((tenths / 10) % 60, 2, 10, QLatin1Char('0'))
        .arg(tenths % 10);
}

} // namespace

SampleWaveform::SampleWaveform(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("sampleWaveform"));
    setAttribute(Qt::WA_StyledBackground);

    play_ = new QToolButton(this);
    play_->setObjectName(QStringLiteral("panelButton"));
    play_->setProperty("iconOnly", true);
    play_->setFocusPolicy(Qt::NoFocus);
    connect(play_, &QToolButton::clicked, this, [this] {
        if (path_.isEmpty())
            return;
        if (playing_ >= 0.0)
            emit stopRequested();
        else
            emit playRequested(path_);
    });

    name_ = new QLabel(this);
    name_->setObjectName(QStringLiteral("sampleWaveformName"));
    name_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); // cut, never widen the panel
    length_ = new QLabel(this);
    length_->setObjectName(QStringLiteral("panelHint"));

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    row->addWidget(play_);
    row->addWidget(name_, 1);
    row->addWidget(length_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    layout->addLayout(row);
    layout->addSpacing(kWaveHeight);

    setCursor(Qt::PointingHandCursor);
    updateButton();
    hide();
}

void SampleWaveform::setFile(const QString& path)
{
    if (path == path_)
        return;
    path_ = path;
    waveform_.reset();
    seconds_ = 0.0;
    playing_ = -1.0;
    message_ = tr("Loading…");
    setVisible(!path.isEmpty());
    updateLabels();
    updateButton();
    update();
}

void SampleWaveform::setWaveform(
    const QString& path, std::shared_ptr<const std::vector<float>> waveform, double seconds)
{
    if (path != path_)
        return; // an older file, loaded late
    waveform_ = std::move(waveform);
    seconds_ = seconds;
    message_.clear();
    updateLabels();
    update();
}

void SampleWaveform::setFailed(const QString& path, const QString& error)
{
    if (path != path_)
        return;
    waveform_.reset();
    message_ = tr("Cannot be read: %1").arg(error);
    update();
}

void SampleWaveform::setPlaying(double seconds)
{
    if (seconds_ > 0.0 && seconds > seconds_)
        seconds = -1.0; // past the end: the fade-out is all that is left
    const bool was = playing_ >= 0.0;
    const bool now = seconds >= 0.0;
    const auto pixel
        = [this](double at) { return seconds_ > 0.0 ? std::lround(at / seconds_ * waveRect().width()) : 0L; };
    const bool moved = was != now || (now && pixel(seconds) != pixel(playing_));
    playing_ = seconds;
    if (was != now)
        updateButton();
    if (moved)
        update(waveRect());
}

QRect SampleWaveform::waveRect() const
{
    const QRect r = contentsRect().adjusted(6, 6, -6, -6);
    return {r.left(), r.bottom() - kWaveHeight + 1, r.width(), kWaveHeight};
}

void SampleWaveform::updateButton()
{
    const bool playing = playing_ >= 0.0;
    play_->setIcon(Theme::icon(playing ? QStringLiteral("stop") : QStringLiteral("play")));
    play_->setToolTip(playing ? tr("Stop the preview") : tr("Play this file"));
    play_->setAccessibleName(play_->toolTip());
}

void SampleWaveform::updateLabels()
{
    const QString name = QFileInfo(path_).fileName();
    name_->setText(
        QFontMetrics(name_->font()).elidedText(name, Qt::ElideMiddle, std::max(20, name_->width())));
    name_->setToolTip(QDir::toNativeSeparators(path_));
    length_->setText(waveform_ != nullptr ? lengthText(seconds_) : QString());
}

void SampleWaveform::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateLabels(); // the name is cut to the room it has
}

void SampleWaveform::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)
        updateButton(); // icons follow the theme
}

void SampleWaveform::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    const Theme::Colors& c = Theme::colors();
    QPainter p(this);
    const QRect wave = waveRect();
    p.fillRect(wave, c.base);

    if (waveform_ == nullptr || waveform_->empty()) {
        p.setPen(c.textDim);
        p.drawText(wave.adjusted(6, 0, -6, 0), Qt::AlignCenter | Qt::TextWordWrap, message_);
        return;
    }
    // One line per pixel column, mirrored round the middle; the part already
    // played in the accent colour.
    const auto& points = *waveform_;
    const double mid = wave.center().y() + 0.5;
    const double half = wave.height() / 2.0 - 1.0;
    const int playedX = playing_ >= 0.0 && seconds_ > 0.0
        ? wave.left() + static_cast<int>(std::lround(playing_ / seconds_ * wave.width()))
        : wave.left() - 1;
    for (int x = wave.left(); x <= wave.right(); ++x) {
        const auto from = static_cast<std::size_t>(
            static_cast<double>(x - wave.left()) / wave.width() * static_cast<double>(points.size()));
        const auto to = std::max(from + 1,
            static_cast<std::size_t>(static_cast<double>(x - wave.left() + 1) / wave.width()
                * static_cast<double>(points.size())));
        float peak = 0.0f;
        for (std::size_t i = from; i < to && i < points.size(); ++i)
            peak = std::max(peak, points[i]);
        const double h = std::max(0.5, static_cast<double>(peak) * half);
        p.setPen(x < playedX ? c.accent : c.textDim);
        p.drawLine(QPointF(x + 0.5, mid - h), QPointF(x + 0.5, mid + h));
    }
    if (playing_ >= 0.0)
        p.fillRect(
            QRect(std::clamp(playedX, wave.left(), wave.right()), wave.top(), 1, wave.height()), c.text);
}

void SampleWaveform::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && waveRect().contains(event->position().toPoint())
        && !path_.isEmpty()) {
        emit playRequested(path_); // from the start
        return;
    }
    QWidget::mousePressEvent(event);
}

} // namespace vloomdaw
