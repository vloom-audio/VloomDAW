// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/PluginManagerDialog.hpp"

#include "core/PluginMetadata.hpp"
#include "core/ProjectFile.hpp"
#include "ui/FilePaths.hpp"
#include "ui/Theme.hpp"
#include "ui/plugins/PluginProber.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QThread>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <system_error>

namespace fs = std::filesystem;

namespace vloomdaw {

namespace {

constexpr int kExtraRole = Qt::UserRole; ///< index into extraFolders, -1 for a default folder
constexpr int kPluginIndexRole = Qt::UserRole; ///< index into store_.plugins on table column 0
constexpr int kFilterKeyRole = Qt::UserRole; ///< canonical std::string key (as QString) on checklist items

enum TableColumn {
    ColEnabled = 0,
    ColFavorite = 1,
    ColName = 2,
    ColFormat = 3,
    ColCategory = 4,
    ColSubType = 5,
    ColVendor = 6,
    ColVersion = 7,
    ColStatus = 8,
    ColPath = 9,
    ColCount = 10,
};

QString utf8(const std::string& text) { return QString::fromStdString(text); }

std::string toUtf8Path(const fs::path& p) { return pathToUtf8(p); }

QString pathText(const fs::path& p) { return QDir::toNativeSeparators(toQString(p)); }

std::string lowerAscii(std::string s)
{
    std::transform(
        s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string vendorKeyOf(const PluginInfo& info)
{
    return info.vendor.empty() ? std::string("Unknown") : info.vendor;
}

bool sameFsPath(const fs::path& a, const fs::path& b)
{
    std::error_code ec;
    if (fs::equivalent(a, b, ec))
        return true;
    const QString qa = QDir::cleanPath(toQString(a));
    const QString qb = QDir::cleanPath(toQString(b));
#if defined(Q_OS_WIN)
    return qa.compare(qb, Qt::CaseInsensitive) == 0;
#else
    return qa == qb;
#endif
}

bool isInsideFolder(const fs::path& child, const fs::path& folder)
{
    const QString c = QDir::cleanPath(toQString(child));
    QString f = QDir::cleanPath(toQString(folder));
    if (!f.endsWith(QLatin1Char('/')))
        f += QLatin1Char('/');
#if defined(Q_OS_WIN)
    return c.startsWith(f, Qt::CaseInsensitive);
#else
    return c.startsWith(f, Qt::CaseSensitive);
#endif
}

/// If the user picked the inner binary inside a `.vst3` bundle folder
/// (`MySynth.vst3/Contents/x86_64-win/MySynth.vst3`), normalize to the outer `.vst3` bundle root.
fs::path normalizePluginModulePath(const fs::path& chosen)
{
    fs::path cur = chosen;
    for (int depth = 0; depth < 4 && cur.has_parent_path(); ++depth) {
        const fs::path parent = cur.parent_path();
        if (parent == cur)
            break;
        if (lowerAscii(toUtf8Path(parent.extension())) == ".vst3")
            return parent;
        cur = parent;
    }
    return chosen;
}

fs::path bundleBinary(const fs::path& bundle)
{
#if defined(_WIN32)
    fs::path binary = bundle / "Contents" / "x86_64-win" / bundle.filename();
#elif defined(__APPLE__)
    fs::path binary = bundle / "Contents" / "MacOS" / bundle.stem();
#else
    fs::path binary = bundle / "Contents" / "x86_64-linux" / bundle.filename();
    binary.replace_extension(".so");
#endif
    std::error_code ec;
    return fs::is_regular_file(binary, ec) ? binary : bundle;
}

PluginInfo makeManualModuleInfo(const fs::path& modulePath, PluginFormat format)
{
    PluginInfo info;
    info.name = toUtf8Path(modulePath.stem());
    info.format = format;
    info.path = modulePath;
    std::error_code ec;
    const bool isDir = fs::is_directory(modulePath, ec);
    const fs::path binary = isDir ? bundleBinary(modulePath) : modulePath;
    if (fs::is_regular_file(binary, ec))
        info.fileSize = fs::file_size(binary, ec);
    info.modified = static_cast<std::int64_t>(fs::last_write_time(binary, ec).time_since_epoch().count());
    return info;
}

/// Table item for the `On` checkbox column so sorting orders checked vs unchecked cleanly.
class SortableCheckItem : public QTableWidgetItem {
public:
    using QTableWidgetItem::QTableWidgetItem;

    bool operator<(const QTableWidgetItem& other) const override
    {
        if (checkState() != other.checkState())
            return checkState() < other.checkState();
        return data(kPluginIndexRole).toULongLong() < other.data(kPluginIndexRole).toULongLong();
    }
};

std::set<std::string> checkedKeysOf(const QListWidget* list)
{
    std::set<std::string> out;
    if (list == nullptr)
        return out;
    for (int i = 0; i < list->count(); ++i) {
        const QListWidgetItem* item = list->item(i);
        if (item->checkState() == Qt::Checked)
            out.insert(item->data(kFilterKeyRole).toString().toStdString());
    }
    return out;
}

void rememberUnchecked(const QListWidget* list, std::set<std::string>& unchecked)
{
    if (list == nullptr)
        return;
    for (int i = 0; i < list->count(); ++i) {
        const QListWidgetItem* item = list->item(i);
        const std::string key = item->data(kFilterKeyRole).toString().toStdString();
        if (item->checkState() == Qt::Checked)
            unchecked.erase(key);
        else
            unchecked.insert(key);
    }
}

} // namespace

PluginManagerDialog::PluginManagerDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("pluginManager"));
    setWindowTitle(tr("Plugin Manager"));
    setMinimumSize(920, 560);
    resize(1140, 700);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(1);

    auto* sidebar = new QFrame(splitter);
    sidebar->setObjectName(QStringLiteral("pluginManagerSidebar"));
    sidebar->setMinimumWidth(235);
    sidebar->setMaximumWidth(360);
    buildSidebar(sidebar);

    auto* mainArea = new QWidget(splitter);
    buildMainArea(mainArea);

    splitter->addWidget(sidebar);
    splitter->addWidget(mainArea);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({265, 875});

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(splitter, 1);

    prober_ = new PluginProber(this);
    connect(prober_, &PluginProber::progress, this, [this](int done, int total, const QString& current) {
        progress_->setRange(0, std::max(total, 1));
        progress_->setValue(done);
        result_->setText(current.isEmpty()
                ? tr("Finishing scan...")
                : tr("Reading %1 (%2 of %3)...").arg(current).arg(done + 1).arg(total));
    });
    connect(prober_, &PluginProber::finished, this,
        [this](const std::vector<PluginInfo>& plugins) { finishScan(plugins); });
}

PluginManagerDialog::~PluginManagerDialog()
{
    if (scanThread_ != nullptr) {
        scanThread_->disconnect(this);
        scanThread_->wait();
        delete scanThread_;
    }
}

void PluginManagerDialog::buildSidebar(QFrame* sidebar)
{
    auto* layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(12, 14, 12, 14);
    layout->setSpacing(8);

    auto* heading = new QLabel(tr("FILTERS"), sidebar);
    heading->setObjectName(QStringLiteral("settingsCardTitle"));
    layout->addWidget(heading);

    searchEdit_ = new QLineEdit(sidebar);
    searchEdit_->setObjectName(QStringLiteral("browserSearch"));
    searchEdit_->setPlaceholderText(tr("Search name, vendor, type, path..."));
    searchEdit_->setClearButtonEnabled(true);
    connect(searchEdit_, &QLineEdit::textChanged, this, [this] { populateTable(); });
    auto* clearFocus
        = new QShortcut(QKeySequence(Qt::Key_Escape), searchEdit_, nullptr, nullptr, Qt::WidgetShortcut);
    connect(clearFocus, &QShortcut::activated, searchEdit_, [this] { searchEdit_->clearFocus(); });
    layout->addWidget(searchEdit_);

    favOnlyBox_ = new QCheckBox(tr("Favorites Only"), sidebar);
    failedOnlyBox_ = new QCheckBox(tr("Failed Only"), sidebar);
    disabledOnlyBox_ = new QCheckBox(tr("Disabled Only"), sidebar);
    connect(favOnlyBox_, &QCheckBox::toggled, this, [this] { populateTable(); });
    connect(failedOnlyBox_, &QCheckBox::toggled, this, [this] { populateTable(); });
    connect(disabledOnlyBox_, &QCheckBox::toggled, this, [this] { populateTable(); });
    layout->addWidget(favOnlyBox_);
    layout->addWidget(failedOnlyBox_);
    layout->addWidget(disabledOnlyBox_);

    const auto makeChecklistSection
        = [this, sidebar, layout](const QString& title, int fixedHeight, int stretch) {
              auto* headerRow = new QHBoxLayout;
              headerRow->setContentsMargins(0, 4, 0, 0);
              headerRow->setSpacing(4);
              auto* lbl = new QLabel(title, sidebar);
              lbl->setObjectName(QStringLiteral("settingsCardTitle"));
              headerRow->addWidget(lbl, 1);

              auto* allBtn = new QToolButton(sidebar);
              allBtn->setObjectName(QStringLiteral("filterLinkBtn"));
              allBtn->setText(tr("All"));
              allBtn->setCursor(Qt::PointingHandCursor);
              allBtn->setFocusPolicy(Qt::NoFocus);

              auto* noneBtn = new QToolButton(sidebar);
              noneBtn->setObjectName(QStringLiteral("filterLinkBtn"));
              noneBtn->setText(tr("Clear"));
              noneBtn->setCursor(Qt::PointingHandCursor);
              noneBtn->setFocusPolicy(Qt::NoFocus);

              headerRow->addWidget(allBtn);
              headerRow->addWidget(noneBtn);
              layout->addLayout(headerRow);

              auto* list = new QListWidget(sidebar);
              list->setObjectName(QStringLiteral("pluginFilterList"));
              list->setSelectionMode(QAbstractItemView::NoSelection);
              list->setUniformItemSizes(true);
              if (fixedHeight > 0)
                  list->setFixedHeight(fixedHeight);
              layout->addWidget(list, stretch);

              connect(allBtn, &QToolButton::clicked, this, [this, list] { setChecklistAll(list, true); });
              connect(noneBtn, &QToolButton::clicked, this, [this, list] { setChecklistAll(list, false); });
              connect(list, &QListWidget::itemChanged, this, [this] {
                  if (!updatingFilters_)
                      populateTable();
              });
              return list;
          };

    formatFilter_ = makeChecklistSection(tr("FORMAT"), 52, 0);
    categoryFilter_ = makeChecklistSection(tr("CATEGORY"), 52, 0);
    subTypeFilter_ = makeChecklistSection(tr("SUB-TYPE"), 0, 1);
    vendorFilter_ = makeChecklistSection(tr("VENDOR"), 0, 1);

    uncheckVendorsBtn_ = new QPushButton(tr("Uncheck All Vendors"), sidebar);
    uncheckVendorsBtn_->setFocusPolicy(Qt::NoFocus);
    connect(
        uncheckVendorsBtn_, &QPushButton::clicked, this, [this] { setChecklistAll(vendorFilter_, false); });
    layout->addWidget(uncheckVendorsBtn_);

    resetFiltersBtn_ = new QPushButton(tr("Reset Filters"), sidebar);
    resetFiltersBtn_->setFocusPolicy(Qt::NoFocus);
    connect(resetFiltersBtn_, &QPushButton::clicked, this, &PluginManagerDialog::resetAllFilters);
    layout->addWidget(resetFiltersBtn_);
}

void PluginManagerDialog::buildMainArea(QWidget* area)
{
    auto* layout = new QVBoxLayout(area);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(10);

    // --- Header + Scan / Action Toolbar ---
    auto* topBar = new QHBoxLayout;
    topBar->setSpacing(8);

    auto* titleCol = new QVBoxLayout;
    titleCol->setSpacing(2);
    auto* heading = new QLabel(tr("Plugin Manager"), area);
    heading->setObjectName(QStringLiteral("settingsHeading"));
    auto* subtitle = new QLabel(tr("Manage VST3 and CLAP plugins, filter by type or vendor, mark favorites, "
                                   "and configure scan paths."),
        area);
    subtitle->setObjectName(QStringLiteral("settingsSubtitle"));
    titleCol->addWidget(heading);
    titleCol->addWidget(subtitle);
    topBar->addLayout(titleCol, 1);

    foldersToggle_ = new QToolButton(area);
    foldersToggle_->setCheckable(true);
    foldersToggle_->setChecked(false);
    foldersToggle_->setFocusPolicy(Qt::NoFocus);
    foldersToggle_->setToolTip(tr("Show or hide the VST3 and CLAP scan folders list"));
    topBar->addWidget(foldersToggle_);

    addFileButton_ = new QPushButton(tr("Add Plugin File..."), area);
    addFileButton_->setToolTip(tr("Manually add individual .vst3 or .clap plugin files"));
    addFileButton_->setFocusPolicy(Qt::NoFocus);
    connect(addFileButton_, &QPushButton::clicked, this, &PluginManagerDialog::addManualPluginFiles);
    topBar->addWidget(addFileButton_);

    retryFailedButton_ = new QPushButton(tr("Retry Failed"), area);
    retryFailedButton_->setToolTip(tr("Probe only the plugins that failed during an earlier scan"));
    retryFailedButton_->setFocusPolicy(Qt::NoFocus);
    connect(retryFailedButton_, &QPushButton::clicked, this, &PluginManagerDialog::retryFailedPlugins);
    topBar->addWidget(retryFailedButton_);

    rescanButton_ = new QPushButton(tr("Rescan All"), area);
    rescanButton_->setToolTip(tr("Read every plugin again from scratch. Your favorites, enabled state, "
                                 "and custom categories are preserved."));
    rescanButton_->setFocusPolicy(Qt::NoFocus);
    connect(rescanButton_, &QPushButton::clicked, this, [this] { startScan(true); });
    topBar->addWidget(rescanButton_);

    scanButton_ = new QPushButton(tr("Scan"), area);
    scanButton_->setObjectName(QStringLiteral("accentButton"));
    scanButton_->setDefault(true);
    connect(scanButton_, &QPushButton::clicked, this, [this] { startScan(false); });
    topBar->addWidget(scanButton_);

    layout->addLayout(topBar);

    // --- Collapsible Scan Folders Card ---
    foldersPanel_ = new QFrame(area);
    foldersPanel_->setObjectName(QStringLiteral("pluginManagerFoldersPanel"));
    auto* foldersLayout = new QVBoxLayout(foldersPanel_);
    foldersLayout->setContentsMargins(12, 10, 12, 10);
    foldersLayout->setSpacing(8);

    folders_ = new QTreeWidget(foldersPanel_);
    folders_->setColumnCount(3);
    folders_->setHeaderLabels({tr("Format"), tr("Folder"), tr("Status")});
    folders_->setRootIsDecorated(false);
    folders_->setUniformRowHeights(true);
    folders_->setFixedHeight(130);
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
    foldersLayout->addWidget(folders_);

    auto* folderControls = new QHBoxLayout;
    folderControls->setSpacing(8);
    addButton_ = new QPushButton(tr("Add Folder"), foldersPanel_);
    auto* addMenu = new QMenu(addButton_);
    addMenu->addAction(tr("VST3 Folder..."), this, [this] { addFolder(PluginFormat::Vst3); });
    addMenu->addAction(tr("CLAP Folder..."), this, [this] { addFolder(PluginFormat::Clap); });
    addButton_->setMenu(addMenu);

    removeButton_ = new QPushButton(tr("Remove Folder"), foldersPanel_);
    removeButton_->setEnabled(false);
    removeButton_->setToolTip(tr("Remove the selected custom folder. Default system folders stay."));
    connect(removeButton_, &QPushButton::clicked, this, &PluginManagerDialog::removeFolder);

    probeBox_ = new QCheckBox(tr("Read vendor and category (loads each plugin)"), foldersPanel_);
    probeBox_->setToolTip(tr("Each new or changed plugin is loaded in an isolated helper process to read "
                             "its vendor and category. Plugins that crash or hang are safely skipped."));
    connect(probeBox_, &QCheckBox::toggled, this, [this](bool on) {
        rescanButton_->setEnabled(on && scanThread_ == nullptr && !prober_->isRunning());
        store_.probePlugins = on;
        updateSummaryLabels();
        emit storeChanged(store_);
    });

    folderControls->addWidget(addButton_);
    folderControls->addWidget(removeButton_);
    folderControls->addSpacing(12);
    folderControls->addWidget(probeBox_, 1);
    foldersLayout->addLayout(folderControls);

    foldersPanel_->setVisible(false);
    connect(foldersToggle_, &QToolButton::toggled, this, [this](bool open) {
        foldersPanel_->setVisible(open);
        updateFoldersToggleText();
    });
    layout->addWidget(foldersPanel_);

    // --- Detailed Plugin Table ---
    table_ = new QTableWidget(0, ColCount, area);
    table_->setObjectName(QStringLiteral("pluginTable"));
    table_->setHorizontalHeaderLabels({
        tr("On"),
        tr("Fav"),
        tr("Plugin Name"),
        tr("Format"),
        tr("Category"),
        tr("Sub-Type"),
        tr("Vendor"),
        tr("Version"),
        tr("Status"),
        tr("Path"),
    });
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(26);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->setShowGrid(false);
    table_->setSortingEnabled(true);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);

    QHeaderView* hh = table_->horizontalHeader();
    hh->setHighlightSections(false);
    hh->setStretchLastSection(true);
    hh->setSectionResizeMode(ColEnabled, QHeaderView::Fixed);
    hh->setSectionResizeMode(ColFavorite, QHeaderView::Fixed);
    hh->setSectionResizeMode(ColName, QHeaderView::Interactive);
    hh->setSectionResizeMode(ColFormat, QHeaderView::Fixed);
    hh->setSectionResizeMode(ColCategory, QHeaderView::Interactive);
    hh->setSectionResizeMode(ColSubType, QHeaderView::Interactive);
    hh->setSectionResizeMode(ColVendor, QHeaderView::Interactive);
    hh->setSectionResizeMode(ColVersion, QHeaderView::Fixed);
    hh->setSectionResizeMode(ColStatus, QHeaderView::Fixed);
    hh->setSectionResizeMode(ColPath, QHeaderView::Stretch);

    table_->setColumnWidth(ColEnabled, 42);
    table_->setColumnWidth(ColFavorite, 48);
    table_->setColumnWidth(ColName, 210);
    table_->setColumnWidth(ColFormat, 62);
    table_->setColumnWidth(ColCategory, 105);
    table_->setColumnWidth(ColSubType, 120);
    table_->setColumnWidth(ColVendor, 140);
    table_->setColumnWidth(ColVersion, 72);
    table_->setColumnWidth(ColStatus, 78);
    table_->sortByColumn(ColName, Qt::AscendingOrder);

    connect(table_, &QTableWidget::cellClicked, this, &PluginManagerDialog::handleTableCellClicked);
    connect(table_, &QTableWidget::itemChanged, this, &PluginManagerDialog::handleTableItemChanged);
    connect(
        table_, &QTableWidget::customContextMenuRequested, this, &PluginManagerDialog::showTableContextMenu);
    layout->addWidget(table_, 1);

    // --- Progress & Footer ---
    progress_ = new QProgressBar(area);
    progress_->setRange(0, 0);
    progress_->setTextVisible(false);
    progress_->setFixedHeight(4);
    progress_->hide();
    layout->addWidget(progress_);

    auto* footerRow = new QHBoxLayout;
    footerRow->setSpacing(12);
    auto* statusCol = new QVBoxLayout;
    statusCol->setSpacing(2);
    statsLabel_ = new QLabel(area);
    statsLabel_->setObjectName(QStringLiteral("settingsRowName"));
    result_ = new QLabel(area);
    result_->setObjectName(QStringLiteral("settingsSubtitle"));
    result_->setWordWrap(true);
    statusCol->addWidget(statsLabel_);
    statusCol->addWidget(result_);
    footerRow->addLayout(statusCol, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, area);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    footerRow->addWidget(buttons, 0, Qt::AlignBottom);
    layout->addLayout(footerRow);
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
    if (!filtersInitialized_ && store_.plugins.empty()) {
        const QSignalBlocker block(foldersToggle_);
        foldersToggle_->setChecked(true);
        foldersPanel_->setVisible(true);
    }
    updateFoldersToggleText();
    rebuildDynamicFilters();
    populateTable();
    updateSummaryLabels();
}

void PluginManagerDialog::updateFoldersToggleText()
{
    const std::size_t count = store_.scanFolders().size();
    foldersToggle_->setText(foldersToggle_->isChecked() ? tr("Hide Scan Folders (%1)").arg(count)
                                                        : tr("Scan Folders (%1)").arg(count));
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
        const bool exists = fs::is_directory(folder.path, ec);
        item->setText(2,
            extraIndex < 0 ? (exists ? tr("Default") : tr("Default, missing"))
                           : (exists ? tr("Custom") : tr("Missing")));
        item->setData(0, kExtraRole, extraIndex);
        if (!exists)
            item->setForeground(1, Theme::colors().textDim);
    };
    for (const PluginFolder& folder : defaultPluginFolders())
        add(folder, -1);
    for (int i = 0; i < static_cast<int>(store_.extraFolders.size()); ++i)
        add(store_.extraFolders[static_cast<std::size_t>(i)], i);
    removeButton_->setEnabled(false);
    updateFoldersToggleText();
}

void PluginManagerDialog::addFolder(PluginFormat format)
{
    const QString title = tr("Add %1 Folder").arg(QString::fromUtf8(formatName(format)));
    const QString path = QFileDialog::getExistingDirectory(this, title, QDir::homePath());
    if (path.isEmpty())
        return;
    if (!isLocalPath(path)) {
        result_->setText(tr("Network share folders cannot be added as plugin folders."));
        return;
    }
    const fs::path chosen = toPath(QDir::cleanPath(path));
    if (isNetworkPath(chosen)) {
        result_->setText(tr("Network share folders cannot be added as plugin folders."));
        return;
    }
    for (const PluginFolder& f : store_.scanFolders()) {
        if (f.format == format && sameFsPath(f.path, chosen))
            return;
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

void PluginManagerDialog::addManualPluginFiles()
{
    if (scanThread_ != nullptr || prober_->isRunning())
        return;

    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Add Plugin Files"), QDir::homePath(),
        tr("Audio Plugins (*.vst3 *.clap);;VST3 Plugins (*.vst3);;CLAP Plugins (*.clap);;All Files (*)"));
    if (files.isEmpty())
        return;

    std::vector<PluginInfo> modules;
    for (const QString& file : files) {
        if (!isLocalPath(file))
            continue;
        const fs::path rawPath = toPath(QDir::cleanPath(file));
        if (isNetworkPath(rawPath))
            continue;
        const fs::path modulePath = normalizePluginModulePath(rawPath);
        const std::string ext = lowerAscii(toUtf8Path(modulePath.extension()));
        if (ext != ".vst3" && ext != ".clap")
            continue;
        const PluginFormat fmt = ext == ".clap" ? PluginFormat::Clap : PluginFormat::Vst3;
        const bool dup = std::any_of(modules.begin(), modules.end(),
            [&](const PluginInfo& m) { return m.format == fmt && sameFsPath(m.path, modulePath); });
        if (!dup)
            modules.push_back(makeManualModuleInfo(modulePath, fmt));
    }
    if (modules.empty())
        return;

    startSubsetProbe(std::move(modules));
}

void PluginManagerDialog::startScan(bool everything)
{
    if (prober_->isRunning()) {
        prober_->stop();
        scanButton_->setEnabled(false);
        return;
    }
    if (scanThread_ != nullptr)
        return;

    scanMode_ = everything ? ScanMode::Full : ScanMode::Incremental;
    subsetTargets_.clear();

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

void PluginManagerDialog::startSubsetProbe(std::vector<PluginInfo> modules)
{
    if (modules.empty() || scanThread_ != nullptr || prober_->isRunning())
        return;

    scanMode_ = ScanMode::Subset;
    subsetTargets_.clear();
    subsetTargets_.reserve(modules.size());
    for (const PluginInfo& m : modules)
        subsetTargets_.emplace_back(m.format, m.path);

    if (!probeBox_->isChecked() || PluginProber::probeProgram().isEmpty()) {
        finishScan(std::move(modules));
        return;
    }

    setBusy(true);
    scanButton_->setText(tr("Stop"));
    scanButton_->setEnabled(true);
    prober_->start(std::move(modules), {});
}

void PluginManagerDialog::retryFailedPlugins()
{
    std::vector<PluginInfo> modules;
    for (const PluginInfo& p : store_.plugins) {
        if (p.probe != PluginInfo::Probe::Failed)
            continue;
        const bool dup = std::any_of(modules.begin(), modules.end(),
            [&](const PluginInfo& m) { return m.format == p.format && sameFsPath(m.path, p.path); });
        if (!dup)
            modules.push_back(makeManualModuleInfo(p.path, p.format));
    }
    startSubsetProbe(std::move(modules));
}

void PluginManagerDialog::rescanSelectedPlugins()
{
    const std::vector<std::size_t> indices = selectedPluginIndices();
    std::vector<PluginInfo> modules;
    for (std::size_t idx : indices) {
        if (idx >= store_.plugins.size())
            continue;
        const PluginInfo& p = store_.plugins[idx];
        const bool dup = std::any_of(modules.begin(), modules.end(),
            [&](const PluginInfo& m) { return m.format == p.format && sameFsPath(m.path, p.path); });
        if (!dup)
            modules.push_back(makeManualModuleInfo(p.path, p.format));
    }
    startSubsetProbe(std::move(modules));
}

void PluginManagerDialog::filesFound(std::vector<PluginInfo> modules)
{
    if (!probeBox_->isChecked() || modules.empty()) {
        finishScan(std::move(modules));
        return;
    }
    scanButton_->setText(tr("Stop"));
    scanButton_->setEnabled(true);
    prober_->start(
        std::move(modules), scanMode_ == ScanMode::Full ? std::vector<PluginInfo>{} : store_.plugins);
}

void PluginManagerDialog::finishScan(std::vector<PluginInfo> plugins)
{
    carryUserCategories(plugins, store_.plugins);

    if (scanMode_ == ScanMode::Subset) {
        std::erase_if(store_.plugins, [this](const PluginInfo& existing) {
            return std::any_of(subsetTargets_.begin(), subsetTargets_.end(), [&](const auto& target) {
                return existing.format == target.first && sameFsPath(existing.path, target.second);
            });
        });
        for (PluginInfo& p : plugins)
            store_.plugins.push_back(std::move(p));
    } else {
        // Keep manually-added plugin files outside scanFolders() if they still exist on disk.
        const std::vector<PluginFolder> folders = store_.scanFolders();
        for (const PluginInfo& existing : store_.plugins) {
            const bool insideWatched
                = std::any_of(folders.begin(), folders.end(), [&](const PluginFolder& f) {
                      return f.format == existing.format && isInsideFolder(existing.path, f.path);
                  });
            if (insideWatched)
                continue;
            std::error_code ec;
            if (!fs::exists(existing.path, ec))
                continue;
            const bool alreadyPresent = std::any_of(plugins.begin(), plugins.end(), [&](const PluginInfo& p) {
                return p.format == existing.format && sameFsPath(p.path, existing.path);
            });
            if (!alreadyPresent)
                plugins.push_back(existing);
        }
        store_.plugins = std::move(plugins);
    }

    std::sort(store_.plugins.begin(), store_.plugins.end(), [](const PluginInfo& a, const PluginInfo& b) {
        const int cmp
            = QString::fromStdString(a.name).compare(QString::fromStdString(b.name), Qt::CaseInsensitive);
        if (cmp != 0)
            return cmp < 0;
        return a.format < b.format;
    });

    subsetTargets_.clear();
    store_.lastScan = QDateTime::currentDateTimeUtc();
    setBusy(false);
    rebuildDynamicFilters();
    populateTable();
    updateSummaryLabels();
    emit storeChanged(store_);
}

void PluginManagerDialog::setBusy(bool busy)
{
    scanButton_->setEnabled(!busy);
    scanButton_->setText(busy ? tr("Scanning...") : tr("Scan"));
    addButton_->setEnabled(!busy);
    addFileButton_->setEnabled(!busy);
    rescanButton_->setEnabled(!busy && probeBox_->isChecked());
    probeBox_->setEnabled(!busy && !PluginProber::probeProgram().isEmpty());
    if (busy) {
        removeButton_->setEnabled(false);
        retryFailedButton_->setEnabled(false);
    }
    progress_->setVisible(busy);
}

void PluginManagerDialog::rebuildDynamicFilters()
{
    if (filtersInitialized_) {
        rememberUnchecked(formatFilter_, uncheckedFormats_);
        rememberUnchecked(categoryFilter_, uncheckedCategories_);
        rememberUnchecked(subTypeFilter_, uncheckedSubTypes_);
        rememberUnchecked(vendorFilter_, uncheckedVendors_);
    }
    filtersInitialized_ = true;
    updatingFilters_ = true;

    std::map<std::string, int> formatCounts{{"VST3", 0}, {"CLAP", 0}};
    std::map<std::string, int> catCounts{{"Instrument", 0}, {"Effect", 0}};
    std::map<std::string, int> subCounts;
    std::map<std::string, int> vendorCounts;

    for (const PluginInfo& p : store_.plugins) {
        formatCounts[std::string(formatName(p.format))]++;
        catCounts[pluginMainGroup(p)]++;
        subCounts[detectPluginSubCategory(p)]++;
        vendorCounts[vendorKeyOf(p)]++;
    }

    const auto fillChecklist
        = [](QListWidget* list, const std::vector<std::pair<std::string, QString>>& entries,
              const std::set<std::string>& unchecked) {
              const QSignalBlocker block(list);
              list->clear();
              for (const auto& [key, label] : entries) {
                  auto* item = new QListWidgetItem(label, list);
                  item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
                  item->setData(kFilterKeyRole, utf8(key));
                  item->setCheckState(unchecked.contains(key) ? Qt::Unchecked : Qt::Checked);
              }
          };

    fillChecklist(formatFilter_,
        {
            {"VST3", tr("VST3 (%1)").arg(formatCounts["VST3"])},
            {"CLAP", tr("CLAP (%1)").arg(formatCounts["CLAP"])},
        },
        uncheckedFormats_);

    fillChecklist(categoryFilter_,
        {
            {"Instrument", tr("Instrument (%1)").arg(catCounts["Instrument"])},
            {"Effect", tr("Effect (%1)").arg(catCounts["Effect"])},
        },
        uncheckedCategories_);

    std::vector<std::pair<std::string, QString>> subEntries;
    for (const std::string& sub : pluginSubCategories()) {
        if (const auto it = subCounts.find(sub); it != subCounts.end() && it->second > 0)
            subEntries.emplace_back(sub, QStringLiteral("%1 (%2)").arg(utf8(sub)).arg(it->second));
    }
    for (const auto& [sub, cnt] : subCounts) {
        const bool already = std::any_of(
            subEntries.begin(), subEntries.end(), [&](const auto& e) { return e.first == sub; });
        if (!already && cnt > 0)
            subEntries.emplace_back(sub, QStringLiteral("%1 (%2)").arg(utf8(sub)).arg(cnt));
    }
    fillChecklist(subTypeFilter_, subEntries, uncheckedSubTypes_);

    std::vector<std::string> vendors;
    vendors.reserve(vendorCounts.size());
    for (const auto& [v, cnt] : vendorCounts) {
        if (cnt > 0)
            vendors.push_back(v);
    }
    std::sort(vendors.begin(), vendors.end(), [](const std::string& a, const std::string& b) {
        const bool aUnk = (a == "Unknown");
        const bool bUnk = (b == "Unknown");
        if (aUnk != bUnk)
            return !aUnk;
        return QString::fromStdString(a).compare(QString::fromStdString(b), Qt::CaseInsensitive) < 0;
    });
    std::vector<std::pair<std::string, QString>> vendorEntries;
    vendorEntries.reserve(vendors.size());
    for (const std::string& v : vendors) {
        const QString display = v == "Unknown" ? tr("Unknown") : utf8(v);
        vendorEntries.emplace_back(v, QStringLiteral("%1 (%2)").arg(display).arg(vendorCounts[v]));
    }
    fillChecklist(vendorFilter_, vendorEntries, uncheckedVendors_);

    updatingFilters_ = false;
}

void PluginManagerDialog::setChecklistAll(QListWidget* list, bool checked)
{
    if (list == nullptr)
        return;
    updatingFilters_ = true;
    {
        const QSignalBlocker block(list);
        for (int i = 0; i < list->count(); ++i)
            list->item(i)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }
    updatingFilters_ = false;
    populateTable();
}

void PluginManagerDialog::resetAllFilters()
{
    updatingFilters_ = true;
    uncheckedFormats_.clear();
    uncheckedCategories_.clear();
    uncheckedSubTypes_.clear();
    uncheckedVendors_.clear();
    {
        const QSignalBlocker b1(searchEdit_);
        const QSignalBlocker b2(favOnlyBox_);
        const QSignalBlocker b3(failedOnlyBox_);
        const QSignalBlocker b4(disabledOnlyBox_);
        searchEdit_->clear();
        favOnlyBox_->setChecked(false);
        failedOnlyBox_->setChecked(false);
        disabledOnlyBox_->setChecked(false);
    }
    for (QListWidget* list : {formatFilter_, categoryFilter_, subTypeFilter_, vendorFilter_}) {
        const QSignalBlocker block(list);
        for (int i = 0; i < list->count(); ++i)
            list->item(i)->setCheckState(Qt::Checked);
    }
    updatingFilters_ = false;
    populateTable();
}

bool PluginManagerDialog::matchesFilters(const PluginInfo& info) const
{
    if (favOnlyBox_->isChecked() && !info.favorite)
        return false;
    if (failedOnlyBox_->isChecked() && info.probe != PluginInfo::Probe::Failed)
        return false;
    if (disabledOnlyBox_->isChecked() && info.enabled)
        return false;

    const std::set<std::string> formats = checkedKeysOf(formatFilter_);
    if (!formats.contains(std::string(formatName(info.format))))
        return false;

    const std::set<std::string> cats = checkedKeysOf(categoryFilter_);
    if (!cats.contains(pluginMainGroup(info)))
        return false;

    if (subTypeFilter_->count() > 0) {
        const std::set<std::string> subs = checkedKeysOf(subTypeFilter_);
        if (!subs.contains(detectPluginSubCategory(info)))
            return false;
    }

    if (vendorFilter_->count() > 0) {
        const std::set<std::string> vendors = checkedKeysOf(vendorFilter_);
        if (!vendors.contains(vendorKeyOf(info)))
            return false;
    }

    const QString query = searchEdit_->text().trimmed();
    if (!query.isEmpty()) {
        const QString name = utf8(info.name);
        const QString vendor = utf8(info.vendor);
        const QString cat = utf8(effectiveCategory(info));
        const QString sub = utf8(detectPluginSubCategory(info));
        const QString path = pathText(info.path);
        if (!name.contains(query, Qt::CaseInsensitive) && !vendor.contains(query, Qt::CaseInsensitive)
            && !cat.contains(query, Qt::CaseInsensitive) && !sub.contains(query, Qt::CaseInsensitive)
            && !path.contains(query, Qt::CaseInsensitive)) {
            return false;
        }
    }
    return true;
}

void PluginManagerDialog::populateTable()
{
    updatingTable_ = true;
    const int sortCol = table_->horizontalHeader()->sortIndicatorSection();
    const Qt::SortOrder sortOrder = table_->horizontalHeader()->sortIndicatorOrder();

    table_->setSortingEnabled(false);
    table_->setRowCount(0);

    const Theme::Colors& colors = Theme::colors();

    int row = 0;
    for (std::size_t i = 0; i < store_.plugins.size(); ++i) {
        const PluginInfo& info = store_.plugins[i];
        if (!matchesFilters(info))
            continue;

        table_->insertRow(row);

        // Col 0: On (checkbox)
        auto* onItem = new SortableCheckItem;
        onItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        onItem->setCheckState(info.enabled ? Qt::Checked : Qt::Unchecked);
        onItem->setData(kPluginIndexRole, static_cast<qulonglong>(i));
        onItem->setToolTip(tr("Enable or disable this plugin in the Browser and Mixer"));
        table_->setItem(row, ColEnabled, onItem);

        // Col 1: Fav (plain text toggle, zero emojis/icons)
        auto* favItem = new QTableWidgetItem(info.favorite ? tr("FAV") : QStringLiteral("-"));
        favItem->setTextAlignment(Qt::AlignCenter);
        favItem->setForeground(info.favorite ? colors.accent : colors.textDim);
        if (info.favorite) {
            QFont f = favItem->font();
            f.setBold(true);
            favItem->setFont(f);
        }
        favItem->setToolTip(tr("Click to toggle Favorite"));
        table_->setItem(row, ColFavorite, favItem);

        // Col 2: Plugin Name
        auto* nameItem = new QTableWidgetItem(utf8(info.name));
        if (!info.enabled || info.probe == PluginInfo::Probe::Failed)
            nameItem->setForeground(colors.textDim);
        table_->setItem(row, ColName, nameItem);

        // Col 3: Format
        auto* fmtItem = new QTableWidgetItem(QString::fromUtf8(formatName(info.format)));
        fmtItem->setTextAlignment(Qt::AlignCenter);
        fmtItem->setForeground(colors.textDim);
        table_->setItem(row, ColFormat, fmtItem);

        // Col 4: Category
        const std::string effCat = effectiveCategory(info);
        const QString catDisplay
            = info.userCategory.empty() ? utf8(effCat) : QStringLiteral("%1 *").arg(utf8(effCat));
        auto* catItem = new QTableWidgetItem(catDisplay);
        if (!info.userCategory.empty())
            catItem->setToolTip(
                tr("Custom category set by you (plugin reported: %1)").arg(utf8(info.category)));
        table_->setItem(row, ColCategory, catItem);

        // Col 5: Sub-Type
        auto* subItem = new QTableWidgetItem(utf8(detectPluginSubCategory(info)));
        subItem->setForeground(colors.textDim);
        table_->setItem(row, ColSubType, subItem);

        // Col 6: Vendor
        const QString vendorDisplay = info.vendor.empty() ? tr("Unknown") : utf8(info.vendor);
        auto* vendorItem = new QTableWidgetItem(vendorDisplay);
        if (info.vendor.empty())
            vendorItem->setForeground(colors.textDim);
        table_->setItem(row, ColVendor, vendorItem);

        // Col 7: Version
        auto* verItem = new QTableWidgetItem(utf8(info.version));
        verItem->setForeground(colors.textDim);
        table_->setItem(row, ColVersion, verItem);

        // Col 8: Status
        QString statusText = tr("OK");
        QColor statusColor = colors.playing;
        QString statusTip = tr("Read cleanly");
        if (info.probe == PluginInfo::Probe::Failed) {
            statusText = tr("Failed");
            statusColor = colors.danger;
            statusTip = info.probeError.empty() ? tr("Plugin crashed or timed out during probing")
                                                : tr("Failed: %1").arg(utf8(info.probeError));
        } else if (info.probe == PluginInfo::Probe::NotProbed) {
            statusText = tr("Unprobed");
            statusColor = colors.textDim;
            statusTip = tr("File listed without loading plugin metadata");
        }
        auto* statusItem = new QTableWidgetItem(statusText);
        statusItem->setTextAlignment(Qt::AlignCenter);
        statusItem->setForeground(statusColor);
        statusItem->setToolTip(statusTip);
        nameItem->setToolTip(statusTip);
        table_->setItem(row, ColStatus, statusItem);

        // Col 9: Path
        const QString nativePath = pathText(info.path);
        auto* pathItem = new QTableWidgetItem(nativePath);
        pathItem->setForeground(colors.textDim);
        pathItem->setToolTip(nativePath);
        table_->setItem(row, ColPath, pathItem);

        ++row;
    }

    table_->setSortingEnabled(true);
    if (sortCol >= 0 && sortCol < ColCount)
        table_->sortByColumn(sortCol, sortOrder);

    updatingTable_ = false;
    updateSummaryLabels();
}

void PluginManagerDialog::updateSummaryLabels()
{
    int instruments = 0;
    int effects = 0;
    int favorites = 0;
    int disabled = 0;
    int failed = 0;
    for (const PluginInfo& p : store_.plugins) {
        if (pluginMainGroup(p) == "Instrument")
            ++instruments;
        else
            ++effects;
        if (p.favorite)
            ++favorites;
        if (!p.enabled)
            ++disabled;
        if (p.probe == PluginInfo::Probe::Failed)
            ++failed;
    }

    const int visible = table_ != nullptr ? table_->rowCount() : 0;
    const int total = static_cast<int>(store_.plugins.size());
    statsLabel_->setText(tr("Showing %1 of %2 plugins  |  Instruments: %3  |  Effects: %4  |  Favorites: %5  "
                            "|  Disabled: %6  |  Failed: %7")
            .arg(visible)
            .arg(total)
            .arg(instruments)
            .arg(effects)
            .arg(favorites)
            .arg(disabled)
            .arg(failed));

    const bool idle = scanThread_ == nullptr && (prober_ == nullptr || !prober_->isRunning());
    retryFailedButton_->setEnabled(
        idle && failed > 0 && probeBox_->isChecked() && !PluginProber::probeProgram().isEmpty());

    if (!idle)
        return;

    if (!store_.lastScan.isValid() && store_.plugins.empty()) {
        result_->setText(tr("Not scanned yet. Press Scan to discover VST3 and CLAP plugins."));
        return;
    }
    int vst3 = 0;
    for (const PluginInfo& p : store_.plugins)
        vst3 += p.format == PluginFormat::Vst3 ? 1 : 0;
    const int clap = total - vst3;
    QString text = store_.lastScan.isValid()
        ? tr("Last scan %1: %2 VST3, %3 CLAP.")
              .arg(QLocale().toString(store_.lastScan.toLocalTime(), QLocale::ShortFormat))
              .arg(vst3)
              .arg(clap)
        : tr("%1 VST3, %2 CLAP.").arg(vst3).arg(clap);
    if (failed > 0) {
        text += QLatin1Char(' ')
            + tr("%n plugin(s) failed probing (hover Status column for details).", nullptr, failed);
    }
    result_->setText(text);
}

void PluginManagerDialog::handleTableCellClicked(int row, int column)
{
    if (updatingTable_ || column != ColFavorite || row < 0 || row >= table_->rowCount())
        return;
    QTableWidgetItem* idItem = table_->item(row, ColEnabled);
    if (idItem == nullptr)
        return;
    const auto idx = static_cast<std::size_t>(idItem->data(kPluginIndexRole).toULongLong());
    if (idx >= store_.plugins.size())
        return;

    PluginInfo& info = store_.plugins[idx];
    info.favorite = !info.favorite;

    if (favOnlyBox_->isChecked() && !info.favorite) {
        populateTable();
    } else if (QTableWidgetItem* favItem = table_->item(row, ColFavorite)) {
        updatingTable_ = true;
        favItem->setText(info.favorite ? tr("FAV") : QStringLiteral("-"));
        favItem->setForeground(info.favorite ? Theme::colors().accent : Theme::colors().textDim);
        QFont f = favItem->font();
        f.setBold(info.favorite);
        favItem->setFont(f);
        updatingTable_ = false;
        updateSummaryLabels();
    }
    emit storeChanged(store_);
}

void PluginManagerDialog::handleTableItemChanged(QTableWidgetItem* item)
{
    if (updatingTable_ || item == nullptr || item->column() != ColEnabled)
        return;
    const auto idx = static_cast<std::size_t>(item->data(kPluginIndexRole).toULongLong());
    if (idx >= store_.plugins.size())
        return;

    const bool enabled = item->checkState() == Qt::Checked;
    if (store_.plugins[idx].enabled == enabled)
        return;
    store_.plugins[idx].enabled = enabled;

    if (disabledOnlyBox_->isChecked() && enabled) {
        populateTable();
    } else {
        updatingTable_ = true;
        if (QTableWidgetItem* nameItem = table_->item(item->row(), ColName)) {
            const bool dim = !enabled || store_.plugins[idx].probe == PluginInfo::Probe::Failed;
            nameItem->setForeground(dim ? Theme::colors().textDim : Theme::colors().text);
        }
        updatingTable_ = false;
        updateSummaryLabels();
    }
    emit storeChanged(store_);
}

std::vector<std::size_t> PluginManagerDialog::selectedPluginIndices() const
{
    std::set<std::size_t> unique;
    for (const QTableWidgetItem* item : table_->selectedItems()) {
        if (const QTableWidgetItem* idItem = table_->item(item->row(), ColEnabled)) {
            const auto idx = static_cast<std::size_t>(idItem->data(kPluginIndexRole).toULongLong());
            if (idx < store_.plugins.size())
                unique.insert(idx);
        }
    }
    return {unique.begin(), unique.end()};
}

void PluginManagerDialog::showTableContextMenu(const QPoint& pos)
{
    if (QTableWidgetItem* clicked = table_->itemAt(pos); clicked != nullptr && !clicked->isSelected()) {
        table_->selectRow(clicked->row());
    }
    const std::vector<std::size_t> indices = selectedPluginIndices();
    if (indices.empty())
        return;

    const PluginInfo& primary = store_.plugins[indices.front()];
    const bool allFav = std::all_of(
        indices.begin(), indices.end(), [this](std::size_t idx) { return store_.plugins[idx].favorite; });
    const bool allEnabled = std::all_of(
        indices.begin(), indices.end(), [this](std::size_t idx) { return store_.plugins[idx].enabled; });
    const bool idle = scanThread_ == nullptr && !prober_->isRunning();

    QMenu menu(this);

    menu.addAction(
        allFav ? tr("Remove Favorite") : tr("Mark as Favorite"), this, [this, indices, target = !allFav] {
            for (std::size_t idx : indices)
                store_.plugins[idx].favorite = target;
            populateTable();
            emit storeChanged(store_);
        });

    menu.addAction(allEnabled ? tr("Disable Selected") : tr("Enable Selected"), this,
        [this, indices, target = !allEnabled] {
            for (std::size_t idx : indices)
                store_.plugins[idx].enabled = target;
            populateTable();
            emit storeChanged(store_);
        });

    menu.addSeparator();

    const QString currentCat = utf8(effectiveCategory(primary));
    const QString currentType = utf8(effectiveInstrumentType(primary));
    QMenu* catMenu = menu.addMenu(tr("Set Category"));
    for (const std::string& category : pluginCategories()) {
        if (category == "Uncategorized")
            continue;
        const QString catName = utf8(category);
        if (category == "Instrument") {
            QMenu* instMenu = catMenu->addMenu(catName);
            for (const std::string& type : instrumentTypes()) {
                QAction* act = instMenu->addAction(utf8(type), this, [this, indices, type] {
                    for (std::size_t idx : indices) {
                        store_.plugins[idx].userCategory = "Instrument";
                        store_.plugins[idx].userInstrumentType = type;
                    }
                    rebuildDynamicFilters();
                    populateTable();
                    emit storeChanged(store_);
                });
                act->setCheckable(true);
                act->setChecked(indices.size() == 1 && currentCat == QLatin1String("Instrument")
                    && currentType == utf8(type));
            }
            continue;
        }
        QAction* act = catMenu->addAction(catName, this, [this, indices, category] {
            for (std::size_t idx : indices) {
                store_.plugins[idx].userCategory = category;
                store_.plugins[idx].userInstrumentType.clear();
            }
            rebuildDynamicFilters();
            populateTable();
            emit storeChanged(store_);
        });
        act->setCheckable(true);
        act->setChecked(indices.size() == 1 && currentCat == catName);
    }
    catMenu->addSeparator();
    const bool anyCustomCat = std::any_of(indices.begin(), indices.end(),
        [this](std::size_t idx) { return !store_.plugins[idx].userCategory.empty(); });
    QAction* resetCat = catMenu->addAction(tr("Reset to Auto Category"), this, [this, indices] {
        for (std::size_t idx : indices) {
            store_.plugins[idx].userCategory.clear();
            store_.plugins[idx].userInstrumentType.clear();
        }
        rebuildDynamicFilters();
        populateTable();
        emit storeChanged(store_);
    });
    resetCat->setEnabled(anyCustomCat);

    menu.addSeparator();

    QAction* rescanAct
        = menu.addAction(tr("Rescan Selected"), this, &PluginManagerDialog::rescanSelectedPlugins);
    rescanAct->setEnabled(idle && probeBox_->isChecked() && !PluginProber::probeProgram().isEmpty());

    QAction* openFolderAct = menu.addAction(tr("Open Containing Folder"), this, [primary] {
        const fs::path folder = primary.path.parent_path();
        if (!folder.empty() && !isNetworkPath(folder)) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::cleanPath(toQString(folder))));
        }
    });
    openFolderAct->setEnabled(!primary.path.empty());

    menu.addSeparator();

    QAction* removeAct = menu.addAction(tr("Remove from List"), this, [this, indices] {
        // Erase in reverse index order so earlier indices remain valid.
        for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
            if (*it < store_.plugins.size())
                store_.plugins.erase(store_.plugins.begin() + static_cast<std::ptrdiff_t>(*it));
        }
        rebuildDynamicFilters();
        populateTable();
        emit storeChanged(store_);
    });
    removeAct->setEnabled(idle);

    menu.exec(table_->viewport()->mapToGlobal(pos));
}

} // namespace vloomdaw
