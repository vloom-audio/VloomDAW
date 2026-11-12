// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/PluginScanner.hpp"

#include <QWidget>

#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

namespace vloomdaw {

struct PluginStore;

/// Side panel listing the plugins found by the user's last scan, grouped by
/// category, vendor or format (the user picks; remembered), with a search
/// on name and vendor. It never scans: until a scan has run it
/// shows a hint and a button that opens the Plugin Manager.
class PluginsPanel : public QWidget {
    Q_OBJECT

public:
    enum class GroupBy { Category, Vendor, Format };

    explicit PluginsPanel(QWidget* parent = nullptr);

    [[nodiscard]] GroupBy groupBy() const;

    /// Shows the plugins and scan time of `store`. Never emits.
    void setStore(const PluginStore& store);

signals:
    /// The user wants the Plugin Manager (to scan or change folders).
    void managerRequested();
    /// The user set the category of plugin `index` (into the store's
    /// plugins); empty category = back to the plugin's own. `instrumentType`
    /// is set only for "Instrument".
    void userCategoryChosen(std::size_t index, const QString& category, const QString& instrumentType);

private:
    void rebuild();
    void showContextMenu(const QPoint& pos);
    void applySearch(const QString& text);

    std::vector<PluginInfo> plugins_;
    QComboBox* groupBy_ = nullptr;

    QLineEdit* search_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QLabel* emptyHint_ = nullptr;
    QPushButton* scanButton_ = nullptr;
    QLabel* footer_ = nullptr;
};

} // namespace vloomdaw
