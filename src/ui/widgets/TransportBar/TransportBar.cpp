// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/TransportBar/TransportBar.hpp"

#include "core/TransportLayout.hpp"
#include "ui/Theme.hpp"
#include "ui/widgets/KeyDisplay/KeyDisplay.hpp"
#include "ui/widgets/Knob/Knob.hpp"
#include "ui/widgets/PatternSelector/PatternSelector.hpp"
#include "ui/widgets/PerformanceMeter/PerformanceMeter.hpp"
#include "ui/widgets/PositionDisplay/PositionDisplay.hpp"
#include "ui/widgets/TempoDisplay/TempoDisplay.hpp"
#include "ui/widgets/ToolGroup/ToolGroup.hpp"
#include "ui/widgets/ViewButtons/ViewButtons.hpp"

#include <QAction>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLayout>
#include <QScreen>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <cmath>

namespace vloomdaw {

namespace {

constexpr int kGroupSpacing = 10; ///< between groups on the bar

QIcon icon(const QString& name) { return Theme::icon(name); }

/// "Play / pause (Space)" — tool tips always name the shortcut.
QString tipWithShortcut(const QString& text, const QKeySequence& shortcut)
{
    if (shortcut.isEmpty())
        return text;
    return QStringLiteral("%1 (%2)").arg(text, shortcut.toString(QKeySequence::NativeText));
}

} // namespace

TransportBar::TransportBar(QWidget* parent)
    : QToolBar(tr("Transport"), parent)
{
    setObjectName(QStringLiteral("transportBar"));
    setMovable(false);
    setFloatable(false);
    setIconSize(QSize(18, 18));

    rewindAction_ = makeAction(
        QStringLiteral("rewind"), tr("Go to Start"), tr("Return to start"), QKeySequence(Qt::Key_Home));
    connect(rewindAction_, &QAction::triggered, this, &TransportBar::rewindRequested);

    stepBackAction_ = makeAction(
        QStringLiteral("step-back"), tr("Rewind Bar"), tr("Step back one bar"), QKeySequence(Qt::Key_Comma));
    connect(stepBackAction_, &QAction::triggered, this, &TransportBar::stepBackwardRequested);

    stepForwardAction_ = makeAction(QStringLiteral("step-forward"), tr("Forward Bar"),
        tr("Step forward one bar"), QKeySequence(Qt::Key_Period));
    connect(stepForwardAction_, &QAction::triggered, this, &TransportBar::stepForwardRequested);

    goToEndAction_ = makeAction(
        QStringLiteral("skip-end"), tr("Go to End"), tr("Jump to project end"), QKeySequence(Qt::Key_End));
    connect(goToEndAction_, &QAction::triggered, this, &TransportBar::goToEndRequested);

    stopAction_ = makeAction(QStringLiteral("stop"), tr("Stop"), tr("Stop and return to start"),
        QKeySequence(Qt::SHIFT | Qt::Key_Space));
    connect(stopAction_, &QAction::triggered, this, &TransportBar::stopRequested);

    playAction_
        = makeAction(QStringLiteral("play"), tr("Play"), tr("Play / pause"), QKeySequence(Qt::Key_Space));
    playAction_->setCheckable(true);
    // The engine owns the play state: undo the automatic check toggle and let
    // setPlaying() reflect what actually happened.
    connect(playAction_, &QAction::triggered, this, [this] {
        playAction_->setChecked(playing_);
        emit playToggled();
    });

    recordAction_
        = makeAction(QStringLiteral("record"), tr("Record"), tr("Arm recording"), QKeySequence(Qt::Key_R));
    recordAction_->setCheckable(true);
    connect(recordAction_, &QAction::toggled, this, &TransportBar::recordArmedChanged);

    loopAction_ = makeAction(
        QStringLiteral("loop"), tr("Loop"), tr("Toggle cycle loop"), QKeySequence(Qt::Key_Slash));
    loopAction_->setCheckable(true);
    loopAction_->setChecked(true);
    connect(loopAction_, &QAction::toggled, this, &TransportBar::loopToggled);

    tapAction_ = new QAction(tr("TAP"), this);
    tapAction_->setShortcut(QKeySequence(Qt::Key_T));
    tapAction_->setToolTip(tipWithShortcut(tr("Tap tempo: tap along to the beat"), tapAction_->shortcut()));
    connect(tapAction_, &QAction::triggered, this, &TransportBar::tapTempoRequested);

    metronomeAction_ = makeAction(QStringLiteral("metronome"), tr("Metronome"), tr("Metronome on / off"),
        QKeySequence(Qt::CTRL | Qt::Key_M));
    metronomeAction_->setCheckable(true);
    metronomeAction_->setChecked(true);
    connect(metronomeAction_, &QAction::toggled, this, &TransportBar::metronomeToggled);

    // Pattern mode plays the current pattern on a loop; song mode plays the playlist.
    songModeAction_ = new QAction(tr("PAT"), this);
    songModeAction_->setCheckable(true);
    songModeAction_->setShortcut(QKeySequence(Qt::Key_L));
    songModeAction_->setToolTip(tipWithShortcut(tr("Pattern / song mode"), songModeAction_->shortcut()));
    connect(songModeAction_, &QAction::toggled, this, [this](bool song) {
        songModeAction_->setText(song ? tr("SONG") : tr("PAT"));
        emit songModeChanged(song);
    });

    // Studio One-inspired transport deck:
    // [ PAT |< << >> >| ■ ▶ ● Loop ]
    QFrame* playGroup = makeToolGroup(this,
        {songModeAction_, rewindAction_, stepBackAction_, stepForwardAction_, goToEndAction_, stopAction_,
            playAction_, recordAction_, loopAction_},
        iconSize());
    for (QToolButton* btn : playGroup->findChildren<QToolButton*>()) {
        const QAction* act = btn->defaultAction();
        if (act == songModeAction_) {
            btn->setObjectName(QStringLiteral("modeButton"));
        } else if (act == stopAction_) {
            btn->setObjectName(QStringLiteral("stopButton"));
        } else if (act == playAction_) {
            btn->setObjectName(QStringLiteral("playButton"));
        } else if (act == recordAction_) {
            btn->setObjectName(QStringLiteral("recordButton"));
        } else if (act == loopAction_) {
            btn->setObjectName(QStringLiteral("loopButton"));
        } else if (act == stepBackAction_ || act == stepForwardAction_) {
            btn->setAutoRepeat(true);
            btn->setAutoRepeatDelay(260);
            btn->setAutoRepeatInterval(85);
        }
    }

    patternSelector_ = new PatternSelector(iconSize(), this);

    tempoDisplay_ = new TempoDisplay(this);
    connect(tempoDisplay_, &TempoDisplay::tempoChanged, this, &TransportBar::tempoChanged);
    keyDisplay_ = new KeyDisplay(this);
    positionDisplay_ = new PositionDisplay(this);

    performanceMeter_ = new PerformanceMeter(this);

    // Master Output Audio Group: [ Mute | Vol- | VOL Knob | Vol+ | DIM ]
    auto* volumeGroup = new QFrame(this);
    volumeGroup->setObjectName(QStringLiteral("volumeControlGroup"));
    auto* volLayout = new QHBoxLayout(volumeGroup);
    volLayout->setContentsMargins(4, 2, 4, 2);
    volLayout->setSpacing(3);

    masterMuteBtn_ = new QToolButton(volumeGroup);
    masterMuteBtn_->setObjectName(QStringLiteral("masterMuteButton"));
    masterMuteBtn_->setCheckable(true);
    masterMuteBtn_->setIcon(icon(QStringLiteral("speaker")));
    masterMuteBtn_->setIconSize(QSize(16, 16));
    masterMuteBtn_->setFixedSize(26, 26);
    masterMuteBtn_->setFocusPolicy(Qt::NoFocus);
    masterMuteBtn_->setToolTip(tr("Mute Master Output"));
    connect(masterMuteBtn_, &QToolButton::toggled, this, [this](bool muted) {
        masterMuted_ = muted;
        masterMuteBtn_->setIcon(icon(muted ? QStringLiteral("speaker-mute") : QStringLiteral("speaker")));
        emit masterMuteToggled(muted);
    });

    volDownBtn_ = new QToolButton(volumeGroup);
    volDownBtn_->setObjectName(QStringLiteral("volumeStepButton"));
    volDownBtn_->setIcon(icon(QStringLiteral("volume-down")));
    volDownBtn_->setIconSize(QSize(15, 15));
    volDownBtn_->setFixedSize(24, 26);
    volDownBtn_->setFocusPolicy(Qt::NoFocus);
    volDownBtn_->setAutoRepeat(true);
    volDownBtn_->setAutoRepeatDelay(250);
    volDownBtn_->setAutoRepeatInterval(75);
    volDownBtn_->setToolTip(tr("Decrease Master Volume (-5%)"));
    connect(volDownBtn_, &QToolButton::clicked, this, [this] { stepMasterVolume(-5.0); });

    // Volume 0..125 %, unity at 100 % (MainWindow maps it to a gain).
    volumeKnob_ = new Knob(tr("VOL"), 0.0, 125.0, 100.0, volumeGroup);
    volumeKnob_->setDescription(tr("Master volume"));
    volumeKnob_->setValueText([](double percent) {
        const double gain = (percent / 100.0) * (percent / 100.0);
        const QString db = gain > 0.0
            ? QString::number(20.0 * std::log10(gain), 'f', 1) + QStringLiteral(" dB")
            : QStringLiteral("-inf dB");
        return QStringLiteral("%1% (%2)").arg(QString::number(percent, 'f', 0), db);
    });
    connect(volumeKnob_, &Knob::valueChanged, this, &TransportBar::masterVolumeChanged);

    volUpBtn_ = new QToolButton(volumeGroup);
    volUpBtn_->setObjectName(QStringLiteral("volumeStepButton"));
    volUpBtn_->setIcon(icon(QStringLiteral("volume-up")));
    volUpBtn_->setIconSize(QSize(15, 15));
    volUpBtn_->setFixedSize(24, 26);
    volUpBtn_->setFocusPolicy(Qt::NoFocus);
    volUpBtn_->setAutoRepeat(true);
    volUpBtn_->setAutoRepeatDelay(250);
    volUpBtn_->setAutoRepeatInterval(75);
    volUpBtn_->setToolTip(tr("Increase Master Volume (+5%)"));
    connect(volUpBtn_, &QToolButton::clicked, this, [this] { stepMasterVolume(+5.0); });

    masterDimBtn_ = new QToolButton(volumeGroup);
    masterDimBtn_->setObjectName(QStringLiteral("masterDimButton"));
    masterDimBtn_->setText(tr("DIM"));
    masterDimBtn_->setCheckable(true);
    masterDimBtn_->setFixedHeight(24);
    masterDimBtn_->setFocusPolicy(Qt::NoFocus);
    masterDimBtn_->setToolTip(tr("Dim Master Output (-15 dB monitor attenuation)"));
    connect(masterDimBtn_, &QToolButton::toggled, this, [this](bool dimmed) {
        masterDimmed_ = dimmed;
        emit masterDimToggled(dimmed);
    });

    volLayout->addWidget(masterMuteBtn_, 0, Qt::AlignVCenter);
    volLayout->addWidget(volDownBtn_, 0, Qt::AlignVCenter);
    volLayout->addWidget(volumeKnob_, 0, Qt::AlignVCenter);
    volLayout->addWidget(volUpBtn_, 0, Qt::AlignVCenter);
    volLayout->addWidget(masterDimBtn_, 0, Qt::AlignVCenter);

    pitchKnob_ = new Knob(tr("PITCH"), -12.0, 12.0, 0.0, this);
    pitchKnob_->setDescription(tr("Master pitch"));
    pitchKnob_->setValueText([](double semitones) {
        return tr("%1%2 semitones")
            .arg(semitones > 0.0 ? QStringLiteral("+") : QString(), QString::number(semitones, 'f', 1));
    });
    connect(pitchKnob_, &Knob::valueChanged, this, &TransportBar::masterPitchChanged);

    QFrame* toolsGroup = makeToolGroup(this, {tapAction_, metronomeAction_}, iconSize());
    toolsGroup->findChildren<QToolButton*>().constFirst()->setObjectName(QStringLiteral("tapButton"));

    viewButtons_ = new ViewButtons(iconSize(), this);

    // Order on the bar; overflow ranks: higher moves into the drop-down first.
    using Zone = TransportItem::Zone;
    groups_ = {
        {playGroup, Zone::Left, 0, 0},
        {patternSelector_, Zone::Left, kGroupSpacing, 1},
        {tempoDisplay_, Zone::Centre, 0, 0},
        {keyDisplay_, Zone::Centre, 4, 2},
        {positionDisplay_, Zone::Centre, 4, 0},
        {performanceMeter_, Zone::Right, 0, 7},
        {volumeGroup, Zone::Right, kGroupSpacing, 3},
        {pitchKnob_, Zone::Right, 4, 6},
        {toolsGroup, Zone::Right, kGroupSpacing, 5},
        {viewButtons_, Zone::Right, kGroupSpacing, 4},
    };

    moreButton_ = new QToolButton(this);
    moreButton_->setObjectName(QStringLiteral("transportMore"));
    moreButton_->setIcon(icon(QStringLiteral("chevrons-right")));
    moreButton_->setIconSize(QSize(16, 16));
    moreButton_->setAutoRaise(true);
    moreButton_->setFocusPolicy(Qt::NoFocus);
    moreButton_->setToolTip(tr("More controls"));
    moreButton_->setAccessibleName(tr("More transport controls"));
    moreButton_->hide();
    connect(moreButton_, &QToolButton::clicked, this, &TransportBar::toggleOverflow);

    overflow_ = new QFrame(this, Qt::Popup);
    overflow_->setObjectName(QStringLiteral("transportOverflow"));
    overflow_->setAttribute(Qt::WA_NoMouseReplay);
    auto* overflowLayout = new QHBoxLayout(overflow_);
    overflowLayout->setContentsMargins(10, 8, 10, 8);
    overflowLayout->setSpacing(kGroupSpacing);

    int rowHeight = 0;
    for (const Group& group : groups_)
        rowHeight = std::max(rowHeight, group.widget->sizeHint().height());
    rowHolder_ = new QWidget(this);
    rowHolder_->setAttribute(Qt::WA_TransparentForMouseEvents);
    rowHolder_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    rowHolder_->setFixedHeight(rowHeight);
    rowHolder_->setMinimumWidth(0);
    addWidget(rowHolder_);
    rowHolder_->lower();
}

QAction* TransportBar::makeAction(
    const QString& iconName, const QString& text, const QString& tip, const QKeySequence& shortcut)
{
    auto* action = new QAction(icon(iconName), text, this);
    if (!shortcut.isEmpty())
        action->setShortcut(shortcut);
    action->setToolTip(tipWithShortcut(tip, shortcut));
    return action;
}

void TransportBar::stepMasterVolume(double deltaPercent)
{
    const double next = std::clamp(std::round(volumeKnob_->value() + deltaPercent), 0.0, 125.0);
    if (std::abs(next - volumeKnob_->value()) < 1e-6)
        return;
    volumeKnob_->setValue(next);
    emit masterVolumeChanged(next);
}

void TransportBar::setLeadingSpace(int pixels)
{
    leadingSpace_ = std::max(0, pixels);
    scheduleRelayout();
}

void TransportBar::resizeEvent(QResizeEvent* event)
{
    QToolBar::resizeEvent(event);
    relayout();
}

void TransportBar::showEvent(QShowEvent* event)
{
    QToolBar::showEvent(event);
    scheduleRelayout();
}

bool TransportBar::event(QEvent* event)
{
    if (event->type() == QEvent::LayoutRequest)
        scheduleRelayout();
    return QToolBar::event(event);
}

void TransportBar::scheduleRelayout()
{
    if (relayoutPending_)
        return;
    relayoutPending_ = true;
    QTimer::singleShot(0, this, [this] {
        relayoutPending_ = false;
        relayout();
    });
}

void TransportBar::relayout()
{
    const QMargins margins = layout() != nullptr ? layout()->contentsMargins() : QMargins();
    const QRect area = rect().marginsRemoved(margins);
    if (area.width() <= 0)
        return;

    std::vector<TransportItem> items;
    items.reserve(groups_.size());
    for (const Group& group : groups_)
        items.push_back({group.widget->sizeHint().width(), group.gapBefore, group.zone, group.overflowRank});
    const QSize more = moreButton_->sizeHint();
    const TransportPlacement placement
        = layoutTransport(items, area.left() + leadingSpace_, area.right() + 1, kGroupSpacing, more.width());

    bool overflowChanged = false;
    for (std::size_t i = 0; i < groups_.size(); ++i) {
        QWidget* widget = groups_[i].widget;
        const bool inOverflow = placement.overflowed[i];
        if (inOverflow != (widget->parentWidget() == overflow_)) {
            overflowChanged = true;
            widget->setParent(inOverflow ? static_cast<QWidget*>(overflow_) : this);
        }
        if (inOverflow)
            continue;
        const QSize hint = widget->sizeHint();
        widget->setGeometry(
            placement.x[i], area.top() + (area.height() - hint.height()) / 2, hint.width(), hint.height());
        widget->show();
    }

    moreButton_->setVisible(placement.showMoreButton);
    if (placement.showMoreButton) {
        moreButton_->setGeometry(placement.moreButtonX, area.top() + (area.height() - more.height()) / 2,
            more.width(), more.height());
        moreButton_->raise();
    }

    if (overflowChanged) {
        auto* list = static_cast<QHBoxLayout*>(overflow_->layout());
        while (list->count() > 0)
            delete list->takeAt(0);
        for (std::size_t i = 0; i < groups_.size(); ++i) {
            if (!placement.overflowed[i])
                continue;
            list->addWidget(groups_[i].widget, 0, Qt::AlignVCenter);
            groups_[i].widget->show();
        }
        if (!placement.showMoreButton)
            overflow_->hide();
        else if (overflow_->isVisible())
            showOverflow();
    }
}

void TransportBar::toggleOverflow()
{
    if (overflow_->isVisible())
        overflow_->hide();
    else
        showOverflow();
}

void TransportBar::showOverflow()
{
    overflow_->adjustSize();
    QPoint at = moreButton_->mapToGlobal(QPoint(moreButton_->width(), moreButton_->height() + 4));
    at.rx() -= overflow_->width();
    if (const QScreen* screen = moreButton_->screen()) {
        const QRect free = screen->availableGeometry();
        at.setX(
            std::clamp(at.x(), free.left(), std::max(free.left(), free.right() + 1 - overflow_->width())));
        at.setY(std::min(at.y(), std::max(free.top(), free.bottom() + 1 - overflow_->height())));
    }
    overflow_->move(at);
    overflow_->show();
}

double TransportBar::tempo() const { return tempoDisplay_->tempo(); }

bool TransportBar::metronomeEnabled() const { return metronomeAction_->isChecked(); }

bool TransportBar::loopEnabled() const { return loopAction_->isChecked(); }

double TransportBar::masterVolume() const { return volumeKnob_->value(); }

void TransportBar::setPosition(const QString& barsBeatsTicks)
{
    positionDisplay_->setPosition(barsBeatsTicks);
}

void TransportBar::setTime(const QString& minutesSeconds) { positionDisplay_->setTime(minutesSeconds); }

void TransportBar::setTempo(double bpm) { tempoDisplay_->setTempo(bpm); }

void TransportBar::setTimeSignature(int beatsPerBar, int beatUnit)
{
    tempoDisplay_->setTimeSignature(beatsPerBar, beatUnit);
}

void TransportBar::setPositionShowsTime(bool time)
{
    positionDisplay_->setMode(time ? PositionDisplay::Mode::Time : PositionDisplay::Mode::BarsBeats);
}

void TransportBar::setLoopEnabled(bool enabled)
{
    const QSignalBlocker blocker(loopAction_);
    loopAction_->setChecked(enabled);
}

void TransportBar::setMasterVolume(double percent) { volumeKnob_->setValue(std::clamp(percent, 0.0, 125.0)); }

void TransportBar::setMasterMuted(bool muted)
{
    masterMuted_ = muted;
    const QSignalBlocker blocker(masterMuteBtn_);
    masterMuteBtn_->setChecked(muted);
    masterMuteBtn_->setIcon(icon(muted ? QStringLiteral("speaker-mute") : QStringLiteral("speaker")));
}

void TransportBar::setMasterDimmed(bool dimmed)
{
    masterDimmed_ = dimmed;
    const QSignalBlocker blocker(masterDimBtn_);
    masterDimBtn_->setChecked(dimmed);
}

void TransportBar::setPlaying(bool playing)
{
    if (playing == playing_)
        return;
    playing_ = playing;
    playAction_->setChecked(playing);
    playAction_->setIcon(icon(playing ? QStringLiteral("pause") : QStringLiteral("play")));
    playAction_->setText(playing ? tr("Pause") : tr("Play"));
}

} // namespace vloomdaw
