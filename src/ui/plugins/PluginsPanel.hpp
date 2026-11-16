// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/PluginScanner.hpp"

#include <QWidget>

#include <optional>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace vloomdaw {

struct PluginStore;

/// Side panel listing the plugins found by the user's last scan, separated into
/// Studio One-style `Instruments` and `FX` tabs, grouped by category, vendor,
/// format or flat A-Z, with inline plugin thumbnails and a bottom inspector card.
class PluginsPanel : public QWidget {
    Q_OBJECT

public:
    enum class Tab { Instruments = 0, Effects = 1 };
    enum class GroupBy { Category = 0, Vendor = 1, Format = 2, Flat = 3 };

    explicit PluginsPanel(QWidget* parent = nullptr);

    [[nodiscard]] Tab currentTab() const { return tab_; }
    void setCurrentTab(Tab tab);

    [[nodiscard]] GroupBy groupBy() const;
    [[nodiscard]] bool showThumbnails() const { return showThumbnails_; }

    /// Shows the plugins and scan time of `store`. Never emits.
    void setStore(const PluginStore& store);

    /// Rebuilds inline thumbnails and inspector preview after a capture/removal.
    void refreshThumbnails();

signals:
    /// The user wants the Plugin Manager (to scan or change folders).
    void managerRequested();
    /// The user set the category of plugin `index` (into the store's
    /// plugins); empty category = back to the plugin's own. `instrumentType`
    /// is set only for "Instrument".
    void userCategoryChosen(std::size_t index, const QString& category, const QString& instrumentType);
    void favoriteToggled(std::size_t index, bool favorite);
    /// Plugin `index` (into the store's plugins) should play as a new
    /// channel (an instrument) or go on a mixer track (an effect). Plugins
    /// can also be dragged onto the channel rack or a mixer strip.
    void addToRackRequested(std::size_t index);
    void addToMixerRequested(std::size_t index);

private:
    void rebuild();
    void updateTabCounts();
    void updateInspector(std::optional<std::size_t> pluginIndex);
    void onItemActivated(QTreeWidgetItem* item);
    void showContextMenu(const QPoint& pos);
    void applySearch(const QString& text);

    std::vector<PluginInfo> plugins_;
    Tab tab_ = Tab::Instruments;
    bool showThumbnails_ = true;
    std::optional<std::size_t> selectedPluginIndex_;

    QWidget* tabBarWidget_ = nullptr;
    QPushButton* instTabBtn_ = nullptr;
    QPushButton* fxTabBtn_ = nullptr;

    QWidget* controlsRow_ = nullptr;
    QComboBox* groupBy_ = nullptr;
    QToolButton* thumbToggleBtn_ = nullptr;

    QLineEdit* search_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QLabel* emptyHint_ = nullptr;
    QPushButton* scanButton_ = nullptr;

    QWidget* inspectorCard_ = nullptr;
    QLabel* inspectorBadge_ = nullptr;
    QLabel* inspectorName_ = nullptr;
    QLabel* inspectorVendor_ = nullptr;
    QLabel* inspectorCategory_ = nullptr;
    QLabel* inspectorThumb_ = nullptr;

    QLabel* footer_ = nullptr;
};

} // namespace vloomdaw
