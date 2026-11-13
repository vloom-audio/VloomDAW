// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/DroppedFiles.hpp"

#include <QFileInfo>
#include <QMimeData>
#include <QUrl>

namespace vloomdaw {

QString droppedWav(const QMimeData* mime)
{
    if (mime == nullptr || !mime->hasUrls())
        return {};
    for (const QUrl& url : mime->urls()) {
        if (!url.isLocalFile() || !url.host().isEmpty())
            continue;
        const QString path = url.toLocalFile();
        if (path.startsWith(QLatin1String("//")) || path.startsWith(QLatin1String("\\\\")))
            continue;
        if (!path.isEmpty()
            && QFileInfo(path).suffix().compare(QStringLiteral("wav"), Qt::CaseInsensitive) == 0)
            return path;
    }
    return {};
}

} // namespace vloomdaw
