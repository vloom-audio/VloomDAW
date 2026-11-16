// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/FilePaths.hpp"

#include <QByteArray>
#include <QFile>

namespace vloomdaw {

QString toQString(const std::filesystem::path& path)
{
#if defined(Q_OS_WIN)
    return QString::fromStdWString(path.wstring());
#else
    return QFile::decodeName(QByteArray::fromStdString(path.native()));
#endif
}

std::filesystem::path toPath(const QString& path)
{
#if defined(Q_OS_WIN)
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(QFile::encodeName(path).toStdString());
#endif
}

bool isLocalPath(const QString& path) noexcept
{
    if (path.size() < 2)
        return !path.isEmpty();
    const auto isSep = [](QChar c) noexcept { return c == QLatin1Char('/') || c == QLatin1Char('\\'); };
    return !(isSep(path[0]) && isSep(path[1]));
}

} // namespace vloomdaw
