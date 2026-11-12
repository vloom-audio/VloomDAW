// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/PluginStore.hpp"

#include <QSettings>
#include <QString>

namespace vloomdaw {

namespace {

constexpr QLatin1StringView kPlugins{"plugins/scanned"};
constexpr QLatin1StringView kLastScan{"plugins/lastScan"};
constexpr QLatin1StringView kFolders{"plugins/extraFolders"};
constexpr QLatin1StringView kProbe{"plugins/probe"};

QString probeKey(PluginInfo::Probe probe)
{
    switch (probe) {
    case PluginInfo::Probe::Ok:
        return QStringLiteral("ok");
    case PluginInfo::Probe::Failed:
        return QStringLiteral("failed");
    case PluginInfo::Probe::NotProbed:
        break;
    }
    return QStringLiteral("none");
}
PluginInfo::Probe probeFromKey(const QString& key)
{
    if (key == QLatin1String("ok"))
        return PluginInfo::Probe::Ok;
    if (key == QLatin1String("failed"))
        return PluginInfo::Probe::Failed;
    return PluginInfo::Probe::NotProbed;
}

std::string text(const QSettings& s, const char* key)
{
    return s.value(QLatin1String(key)).toString().toStdString();
}

QString formatKey(PluginFormat format)
{
    return format == PluginFormat::Clap ? QStringLiteral("clap") : QStringLiteral("vst3");
}
PluginFormat formatFromKey(const QString& key)
{
    return key == QLatin1String("clap") ? PluginFormat::Clap : PluginFormat::Vst3;
}

// Paths go through QString::fromStdWString / toStdWString on Windows so
// non-ASCII folder names survive.
QString toQString(const std::filesystem::path& p) { return QString::fromStdWString(p.wstring()); }
std::filesystem::path toPath(const QString& s) { return std::filesystem::path(s.toStdWString()); }

} // namespace

PluginStore PluginStore::load()
{
    QSettings s;
    PluginStore store;
    const int count = s.beginReadArray(kPlugins);
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        PluginInfo info;
        info.name = s.value(QStringLiteral("name")).toString().toStdString();
        info.format = formatFromKey(s.value(QStringLiteral("format")).toString());
        info.path = toPath(s.value(QStringLiteral("path")).toString());
        info.vendor = text(s, "vendor");
        info.version = text(s, "version");
        info.tags = text(s, "tags");
        info.category
            = s.value(QStringLiteral("category"), QStringLiteral("Uncategorized")).toString().toStdString();
        info.fileSize = s.value(QStringLiteral("size")).toULongLong();
        info.modified = s.value(QStringLiteral("modified")).toLongLong();
        info.probe = probeFromKey(s.value(QStringLiteral("probe")).toString());
        info.probeError = text(s, "probeError");
        info.userCategory = text(s, "userCategory");
        info.userInstrumentType = text(s, "userType");
        if (!info.name.empty())
            store.plugins.push_back(std::move(info));
    }
    s.endArray();
    store.lastScan = s.value(kLastScan).toDateTime();
    store.probePlugins = s.value(kProbe, true).toBool();

    const int folders = s.beginReadArray(kFolders);
    for (int i = 0; i < folders; ++i) {
        s.setArrayIndex(i);
        const QString path = s.value(QStringLiteral("path")).toString();
        if (!path.isEmpty())
            store.extraFolders.push_back(
                {formatFromKey(s.value(QStringLiteral("format")).toString()), toPath(path)});
    }
    s.endArray();
    return store;
}

void PluginStore::save() const
{
    QSettings s;
    s.remove(kPlugins);
    s.beginWriteArray(kPlugins, static_cast<int>(plugins.size()));
    for (int i = 0; i < static_cast<int>(plugins.size()); ++i) {
        const PluginInfo& info = plugins[static_cast<std::size_t>(i)];
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("name"), QString::fromStdString(info.name));
        s.setValue(QStringLiteral("format"), formatKey(info.format));
        s.setValue(QStringLiteral("path"), toQString(info.path));
        s.setValue(QStringLiteral("vendor"), QString::fromStdString(info.vendor));
        s.setValue(QStringLiteral("version"), QString::fromStdString(info.version));
        s.setValue(QStringLiteral("tags"), QString::fromStdString(info.tags));
        s.setValue(QStringLiteral("category"), QString::fromStdString(info.category));
        s.setValue(QStringLiteral("size"), static_cast<qulonglong>(info.fileSize));
        s.setValue(QStringLiteral("modified"), static_cast<qlonglong>(info.modified));
        s.setValue(QStringLiteral("probe"), probeKey(info.probe));
        s.setValue(QStringLiteral("probeError"), QString::fromStdString(info.probeError));
        s.setValue(QStringLiteral("userCategory"), QString::fromStdString(info.userCategory));
        s.setValue(QStringLiteral("userType"), QString::fromStdString(info.userInstrumentType));
    }
    s.endArray();
    s.setValue(kLastScan, lastScan);
    s.setValue(kProbe, probePlugins);

    s.remove(kFolders);
    s.beginWriteArray(kFolders, static_cast<int>(extraFolders.size()));
    for (int i = 0; i < static_cast<int>(extraFolders.size()); ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("format"), formatKey(extraFolders[static_cast<std::size_t>(i)].format));
        s.setValue(QStringLiteral("path"), toQString(extraFolders[static_cast<std::size_t>(i)].path));
    }
    s.endArray();
}

std::vector<PluginFolder> PluginStore::scanFolders() const
{
    std::vector<PluginFolder> folders = defaultPluginFolders();
    folders.insert(folders.end(), extraFolders.begin(), extraFolders.end());
    return folders;
}

} // namespace vloomdaw
