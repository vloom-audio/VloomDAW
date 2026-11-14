// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/AudioDecoder.hpp"

#include "audio/FileDecoder.hpp"
#include "core/BrowserFilter.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <cmath>

namespace vloomdaw {

namespace {

/// Where the decoded copy of this version of `file` lives: named after a
/// hash of its path, size and modification time, so an edited file is
/// decoded again.
QString cachePath(const QFileInfo& file, int sampleRate)
{
    const QString key = file.absoluteFilePath() + QLatin1Char('\n') + QString::number(file.size())
        + QLatin1Char('\n') + QString::number(file.lastModified().toMSecsSinceEpoch()) + QLatin1Char('\n')
        + QString::number(sampleRate);
    const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex();
    const QString folder
        = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/decoded");
    return folder + QLatin1Char('/') + QString::fromLatin1(hash) + QStringLiteral(".wav");
}

} // namespace

bool isWavFile(const QString& path)
{
    return QFileInfo(path).suffix().compare(QStringLiteral("wav"), Qt::CaseInsensitive) == 0;
}

bool needsDecoding(const QString& path)
{
    return !isWavFile(path) && isAudioFile(QFileInfo(path).fileName().toStdString());
}

QString playableWav(const QString& path, double sampleRate, QString* error)
{
    if (isWavFile(path))
        return path;
    if (!needsDecoding(path)) {
        if (error != nullptr)
            *error = QStringLiteral("it is not an audio file the app can read");
        return {};
    }
    const QFileInfo file(path);
    if (!file.isFile()) {
        if (error != nullptr)
            *error = QStringLiteral("the file is missing");
        return {};
    }
    // Whole rates only (every device rate is); anything else: the file's own.
    const int rate
        = sampleRate >= 8000.0 && sampleRate <= 768000.0 ? static_cast<int>(std::lround(sampleRate)) : 0;
    const QString target = cachePath(file, rate);
    if (QFileInfo::exists(target))
        return target; // decoded before
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
        if (error != nullptr)
            *error = QStringLiteral("the cache folder cannot be created");
        return {};
    }
    // FFmpeg decodes (and resamples); the writer renames a finished file into
    // place, so a decode cut short (or two at once) never leaves a broken copy.
    std::string why;
    if (decodeToWav(path.toStdString(), target.toStdString(), rate, &why))
        return target;
    if (error != nullptr)
        *error = QString::fromStdString(why);
    return {};
}

} // namespace vloomdaw
