// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/update/UpdateInstaller.hpp"

#include "ui/update/UpdateService.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>

#include <cstdio>

namespace vloomdaw::UpdateInstaller {

namespace {

[[maybe_unused]] QString pidText() { return QString::number(QCoreApplication::applicationPid()); }

#if defined(Q_OS_MACOS)
/// VloomDAW.app that this process runs from, or empty.
QString appBundle()
{
    QDir dir(QCoreApplication::applicationDirPath()); // .../VloomDAW.app/Contents/MacOS
    if (!dir.cdUp() || !dir.cdUp() || !dir.absolutePath().endsWith(QStringLiteral(".app")))
        return {};
    return dir.absolutePath();
}

// Paths come in as arguments ($1..$3), never inside the script text.
// The old app is kept until the new one is in place, and put back if not.
constexpr const char* kMacScript = R"sh(
pid="$1"; dmg="$2"; app="$3"
while kill -0 "$pid" 2>/dev/null; do sleep 0.5; done
mnt=$(mktemp -d "${TMPDIR:-/tmp}/vloomdaw-update.XXXXXX") || exit 1
if hdiutil attach -nobrowse -readonly -noautoopen -mountpoint "$mnt" "$dmg"; then
  new="$app.update"; old="$app.old"
  rm -rf "$new" "$old"
  if [ -d "$mnt/VloomDAW.app" ] && ditto "$mnt/VloomDAW.app" "$new"; then
    if mv "$app" "$old"; then
      if mv "$new" "$app"; then rm -rf "$old"; else mv "$old" "$app"; fi
    fi
  fi
  rm -rf "$new"
  hdiutil detach "$mnt" -quiet || hdiutil detach "$mnt" -force -quiet
fi
rmdir "$mnt" 2>/dev/null
rm -f "$dmg"
open "$app"
)sh";
#endif

#if defined(Q_OS_LINUX)
/// The AppImage this process runs from, or empty (not an AppImage).
QString appImage()
{
    const QString path = qEnvironmentVariable("APPIMAGE");
    return !path.isEmpty() && QFileInfo(path).isFile() ? QFileInfo(path).absoluteFilePath() : QString();
}

constexpr const char* kLinuxScript = R"sh(
pid="$1"; app="$2"
while kill -0 "$pid" 2>/dev/null; do sleep 0.3; done
exec "$app"
)sh";
#endif

} // namespace

bool canInstall()
{
    if (!UpdateService::updateKey())
        return false;
#if defined(Q_OS_WIN)
    return true; // the installer asks for administrator rights if it needs them
#elif defined(Q_OS_MACOS)
    const QString app = appBundle();
    return !app.isEmpty() && QFileInfo(QFileInfo(app).absolutePath()).isWritable();
#elif defined(Q_OS_LINUX)
    const QString app = appImage();
    return !app.isEmpty() && QFileInfo(QFileInfo(app).absolutePath()).isWritable();
#else
    return false;
#endif
}

bool start(const QString& installer, QString* error)
{
    const auto fail = [error](const QString& why) {
        if (error != nullptr)
            *error = why;
        return false;
    };
    if (!canInstall())
        return fail(
            QCoreApplication::translate("UpdateInstaller", "This copy of VloomDAW cannot update itself."));
    if (!QFileInfo(installer).isFile())
        return fail(QCoreApplication::translate("UpdateInstaller", "The downloaded update is missing."));

#if defined(Q_OS_WIN)
    // packaging/windows/VloomDAW.iss reads /waitpid and /relaunch.
    const QStringList args = {QStringLiteral("/SILENT"), QStringLiteral("/SUPPRESSMSGBOXES"),
        QStringLiteral("/NORESTART"), QStringLiteral("/waitpid=") + pidText(), QStringLiteral("/relaunch=1")};
    if (!QProcess::startDetached(installer, args))
        return fail(QCoreApplication::translate("UpdateInstaller", "The installer could not be started."));
    return true;
#elif defined(Q_OS_MACOS)
    if (!QProcess::startDetached(QStringLiteral("/bin/sh"),
            {QStringLiteral("-c"), QString::fromLatin1(kMacScript), QStringLiteral("vloomdaw-update"),
                pidText(), installer, appBundle()}))
        return fail(QCoreApplication::translate("UpdateInstaller", "The update could not be started."));
    return true;
#elif defined(Q_OS_LINUX)
    // Replace the AppImage now: the running app keeps the old file open,
    // and rename() swaps the name atomically on the same file system.
    const QString app = appImage();
    const QString next = app + QStringLiteral(".update");
    QFile::remove(next);
    if (!QFile::copy(installer, next))
        return fail(QCoreApplication::translate("UpdateInstaller", "Could not write next to %1.").arg(app));
    QFile::setPermissions(next,
        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup
            | QFile::ReadOther | QFile::ExeOther);
    if (std::rename(QFile::encodeName(next).constData(), QFile::encodeName(app).constData()) != 0) {
        QFile::remove(next);
        return fail(QCoreApplication::translate("UpdateInstaller", "Could not replace %1.").arg(app));
    }
    QFile::remove(installer);
    // The new version is in place either way; this only restarts it.
    static_cast<void>(QProcess::startDetached(QStringLiteral("/bin/sh"),
        {QStringLiteral("-c"), QString::fromLatin1(kLinuxScript), QStringLiteral("vloomdaw-update"),
            pidText(), app}));
    return true;
#else
    return fail(
        QCoreApplication::translate("UpdateInstaller", "Updates cannot be installed on this system."));
#endif
}

} // namespace vloomdaw::UpdateInstaller
