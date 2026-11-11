// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QToolBar>

#include <initializer_list>

class QAction;
class QFrame;

namespace vloomdaw {

class PositionDisplay;
class TempoDisplay;

/// The transport: play / stop, tempo and time signature, song position,
/// tap tempo and metronome.
///
/// A pure view: it only emits what the user did and shows what it is told.
/// It knows nothing about the engine; MainWindow connects the two.
class TransportBar : public QToolBar {
    Q_OBJECT

public:
    explicit TransportBar(QWidget* parent = nullptr);

    [[nodiscard]] double tempo() const;
    [[nodiscard]] bool metronomeEnabled() const;

    /// The bar's actions, so menus can offer the same commands and shortcuts.
    [[nodiscard]] QAction* playAction() const { return playAction_; }
    [[nodiscard]] QAction* stopAction() const { return stopAction_; }
    [[nodiscard]] QAction* rewindAction() const { return rewindAction_; }
    [[nodiscard]] QAction* tapTempoAction() const { return tapAction_; }
    [[nodiscard]] QAction* metronomeAction() const { return metronomeAction_; }

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
    void rewindRequested();
    void tempoChanged(double bpm);
    void tapTempoRequested();
    void metronomeToggled(bool enabled);

private:
    QAction* makeAction(
        const QString& iconName, const QString& text, const QString& tip, const QKeySequence& shortcut);
    QFrame* makeGroup(std::initializer_list<QAction*> actions);

    QAction* playAction_ = nullptr;
    QAction* stopAction_ = nullptr;
    QAction* rewindAction_ = nullptr;
    QAction* tapAction_ = nullptr;
    QAction* metronomeAction_ = nullptr;
    QWidget* leadingSpace_ = nullptr;
    TempoDisplay* tempoDisplay_ = nullptr;
    PositionDisplay* positionDisplay_ = nullptr;
    bool playing_ = false;
};

} // namespace vloomdaw
