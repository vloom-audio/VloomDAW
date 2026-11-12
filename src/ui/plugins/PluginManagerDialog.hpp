// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/PluginStore.hpp"

#include <QDialog>

class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QThread;
class QTreeWidget;

namespace vloomdaw {

class PluginProber;

/// The Plugin Manager: the folders to search and the Scan button.
///
/// Scanning only happens when the user presses Scan, never on its own. It
/// first lists the plugin files on a worker thread. Then, if "Read vendor and
/// category" is on, it loads each new or changed plugin in a separate process
/// (PluginProber) to ask its factory; that runs the plugin's own code. A
/// plugin that crashes or hangs is skipped and marked.
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
    /// `everything`: load every plugin again, ignoring earlier results.
    void startScan(bool everything = false);
    void filesFound(std::vector<PluginInfo> modules);
    void finishScan(std::vector<PluginInfo> plugins);
    void setBusy(bool busy);
    void showResult();

    PluginStore store_;
    QTreeWidget* folders_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QPushButton* scanButton_ = nullptr;
    QPushButton* rescanButton_ = nullptr;
    bool rescanAll_ = false; ///< the running scan ignores earlier results
    QProgressBar* progress_ = nullptr;
    QLabel* result_ = nullptr;
    QCheckBox* probeBox_ = nullptr;
    QThread* scanThread_ = nullptr; ///< running file scan, or null
    PluginProber* prober_ = nullptr;
};

} // namespace vloomdaw
