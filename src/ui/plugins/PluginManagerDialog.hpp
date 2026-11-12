// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/PluginStore.hpp"

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;
class QThread;
class QTreeWidget;

namespace vloomdaw {

/// The Plugin Manager: the folders to search and the Scan button.
///
/// Scanning only happens when the user presses Scan, never on its own. It
/// runs on a worker thread so the window stays responsive, and it only reads
/// file names: no plugin code is loaded or run.
class PluginManagerDialog : public QDialog {
    Q_OBJECT

public:
    explicit PluginManagerDialog(QWidget* parent = nullptr);
    ~PluginManagerDialog() override;

    /// Shows `store`'s folders and results. Never emits.
    void setStore(const PluginStore& store);

signals:
    /// A scan finished or the folder list changed; `store` is the new state.
    void storeChanged(const PluginStore& store);

private:
    void listFolders();
    void addFolder(PluginFormat format);
    void removeFolder();
    void startScan();
    void finishScan(std::vector<PluginInfo> plugins);
    void showResult();

    PluginStore store_;
    QTreeWidget* folders_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QPushButton* scanButton_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* result_ = nullptr;
    QThread* scanThread_ = nullptr; ///< running scan, or null
};

} // namespace vloomdaw
