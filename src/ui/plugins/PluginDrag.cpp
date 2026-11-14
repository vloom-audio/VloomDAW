// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/PluginDrag.hpp"

#include <QApplication>
#include <QMimeData>

namespace vloomdaw {

namespace {

const QString kMimeType = QStringLiteral("application/x-vloomdaw-plugin");

/// Ties a drag to this process, so a drag from another running copy of the
/// app (with another plugin list) is not taken for one of ours.
QByteArray processTag() { return QByteArray::number(QApplication::applicationPid()); }

} // namespace

QMimeData* pluginMimeData(std::size_t index)
{
    auto* data = new QMimeData;
    data->setData(kMimeType, processTag() + ':' + QByteArray::number(static_cast<qulonglong>(index)));
    return data;
}

std::optional<std::size_t> droppedPlugin(const QMimeData* data)
{
    if (data == nullptr || !data->hasFormat(kMimeType))
        return std::nullopt;
    const QList<QByteArray> parts = data->data(kMimeType).split(':');
    if (parts.size() != 2 || parts[0] != processTag())
        return std::nullopt;
    bool ok = false;
    const qulonglong index = parts[1].toULongLong(&ok);
    if (!ok)
        return std::nullopt;
    return static_cast<std::size_t>(index);
}

} // namespace vloomdaw
