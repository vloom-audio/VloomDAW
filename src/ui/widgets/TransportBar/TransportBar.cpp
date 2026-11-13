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
    //         [ CPU | RAM ] VOL PITCH [ TAP 🎵 ] [ views ] [ » ]
    //
    // The groups are children of the bar but not tool bar items: relayout()
    // places them itself (core/TransportLayout), so the displays stay centred
    // and groups that do not fit go into a drop-down behind our own » button.
    // Qt's own » expanded the bar into a second row, pushing the workspace
    // down, and that row was sometimes left blank.
    QFrame* playGroup
        = makeToolGroup(this, {songModeAction_, playAction_, stopAction_, recordAction_}, iconSize());
    const auto playButtons = playGroup->findChildren<QToolButton*>();
    playButtons.at(0)->setObjectName(QStringLiteral("modeButton"));
    playButtons.at(1)->setObjectName(QStringLiteral("playButton"));
    playButtons.at(3)->setObjectName(QStringLiteral("recordButton"));
    patternSelector_ = new PatternSelector(iconSize(), this);

    tempoDisplay_ = new TempoDisplay(this);
    connect(tempoDisplay_, &TempoDisplay::tempoChanged, this, &TransportBar::tempoChanged);
    keyDisplay_ = new KeyDisplay(this);
    positionDisplay_ = new PositionDisplay(this);

    performanceMeter_ = new PerformanceMeter(this);

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
        {volumeKnob_, Zone::Right, kGroupSpacing, 3},
        {pitchKnob_, Zone::Right, 2, 6},
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
    // A press outside closes the drop-down and is not passed on, so pressing
    // » while it is open closes it instead of reopening it.
    overflow_->setAttribute(Qt::WA_NoMouseReplay);
    auto* overflowLayout = new QHBoxLayout(overflow_);
    overflowLayout->setContentsMargins(10, 8, 10, 8);
    overflowLayout->setSpacing(kGroupSpacing);

    // The one real tool bar item: gives the bar its height, ignores the
    // mouse (so empty bar space still moves the window) and never overflows.
    int rowHeight = 0;
    for (const Group& group : groups_)
        rowHeight = std::max(rowHeight, group.widget->sizeHint().height());
    rowHolder_ = new QWidget(this);
    rowHolder_->setAttribute(Qt::WA_TransparentForMouseEvents);
    rowHolder_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    rowHolder_->setFixedHeight(rowHeight);
    rowHolder_->setMinimumWidth(0);
    addWidget(rowHolder_);
    rowHolder_->lower(); // the groups paint and take clicks above it
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
    leadingSpace_ = std::max(0, pixels);
    scheduleRelayout();
}

void TransportBar::resizeEvent(QResizeEvent* event)
{
    QToolBar::resizeEvent(event);
    relayout(); // at once: the groups must not lag behind a window resize
}

void TransportBar::showEvent(QShowEvent* event)
{
    QToolBar::showEvent(event);
    scheduleRelayout();
}

bool TransportBar::event(QEvent* event)
{
    // A group's size hint changed (e.g. wider text): place everything again.
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
        // The drop-down lists its groups in bar order.
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
            showOverflow(); // resize and re-anchor what is open
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
    // Below », right-aligned with it, kept on the screen.
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
