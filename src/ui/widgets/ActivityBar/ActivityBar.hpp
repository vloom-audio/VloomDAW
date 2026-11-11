// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QList>
#include <QWidget>

class QAction;
class QToolButton;
class QVBoxLayout;

namespace vloomdaw {

/// Narrow vertical strip of icons at the window edge that opens side panels.
///
/// At most one page is open. Clicking the open page's icon closes it, so the
/// strip doubles as the show / hide control for the side panel.
class ActivityBar : public QWidget {
    Q_OBJECT

public:
    explicit ActivityBar(QWidget* parent = nullptr);

    /// Adds a page icon at the top; returns its index.
    int addPage(const QIcon& icon, const QString& toolTip);
    /// Adds a plain command (e.g. Settings) at the bottom.
    void addBottomAction(QAction* action);

    /// Index of the open page, or -1 when the side panel is closed.
    [[nodiscard]] int current() const { return current_; }

public slots:
    /// Opens page `index` (-1 closes). Does not emit currentChanged().
    void setCurrent(int index);

signals:
    /// The user opened page `index`, or closed the panel (-1).
    void currentChanged(int index);

private:
    void syncButtons();

    QVBoxLayout* top_ = nullptr;
    QVBoxLayout* bottom_ = nullptr;
    QList<QToolButton*> pages_;
    int current_ = -1;
};

} // namespace vloomdaw
