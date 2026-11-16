// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/PluginStore.hpp"

#include <QDialog>

#include <cstddef>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

class QCheckBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPoint;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class QThread;
class QTreeWidget;

namespace vloomdaw {

class PluginProber;

/// Detailed Studio Plugin Manager: filter sidebar (Search, Favorites/Failed/Disabled
/// toggles, Format, Category, Sub-Type, and Vendor checklists), sortable plugin
/// table with Enable/Favorite/Category overrides, manual plugin file addition,
/// partial/failed rescan, and collapsible scan folders management.
class PluginManagerDialog : public QDialog {
    Q_OBJECT

public:
    explicit PluginManagerDialog(QWidget* parent = nullptr);
    ~PluginManagerDialog() override;

    /// Shows `store`'s folders and results. Never emits.
    void setStore(const PluginStore& store);

signals:
    /// A scan finished or plugin metadata/folders changed; `store` is the new state.
    void storeChanged(const PluginStore& store);

private:
    enum class ScanMode { Incremental, Full, Subset };

    void buildSidebar(QFrame* sidebar);
    void buildMainArea(QWidget* area);

    void listFolders();
    void updateFoldersToggleText();
    void addFolder(PluginFormat format);
    void removeFolder();
    void addManualPluginFiles();

    void startScan(bool everything = false);
    void startSubsetProbe(std::vector<PluginInfo> modules);
    void retryFailedPlugins();
    void rescanSelectedPlugins();
    void filesFound(std::vector<PluginInfo> modules);
    void finishScan(std::vector<PluginInfo> plugins);
    void setBusy(bool busy);

    void rebuildDynamicFilters();
    void populateTable();
    void updateSummaryLabels();
    void resetAllFilters();
    void setChecklistAll(QListWidget* list, bool checked);

    [[nodiscard]] bool matchesFilters(const PluginInfo& info) const;
    [[nodiscard]] std::vector<std::size_t> selectedPluginIndices() const;
    void showTableContextMenu(const QPoint& pos);
    void handleTableCellClicked(int row, int column);
    void handleTableItemChanged(QTableWidgetItem* item);

    PluginStore store_;
    ScanMode scanMode_ = ScanMode::Incremental;
    std::vector<std::pair<PluginFormat, std::filesystem::path>> subsetTargets_;

    // Left filter sidebar
    QLineEdit* searchEdit_ = nullptr;
    QCheckBox* favOnlyBox_ = nullptr;
    QCheckBox* failedOnlyBox_ = nullptr;
    QCheckBox* disabledOnlyBox_ = nullptr;
    QListWidget* formatFilter_ = nullptr;
    QListWidget* categoryFilter_ = nullptr;
    QListWidget* subTypeFilter_ = nullptr;
    QListWidget* vendorFilter_ = nullptr;
    QPushButton* uncheckVendorsBtn_ = nullptr;
    QPushButton* resetFiltersBtn_ = nullptr;

    // Remembered unchecked keys across dynamic filter rebuilds
    std::set<std::string> uncheckedFormats_;
    std::set<std::string> uncheckedCategories_;
    std::set<std::string> uncheckedSubTypes_;
    std::set<std::string> uncheckedVendors_;
    bool filtersInitialized_ = false;
    bool updatingFilters_ = false;
    bool updatingTable_ = false;

    // Top bar & collapsible scan folders panel
    QPushButton* scanButton_ = nullptr;
    QPushButton* rescanButton_ = nullptr;
    QPushButton* retryFailedButton_ = nullptr;
    QPushButton* addFileButton_ = nullptr;
    QPushButton* foldersToggle_ = nullptr;
    QFrame* foldersPanel_ = nullptr;
    QTreeWidget* folders_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    QCheckBox* probeBox_ = nullptr;

    // Plugin table & status bar
    QTableWidget* table_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* statsLabel_ = nullptr;
    QLabel* result_ = nullptr;

    QThread* scanThread_ = nullptr; ///< running file scan, or null
    PluginProber* prober_ = nullptr;
};

} // namespace vloomdaw
