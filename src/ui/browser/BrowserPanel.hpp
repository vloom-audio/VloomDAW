// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QStringList>
#include <QWidget>

class QComboBox;
class QLabel;
class QFileSystemModel;
class QLineEdit;
class QToolButton;
class QTreeView;

namespace vloomdaw {

/// Side panel listing audio files in the user's sample folders.
///
/// The user keeps a list of folders, picks one, and can search it by name.
/// QFileSystemModel reads the disk on its own thread, so large folders never
/// block the UI. Read-only: nothing here changes files.
class BrowserPanel : public QWidget {
    Q_OBJECT

public:
    explicit BrowserPanel(QWidget* parent = nullptr);

    /// Replaces the folder list and shows `current`. Never emits.
    void setFolders(const QStringList& folders, const QString& current);

signals:
    /// The user added, removed or switched folders.
    void foldersChanged(const QStringList& folders, const QString& current);

private:
    void showFolder(const QString& path);
    void applySearch(const QString& text);
    void addFolder();
    void removeFolder();
    void emitFolders();

    QComboBox* folders_ = nullptr;
    QToolButton* removeButton_ = nullptr;
    QLineEdit* search_ = nullptr;
    QTreeView* tree_ = nullptr;
    QLabel* emptyHint_ = nullptr;
    QFileSystemModel* model_ = nullptr;
};

} // namespace vloomdaw
