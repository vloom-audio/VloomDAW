// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/DroppedFiles.hpp"

#include "ui/AudioDecoder.hpp"

#include <QMimeData>
#include <QUrl>

namespace vloomdaw {

QString droppedAudio(const QMimeData* mime)
{
    if (mime == nullptr || !mime->hasUrls())
        return {};
    for (const QUrl& url : mime->urls()) {
        if (!url.isLocalFile() || !url.host().isEmpty())
            continue;
        const QString path = url.toLocalFile();
        if (path.startsWith(QLatin1String("//")) || path.startsWith(QLatin1String("\\\\")))
            continue;
        if (!path.isEmpty() && (isWavFile(path) || needsDecoding(path)))
            return path;
    }
    return {};
}

} // namespace vloomdaw
