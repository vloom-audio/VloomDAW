// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/Minisign.hpp"
#include "core/UpdateFeed.hpp"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

class QCryptographicHash;
class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

namespace vloomdaw {

/// Finds and fetches VloomDAW updates from the GitHub Releases of
/// kUpdateRepository.
///
/// check() reads the list of releases (one HTTPS request to api.github.com,
/// nothing about the user is sent). download() then fetches, in order:
///   1. SHA256SUMS.txt and SHA256SUMS.txt.minisig, and checks the signature
///      with the public key built into the app (updateKey());
///   2. the installer, checking its SHA-256 against the signed list while it
///      streams to disk. A file that fails any check is deleted.
/// Only one request runs at a time.
class UpdateService : public QObject {
    Q_OBJECT

public:
    explicit UpdateService(QObject* parent = nullptr);
    ~UpdateService() override;

    /// The key update files must be signed with; nothing in a build made
    /// without one (packaging/update/minisign.pub), which then only points
    /// to the download page.
    [[nodiscard]] static const std::optional<MinisignKey>& updateKey();
    /// The version of this app.
    [[nodiscard]] static Version currentVersion();

    [[nodiscard]] bool busy() const { return reply_ != nullptr; }

    /// Emits updateAvailable(), upToDate() or checkFailed().
    void check(bool includePrereleases);
    /// Emits downloadProgress() and then downloaded() or downloadFailed().
    void download(const UpdateRelease& release);
    /// Stops what is under way, without a signal.
    void cancel();

signals:
    void updateAvailable(const vloomdaw::UpdateRelease& release);
    void upToDate();
    void checkFailed(const QString& reason);
    void downloadProgress(qint64 received, qint64 total);
    /// `path`: the installer, checked, in the update cache folder.
    void downloaded(const QString& path);
    void downloadFailed(const QString& reason);

private:
    using Done = std::function<void(const QByteArray& body)>;

    /// GETs a small file into memory: at most `maxBytes`, or it fails.
    void fetch(const QUrl& url, qint64 maxBytes, Done done, std::function<void(const QString&)> failed);
    void checksumsFetched(const QByteArray& sums, const QByteArray& signature);
    void startInstaller();
    void installerData();
    void installerFinished();
    void failDownload(const QString& reason);
    [[nodiscard]] static QString cacheFolder();

    QNetworkAccessManager* network_ = nullptr;
    QNetworkReply* reply_ = nullptr;

    // The download under way.
    std::optional<UpdateRelease> release_;
    QByteArray expectedSha256_; ///< hex, from the signed SHA256SUMS.txt
    std::unique_ptr<QFile> part_;
    std::unique_ptr<QCryptographicHash> hash_;
    qint64 received_ = 0;
};

} // namespace vloomdaw
