// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/PluginStore.hpp"

#include <QWidget>

#include <cstddef>
#include <vector>

class QLabel;
class QLineEdit;
class QRadioButton;
class QScrollArea;
class QStackedWidget;
class QToolButton;
class QTreeWidget;

namespace vloomdaw {

/// An FL Studio-style plugin picker popup.
/// Displays plugins organized by category in a clean multi-column layout,
/// with live search filtering, smooth mouse-wheel scrolling, and view mode switching
/// (Categories, Simple, Tree).
class PluginPickerPopup : public QWidget {
    Q_OBJECT

public:
    enum class Filter { Effect, Instrument, All };

    PluginPickerPopup(const PluginStore& store, Filter filter = Filter::Effect, QWidget* parent = nullptr);

    /// Shows and positions the picker anchored near `globalPos` while ensuring
    /// it remains strictly within the visible screen boundaries.
    void showAt(const QPoint& globalPos);

signals:
    void pluginSelected(std::size_t pluginIndex);
    void morePluginsRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct Entry {
        std::size_t index = 0;
        QString name;
        QString vendor;
        QString category;
        QString tags;
        QWidget* buttonWidget = nullptr;
        QWidget* simpleWidget = nullptr;
    };

    struct CategoryGroup {
        QString name;
        std::vector<Entry*> entries;
        QWidget* groupWidget = nullptr;
    };

    void initUi();
    void collectEntries();
    void buildCategoriesView();
    void buildSimpleView();
    void buildTreeView();
    void filterPlugins(const QString& query);
    void selectFirstVisible();

    const PluginStore& store_;
    Filter filter_;

    QLineEdit* searchEdit_ = nullptr;
    QLabel* countLabel_ = nullptr;
    QStackedWidget* viewStack_ = nullptr;
    QScrollArea* catScroll_ = nullptr;
    QScrollArea* simpleScroll_ = nullptr;
    QTreeWidget* treeWidget_ = nullptr;
    QLabel* noMatchLabel_ = nullptr;

    QRadioButton* catRadio_ = nullptr;
    QRadioButton* simpleRadio_ = nullptr;
    QRadioButton* treeRadio_ = nullptr;

    std::vector<Entry> allEntries_;
    std::vector<CategoryGroup> groups_;
};

} // namespace vloomdaw
