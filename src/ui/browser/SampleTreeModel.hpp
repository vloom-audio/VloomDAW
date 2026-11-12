// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QAbstractItemModel>
#include <QHash>
#include <QIcon>

#include <cstdint>
#include <memory>
#include <vector>

class QThread;

namespace vloomdaw {

struct FolderListing;

/// The sample browser's tree: folders and audio files under one root.
///
/// Replaces QFileSystemModel, which sorts and inserts rows on the UI thread
/// and froze the app for seconds on folders with thousands of samples. Here a
/// folder is read and sorted on a worker thread (core/SampleFolder) the first
/// time it is expanded, and its rows go in with one insert.
class SampleTreeModel : public QAbstractItemModel {
    Q_OBJECT

public:
    explicit SampleTreeModel(QObject* parent = nullptr);
    ~SampleTreeModel() override;
    SampleTreeModel(const SampleTreeModel&) = delete;
    SampleTreeModel& operator=(const SampleTreeModel&) = delete;

    /// Shows `path` (empty: nothing) and starts reading it.
    void setRootPath(const QString& path);
    void setIcons(const QIcon& folder, const QIcon& file);

    [[nodiscard]] QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    [[nodiscard]] bool hasChildren(const QModelIndex& parent = {}) const override;
    [[nodiscard]] bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override;

private:
    struct Node;
    enum class Kind { Folder, File, Note };
    enum class State { NotRead, Reading, Read };

    [[nodiscard]] Node* nodeOf(const QModelIndex& index) const;
    [[nodiscard]] QModelIndex indexOf(Node* node) const;
    void requestListing(Node* folder);
    void applyListing(std::uint64_t request, const std::shared_ptr<FolderListing>& listing);

    std::unique_ptr<Node> root_; ///< invisible; its children are the top rows
    QHash<std::uint64_t, Node*> pending_; ///< requests in flight; a new root drops them
    std::uint64_t nextRequest_ = 1;
    QIcon folderIcon_;
    QIcon fileIcon_;

    QThread* worker_ = nullptr; ///< reads folders; joined in the destructor
    QObject* lister_ = nullptr; ///< lives on worker_
};

} // namespace vloomdaw
