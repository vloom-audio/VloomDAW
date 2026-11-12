// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

namespace vloomdaw {

struct PluginStore;

/// Side panel listing the plugins found by the user's last scan, grouped by
/// format, with a name search. It never scans: until a scan has run it
/// shows a hint and a button that opens the Plugin Manager.
class PluginsPanel : public QWidget {
    Q_OBJECT

public:
    explicit PluginsPanel(QWidget* parent = nullptr);

    /// Shows the plugins and scan time of `store`. Never emits.
    void setStore(const PluginStore& store);

signals:
    /// The user wants the Plugin Manager (to scan or change folders).
    void managerRequested();

private:
    void applySearch(const QString& text);

    QLineEdit* search_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QLabel* emptyHint_ = nullptr;
    QPushButton* scanButton_ = nullptr;
    QLabel* footer_ = nullptr;
};

} // namespace vloomdaw
