// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/stems/StemModelStore.hpp"

#include "core/StemModels.hpp"
#include "ui/FilePaths.hpp"
#include "ui/stems/StemSeparator.hpp"

#include <QCheckBox>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QStorageInfo>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace vloomdaw {

namespace {

constexpr int kIdRole = Qt::UserRole;
constexpr int kIdleTimeoutMs = 30000; // no data for this long: give up

QString text(std::string_view latin1)
{
    return QString::fromLatin1(latin1.data(), static_cast<qsizetype>(latin1.size()));
}

QString sizeText(std::uint64_t bytes) { return QLocale().formattedDataSize(static_cast<qint64>(bytes), 0); }

QString filePath(const StemModelFile& file)
{
    return QDir(StemSeparator::modelFolder()).filePath(text(file.name));
}

/// Whether the file is here, whole and the one we know (reads all of it).
bool intact(const StemModelFile& file)
{
    QFile onDisk(filePath(file));
    if (static_cast<quint64>(QFileInfo(onDisk).size()) != file.bytes || !onDisk.open(QIODevice::ReadOnly))
        return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&onDisk)
        && hash.result().toHex()
        == QByteArray(file.sha256.data(), static_cast<qsizetype>(file.sha256.size()));
}

bool installed(const StemModel& model)
{
    return stemModelInstalled(model, toPath(StemSeparator::modelFolder()));
}

} // namespace

StemModelStore::StemModelStore(const QString& chosen, bool useGpu, QWidget* parent)
    : QDialog(parent)
    , chosen_(chosen)
{
    setWindowTitle(tr("Stem Models"));
    setObjectName(QStringLiteral("stemModelStore"));
    resize(780, 460);

    auto* layout = new QVBoxLayout(this);
    auto* heading = new QLabel(tr("Stem Models"), this);
    heading->setObjectName(QStringLiteral("settingsHeading"));
    layout->addWidget(heading);
    auto* intro
        = new QLabel(tr("The stem extractor splits an audio clip into its parts with a Demucs or an "
                        "MDX-Net model. Download the one you want; it then works offline. Models come "
                        "from huggingface.co and github.com, and each file is checked before it is used."),
            this);
    intro->setObjectName(QStringLiteral("settingsSubtitle"));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    list_ = new QTreeWidget(this);
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->setHeaderLabels({tr("Model"), tr("Stems"), tr("Runs on"), tr("Download"), tr("Status")});
    for (const StemModel& model : stemModels()) {
        auto* row = new QTreeWidgetItem(list_);
        row->setData(0, kIdRole, text(model.id));
        row->setText(0, text(model.name));
        QStringList stems;
        for (const std::string_view stem : model.stems)
            stems << text(stem);
        row->setText(1, stems.join(QStringLiteral(", ")));
        row->setText(2,
            model.gpu                                     ? tr("Graphics card")
                : model.engine == StemModel::Engine::Onnx ? tr("Processor")
                                                          : tr("Processor (slow)"));
        row->setText(3, sizeText(model.bytes()));
    }
    list_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    list_->resizeColumnToContents(0);
    list_->setCurrentItem(list_->topLevelItem(0));
    layout->addWidget(list_, 1);

    about_ = new QLabel(this);
    about_->setWordWrap(true);
    layout->addWidget(about_);

    progress_ = new QProgressBar(this);
    progress_->setRange(0, 1000);
    progress_->setTextVisible(false);
    progress_->hide();
    layout->addWidget(progress_);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("settingsSubtitle"));
    status_->setWordWrap(true);
    layout->addWidget(status_);

    // Only where some model has a graphics-card path at all.
    bool anyGpu = false;
    for (const StemModel& model : stemModels())
        anyGpu = anyGpu || model.gpu;
    if (anyGpu) {
        gpu_ = new QCheckBox(
            tr("Use the graphics card for the models that can (falls back to the processor)"), this);
        gpu_->setToolTip(
            tr("Any DirectX 12 graphics card: NVIDIA, AMD or Intel. Much faster than the processor. If the "
               "card cannot run a model, or gives a wrong result, the processor takes over."));
        gpu_->setChecked(useGpu);
        connect(gpu_, &QCheckBox::toggled, this, &StemModelStore::useGpuChanged);
        layout->addWidget(gpu_);
    }

    auto* buttons = new QHBoxLayout;
    downloadButton_ = new QPushButton(this);
    removeButton_ = new QPushButton(tr("Remove"), this);
    useButton_ = new QPushButton(tr("Use This Model"), this);
    useButton_->setObjectName(QStringLiteral("accentButton"));
    auto* close = new QPushButton(tr("Close"), this);
    buttons->addWidget(downloadButton_);
    buttons->addWidget(removeButton_);
    buttons->addStretch(1);
    buttons->addWidget(useButton_);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    network_ = new QNetworkAccessManager(this);
    // HTTPS only, also where the server sends us on.
    network_->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    network_->setStrictTransportSecurityEnabled(true);

    connect(list_, &QTreeWidget::currentItemChanged, this, &StemModelStore::refresh);
    connect(list_, &QTreeWidget::itemDoubleClicked, this, &StemModelStore::use);
    connect(downloadButton_, &QPushButton::clicked, this, [this] {
        if (downloading_ != nullptr)
            stopDownload(tr("Download cancelled."));
        else
            download();
    });
    connect(removeButton_, &QPushButton::clicked, this, &StemModelStore::remove);
    connect(useButton_, &QPushButton::clicked, this, &StemModelStore::use);
    connect(close, &QPushButton::clicked, this, &QDialog::close);

    // The model in use must be one that is here.
    const StemModel* current = findStemModel(chosen_.toStdString());
    if (current == nullptr || !installed(*current))
        chosen_.clear();
    refresh();
}

StemModelStore::~StemModelStore() { stopDownload(QString()); }

void StemModelStore::closeEvent(QCloseEvent* event)
{
    if (downloading_ != nullptr) {
        const auto answer = QMessageBox::question(this, tr("Stem Models"),
            tr("A model is still downloading. Stop the download?"), QMessageBox::Yes | QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        stopDownload(tr("Download cancelled."));
    }
    QDialog::closeEvent(event);
}

bool StemModelStore::useGpu() const { return gpu_ != nullptr && gpu_->isChecked(); }

const StemModel* StemModelStore::selected() const
{
    const QTreeWidgetItem* row = list_->currentItem();
    return row != nullptr ? findStemModel(row->data(0, kIdRole).toString().toStdString()) : nullptr;
}

void StemModelStore::refresh()
{
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* row = list_->topLevelItem(i);
        const QString id = row->data(0, kIdRole).toString();
        const StemModel* model = findStemModel(id.toStdString());
        const bool here = model != nullptr && installed(*model);
        row->setText(4,
            model == downloading_ && downloading_ != nullptr ? tr("Downloading…")
                : id == chosen_ && here                      ? tr("In use")
                : here                                       ? tr("Downloaded")
                                                             : tr("Not downloaded"));
        QFont font = row->font(0);
        font.setBold(id == chosen_ && here);
        row->setFont(0, font);
    }
    const StemModel* model = selected();
    const bool here = model != nullptr && installed(*model);
    const bool busy = downloading_ != nullptr;
    about_->setText(model != nullptr ? text(model->about) : QString());
    downloadButton_->setText(busy ? tr("Cancel Download") : tr("Download"));
    // Also for a model that is here: Download checks its files and fetches a damaged one again.
    downloadButton_->setEnabled(busy || model != nullptr);
    removeButton_->setEnabled(!busy && here);
    useButton_->setEnabled(here && text(model->id) != chosen_);
    list_->setEnabled(!busy);
}

void StemModelStore::choose(const QString& id)
{
    if (id == chosen_)
        return;
    chosen_ = id;
    emit chosenChanged(chosen_);
}

void StemModelStore::use()
{
    const StemModel* model = selected();
    if (model == nullptr || !installed(*model) || downloading_ != nullptr)
        return;
    choose(text(model->id));
    status_->setText(tr("%1 is the model in use.").arg(text(model->name)));
    refresh();
}

void StemModelStore::remove()
{
    const StemModel* model = selected();
    if (model == nullptr || downloading_ != nullptr)
        return;
    for (const StemModelFile& file : model->files)
        QFile::remove(filePath(file));
    if (text(model->id) == chosen_)
        choose(QString());
    status_->setText(tr("%1 removed.").arg(text(model->name)));
    refresh();
}

void StemModelStore::download()
{
    const StemModel* model = selected();
    if (model == nullptr || downloading_ != nullptr)
        return;
    const QString folder = StemSeparator::modelFolder();
    if (!QDir().mkpath(folder)) {
        status_->setText(
            tr("Could not create the folder for models (%1).").arg(QDir::toNativeSeparators(folder)));
        return;
    }
    if (const QStorageInfo disk(folder);
        disk.isValid() && disk.bytesAvailable() < static_cast<qint64>(model->bytes() + model->bytes() / 10)) {
        status_->setText(
            tr("Not enough room on the disk for %1 (%2).").arg(text(model->name), sizeText(model->bytes())));
        return;
    }
    downloading_ = model;
    fileIndex_ = 0;
    doneBytes_ = 0;
    progress_->setValue(0);
    progress_->show();
    refresh();
    nextFile();
}

void StemModelStore::nextFile()
{
    // Files already here and whole (an earlier download of the set) are kept.
    while (fileIndex_ < downloading_->files.size()) {
        const StemModelFile& file = downloading_->files[fileIndex_];
        if (!intact(file))
            break; // missing, or not the file it should be: fetched (again)
        doneBytes_ += file.bytes;
        ++fileIndex_;
    }
    if (fileIndex_ >= downloading_->files.size()) {
        const StemModel* model = downloading_;
        downloading_ = nullptr;
        progress_->hide();
        status_->setText(tr("%1 downloaded.").arg(text(model->name)));
        if (chosen_.isEmpty())
            choose(text(model->id)); // the first model is used at once
        refresh();
        return;
    }
    const StemModelFile& file = downloading_->files[fileIndex_];
    part_ = std::make_unique<QFile>(filePath(file) + QStringLiteral(".part"));
    if (!part_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        stopDownload(tr("Could not write to the folder for models."));
        return;
    }
    hash_ = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    fileBytes_ = 0;
    QNetworkRequest request(QUrl(QString::fromStdString(stemModelUrl(*downloading_, file))));
    request.setTransferTimeout(kIdleTimeoutMs);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    reply_ = network_->get(request);
    connect(reply_, &QNetworkReply::readyRead, this, &StemModelStore::received);
    connect(reply_, &QNetworkReply::finished, this, &StemModelStore::fileFinished);
    status_->setText(tr("Downloading %1 (file %2 of %3)…")
            .arg(text(downloading_->name))
            .arg(fileIndex_ + 1)
            .arg(downloading_->files.size()));
}

void StemModelStore::received()
{
    if (reply_ == nullptr || downloading_ == nullptr)
        return;
    const QByteArray chunk = reply_->readAll();
    const StemModelFile& file = downloading_->files[fileIndex_];
    fileBytes_ += static_cast<quint64>(chunk.size());
    if (fileBytes_ > file.bytes) {
        // More than the file we know: not it. Stop before it fills the disk.
        stopDownload(tr("The download is not the file that was expected. Nothing was kept."));
        return;
    }
    hash_->addData(chunk);
    if (part_->write(chunk) != chunk.size()) {
        stopDownload(tr("Could not write the download (is the disk full?)."));
        return;
    }
    const double total = static_cast<double>(downloading_->bytes());
    progress_->setValue(static_cast<int>(1000.0 * static_cast<double>(doneBytes_ + fileBytes_) / total));
}

void StemModelStore::fileFinished()
{
    if (reply_ == nullptr || downloading_ == nullptr)
        return;
    received(); // what is left in the reply
    if (reply_ == nullptr)
        return; // received() stopped the download
    const QNetworkReply::NetworkError error = reply_->error();
    const QString reason = reply_->errorString();
    reply_->deleteLater();
    reply_ = nullptr;
    const StemModelFile& file = downloading_->files[fileIndex_];
    part_->close();
    if (error != QNetworkReply::NoError) {
        stopDownload(tr("The download failed: %1").arg(reason));
        return;
    }
    const QByteArray expected(file.sha256.data(), static_cast<qsizetype>(file.sha256.size()));
    if (fileBytes_ != file.bytes || hash_->result().toHex() != expected) {
        stopDownload(tr("The download does not match the file that was expected. Nothing was kept."));
        return;
    }
    const QString target = filePath(file);
    QFile::remove(target);
    if (!part_->rename(target)) {
        stopDownload(tr("Could not write to the folder for models."));
        return;
    }
    part_.reset();
    doneBytes_ += file.bytes;
    ++fileIndex_;
    nextFile();
}

void StemModelStore::stopDownload(const QString& message)
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
    if (downloading_ == nullptr)
        return;
    downloading_ = nullptr;
    progress_->hide();
    status_->setText(message);
    refresh();
}

} // namespace vloomdaw
