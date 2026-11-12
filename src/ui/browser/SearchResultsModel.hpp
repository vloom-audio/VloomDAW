// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QAbstractListModel>
#include <QIcon>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

class QThread;

namespace vloomdaw {

struct SearchResult;

/// The sample browser's search results: every audio file below the root
/// folder whose name matches, as one flat list. The search runs on a worker
/// thread (core/SampleFolder's searchSampleFolder) and a new search cancels
/// the one in progress.
class SearchResultsModel : public QAbstractListModel {
    Q_OBJECT

public:
    explicit SearchResultsModel(QObject* parent = nullptr);
    ~SearchResultsModel() override;
    SearchResultsModel(const SearchResultsModel&) = delete;
    SearchResultsModel& operator=(const SearchResultsModel&) = delete;

    /// Searches `root` for `text` (both non-empty), replacing the results.
    void search(const QString& root, const QString& text);
    /// Cancels the search in progress and empties the list.
    void clear();
    void setFileIcon(const QIcon& icon) { fileIcon_ = icon; }

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override;

private:
    struct Row {
        QString name; ///< file name, or the text of a note row
        QString folder; ///< relative to the root, shown as a tooltip
        QString path; ///< empty for a note row
    };

    void setRows(std::vector<Row> rows);
    void applyResult(std::uint64_t request, const std::shared_ptr<SearchResult>& result);

    std::vector<Row> rows_;
    QIcon fileIcon_;
    std::uint64_t request_ = 0; ///< the search whose results are wanted
    std::shared_ptr<std::atomic<bool>> cancel_; ///< of the search in progress

    QThread* worker_ = nullptr; ///< searches; joined in the destructor
    QObject* searcher_ = nullptr; ///< lives on worker_
};

} // namespace vloomdaw
