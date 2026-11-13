// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/browser/SearchResultsModel.hpp"

#include "core/SampleFolder.hpp"
#include "ui/FilePaths.hpp"
#include "ui/browser/BrowserRoles.hpp"

#include <QMimeData>
#include <QThread>
#include <QUrl>

#include <utility>

namespace vloomdaw {

SearchResultsModel::SearchResultsModel(QObject* parent)
    : QAbstractListModel(parent)
{
    worker_ = new QThread(this);
    worker_->setObjectName(QStringLiteral("SampleSearch"));
    searcher_ = new QObject; // no parent: it moves to the worker thread
    searcher_->moveToThread(worker_);
    connect(worker_, &QThread::finished, searcher_, &QObject::deleteLater);
    worker_->start(QThread::LowPriority);
}

SearchResultsModel::~SearchResultsModel()
{
    if (cancel_)
        cancel_->store(true);
    worker_->quit();
    worker_->wait(); // nothing reaches this object afterwards
}

void SearchResultsModel::clear()
{
    if (cancel_)
        cancel_->store(true);
    cancel_.reset();
    ++request_; // results still on their way are ignored
    setRows({});
}

void SearchResultsModel::search(const QString& root, const QString& text)
{
    if (cancel_)
        cancel_->store(true); // stop the previous search: its results are no longer wanted
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    const std::uint64_t request = ++request_;
    setRows({{tr("Searching…"), {}, {}}});

    const QByteArray utf8 = text.toUtf8();
    const std::filesystem::path path = toPath(root);
    QMetaObject::invokeMethod(
        searcher_,
        [this, request, path, term = std::string(utf8.constData(), static_cast<std::size_t>(utf8.size())),
            cancel = cancel_] {
            // Worker thread. `this` outlives it: the destructor joins the thread.
            if (cancel->load())
                return; // already replaced while queued
            auto result = std::make_shared<SearchResult>(searchSampleFolder(path, term, cancel.get()));
            if (result->cancelled)
                return;
            QMetaObject::invokeMethod(
                this, [this, request, result] { applyResult(request, result); }, Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void SearchResultsModel::applyResult(std::uint64_t request, const std::shared_ptr<SearchResult>& result)
{
    if (request != request_)
        return;
    cancel_.reset();
    std::vector<Row> rows;
    rows.reserve(result->hits.size() + 1);
    for (const SearchHit& hit : result->hits) {
        rows.push_back({QString::fromUtf8(hit.name.data(), static_cast<qsizetype>(hit.name.size())),
            QString::fromUtf8(hit.folder.data(), static_cast<qsizetype>(hit.folder.size())),
            toQString(hit.path)});
    }
    if (rows.empty())
        rows.push_back({tr("No samples match"), {}, {}});
    else if (result->truncated)
        rows.push_back(
            {tr("First %1 matches shown: type more to narrow the search").arg(kMaxSearchResults), {}, {}});
    setRows(std::move(rows));
}

void SearchResultsModel::setRows(std::vector<Row> rows)
{
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

int SearchResultsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant SearchResultsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || static_cast<std::size_t>(index.row()) >= rows_.size())
        return {};
    const Row& row = rows_[static_cast<std::size_t>(index.row())];
    const bool note = row.path.isEmpty();
    switch (role) {
    case Qt::DisplayRole:
        return row.name;
    case Qt::DecorationRole:
        return note ? QVariant() : QVariant(fileIcon_);
    case Qt::ToolTipRole:
        if (note)
            return {};
        return row.folder.isEmpty() ? row.name : row.folder + QLatin1Char('/') + row.name;
    case kSampleFileRole:
        return note ? QVariant() : QVariant(row.path);
    default:
        return {};
    }
}

Qt::ItemFlags SearchResultsModel::flags(const QModelIndex& index) const
{
    if (!index.isValid() || static_cast<std::size_t>(index.row()) >= rows_.size())
        return Qt::NoItemFlags;
    if (rows_[static_cast<std::size_t>(index.row())].path.isEmpty())
        return Qt::ItemNeverHasChildren; // a note: greyed, not selectable
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemNeverHasChildren;
}

QStringList SearchResultsModel::mimeTypes() const { return {QStringLiteral("text/uri-list")}; }

QMimeData* SearchResultsModel::mimeData(const QModelIndexList& indexes) const
{
    QList<QUrl> urls;
    for (const QModelIndex& index : indexes) {
        if (!index.isValid() || static_cast<std::size_t>(index.row()) >= rows_.size())
            continue;
        const Row& row = rows_[static_cast<std::size_t>(index.row())];
        if (!row.path.isEmpty())
            urls << QUrl::fromLocalFile(row.path);
    }
    if (urls.isEmpty())
        return nullptr;
    auto* mime = new QMimeData;
    mime->setUrls(urls);
    return mime;
}

} // namespace vloomdaw
