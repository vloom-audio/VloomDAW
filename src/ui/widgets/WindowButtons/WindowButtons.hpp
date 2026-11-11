// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QWidget>

class QToolButton;

namespace vloomdaw {

/// Minimize / maximize / close buttons for a window without a native title bar.
///
/// Emits the user's intent only; the window decides what to do with it and
/// reports its state back with setMaximized().
class WindowButtons : public QWidget {
    Q_OBJECT

public:
    explicit WindowButtons(QWidget* parent = nullptr);

public slots:
    /// Shows the restore icon while the window is maximized.
    void setMaximized(bool maximized);

signals:
    void minimizeRequested();
    void maximizeToggled();
    void closeRequested();

private:
    QToolButton* maximize_ = nullptr;
};

} // namespace vloomdaw
