// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QWidget>

class QHBoxLayout;
class QLabel;

namespace vloomdaw {

/// A workspace panel: a header row with a title ("CHANNEL RACK"), a context
/// line ("Pattern 2 · Kick") and optional buttons, above the panel's body.
class PanelFrame : public QWidget {
    Q_OBJECT

public:
    PanelFrame(const QString& title, QWidget* body, QWidget* parent = nullptr);

    void setContext(const QString& text);
    /// Adds `widget` at the right end of the header.
    void addHeaderWidget(QWidget* widget);

private:
    QLabel* context_ = nullptr;
    QHBoxLayout* header_ = nullptr;
};

} // namespace vloomdaw
