// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/PluginManagerDialog.hpp"

#include "core/PluginMetadata.hpp"
#include "ui/plugins/PluginProber.hpp"

#include <QCheckBox>
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
#include <QSignalBlocker>
#include <QThread>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
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
                                "automatically: press Scan after installing or removing plugins."),
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
            && selected.constFirst()->data(0, kExtraRole).toInt() >= 0 && scanThread_ == nullptr
            && !prober_->isRunning());
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
    connect(scanButton_, &QPushButton::clicked, this, [this] { startScan(false); });
    rescanButton_ = new QPushButton(tr("Rescan All"), this);
    rescanButton_->setToolTip(tr("Read every plugin again, also the ones an earlier scan already read "
                                 "(after updating plugins). Categories you set yourself are kept."));
    connect(rescanButton_, &QPushButton::clicked, this, [this] { startScan(true); });

    probeBox_ = new QCheckBox(tr("Read vendor and category (loads each plugin)"), this);
    probeBox_->setToolTip(tr("Each new or changed plugin is loaded in a separate process and asked for its "
                             "vendor and category. This runs the plugin's own code. A plugin that crashes or "
                             "hangs is skipped. Off: only file names are read."));
    connect(probeBox_, &QCheckBox::toggled, this, [this](bool on) {
        rescanButton_->setEnabled(on);
        store_.probePlugins = on;
        emit storeChanged(store_);
    });

    prober_ = new PluginProber(this);
    connect(prober_, &PluginProber::progress, this, [this](int done, int total, const QString& current) {
        progress_->setRange(0, std::max(total, 1));
        progress_->setValue(done);
        result_->setText(current.isEmpty()
                ? tr("Finishing...")
                : tr("Reading %1 (%2 of %3)...").arg(current).arg(done + 1).arg(total));
    });
    connect(prober_, &PluginProber::finished, this,
        [this](const std::vector<PluginInfo>& plugins) { finishScan(plugins); });

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
    folderButtons->addWidget(rescanButton_);
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
    layout->addWidget(probeBox_);
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
    const bool probeAvailable = !PluginProber::probeProgram().isEmpty();
    {
        const QSignalBlocker block(probeBox_);
        probeBox_->setChecked(probeAvailable && store_.probePlugins);
    }
    probeBox_->setEnabled(probeAvailable);
    rescanButton_->setEnabled(probeAvailable && store_.probePlugins);
    if (!probeAvailable)
        probeBox_->setToolTip(tr("The plugin probe (vloomdaw-plugin-probe) is missing next to VloomDAW."));
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

void PluginManagerDialog::startScan(bool everything)
{
    if (prober_->isRunning()) {
        // The button reads "Stop" while plugins are being read.
        prober_->stop();
        scanButton_->setEnabled(false);
        return;
    }
    if (scanThread_ != nullptr)
        return;
    rescanAll_ = everything;
    // The worker gets its own copy of the folders and writes only `found`,
    // which the GUI thread reads after the thread has finished.
    auto found = std::make_shared<std::vector<PluginInfo>>();
    scanThread_
        = QThread::create([folders = store_.scanFolders(), found] { *found = scanPluginFolders(folders); });
    connect(scanThread_, &QThread::finished, this, [this, found] {
        scanThread_->deleteLater();
        scanThread_ = nullptr;
        filesFound(std::move(*found));
    });

    setBusy(true);
    progress_->setRange(0, 0);
    result_->setText(tr("Scanning plugin folders..."));
    scanThread_->start(QThread::LowPriority);
}

void PluginManagerDialog::filesFound(std::vector<PluginInfo> modules)
{
    if (!probeBox_->isChecked() || modules.empty()) {
        finishScan(std::move(modules));
        return;
    }
    scanButton_->setText(tr("Stop"));
    scanButton_->setEnabled(true);
    // Rescan All: no earlier results to reuse.
    prober_->start(std::move(modules), rescanAll_ ? std::vector<PluginInfo>{} : store_.plugins);
}

void PluginManagerDialog::finishScan(std::vector<PluginInfo> plugins)
{
    carryUserCategories(plugins, store_.plugins); // a scan never undoes the user's choices
    store_.plugins = std::move(plugins);
    store_.lastScan = QDateTime::currentDateTimeUtc();
    setBusy(false);
    showResult();
    emit storeChanged(store_);
}

void PluginManagerDialog::setBusy(bool busy)
{
    scanButton_->setEnabled(!busy);
    scanButton_->setText(busy ? tr("Scanning...") : tr("Scan"));
    addButton_->setEnabled(!busy);
    rescanButton_->setEnabled(!busy && probeBox_->isChecked());
    probeBox_->setEnabled(!busy && !PluginProber::probeProgram().isEmpty());
    if (busy)
        removeButton_->setEnabled(false);
    progress_->setVisible(busy);
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
    int failed = 0;
    for (const PluginInfo& p : store_.plugins)
        failed += p.probe == PluginInfo::Probe::Failed ? 1 : 0;
    QString text = tr("Last scan %1: %n plugin(s) found (%2 VST3, %3 CLAP).", nullptr,
        static_cast<int>(store_.plugins.size()))
                       .arg(QLocale().toString(store_.lastScan.toLocalTime(), QLocale::ShortFormat))
                       .arg(vst3)
                       .arg(clap);
    if (failed > 0)
        text += QLatin1Char(' ')
            + tr("%n could not be read (crashed, hung or not a plugin); hover them in the list for why.",
                nullptr, failed);
    result_->setText(text);
}

} // namespace vloomdaw
