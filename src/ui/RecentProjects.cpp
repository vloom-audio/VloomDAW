// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/RecentProjects.hpp"

#include <QDir>
#include <QSettings>

#include <algorithm>

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

namespace {
const QString kPinnedKey = QStringLiteral("project/pinned");
constexpr int kMaxPinned = 50;
} // namespace

QStringList pinned()
{
    QStringList list = QSettings().value(kPinnedKey).toStringList();
    list.removeAll(QString());
    return list.mid(0, kMaxPinned);
}

bool isPinned(const QString& path)
{
    const QStringList list = pinned();
    return std::any_of(list.begin(), list.end(), [&](const QString& p) { return same(p, path); });
}

void setPinned(const QString& path, bool pin)
{
    QStringList list = pinned();
    list.removeIf([&](const QString& p) { return same(p, path); });
    if (pin)
        list.prepend(QDir::cleanPath(path));
    QSettings().setValue(kPinnedKey, list.mid(0, kMaxPinned));
}

} // namespace vloomdaw::recent
