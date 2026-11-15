// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QDialog>
#include <QString>

#include <cstddef>
#include <memory>

class QCryptographicHash;
class QFile;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QTreeWidget;

namespace vloomdaw {

struct StemModel;

/// The model store: the models the stem extractor can use, which of them are
/// on this computer, and which one is used.
///
/// Nothing is downloaded until the user presses Download. A model comes over
/// HTTPS from a fixed revision of its repository; each file must have the
/// size and the SHA-256 the app knows (core/StemModels), or it is deleted.
class StemModelStore : public QDialog {
    Q_OBJECT

public:
    /// `chosen`: the id of the model in use (may be empty or unknown).
    explicit StemModelStore(const QString& chosen, QWidget* parent = nullptr);
    ~StemModelStore() override;

    /// The model in use: one that is installed, or empty.
    [[nodiscard]] QString chosen() const { return chosen_; }

signals:
    /// The user picked another model to use (or the one in use was removed).
    void chosenChanged(const QString& id);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    [[nodiscard]] const StemModel* selected() const;
    void refresh();
    void download();
    void nextFile();
    void received();
    void fileFinished();
    void stopDownload(const QString& message);
    void remove();
    void use();
    void choose(const QString& id);

    QString chosen_;
    QTreeWidget* list_ = nullptr;
    QLabel* about_ = nullptr;
    QLabel* status_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QPushButton* downloadButton_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    QPushButton* useButton_ = nullptr;

    // The download under way, one file after another.
    QNetworkAccessManager* network_ = nullptr;
    QNetworkReply* reply_ = nullptr;
    const StemModel* downloading_ = nullptr;
    std::size_t fileIndex_ = 0;
    std::unique_ptr<QFile> part_; ///< "<name>.part" until it is whole and checked
    std::unique_ptr<QCryptographicHash> hash_;
    quint64 fileBytes_ = 0; ///< of the current file so far
    quint64 doneBytes_ = 0; ///< of the files before it
};

} // namespace vloomdaw
