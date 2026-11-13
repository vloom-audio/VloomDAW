// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QStringList>
#include <QWidget>

class QAbstractItemView;
class QComboBox;
class QLabel;
class QLineEdit;
class QTimer;
class QToolButton;
class QTreeView;

namespace vloomdaw {

/// Side panel listing audio files in the user's sample folders.
///
/// The user keeps a list of folders, picks one, and can search it by name.
/// QFileSystemModel reads the disk on its own thread, so large folders never
/// block the UI. Read-only: nothing here changes files.
class SampleTreeModel;
class SearchResultsModel;

class BrowserPanel : public QWidget {
    Q_OBJECT

public:
    explicit BrowserPanel(QWidget* parent = nullptr);

    /// Replaces the folder list and shows `current`. Never emits.
    void setFolders(const QStringList& folders, const QString& current);
    /// Sets the Auto preview toggle. Never emits.
    void setAutoPreview(bool on);
    [[nodiscard]] bool autoPreview() const;
    /// Reads the folder again (F5): the folders that are open stay open, the
    /// selection and scroll position stay where they were. A search runs again.
    void refresh();

signals:
    /// The user added, removed or switched folders.
    void foldersChanged(const QStringList& folders, const QString& current);
    /// Play this file from its start: the user clicked it, or moved to it
    /// with the keyboard while Auto preview is on.
    void previewRequested(const QString& path);
    /// The user clicked the file that was already selected: stop it if it
    /// is the one playing, play it again otherwise.
    void previewToggled(const QString& path);
    /// The user pressed Esc in the list.
    void previewStopRequested();
    /// The user switched Auto preview.
    void autoPreviewChanged(bool on);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void showFolder(const QString& path);
    void applySearch();
    void addFolder();
    void removeFolder();
    void emitFolders();
    void watchForPreview(QAbstractItemView* view);
    /// After refresh(): reopens folders and reselects the item as their rows arrive.
    void restoreRows(const QModelIndex& parent, int first, int last);

    QComboBox* folders_ = nullptr;
    QToolButton* removeButton_ = nullptr;
    QToolButton* autoPreview_ = nullptr;
    QLineEdit* search_ = nullptr;
    QTreeView* tree_ = nullptr;
    QLabel* emptyHint_ = nullptr;
    SampleTreeModel* model_ = nullptr;
    QTreeView* results_ = nullptr; ///< shown instead of tree_ while searching
    SearchResultsModel* resultsModel_ = nullptr;
    QTimer* searchDelay_ = nullptr; ///< searches once typing pauses, not per key
    QString root_; ///< the folder shown; empty = none
    bool pressedOnCurrent_ = false; ///< the last mouse press hit the selected item
    bool mouseDown_ = false; ///< a mouse button is down in a list (not a key)
    bool restoring_ = false; ///< refresh() is putting the selection back: no preview
    QStringList reopen_; ///< after refresh(): folders still to open again
    QString reselect_; ///< after refresh(): the item to select again
    int rescroll_ = -1; ///< after refresh(): the scroll position to go back to
};

} // namespace vloomdaw
