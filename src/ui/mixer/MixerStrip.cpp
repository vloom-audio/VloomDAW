// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/mixer/MixerStrip.hpp"

#include "core/Decibels.hpp"
#include "ui/widgets/Fader/Fader.hpp"
#include "ui/widgets/Knob/Knob.hpp"
#include "ui/widgets/LevelMeter/LevelMeter.hpp"

#include <QEvent>
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
    return MixerStrip::tr("%1% %2").arg(QString::number(std::abs(percent), 'f', 0),
        percent < 0 ? MixerStrip::tr("left") : MixerStrip::tr("right"));
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

    name_ = new QLabel(this);
    name_->setObjectName(QStringLiteral("stripName"));
    name_->setAlignment(Qt::AlignCenter);
    layout->addWidget(name_);
    // Takes the strip's width whatever the text: a long name is cut, not widening it.
    name_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    name_->installEventFilter(this); // re-cut on resize / font; inserts: double-click to rename

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
    mute_ = addToggle(QStringLiteral("M"), QStringLiteral("muteButton"), QString());
    connect(mute_, &QToolButton::toggled, this, &MixerStrip::muteToggled);
    if (kind == Kind::Insert) {
        solo_ = addToggle(QStringLiteral("S"), QStringLiteral("soloButton"), QString());
        connect(solo_, &QToolButton::toggled, this, &MixerStrip::soloToggled);
    }
    layout->addLayout(buttons);

    number_ = new QLabel(this);
    number_->setObjectName(QStringLiteral("stripNumber"));
    number_->setAlignment(Qt::AlignCenter);
    layout->addWidget(number_);
    setLabel(name, number);
}

bool MixerStrip::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == name_) {
        switch (event->type()) {
        case QEvent::MouseButtonDblClick:
            if (solo_ != nullptr) { // inserts only
                emit renameRequested();
                return true;
            }
            break;
        case QEvent::Resize:
        case QEvent::FontChange:
        case QEvent::StyleChange:
        case QEvent::Polish:
            elideName();
            break;
        default:
            break;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void MixerStrip::elideName()
{
    const QString shown
        = name_->fontMetrics().elidedText(fullName_, Qt::ElideRight, name_->contentsRect().width());
    if (shown != name_->text())
        name_->setText(shown);
}

void MixerStrip::setLabel(const QString& name, const QString& number)
{
    // The strip is narrow: a long name is cut with "…"; the tooltip has it all.
    fullName_ = name;
    elideName();
    name_->setToolTip(solo_ != nullptr ? tr("%1\nDouble-click to rename").arg(name) : name);
    number_->setText(number);
    mute_->setToolTip(tr("Mute %1").arg(name));
    mute_->setAccessibleName(mute_->toolTip());
    if (solo_ != nullptr) {
        solo_->setToolTip(tr("Solo %1").arg(name));
        solo_->setAccessibleName(solo_->toolTip());
    }
}

void MixerStrip::showDb(double position)
{
    db_->setText(QString::fromStdString(formatDb(faderToGain(position))));
}

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
