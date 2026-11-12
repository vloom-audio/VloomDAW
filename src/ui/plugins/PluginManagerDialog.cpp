// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/PluginManagerDialog.hpp"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <memory>

namespace vloomdaw {

namespace {

constexpr int kExtraRole = Qt::UserRole; ///< index into extraFolders, -1 for a default folder

QString pathText(const std::filesystem::path& p)
{
    return QDir::toNativeSeparators(QString::fromStdWString(p.wstring()));
}

} // namespace

PluginManagerDialog::PluginManagerDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("pluginManager"));
    setWindowTitle(tr("Plugin Manager"));
    setMinimumSize(620, 440);
    resize(720, 500);

    auto* heading = new QLabel(tr("Plugin folders"), this);
    heading->setObjectName(QStringLiteral("settingsHeading"));
    auto* intro = new QLabel(tr("Plugins can be used once they have been found by a scan. Nothing is scanned "
                                "automatically: press Scan after installing or removing plugins. Scanning "
                                "only reads file names; no plugin is loaded."),
        this);
    intro->setObjectName(QStringLiteral("settingsSubtitle"));
    intro->setWordWrap(true);

    folders_ = new QTreeWidget(this);
    folders_->setColumnCount(3);
    folders_->setHeaderLabels({tr("Format"), tr("Folder"), tr("")});
    folders_->setRootIsDecorated(false);
    folders_->setUniformRowHeights(true);
    folders_->header()->setStretchLastSection(false);
    folders_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    folders_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    folders_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    connect(folders_, &QTreeWidget::itemSelectionChanged, this, [this] {
        const auto selected = folders_->selectedItems();
        removeButton_->setEnabled(!selected.isEmpty()
            && selected.constFirst()->data(0, kExtraRole).toInt() >= 0 && scanThread_ == nullptr);
    });

    addButton_ = new QPushButton(tr("Add Folder"), this);
    auto* addMenu = new QMenu(addButton_);
    addMenu->addAction(tr("VST3 Folder…"), this, [this] { addFolder(PluginFormat::Vst3); });
    addMenu->addAction(tr("CLAP Folder…"), this, [this] { addFolder(PluginFormat::Clap); });
    addButton_->setMenu(addMenu);
    removeButton_ = new QPushButton(tr("Remove"), this);
    removeButton_->setEnabled(false);
    removeButton_->setToolTip(tr("Remove the selected folder you added. Default folders stay."));
    connect(removeButton_, &QPushButton::clicked, this, &PluginManagerDialog::removeFolder);

    scanButton_ = new QPushButton(tr("Scan"), this);
    scanButton_->setObjectName(QStringLiteral("accentButton"));
    scanButton_->setDefault(true);
    connect(scanButton_, &QPushButton::clicked, this, &PluginManagerDialog::startScan);

    progress_ = new QProgressBar(this);
    progress_->setRange(0, 0); // busy: the scan does not know its total
    progress_->setTextVisible(false);
    progress_->setFixedHeight(4);
    progress_->hide();

    result_ = new QLabel(this);
    result_->setWordWrap(true);

    auto* folderButtons = new QHBoxLayout;
    folderButtons->addWidget(addButton_);
    folderButtons->addWidget(removeButton_);
    folderButtons->addStretch(1);
    folderButtons->addWidget(scanButton_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(10);
    layout->addWidget(heading);
    layout->addWidget(intro);
    layout->addWidget(folders_, 1);
    layout->addLayout(folderButtons);
    layout->addWidget(progress_);
    layout->addWidget(result_);
    layout->addWidget(buttons);
}

PluginManagerDialog::~PluginManagerDialog()
{
    // The worker only touches its own copies, but it must end before its
    // QThread object goes.
    if (scanThread_ != nullptr) {
        scanThread_->disconnect(this);
        scanThread_->wait();
        delete scanThread_;
    }
}

void PluginManagerDialog::setStore(const PluginStore& store)
{
    store_ = store;
    listFolders();
    showResult();
}

void PluginManagerDialog::listFolders()
{
    folders_->clear();
    const auto add = [this](const PluginFolder& folder, int extraIndex) {
        auto* item = new QTreeWidgetItem(folders_);
        item->setText(0, QString::fromUtf8(formatName(folder.format)));
        item->setText(1, pathText(folder.path));
        item->setToolTip(1, item->text(1));
        std::error_code ec;
        const bool exists = std::filesystem::is_directory(folder.path, ec);
        item->setText(2,
            extraIndex < 0 ? (exists ? tr("Default") : tr("Default, missing"))
                           : (exists ? tr("Added") : tr("Missing")));
        item->setData(0, kExtraRole, extraIndex);
        if (!exists)
            item->setForeground(1, palette().color(QPalette::PlaceholderText));
    };
    for (const PluginFolder& folder : defaultPluginFolders())
        add(folder, -1);
    for (int i = 0; i < static_cast<int>(store_.extraFolders.size()); ++i)
        add(store_.extraFolders[static_cast<std::size_t>(i)], i);
    removeButton_->setEnabled(false);
}

void PluginManagerDialog::addFolder(PluginFormat format)
{
    const QString title = tr("Add a %1 folder").arg(QString::fromUtf8(formatName(format)));
    const QString path = QFileDialog::getExistingDirectory(this, title, QDir::homePath());
    if (path.isEmpty())
        return;
    const std::filesystem::path chosen(QDir::cleanPath(path).toStdWString());
    for (const PluginFolder& f : store_.scanFolders()) {
        std::error_code ec;
        if (f.format == format && std::filesystem::equivalent(f.path, chosen, ec))
            return; // already listed
    }
    store_.extraFolders.push_back({format, chosen});
    listFolders();
    emit storeChanged(store_);
}

void PluginManagerDialog::removeFolder()
{
    const auto selected = folders_->selectedItems();
    if (selected.isEmpty())
        return;
    const int index = selected.constFirst()->data(0, kExtraRole).toInt();
    if (index < 0 || index >= static_cast<int>(store_.extraFolders.size()))
        return;
    store_.extraFolders.erase(store_.extraFolders.begin() + index);
    listFolders();
    emit storeChanged(store_);
}

void PluginManagerDialog::startScan()
{
    if (scanThread_ != nullptr)
        return;
    // The worker gets its own copy of the folders and writes only `found`,
    // which the GUI thread reads after the thread has finished.
    auto found = std::make_shared<std::vector<PluginInfo>>();
    scanThread_
        = QThread::create([folders = store_.scanFolders(), found] { *found = scanPluginFolders(folders); });
    connect(scanThread_, &QThread::finished, this, [this, found] {
        scanThread_->deleteLater();
        scanThread_ = nullptr;
        finishScan(std::move(*found));
    });

    scanButton_->setEnabled(false);
    scanButton_->setText(tr("Scanning…"));
    addButton_->setEnabled(false);
    removeButton_->setEnabled(false);
    progress_->show();
    result_->setText(tr("Scanning plugin folders…"));
    scanThread_->start(QThread::LowPriority);
}

void PluginManagerDialog::finishScan(std::vector<PluginInfo> plugins)
{
    store_.plugins = std::move(plugins);
    store_.lastScan = QDateTime::currentDateTimeUtc();
    scanButton_->setEnabled(true);
    scanButton_->setText(tr("Scan"));
    addButton_->setEnabled(true);
    progress_->hide();
    showResult();
    emit storeChanged(store_);
}

void PluginManagerDialog::showResult()
{
    if (!store_.lastScan.isValid()) {
        result_->setText(tr("Not scanned yet. No plugins are available until you scan."));
        return;
    }
    int vst3 = 0;
    for (const PluginInfo& p : store_.plugins)
        vst3 += p.format == PluginFormat::Vst3 ? 1 : 0;
    const int clap = static_cast<int>(store_.plugins.size()) - vst3;
    result_->setText(tr("Last scan %1: %n plugin(s) found (%2 VST3, %3 CLAP).", nullptr,
        static_cast<int>(store_.plugins.size()))
            .arg(QLocale().toString(store_.lastScan.toLocalTime(), QLocale::ShortFormat))
            .arg(vst3)
            .arg(clap));
}

} // namespace vloomdaw
