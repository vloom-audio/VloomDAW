// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/DetachButton/DetachButton.hpp"

#include "ui/Theme.hpp"

#include <QIcon>

namespace vloomdaw {

DetachButton::DetachButton(const QString& panelName, QWidget* parent)
    : QToolButton(parent)
    , panelName_(panelName)
{
    setObjectName(QStringLiteral("panelClose")); // same look as the close button
    setIconSize(QSize(14, 14));
    setFocusPolicy(Qt::NoFocus);
    setDetached(false);
}

void DetachButton::setDetached(bool detached)
{
    setIcon(Theme::icon(detached ? QStringLiteral("dock") : QStringLiteral("detach")));
    const QString tip = detached ? tr("Dock the %1 back into the main window").arg(panelName_)
                                 : tr("Detach the %1 into its own window").arg(panelName_);
    setToolTip(tip);
    setAccessibleName(tip);
}

} // namespace vloomdaw
