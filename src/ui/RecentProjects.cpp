// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/RecentProjects.hpp"

#include <QDir>
#include <QSettings>

namespace vloomdaw::recent {

namespace {

const QString kKey = QStringLiteral("project/recent");

/// Paths compare as the file system does (case-insensitively on Windows).
bool same(const QString& a, const QString& b)
{
#if defined(Q_OS_WIN)
    return QDir::cleanPath(a).compare(QDir::cleanPath(b), Qt::CaseInsensitive) == 0;
#else
    return QDir::cleanPath(a) == QDir::cleanPath(b);
#endif
}

void store(QStringList list)
{
    while (list.size() > kMaxProjects)
        list.removeLast();
    QSettings().setValue(kKey, list);
}

} // namespace

QStringList projects()
{
    QStringList list = QSettings().value(kKey).toStringList();
    list.removeAll(QString());
    while (list.size() > kMaxProjects)
        list.removeLast();
    return list;
}

void add(const QString& path)
{
    QStringList list = projects();
    list.removeIf([&](const QString& p) { return same(p, path); });
    list.prepend(QDir::cleanPath(path));
    store(list);
}

void remove(const QString& path)
{
    QStringList list = projects();
    list.removeIf([&](const QString& p) { return same(p, path); });
    store(list);
}

void clear() { QSettings().remove(kKey); }

} // namespace vloomdaw::recent
