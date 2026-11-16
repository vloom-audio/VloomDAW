// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/DroppedFiles.hpp"

#include "core/BrowserFilter.hpp"
#include "core/Project.hpp"
#include "ui/AudioDecoder.hpp"

#include <QDir>
#include <QFileInfo>
#include <QMimeData>
#include <QSet>
#include <QUrl>

namespace vloomdaw {

namespace {

QString localAudioPath(const QUrl& url)
{
    if (!url.isLocalFile() || !url.host().isEmpty())
        return {};
    const QString path = url.toLocalFile();
    if (path.startsWith(QLatin1String("//")) || path.startsWith(QLatin1String("\\\\")))
        return {};
    if (path.isEmpty() || (!isWavFile(path) && !needsDecoding(path)))
        return {};
    return path;
}

QString localMidiPath(const QUrl& url)
{
    if (!url.isLocalFile() || !url.host().isEmpty())
        return {};
    const QString path = url.toLocalFile();
    if (path.startsWith(QLatin1String("//")) || path.startsWith(QLatin1String("\\\\")))
        return {};
    if (path.isEmpty() || !isMidiFile(QFileInfo(path).fileName().toStdString()))
        return {};
    return path;
}

} // namespace

QStringList droppedAudioFiles(const QMimeData* mime)
{
    QStringList out;
    if (mime == nullptr || !mime->hasUrls())
        return out;
    QSet<QString> seen;
    for (const QUrl& url : mime->urls()) {
        const QString path = localAudioPath(url);
        if (path.isEmpty())
            continue;
        const QString clean = QDir::cleanPath(path);
        if (seen.contains(clean))
            continue;
        seen.insert(clean);
        out.push_back(path);
        if (out.size() >= Project::kMaxPlaylistTracks)
            break;
    }
    return out;
}

QString droppedAudio(const QMimeData* mime)
{
    if (mime == nullptr || !mime->hasUrls())
        return {};
    for (const QUrl& url : mime->urls()) {
        const QString path = localAudioPath(url);
        if (!path.isEmpty())
            return path;
    }
    return {};
}

QStringList droppedMidiFiles(const QMimeData* mime)
{
    QStringList out;
    if (mime == nullptr || !mime->hasUrls())
        return out;
    QSet<QString> seen;
    for (const QUrl& url : mime->urls()) {
        const QString path = localMidiPath(url);
        if (path.isEmpty())
            continue;
        const QString clean = QDir::cleanPath(path);
        if (seen.contains(clean))
            continue;
        seen.insert(clean);
        out.push_back(path);
        if (out.size() >= Project::kMaxPlaylistTracks)
            break;
    }
    return out;
}

QString droppedProjectFile(const QMimeData* mime)
{
    if (mime == nullptr || !mime->hasUrls())
        return {};
    for (const QUrl& url : mime->urls()) {
        if (!url.isLocalFile() || !url.host().isEmpty())
            continue;
        const QString path = url.toLocalFile();
        if (path.isEmpty() || path.startsWith(QLatin1String("//")) || path.startsWith(QLatin1String("\\\\")))
            continue;
        if (path.endsWith(QStringLiteral(".vloom"), Qt::CaseInsensitive))
            return path;
    }
    return {};
}

} // namespace vloomdaw
