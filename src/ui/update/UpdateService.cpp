// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/update/UpdateService.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUrl>

#include <utility>

#ifndef VLOOMDAW_UPDATE_PUBLIC_KEY
#define VLOOMDAW_UPDATE_PUBLIC_KEY ""
#endif

namespace vloomdaw {

namespace {

constexpr int kTimeoutMs = 30000; // no data for this long: give up
constexpr qint64 kMaxReleaseList = 4 * 1024 * 1024;
constexpr qint64 kMaxChecksums = 64 * 1024;
constexpr qint64 kMaxSignature = 4 * 1024;
constexpr qint64 kMaxInstaller = qint64(2) * 1024 * 1024 * 1024;

QString fromStd(const std::string& s) { return QString::fromStdString(s); }

QNetworkRequest request(const QUrl& url)
{
    QNetworkRequest r(url);
    r.setTransferTimeout(kTimeoutMs);
    r.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    // GitHub's API wants a User-Agent: the app and its version, nothing else.
    r.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("VloomDAW/%1").arg(QString::fromLatin1(VLOOMDAW_VERSION)));
    return r;
}

/// The reply's failure, or empty if it brought a 200 OK.
QString replyError(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError)
        return reply->errorString();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 403 || status == 429)
        return QCoreApplication::translate("UpdateService", "GitHub is limiting requests; try again later");
    if (status != 200)
        return QCoreApplication::translate("UpdateService", "the server answered %1").arg(status);
    return {};
}

} // namespace

UpdateService::UpdateService(QObject* parent)
    : QObject(parent)
    , network_(new QNetworkAccessManager(this))
{
    // HTTPS only, also where GitHub sends downloads on (its asset storage).
    network_->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    network_->setStrictTransportSecurityEnabled(true);

    // Installers from an earlier update (it ran, or was not wanted).
    const QString folder = cacheFolder();
    if (!folder.isEmpty())
        QDir(folder).removeRecursively();
}

UpdateService::~UpdateService() { cancel(); }

const std::optional<MinisignKey>& UpdateService::updateKey()
{
    static const std::optional<MinisignKey> key = parseMinisignKey(VLOOMDAW_UPDATE_PUBLIC_KEY);
    return key;
}

Version UpdateService::currentVersion()
{
    static const Version version = parseVersion(VLOOMDAW_VERSION).value_or(Version{});
    return version;
}

QString UpdateService::cacheFolder()
{
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return cache.isEmpty() ? QString() : QDir(cache).filePath(QStringLiteral("updates"));
}

void UpdateService::cancel()
{
    if (reply_ != nullptr) {
        reply_->disconnect(this);
        reply_->abort();
        reply_->deleteLater();
        reply_ = nullptr;
    }
    if (part_ != nullptr) {
        part_->close();
        part_->remove();
        part_.reset();
    }
    hash_.reset();
    release_.reset();
}

void UpdateService::fetch(
    const QUrl& url, qint64 maxBytes, Done done, std::function<void(const QString&)> failed)
{
    auto body = std::make_shared<QByteArray>();
    reply_ = network_->get(request(url));
    QNetworkReply* reply = reply_;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, body, maxBytes, failed] {
        body->append(reply->readAll());
        if (body->size() > maxBytes) {
            cancel();
            failed(tr("the server sent more than expected"));
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, body, maxBytes, done, failed] {
        if (reply != reply_)
            return;
        body->append(reply->readAll());
        const QString error = replyError(reply);
        reply_->deleteLater();
        reply_ = nullptr;
        if (!error.isEmpty()) {
            failed(error);
            return;
        }
        if (body->size() > maxBytes) {
            failed(tr("the server sent more than expected"));
            return;
        }
        done(*body);
    });
}

void UpdateService::check(bool includePrereleases)
{
    if (busy())
        return;
    const QUrl url(QStringLiteral("https://api.github.com/repos/%1/releases?per_page=30")
            .arg(QString::fromLatin1(
                kUpdateRepository.data(), static_cast<qsizetype>(kUpdateRepository.size()))));
    fetch(
        url, kMaxReleaseList,
        [this, includePrereleases](const QByteArray& body) {
            const UpdateFeedResult result = newestUpdate(std::string_view(body.constData(), body.size()),
                currentVersion(), includePrereleases, currentUpdatePlatform());
            if (!result.error.empty())
                emit checkFailed(fromStd(result.error));
            else if (result.release)
                emit updateAvailable(*result.release);
            else
                emit upToDate();
        },
        [this](const QString& reason) { emit checkFailed(reason); });
}

void UpdateService::download(const UpdateRelease& release)
{
    if (busy())
        return;
    if (!updateKey()) {
        emit downloadFailed(tr("this build of VloomDAW cannot check updates"));
        return;
    }
    if (release.installer.size > static_cast<std::uint64_t>(kMaxInstaller)) {
        emit downloadFailed(tr("the installer is larger than any VloomDAW installer should be"));
        return;
    }
    release_ = release;
    const auto failed = [this](const QString& reason) { failDownload(reason); };
    fetch(
        QUrl(fromStd(release.checksums.url)), kMaxChecksums,
        [this, failed](const QByteArray& sums) {
            if (!release_)
                return;
            fetch(
                QUrl(fromStd(release_->signature.url)), kMaxSignature,
                [this, sums](const QByteArray& signature) { checksumsFetched(sums, signature); }, failed);
        },
        failed);
}

void UpdateService::checksumsFetched(const QByteArray& sums, const QByteArray& signature)
{
    if (!release_)
        return;
    std::string why;
    const std::string_view sumsText(sums.constData(), static_cast<std::size_t>(sums.size()));
    if (!minisignVerify(*updateKey(), sumsText,
            std::string_view(signature.constData(), static_cast<std::size_t>(signature.size())), &why)) {
        failDownload(tr("the update is not signed by the VloomDAW developers (%1)").arg(fromStd(why)));
        return;
    }
    const auto expected = checksumFor(sumsText, release_->installer.name);
    if (!expected) {
        failDownload(tr("the signed checksum list does not name the installer"));
        return;
    }
    expectedSha256_ = QByteArray::fromStdString(*expected);
    startInstaller();
}

void UpdateService::startInstaller()
{
    const QString folder = cacheFolder();
    if (folder.isEmpty() || !QDir().mkpath(folder)) {
        failDownload(tr("could not create a folder for the download"));
        return;
    }
    const auto size = static_cast<qint64>(release_->installer.size);
    if (const QStorageInfo disk(folder); disk.isValid() && disk.bytesAvailable() < size + size / 10) {
        failDownload(tr("not enough room on the disk"));
        return;
    }
    part_ = std::make_unique<QFile>(
        QDir(folder).filePath(fromStd(release_->installer.name) + QStringLiteral(".part")));
    if (!part_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        failDownload(tr("could not write the download"));
        return;
    }
    hash_ = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    received_ = 0;
    reply_ = network_->get(request(QUrl(fromStd(release_->installer.url))));
    connect(reply_, &QNetworkReply::readyRead, this, &UpdateService::installerData);
    connect(reply_, &QNetworkReply::finished, this, &UpdateService::installerFinished);
    emit downloadProgress(0, size);
}

void UpdateService::installerData()
{
    if (reply_ == nullptr || !release_)
        return;
    const QByteArray chunk = reply_->readAll();
    received_ += chunk.size();
    const auto size = static_cast<qint64>(release_->installer.size);
    if (received_ > size) {
        // More than the release says: not its installer. Stop before it fills the disk.
        failDownload(tr("the download is not the installer that was expected"));
        return;
    }
    hash_->addData(chunk);
    if (part_->write(chunk) != chunk.size()) {
        failDownload(tr("could not write the download (is the disk full?)"));
        return;
    }
    emit downloadProgress(received_, size);
}

void UpdateService::installerFinished()
{
    if (reply_ == nullptr || !release_)
        return;
    installerData(); // what is left in the reply
    if (reply_ == nullptr)
        return; // installerData() failed the download
    const QString error = replyError(reply_);
    reply_->deleteLater();
    reply_ = nullptr;
    if (!error.isEmpty()) {
        failDownload(error);
        return;
    }
    part_->close();
    if (hash_->result().toHex() != expectedSha256_) {
        failDownload(tr("the download does not match the signed checksum"));
        return;
    }
    const QString target = part_->fileName().chopped(5); // without ".part"
    QFile::remove(target);
    if (!part_->rename(target)) {
        failDownload(tr("could not write the download"));
        return;
    }
    part_.reset();
    hash_.reset();
    release_.reset();
    emit downloaded(target);
}

void UpdateService::failDownload(const QString& reason)
{
    cancel();
    emit downloadFailed(reason);
}

} // namespace vloomdaw
