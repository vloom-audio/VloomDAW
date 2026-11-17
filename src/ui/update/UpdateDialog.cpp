// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/update/UpdateDialog.hpp"

#include "ui/update/UpdateInstaller.hpp"
#include "ui/update/UpdateService.hpp"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace vloomdaw {

namespace {
QString fromStd(const std::string& s) { return QString::fromStdString(s); }
} // namespace

UpdateDialog::UpdateDialog(const UpdateRelease& release, UpdateService* service, QWidget* parent)
    : QDialog(parent)
    , release_(release)
    , service_(service)
    , canInstall_(UpdateInstaller::canInstall())
{
    setWindowTitle(tr("Software Update"));
    setObjectName(QStringLiteral("updateDialog"));
    resize(560, 460);

    auto* layout = new QVBoxLayout(this);
    const QString version = fromStd(release.version.toString());
    auto* heading = new QLabel(release.prerelease ? tr("VloomDAW %1 (beta) is available").arg(version)
                                                  : tr("VloomDAW %1 is available").arg(version),
        this);
    heading->setObjectName(QStringLiteral("settingsHeading"));
    layout->addWidget(heading);
    auto* subtitle = new QLabel(
        tr("You have %1. VloomDAW offers to save your project first, and starts again after the update.")
            .arg(fromStd(UpdateService::currentVersion().toString())),
        this);
    subtitle->setObjectName(QStringLiteral("settingsSubtitle"));
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);

    // The notes are text from GitHub: links open only as web pages, in the
    // browser (never file: or other schemes), and nothing is fetched.
    notes_ = new QTextBrowser(this);
    notes_->setOpenLinks(false);
    notes_->setMarkdown(release.notes.empty() ? tr("No release notes.") : fromStd(release.notes));
    connect(notes_, &QTextBrowser::anchorClicked, this, &UpdateDialog::openLink);
    layout->addWidget(notes_, 1);

    progress_ = new QProgressBar(this);
    progress_->setRange(0, 1000);
    progress_->setTextVisible(false);
    progress_->hide();
    layout->addWidget(progress_);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("settingsSubtitle"));
    status_->setWordWrap(true);
    layout->addWidget(status_);
    if (!canInstall_)
        status_->setText(
            tr("This copy of VloomDAW cannot update itself; download the new version from its page."));

    auto* buttons = new QHBoxLayout;
    skip_ = new QPushButton(tr("Skip This Version"), this);
    later_ = new QPushButton(tr("Later"), this);
    install_ = new QPushButton(canInstall_ ? tr("Install and Restart") : tr("Open Download Page"), this);
    install_->setObjectName(QStringLiteral("accentButton"));
    install_->setDefault(true);
    buttons->addWidget(skip_);
    buttons->addStretch(1);
    buttons->addWidget(later_);
    buttons->addWidget(install_);
    layout->addLayout(buttons);

    connect(skip_, &QPushButton::clicked, this, [this] {
        emit skipRequested(fromStd(release_.tag));
        close();
    });
    connect(later_, &QPushButton::clicked, this, &QDialog::reject);
    connect(install_, &QPushButton::clicked, this, &UpdateDialog::install);

    connect(service_, &UpdateService::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (!downloading_)
            return;
        progress_->setValue(total > 0 ? static_cast<int>(1000 * received / total) : 0);
        status_->setText(tr("Downloading %1 of %2…")
                .arg(QLocale().formattedDataSize(received, 1), QLocale().formattedDataSize(total, 1)));
    });
    connect(service_, &UpdateService::downloaded, this, [this](const QString& path) {
        if (!downloading_)
            return;
        setDownloading(false);
        status_->setText(tr("Downloaded and checked."));
        emit installReady(path);
    });
    connect(service_, &UpdateService::downloadFailed, this, [this](const QString& reason) {
        if (!downloading_)
            return;
        setDownloading(false);
        status_->setText(tr("The update was not installed: %1.").arg(reason));
    });
}

UpdateDialog::~UpdateDialog()
{
    if (downloading_)
        service_->cancel();
}

void UpdateDialog::reject()
{
    if (downloading_) {
        service_->cancel();
        setDownloading(false);
        status_->setText(tr("Download cancelled."));
        return; // first Escape or Later stops the download; the next closes
    }
    QDialog::reject();
}

void UpdateDialog::install()
{
    if (!canInstall_) {
        openLink(QUrl(fromStd(release_.pageUrl)));
        return;
    }
    if (downloading_ || service_->busy())
        return;
    setDownloading(true);
    status_->setText(tr("Checking the signature…"));
    service_->download(release_);
}

void UpdateDialog::setDownloading(bool on)
{
    downloading_ = on;
    progress_->setVisible(on);
    progress_->setValue(0);
    install_->setEnabled(!on);
    skip_->setEnabled(!on);
    later_->setText(on ? tr("Cancel") : tr("Later"));
}

void UpdateDialog::openLink(const QUrl& url)
{
    if (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http"))
        QDesktopServices::openUrl(url);
}

} // namespace vloomdaw
