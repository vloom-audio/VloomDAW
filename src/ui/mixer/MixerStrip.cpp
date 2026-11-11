// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/mixer/MixerStrip.hpp"

#include "core/Decibels.hpp"
#include "ui/widgets/Fader/Fader.hpp"
#include "ui/widgets/Knob/Knob.hpp"
#include "ui/widgets/LevelMeter/LevelMeter.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace vloomdaw {

namespace {

constexpr int kInsertWidth = 64;
constexpr int kMasterWidth = 76;

QString panText(double percent)
{
    if (std::abs(percent) < 0.5)
        return MixerStrip::tr("Centre");
    return MixerStrip::tr("%1% %2")
        .arg(QString::number(std::abs(percent), 'f', 0), percent < 0 ? MixerStrip::tr("left") : MixerStrip::tr("right"));
}

} // namespace

MixerStrip::MixerStrip(Kind kind, const QString& name, const QString& number, QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("mixerStrip"));
    setProperty("master", kind == Kind::Master); // styled differently in Theme
    setFixedWidth(kind == Kind::Master ? kMasterWidth : kInsertWidth);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 6, 5, 6);
    layout->setSpacing(6);

    auto* nameLabel = new QLabel(name, this);
    nameLabel->setObjectName(QStringLiteral("stripName"));
    nameLabel->setAlignment(Qt::AlignCenter);
    nameLabel->setToolTip(name);
    layout->addWidget(nameLabel);

    pan_ = new Knob(tr("PAN"), -100.0, 100.0, 0.0, this);
    pan_->setDescription(tr("Pan"));
    pan_->setValueText(panText);
    connect(pan_, &Knob::valueChanged, this, [this](double percent) { emit panChanged(percent / 100.0); });
    layout->addWidget(pan_, 0, Qt::AlignHCenter);

    auto* levels = new QHBoxLayout;
    levels->setSpacing(4);
    meter_ = new LevelMeter(this);
    fader_ = new Fader(this);
    connect(fader_, &Fader::valueChanged, this, [this](double position) {
        showDb(position);
        emit faderChanged(position);
    });
    levels->addStretch();
    levels->addWidget(meter_);
    levels->addWidget(fader_);
    levels->addStretch();
    layout->addLayout(levels, 1);

    db_ = new QLabel(this);
    db_->setObjectName(QStringLiteral("stripDb"));
    db_->setAlignment(Qt::AlignCenter);
    layout->addWidget(db_);
    showDb(fader_->value());

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(3);
    const auto addToggle = [&](const QString& text, const QString& objectName, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setObjectName(objectName);
        b->setCheckable(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        buttons->addWidget(b);
        return b;
    };
    auto* mute = addToggle(QStringLiteral("M"), QStringLiteral("muteButton"), tr("Mute %1").arg(name));
    connect(mute, &QToolButton::toggled, this, &MixerStrip::muteToggled);
    if (kind == Kind::Insert) {
        auto* solo = addToggle(QStringLiteral("S"), QStringLiteral("soloButton"), tr("Solo %1").arg(name));
        connect(solo, &QToolButton::toggled, this, &MixerStrip::soloToggled);
    }
    layout->addLayout(buttons);

    auto* numberLabel = new QLabel(number, this);
    numberLabel->setObjectName(QStringLiteral("stripNumber"));
    numberLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(numberLabel);
}

void MixerStrip::showDb(double position) { db_->setText(QString::fromStdString(formatDb(faderToGain(position)))); }

void MixerStrip::setSilenced(bool silenced)
{
    if (property("silenced").toBool() == silenced)
        return;
    setProperty("silenced", silenced);
    // Re-apply the style sheet so the [silenced="true"] rule takes effect.
    style()->unpolish(this);
    style()->polish(this);
    update();
}

} // namespace vloomdaw
