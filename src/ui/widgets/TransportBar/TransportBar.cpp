// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/TransportBar/TransportBar.hpp"

#include "ui/widgets/PositionDisplay/PositionDisplay.hpp"
#include "ui/widgets/TempoDisplay/TempoDisplay.hpp"

#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QToolButton>

namespace vloomdaw {

namespace {

constexpr int kGroupSpacing = 10; ///< between groups on the bar

QIcon icon(const QString& name) { return QIcon(QStringLiteral(":/icons/%1.svg").arg(name)); }

/// "Play / pause (Space)" — tool tips always name the shortcut.
QString tipWithShortcut(const QString& text, const QKeySequence& shortcut)
{
    return QStringLiteral("%1 (%2)").arg(text, shortcut.toString(QKeySequence::NativeText));
}

/// Fixed-width gap, or a stretching one when `width` is negative.
QWidget* spacer(QWidget* parent, int width)
{
    auto* w = new QWidget(parent);
    if (width < 0)
        w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    else
        w->setFixedWidth(width);
    w->setAttribute(Qt::WA_TransparentForMouseEvents);
    return w;
}

} // namespace

TransportBar::TransportBar(QWidget* parent)
    : QToolBar(tr("Transport"), parent)
{
    setObjectName(QStringLiteral("transportBar"));
    setMovable(false);
    setFloatable(false);
    setIconSize(QSize(18, 18));

    playAction_
        = makeAction(QStringLiteral("play"), tr("Play"), tr("Play / pause"), QKeySequence(Qt::Key_Space));
    playAction_->setCheckable(true);
    // The engine owns the play state: undo the automatic check toggle and let
    // setPlaying() reflect what actually happened.
    connect(playAction_, &QAction::triggered, this, [this] {
        playAction_->setChecked(playing_);
        emit playToggled();
    });

    stopAction_ = makeAction(QStringLiteral("stop"), tr("Stop"), tr("Stop and return to start"),
        QKeySequence(Qt::SHIFT | Qt::Key_Space));
    connect(stopAction_, &QAction::triggered, this, &TransportBar::stopRequested);

    // Menu-only: Stop already returns to start from the bar.
    rewindAction_ = new QAction(tr("Go to Start"), this);
    rewindAction_->setShortcut(QKeySequence(Qt::Key_Home));
    connect(rewindAction_, &QAction::triggered, this, &TransportBar::rewindRequested);

    tapAction_ = new QAction(tr("TAP"), this);
    tapAction_->setShortcut(QKeySequence(Qt::Key_T));
    tapAction_->setToolTip(tipWithShortcut(tr("Tap tempo: tap along to the beat"), tapAction_->shortcut()));
    connect(tapAction_, &QAction::triggered, this, &TransportBar::tapTempoRequested);

    metronomeAction_ = makeAction(QStringLiteral("metronome"), tr("Metronome"), tr("Metronome on / off"),
        QKeySequence(Qt::CTRL | Qt::Key_M));
    metronomeAction_->setCheckable(true);
    metronomeAction_->setChecked(true);
    connect(metronomeAction_, &QAction::toggled, this, &TransportBar::metronomeToggled);

    // Layout: [ ▶ ■ ]  ...  [ BPM | SIG ][ BAR · BEAT · TICK ]  ...  [ TAP 🎵 ]
    leadingSpace_ = spacer(this, 0);
    addWidget(leadingSpace_);

    QFrame* playGroup = makeGroup({playAction_, stopAction_});
    playGroup->findChildren<QToolButton*>().constFirst()->setObjectName(QStringLiteral("playButton"));
    addWidget(playGroup);
    addWidget(spacer(this, kGroupSpacing));
    addWidget(spacer(this, -1)); // centres the displays

    tempoDisplay_ = new TempoDisplay(this);
    connect(tempoDisplay_, &TempoDisplay::tempoChanged, this, &TransportBar::tempoChanged);
    addWidget(tempoDisplay_);
    addWidget(spacer(this, 4));
    positionDisplay_ = new PositionDisplay(this);
    addWidget(positionDisplay_);
    addWidget(spacer(this, kGroupSpacing));
    addWidget(spacer(this, -1));

    QFrame* toolsGroup = makeGroup({tapAction_, metronomeAction_});
    toolsGroup->findChildren<QToolButton*>().constFirst()->setObjectName(QStringLiteral("tapButton"));
    addWidget(toolsGroup);
}

QAction* TransportBar::makeAction(
    const QString& iconName, const QString& text, const QString& tip, const QKeySequence& shortcut)
{
    auto* action = new QAction(icon(iconName), text, this);
    action->setShortcut(shortcut);
    action->setToolTip(tipWithShortcut(tip, shortcut));
    return action;
}

QFrame* TransportBar::makeGroup(std::initializer_list<QAction*> actions)
{
    auto* group = new QFrame(this);
    group->setObjectName(QStringLiteral("toolGroup"));
    auto* layout = new QHBoxLayout(group);
    layout->setContentsMargins(3, 3, 3, 3);
    layout->setSpacing(2);
    for (QAction* action : actions) {
        auto* button = new QToolButton(group);
        button->setDefaultAction(action); // also makes the shortcut live
        button->setIconSize(iconSize());
        button->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(button);
    }
    return group;
}

void TransportBar::setLeadingSpace(int pixels) { leadingSpace_->setFixedWidth(pixels); }

double TransportBar::tempo() const { return tempoDisplay_->tempo(); }

bool TransportBar::metronomeEnabled() const { return metronomeAction_->isChecked(); }

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
