// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QFrame>

class QAction;
class QActionGroup;
class QMenu;
class QToolButton;

namespace vloomdaw {

/// "[ Pattern 1 ▾ | + | − ]": picks the current pattern from a menu, adds
/// and deletes patterns, like FL Studio's pattern selector. Patterns are
/// numbered from 1; deleting one renumbers the ones after it. There is
/// always at least one pattern.
class PatternSelector : public QFrame {
    Q_OBJECT

public:
    explicit PatternSelector(QSize iconSize, QWidget* parent = nullptr);

    [[nodiscard]] int current() const { return current_; }
    [[nodiscard]] int count() const { return count_; }

public slots:
    /// Selects pattern `number` (clamped to 1..count) without emitting.
    void setCurrent(int number);
    /// Adds a pattern after the last one and selects it.
    void addPattern();
    /// Deletes the current pattern (not the last one left) and selects the
    /// one before it.
    void deleteCurrentPattern();
    /// Back to a single "Pattern 1", without emitting (a new project).
    void reset();

signals:
    /// The user picked, added or deleted a pattern; `number` is now current.
    void currentChanged(int number);
    /// Pattern `number` was deleted; patterns after it moved down by one.
    void patternDeleted(int number);

private:
    void appendAction(int number);
    void sync(); ///< label, check mark and enabled state follow the model

    QToolButton* button_ = nullptr;
    QToolButton* deleteButton_ = nullptr;
    QMenu* menu_ = nullptr;
    QActionGroup* group_ = nullptr; ///< exclusive; action i is pattern i + 1
    QAction* separator_ = nullptr; ///< pattern actions go above it
    QAction* deleteAction_ = nullptr;
    int count_ = 1;
    int current_ = 1;
};

} // namespace vloomdaw
