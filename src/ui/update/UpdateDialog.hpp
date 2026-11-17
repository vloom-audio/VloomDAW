// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/UpdateFeed.hpp"

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;
class QTextBrowser;

namespace vloomdaw {

class UpdateService;

/// "VloomDAW x.y.z is available": the release notes, and Install and
/// Restart (download, check, then installReady()), Later, or Skip This
/// Version. Where this copy cannot update itself, Install becomes Open
/// Download Page.
class UpdateDialog : public QDialog {
    Q_OBJECT

public:
    UpdateDialog(const UpdateRelease& release, UpdateService* service, QWidget* parent = nullptr);
    ~UpdateDialog() override;

    [[nodiscard]] const UpdateRelease& release() const { return release_; }

signals:
    /// The installer is downloaded and checked: save, start it and quit.
    void installReady(const QString& installer);
    /// Do not offer this version again (automatic checks only).
    void skipRequested(const QString& tag);

protected:
    void reject() override;

private:
    void install();
    void setDownloading(bool on);
    void openLink(const QUrl& url);

    UpdateRelease release_;
    UpdateService* service_ = nullptr;
    bool canInstall_ = false;
    bool downloading_ = false;
    QTextBrowser* notes_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* skip_ = nullptr;
    QPushButton* later_ = nullptr;
    QPushButton* install_ = nullptr;
};

} // namespace vloomdaw
