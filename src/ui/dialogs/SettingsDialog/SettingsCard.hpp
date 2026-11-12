// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QFrame>

class QLabel;
class QVBoxLayout;

namespace vloomdaw {

/// A titled group of settings on a Settings page: a rounded card holding
/// rows, each with a name, a short description and its control on the right.
/// Rows are separated by hairlines. Styled by "QFrame#settingsCard" in Theme.
class SettingsCard : public QFrame {
    Q_OBJECT

public:
    explicit SettingsCard(const QString& title, QWidget* parent = nullptr);

    /// Adds a row; `control` is reparented. Returns the description label,
    /// for rows whose description changes (e.g. the measured latency).
    QLabel* addRow(const QString& name, const QString& description, QWidget* control);
    /// Adds a full-width widget (e.g. a grid of theme tiles) under the rows.
    void addWide(QWidget* widget);

private:
    void addSeparatorIfNeeded();

    QVBoxLayout* rows_ = nullptr;
    int count_ = 0;
};

} // namespace vloomdaw
