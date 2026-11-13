// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QFrame>
#include <QStringList>

class QAction;
class QActionGroup;
class QMenu;
class QToolButton;

namespace vloomdaw {

/// "[ Pattern 1 ▾ | + | − ]": picks the current pattern from a menu, adds
/// and deletes patterns, like FL Studio's pattern selector.
///
/// A pure view of the project's patterns: it shows the names and order it is
/// given with setPatterns() and only emits what the user asked for; the
/// project (through MainWindow) adds, deletes, renames and reorders them.
/// Indices are 0-based, as in core/Project.
class PatternSelector : public QFrame {
    Q_OBJECT

public:
    explicit PatternSelector(QSize iconSize, QWidget* parent = nullptr);

    /// Shows these patterns with `current` selected. Never emits.
    void setPatterns(const QStringList& names, int current);
    [[nodiscard]] int current() const { return current_; }
    [[nodiscard]] int count() const { return static_cast<int>(names_.size()); }

signals:
    /// The user picked pattern `index` from the menu.
    void patternPicked(int index);
    /// The user asked for a new pattern (+ or the menu).
    void addRequested();
    /// The user asked to delete the current pattern (− or the menu).
    void deleteRequested();

private:
    void sync(); ///< label, check mark and enabled state follow names_ / current_

    QToolButton* button_ = nullptr;
    QToolButton* deleteButton_ = nullptr;
    QMenu* menu_ = nullptr;
    QActionGroup* group_ = nullptr; ///< exclusive; action i is pattern i
    QAction* separator_ = nullptr; ///< pattern actions go above it
    QAction* deleteAction_ = nullptr;
    QStringList names_{QStringLiteral("Pattern 1")};
    int current_ = 0;
};

} // namespace vloomdaw
