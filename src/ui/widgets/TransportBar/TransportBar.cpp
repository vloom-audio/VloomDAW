// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/TransportBar/TransportBar.hpp"

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
#include <QFrame>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
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

    recordAction_
        = makeAction(QStringLiteral("record"), tr("Record"), tr("Arm recording"), QKeySequence(Qt::Key_R));
    recordAction_->setCheckable(true);
    connect(recordAction_, &QAction::toggled, this, &TransportBar::recordArmedChanged);

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

    // FL Studio's L: pattern mode plays the current pattern on a loop, song
    // mode plays the playlist.
    songModeAction_ = new QAction(tr("PAT"), this);
    songModeAction_->setCheckable(true);
    songModeAction_->setShortcut(QKeySequence(Qt::Key_L));
    songModeAction_->setToolTip(tipWithShortcut(tr("Pattern / song mode"), songModeAction_->shortcut()));
    connect(songModeAction_, &QAction::toggled, this, [this](bool song) {
        songModeAction_->setText(song ? tr("SONG") : tr("PAT"));
        emit songModeChanged(song);
    });

    // Layout: [ PAT ▶ ■ ● ] [ Pattern 1 ▾ + − ]  ...  [ BPM | SIG ][ ROOT | SCALE ][ BAR · BEAT · TICK ]  ...
    //         [ CPU | RAM ] VOL PITCH [ TAP 🎵 ] [ views ]
    leadingSpace_ = spacer(this, 0);
    addWidget(leadingSpace_);
    appName_ = new QLabel(this);
    appName_->setObjectName(QStringLiteral("appName"));
    appName_->setAttribute(Qt::WA_TransparentForMouseEvents);
    appNameAction_ = addWidget(appName_);
    appNameAction_->setVisible(false);

    QFrame* playGroup
        = makeToolGroup(this, {songModeAction_, playAction_, stopAction_, recordAction_}, iconSize());
    const auto playButtons = playGroup->findChildren<QToolButton*>();
    playButtons.at(0)->setObjectName(QStringLiteral("modeButton"));
    playButtons.at(1)->setObjectName(QStringLiteral("playButton"));
    playButtons.at(3)->setObjectName(QStringLiteral("recordButton"));
    addWidget(playGroup);
    addWidget(spacer(this, kGroupSpacing));
    patternSelector_ = new PatternSelector(iconSize(), this);
    addWidget(patternSelector_);
    addWidget(spacer(this, kGroupSpacing));
    // Sized by recentre() so the displays sit in the middle of the window,
    // not just between the uneven groups either side of them.
    centreSpacer_ = spacer(this, 0);
    addWidget(centreSpacer_);

    tempoDisplay_ = new TempoDisplay(this);
    connect(tempoDisplay_, &TempoDisplay::tempoChanged, this, &TransportBar::tempoChanged);
    addWidget(tempoDisplay_);
    addWidget(spacer(this, 4));
    keyDisplay_ = new KeyDisplay(this);
    addWidget(keyDisplay_);
    addWidget(spacer(this, 4));
    positionDisplay_ = new PositionDisplay(this);
    addWidget(positionDisplay_);
    addWidget(spacer(this, -1)); // takes what is left, so the right groups sit at the end

    // Everything right of the displays, measured by recentre().
    const auto addRight = [this](QWidget* w) {
        addWidget(w);
        rightSide_.append(w);
    };
    addRight(spacer(this, kGroupSpacing));
    performanceMeter_ = new PerformanceMeter(this);
    addRight(performanceMeter_);
    addRight(spacer(this, kGroupSpacing));

    // Volume 0..125 %, unity at 100 % (MainWindow maps it to a gain).
    volumeKnob_ = new Knob(tr("VOL"), 0.0, 125.0, 100.0, this);
    volumeKnob_->setDescription(tr("Master volume"));
    volumeKnob_->setValueText([](double percent) {
        const double gain = (percent / 100.0) * (percent / 100.0);
        const QString db = gain > 0.0
            ? QString::number(20.0 * std::log10(gain), 'f', 1) + QStringLiteral(" dB")
            : QStringLiteral("-inf dB");
        return QStringLiteral("%1% (%2)").arg(QString::number(percent, 'f', 0), db);
    });
    connect(volumeKnob_, &Knob::valueChanged, this, &TransportBar::masterVolumeChanged);
    addRight(volumeKnob_);
    addRight(spacer(this, 2));

    pitchKnob_ = new Knob(tr("PITCH"), -12.0, 12.0, 0.0, this);
    pitchKnob_->setDescription(tr("Master pitch"));
    pitchKnob_->setValueText([](double semitones) {
        return tr("%1%2 semitones")
            .arg(semitones > 0.0 ? QStringLiteral("+") : QString(), QString::number(semitones, 'f', 1));
    });
    connect(pitchKnob_, &Knob::valueChanged, this, &TransportBar::masterPitchChanged);
    addRight(pitchKnob_);
    addRight(spacer(this, kGroupSpacing));

    QFrame* toolsGroup = makeToolGroup(this, {tapAction_, metronomeAction_}, iconSize());
    toolsGroup->findChildren<QToolButton*>().constFirst()->setObjectName(QStringLiteral("tapButton"));
    addRight(toolsGroup);
    addRight(spacer(this, kGroupSpacing));

    viewButtons_ = new ViewButtons(iconSize(), this);
    addRight(viewButtons_);
}

QAction* TransportBar::makeAction(
    const QString& iconName, const QString& text, const QString& tip, const QKeySequence& shortcut)
{
    auto* action = new QAction(icon(iconName), text, this);
    action->setShortcut(shortcut);
    action->setToolTip(tipWithShortcut(tip, shortcut));
    return action;
}

void TransportBar::setLeadingSpace(int pixels)
{
    leadingSpace_->setFixedWidth(pixels);
    scheduleRecentre();
}

void TransportBar::showAppName(const QString& name)
{
    appName_->setText(name);
    appNameAction_->setVisible(true);
    scheduleRecentre();
}

void TransportBar::resizeEvent(QResizeEvent* event)
{
    QToolBar::resizeEvent(event);
    scheduleRecentre();
}

void TransportBar::showEvent(QShowEvent* event)
{
    QToolBar::showEvent(event);
    scheduleRecentre();
}

void TransportBar::scheduleRecentre()
{
    // After the layout has placed the widgets for the new size.
    QTimer::singleShot(0, this, &TransportBar::recentre);
}

void TransportBar::recentre()
{
    const auto widthOf = [](const QWidget* w) { return std::max(w->sizeHint().width(), w->minimumWidth()); };

    // Where the centre spacer starts: the left groups do not depend on it.
    const int leftEnd = centreSpacer_->x();
    const int displays = positionDisplay_->geometry().right() + 1 - tempoDisplay_->x();

    int right = layout() != nullptr ? layout()->contentsMargins().right() : 0;
    for (const QWidget* w : rightSide_)
        right += widthOf(w);

    // Centre the displays in the bar; when the window is too narrow, give the
    // right groups their room first (they then sit right after the displays).
    const int wanted = (width() - displays) / 2 - leftEnd;
    const int room = width() - leftEnd - displays - right;
    const int gap = std::clamp(wanted, 0, std::max(0, room));
    if (centreSpacer_->width() != gap)
        centreSpacer_->setFixedWidth(gap);
}

double TransportBar::tempo() const { return tempoDisplay_->tempo(); }

bool TransportBar::metronomeEnabled() const { return metronomeAction_->isChecked(); }

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
