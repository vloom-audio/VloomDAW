// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QMap>
#include <QWidget>

class QAction;

namespace vloomdaw {

/// Buttons that open the main windows (playlist, channel rack, piano roll,
/// mixer, browser), like the window buttons on FL Studio's tool bar.
///
/// A pure view: it emits which window the user asked for; MainWindow decides
/// what that means.
class ViewButtons : public QWidget {
    Q_OBJECT

public:
    enum class View { Playlist, ChannelRack, PianoRoll, Mixer, Browser };
    Q_ENUM(View)

    /// `iconSize` matches the tool bar the buttons sit on.
    explicit ViewButtons(QSize iconSize, QWidget* parent = nullptr);

public slots:
    /// Lights the button of `view` while that view is open.
    void setViewOpen(vloomdaw::ViewButtons::View view, bool open);

signals:
    void viewRequested(vloomdaw::ViewButtons::View view);

private:
    QAction* makeViewAction(
        const QString& iconName, const QString& text, const QKeySequence& shortcut, View view);

    QMap<View, QAction*> actions_;
};

} // namespace vloomdaw
