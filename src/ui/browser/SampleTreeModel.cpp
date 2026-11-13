// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/browser/SampleTreeModel.hpp"

#include "core/SampleFolder.hpp"
#include "ui/FilePaths.hpp"
#include "ui/browser/BrowserRoles.hpp"

#include <QMimeData>
#include <QThread>
#include <QUrl>

#include <utility>

namespace vloomdaw {

struct SampleTreeModel::Node {
    Kind kind = Kind::Folder;
    State state = State::NotRead;
    QString name;
    QString path;
    Node* parent = nullptr;
    int row = 0; ///< index in parent->children
    std::vector<std::unique_ptr<Node>> children;
};

SampleTreeModel::SampleTreeModel(QObject* parent)
    : QAbstractItemModel(parent)
    , root_(std::make_unique<Node>())
{
    worker_ = new QThread(this);
    worker_->setObjectName(QStringLiteral("SampleBrowser"));
    lister_ = new QObject; // no parent: it moves to the worker thread
    lister_->moveToThread(worker_);
    connect(worker_, &QThread::finished, lister_, &QObject::deleteLater);
    worker_->start(QThread::LowPriority);
}

SampleTreeModel::~SampleTreeModel()
{
    // Waits for a listing in progress; nothing reaches this object afterwards.
    worker_->quit();
    worker_->wait();
}

void SampleTreeModel::setIcons(const QIcon& folder, const QIcon& file)
{
    folderIcon_ = folder;
    fileIcon_ = file;
}

void SampleTreeModel::setRootPath(const QString& path)
{
    beginResetModel();
    root_ = std::make_unique<Node>();
    root_->path = path;
    root_->state = path.isEmpty() ? State::Read : State::NotRead;
    pending_.clear(); // results for the old root are ignored when they arrive
    endResetModel();
    if (!path.isEmpty())
        requestListing(root_.get());
}

SampleTreeModel::Node* SampleTreeModel::nodeOf(const QModelIndex& index) const
{
    return index.isValid() ? static_cast<Node*>(index.internalPointer()) : root_.get();
}

QModelIndex SampleTreeModel::indexOf(Node* node) const
{
    return node == root_.get() ? QModelIndex() : createIndex(node->row, 0, node);
}

QModelIndex SampleTreeModel::index(int row, int column, const QModelIndex& parent) const
{
    const Node* folder = nodeOf(parent);
    if (column != 0 || row < 0 || static_cast<std::size_t>(row) >= folder->children.size())
        return {};
    return createIndex(row, 0, folder->children[static_cast<std::size_t>(row)].get());
}

QModelIndex SampleTreeModel::parent(const QModelIndex& child) const
{
    if (!child.isValid())
        return {};
    return indexOf(nodeOf(child)->parent);
}

int SampleTreeModel::rowCount(const QModelIndex& parent) const
{
    if (parent.column() > 0)
        return 0;
    return static_cast<int>(nodeOf(parent)->children.size());
}

int SampleTreeModel::columnCount(const QModelIndex& /*parent*/) const { return 1; }

QVariant SampleTreeModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    const Node* node = nodeOf(index);
    switch (role) {
    case Qt::DisplayRole:
        return node->name;
    case Qt::DecorationRole:
        if (node->kind == Kind::Note)
            return {};
        return node->kind == Kind::Folder ? folderIcon_ : fileIcon_;
    case Qt::ToolTipRole:
        return node->kind == Kind::Note ? QVariant() : QVariant(node->path);
    case kSampleFileRole:
        return node->kind == Kind::File ? QVariant(node->path) : QVariant();
    case kItemPathRole:
        return node->kind == Kind::Note ? QVariant() : QVariant(node->path);
    default:
        return {};
    }
}

Qt::ItemFlags SampleTreeModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    switch (nodeOf(index)->kind) {
    case Kind::File:
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemNeverHasChildren;
    case Kind::Folder:
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    case Kind::Note:
        return Qt::ItemNeverHasChildren;
    }
    return Qt::NoItemFlags;
}

bool SampleTreeModel::hasChildren(const QModelIndex& parent) const
{
    const Node* node = nodeOf(parent);
    if (node->kind != Kind::Folder)
        return false;
    // Unread folders show an arrow; reading them may find nothing.
    return node->state != State::Read || !node->children.empty();
}

bool SampleTreeModel::canFetchMore(const QModelIndex& parent) const
{
    const Node* node = nodeOf(parent);
    return node->kind == Kind::Folder && node->state == State::NotRead;
}

void SampleTreeModel::fetchMore(const QModelIndex& parent)
{
    Node* node = nodeOf(parent);
    if (node->kind == Kind::Folder && node->state == State::NotRead)
        requestListing(node);
}

void SampleTreeModel::requestListing(Node* folder)
{
    folder->state = State::Reading;
    const std::uint64_t request = nextRequest_++;
    pending_.insert(request, folder);
    const std::filesystem::path path = toPath(folder->path);
    QMetaObject::invokeMethod(
        lister_,
        [this, request, path] {
            // Worker thread. `this` outlives it: the destructor joins the thread.
            auto listing = std::make_shared<FolderListing>(listSampleFolder(path));
            QMetaObject::invokeMethod(
                this, [this, request, listing] { applyListing(request, listing); }, Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void SampleTreeModel::applyListing(std::uint64_t request, const std::shared_ptr<FolderListing>& listing)
{
    Node* folder = pending_.take(request);
    if (folder == nullptr)
        return; // the root changed meanwhile
    folder->state = State::Read;

    std::vector<std::unique_ptr<Node>> rows;
    rows.reserve(listing->entries.size() + 1);
    for (FolderEntry& entry : listing->entries) {
        auto node = std::make_unique<Node>();
        node->kind = entry.isDir ? Kind::Folder : Kind::File;
        node->path = toQString(entry.path);
        node->name = QString::fromUtf8(entry.name.data(), static_cast<qsizetype>(entry.name.size()));
        rows.push_back(std::move(node));
    }
    if (listing->truncated || listing->failed) {
        auto note = std::make_unique<Node>();
        note->kind = Kind::Note;
        note->state = State::Read;
        note->name = listing->truncated
            ? tr("More than %1 items: the rest is not shown").arg(kMaxFolderEntries)
            : tr("Could not read this folder");
        rows.push_back(std::move(note));
    }

    const QModelIndex parentIndex = indexOf(folder);
    if (rows.empty()) {
        if (parentIndex.isValid())
            emit dataChanged(parentIndex, parentIndex); // drop the expand arrow
        return;
    }
    beginInsertRows(parentIndex, 0, static_cast<int>(rows.size()) - 1);
    folder->children = std::move(rows);
    for (std::size_t i = 0; i < folder->children.size(); ++i) {
        folder->children[i]->parent = folder;
        folder->children[i]->row = static_cast<int>(i);
    }
    endInsertRows();
}

QStringList SampleTreeModel::mimeTypes() const { return {QStringLiteral("text/uri-list")}; }

QMimeData* SampleTreeModel::mimeData(const QModelIndexList& indexes) const
{
    QList<QUrl> urls;
    for (const QModelIndex& index : indexes) {
        const Node* node = nodeOf(index);
        if (index.isValid() && node->kind == Kind::File)
            urls << QUrl::fromLocalFile(node->path);
    }
    if (urls.isEmpty())
        return nullptr;
    auto* mime = new QMimeData;
    mime->setUrls(urls);
    return mime;
}

} // namespace vloomdaw
