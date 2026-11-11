// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/PositionDisplay/PositionDisplay.hpp"

#include <QMouseEvent>

namespace vloomdaw {

PositionDisplay::PositionDisplay(QWidget* parent)
    : LcdDisplay(parent)
{
    setObjectName(QStringLiteral("positionDisplay"));
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Song position — click to switch between bars and time"));
    showCurrent();
}

void PositionDisplay::setPosition(const QString& text)
{
    if (text == position_)
        return;
    position_ = text;
    if (mode_ == Mode::BarsBeats)
        showCurrent();
}

void PositionDisplay::setTime(const QString& text)
{
    if (text == time_)
        return;
    time_ = text;
    if (mode_ == Mode::Time)
        showCurrent();
}

void PositionDisplay::setMode(Mode mode)
{
    if (mode == mode_)
        return;
    mode_ = mode;
    showCurrent();
}

void PositionDisplay::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        LcdDisplay::mousePressEvent(event);
        return;
    }
    setMode(mode_ == Mode::BarsBeats ? Mode::Time : Mode::BarsBeats);
}

void PositionDisplay::showCurrent()
{
    // Both modes use three fields with the same minimum widths, so switching
    // modes keeps the panel the same size.
    if (mode_ == Mode::BarsBeats) {
        const QStringList p = position_.split(QLatin1Char('.'));
        if (p.size() != 3)
            return;
        setFields({
            {p[0], QStringLiteral("8888"), tr("BAR"), Separator::Dot},
            {p[1], QStringLiteral("88"), tr("BEAT"), Separator::Dot},
            {p[2], QStringLiteral("888"), tr("TICK"), Separator::Dot},
        });
        return;
    }

    const qsizetype colon = time_.indexOf(QLatin1Char(':'));
    const qsizetype dot = time_.indexOf(QLatin1Char('.'), colon + 1);
    if (colon < 0 || dot < 0)
        return;
    setFields({
        {time_.left(colon), QStringLiteral("8888"), tr("MIN"), Separator::Colon},
        {time_.mid(colon + 1, dot - colon - 1), QStringLiteral("88"), tr("SEC"), Separator::Dot},
        {time_.mid(dot + 1), QStringLiteral("888"), tr("MS"), Separator::Dot},
    });
}

} // namespace vloomdaw
