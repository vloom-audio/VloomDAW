// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/ParameterText.hpp"

namespace vloomdaw {

QString parameterValueText(std::string_view unit, double value)
{
    if (unit == "Hz")
        return value >= 1000.0 ? QStringLiteral("%1 kHz").arg(value / 1000.0, 0, 'f', 2)
                               : QStringLiteral("%1 Hz").arg(value, 0, 'f', value < 100.0 ? 1 : 0);
    if (unit == "dB")
        return QStringLiteral("%1%2 dB")
            .arg(value > 0.0 ? QStringLiteral("+") : QString())
            .arg(value, 0, 'f', 1);
    const QString number = QString::number(value, 'f', 2);
    return unit.empty() ? number
                        : QStringLiteral("%1 %2").arg(
                              number, QString::fromUtf8(unit.data(), static_cast<qsizetype>(unit.size())));
}

} // namespace vloomdaw
