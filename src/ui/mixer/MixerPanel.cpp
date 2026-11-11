// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/mixer/MixerPanel.hpp"

#include "ui/Theme.hpp"
#include "ui/mixer/MixerStrip.hpp"
#include "ui/widgets/DetachButton/DetachButton.hpp"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace vloomdaw {

MixerPanel::MixerPanel(std::size_t insertCount, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("mixerPanel"));
    setAttribute(Qt::WA_StyledBackground);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 10);
    layout->setSpacing(6);

    // Header: title, detach / dock and close. A widget of its own, so a
    // detached window can be dragged by it.
    auto* headerBox = new QWidget(this);
    headerBox->setObjectName(QStringLiteral("mixerHeader"));
    header_ = new QHBoxLayout(headerBox);
    header_->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel(tr("MIXER"), headerBox);
    title->setObjectName(QStringLiteral("panelTitle"));
    header_->addWidget(title);
    header_->addStretch();
    detach_ = new DetachButton(tr("mixer"), headerBox);
    connect(detach_, &QToolButton::clicked, this, &MixerPanel::detachRequested);
    header_->addWidget(detach_);
    close_ = new QToolButton(headerBox);
    close_->setObjectName(QStringLiteral("panelClose"));
    close_->setIcon(Theme::icon(QStringLiteral("window-close")));
    close_->setIconSize(QSize(14, 14));
    close_->setFocusPolicy(Qt::NoFocus);
    close_->setToolTip(tr("Close the mixer (F9)"));
    close_->setAccessibleName(tr("Close the mixer"));
    connect(close_, &QToolButton::clicked, this, &MixerPanel::closeRequested);
    header_->addWidget(close_);
    layout->addWidget(headerBox);

    // Master stays put; the inserts scroll beside it.
    auto* body = new QHBoxLayout;
    body->setSpacing(8);

    auto* master = new MixerStrip(MixerStrip::Kind::Master, tr("Master"), tr("M"), this);
    connectStrip(master, 0);
    strips_.append(master);
    body->addWidget(master);

    auto* inserts = new QWidget;
    inserts->setObjectName(QStringLiteral("mixerInserts"));
    auto* row = new QHBoxLayout(inserts);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    for (std::size_t i = 1; i <= insertCount; ++i) {
        auto* strip = new MixerStrip(
            MixerStrip::Kind::Insert, tr("Insert %1").arg(i), QString::number(i), inserts);
        connectStrip(strip, i);
        strips_.append(strip);
        row->addWidget(strip);
    }
    row->addStretch();

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("mixerScroll"));
    scroll->setWidget(inserts);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    body->addWidget(scroll, 1);

    layout->addLayout(body, 1);
}

void MixerPanel::connectStrip(MixerStrip* strip, std::size_t track)
{
    connect(strip, &MixerStrip::faderChanged, this, [this, track](double p) { emit faderChanged(track, p); });
    connect(strip, &MixerStrip::panChanged, this, [this, track](double p) { emit panChanged(track, p); });
    connect(strip, &MixerStrip::muteToggled, this, [this, track](bool on) { emit muteToggled(track, on); });
    connect(strip, &MixerStrip::soloToggled, this, [this, track](bool on) { emit soloToggled(track, on); });
}

LevelMeter* MixerPanel::meter(std::size_t track) const
{
    return track < trackCount() ? strips_[static_cast<qsizetype>(track)]->meter() : nullptr;
}

void MixerPanel::setDetached(bool detached)
{
    detach_->setDetached(detached);
    // A detached window has its own close button.
    close_->setVisible(!detached);
}

void MixerPanel::addHeaderWidget(QWidget* widget) { header_->addWidget(widget); }

void MixerPanel::setSilenced(std::size_t track, bool silenced)
{
    if (track < trackCount())
        strips_[static_cast<qsizetype>(track)]->setSilenced(silenced);
}

} // namespace vloomdaw
