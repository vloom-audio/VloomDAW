// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QToolButton>

namespace vloomdaw {

/// Header button that moves a panel into its own window ("Detach") and back
/// ("Dock"). The owner says which state the panel is in with setDetached();
/// a click only emits clicked().
class DetachButton : public QToolButton {
    Q_OBJECT

public:
    /// `panelName` goes into the tool tip: "Detach the mixer".
    DetachButton(const QString& panelName, QWidget* parent = nullptr);

    void setDetached(bool detached);

private:
    QString panelName_;
};

} // namespace vloomdaw
