// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QWidget>

class QHBoxLayout;
class QLabel;
class QResizeEvent;

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

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateElidedContext();

    QLabel* context_ = nullptr;
    QString fullContext_;
    QHBoxLayout* header_ = nullptr;
};

} // namespace vloomdaw
