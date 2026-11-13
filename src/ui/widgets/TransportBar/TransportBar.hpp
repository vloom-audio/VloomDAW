// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/TransportLayout.hpp"

#include <QToolBar>

#include <vector>

class QAction;
class QEvent;
class QFrame;
class QToolButton;
class QResizeEvent;
class QShowEvent;

namespace vloomdaw {

class Knob;
class PatternSelector;
class PerformanceMeter;
class PositionDisplay;
class KeyDisplay;
class TempoDisplay;
class ViewButtons;

/// The transport: pattern / song mode, play / stop / record arm, tempo and
/// time signature, song position, CPU / RAM meter, master volume and pitch,
/// tap tempo and metronome, pattern selector, root note and the main-window
/// buttons.
///
/// A pure view: it only emits what the user did and shows what it is told.
/// It knows nothing about the engine; MainWindow connects the two.
///
/// Its groups are laid out by core/TransportLayout, not by QToolBar: the
/// displays sit in the middle of the window, and when the window is too
/// narrow the least important groups move into a drop-down behind a »
/// button (CPU / RAM first, then pitch, tap / metronome, views, volume, key,
/// patterns; play, tempo and position always stay).
class TransportBar : public QToolBar {
    Q_OBJECT

public:
    explicit TransportBar(QWidget* parent = nullptr);

    [[nodiscard]] double tempo() const;
    [[nodiscard]] bool metronomeEnabled() const;

    /// The bar's actions, so menus can offer the same commands and shortcuts.
    [[nodiscard]] QAction* playAction() const { return playAction_; }
    [[nodiscard]] QAction* stopAction() const { return stopAction_; }
    [[nodiscard]] QAction* recordAction() const { return recordAction_; }
    [[nodiscard]] QAction* rewindAction() const { return rewindAction_; }
    [[nodiscard]] QAction* tapTempoAction() const { return tapAction_; }
    [[nodiscard]] QAction* metronomeAction() const { return metronomeAction_; }
    [[nodiscard]] QAction* songModeAction() const { return songModeAction_; }
    [[nodiscard]] PerformanceMeter* performanceMeter() const { return performanceMeter_; }
    [[nodiscard]] PatternSelector* patternSelector() const { return patternSelector_; }
    [[nodiscard]] KeyDisplay* keyDisplay() const { return keyDisplay_; }
    [[nodiscard]] ViewButtons* viewButtons() const { return viewButtons_; }
    /// Master volume in percent (0..125, unity at 100).
    [[nodiscard]] double masterVolume() const;

    /// Room at the start of the bar, e.g. for the macOS window buttons.
    void setLeadingSpace(int pixels);

public slots:
    void setPosition(const QString& barsBeatsTicks);
    void setTime(const QString& minutesSeconds);
    void setPlaying(bool playing);
    void setTempo(double bpm);
    void setTimeSignature(int beatsPerBar, int beatUnit);
    /// Position display in minutes:seconds instead of bars.beats.ticks.
    void setPositionShowsTime(bool time);

signals:
    void playToggled();
    void stopRequested();
    /// The user armed (true) or disarmed (false) recording.
    void recordArmedChanged(bool armed);
    void rewindRequested();
    void tempoChanged(double bpm);
    void tapTempoRequested();
    void metronomeToggled(bool enabled);
    /// Pattern mode (false) or song mode (true).
    void songModeChanged(bool song);
    /// Master volume knob, in percent (0..125, unity at 100).
    void masterVolumeChanged(double percent);
    /// Master pitch knob, in semitones (-12..+12).
    void masterPitchChanged(double semitones);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool event(QEvent* event) override;

private:
    /// One group of controls and how it takes part in the layout.
    struct Group {
        QWidget* widget;
        TransportItem::Zone zone;
        int gapBefore;
        int overflowRank;
    };

    void scheduleRelayout();
    /// Places the groups for the current width; moves groups between the bar
    /// and the drop-down.
    void relayout();
    void toggleOverflow();
    void showOverflow();

    QAction* makeAction(
        const QString& iconName, const QString& text, const QString& tip, const QKeySequence& shortcut);

    QAction* playAction_ = nullptr;
    QAction* stopAction_ = nullptr;
    QAction* recordAction_ = nullptr;
    QAction* rewindAction_ = nullptr;
    QAction* tapAction_ = nullptr;
    QAction* metronomeAction_ = nullptr;
    QAction* songModeAction_ = nullptr;
    PerformanceMeter* performanceMeter_ = nullptr;
    PatternSelector* patternSelector_ = nullptr;
    KeyDisplay* keyDisplay_ = nullptr;
    ViewButtons* viewButtons_ = nullptr;
    Knob* volumeKnob_ = nullptr;
    Knob* pitchKnob_ = nullptr;
    std::vector<Group> groups_; ///< in bar order
    QWidget* rowHolder_ = nullptr; ///< the only tool bar item: gives the bar its height
    QToolButton* moreButton_ = nullptr; ///< » : opens overflow_
    QFrame* overflow_ = nullptr; ///< drop-down with the groups that do not fit
    int leadingSpace_ = 0; ///< room for the macOS window buttons
    bool relayoutPending_ = false;
    TempoDisplay* tempoDisplay_ = nullptr;
    PositionDisplay* positionDisplay_ = nullptr;
    bool playing_ = false;
};

} // namespace vloomdaw
