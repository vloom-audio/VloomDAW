// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/PerformanceMeter/PerformanceMeter.hpp"

#include <algorithm>
#include <cmath>

namespace vloomdaw {

namespace {
enum FieldIndex : qsizetype { kCpu = 0, kRam = 1 };
constexpr std::uint64_t kBytesPerMegabyte = 1024ULL * 1024ULL;
} // namespace

PerformanceMeter::PerformanceMeter(QWidget* parent)
    : LcdDisplay(parent)
{
    setObjectName(QStringLiteral("performanceMeter"));
    setToolTip(tr("CPU: share of each audio buffer the engine needs (near 100% audio drops out)\n"
                  "RAM: memory VloomDAW uses"));
    setFields({
        {QStringLiteral("0"), QStringLiteral("888"), tr("CPU %"), Separator::Rule},
        {QStringLiteral("-"), QStringLiteral("8888"), tr("RAM MB"), Separator::Dot},
    });
}

void PerformanceMeter::setCpuLoad(float load)
{
    const int percent = std::clamp(static_cast<int>(std::lround(load * 100.0f)), 0, 999);
    setFieldValue(kCpu, QString::number(percent));
}

void PerformanceMeter::setMemoryBytes(std::optional<std::uint64_t> bytes)
{
    if (!bytes) {
        setFieldValue(kRam, QStringLiteral("-"));
        return;
    }
    const auto megabytes = std::min<std::uint64_t>(*bytes / kBytesPerMegabyte, 9999);
    setFieldValue(kRam, QString::number(megabytes));
}

} // namespace vloomdaw
