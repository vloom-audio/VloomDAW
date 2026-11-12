// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/browser/BrowserPanel.hpp"

#include "ui/Theme.hpp"
#include "ui/browser/SampleTreeModel.hpp"
#include "ui/browser/SearchResultsModel.hpp"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace vloomdaw {

namespace {

QToolButton* smallButton(QWidget* parent, const QString& icon, const QString& tip)
{
    auto* button = new QToolButton(parent);
    button->setIcon(Theme::icon(icon));
    button->setIconSize(QSize(14, 14));
    button->setToolTip(tip);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

/// The browser's list style, shared by the folder tree and the search results.
QTreeView* browserView(QWidget* parent)
{
    auto* view = new QTreeView(parent);
    view->setObjectName(QStringLiteral("browserTree"));
    view->setHeaderHidden(true);
    view->setFrameShape(QFrame::NoFrame);
    view->setIndentation(14);
    view->setIconSize(QSize(16, 16));
    view->setUniformRowHeights(true);
    // Files drag out as URLs: drop one on a channel in the channel rack.
    view->setDragEnabled(true);
    view->setDragDropMode(QAbstractItemView::DragOnly);
    return view;
}

} // namespace

BrowserPanel::BrowserPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("browserPanel"));
    setAttribute(Qt::WA_StyledBackground);

    auto* title = new QLabel(tr("BROWSER"), this);
    title->setObjectName(QStringLiteral("panelTitle"));

    folders_ = new QComboBox(this);
    folders_->setToolTip(tr("Sample folder"));
    folders_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    folders_->setMinimumContentsLength(8);
    connect(folders_, &QComboBox::currentIndexChanged, this, [this] {
        showFolder(folders_->currentData().toString());
        emitFolders();
    });

    auto* addButton = smallButton(this, QStringLiteral("plus"), tr("Add a folder…"));
    connect(addButton, &QToolButton::clicked, this, &BrowserPanel::addFolder);
    removeButton_ = smallButton(this, QStringLiteral("minus"), tr("Remove this folder from the list"));
    connect(removeButton_, &QToolButton::clicked, this, &BrowserPanel::removeFolder);

    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("browserSearch"));
    search_->setPlaceholderText(tr("Search (Esc to leave)"));
    search_->setClearButtonEnabled(true);
    searchDelay_ = new QTimer(this);
    searchDelay_->setSingleShot(true);
    searchDelay_->setInterval(150);
    connect(searchDelay_, &QTimer::timeout, this, &BrowserPanel::applySearch);
    connect(search_, &QLineEdit::textChanged, searchDelay_, qOverload<>(&QTimer::start));
    // Escape gives the keyboard back to the transport shortcuts (Space etc.).
    auto* leaveSearch
        = new QShortcut(QKeySequence(Qt::Key_Escape), search_, nullptr, nullptr, Qt::WidgetShortcut);
    connect(leaveSearch, &QShortcut::activated, search_, [this] { search_->clearFocus(); });

    // Folders are read, and searched, on worker threads (SampleTreeModel,
    // SearchResultsModel): big sample libraries must not freeze the UI.
    const QIcon fileIcon = Theme::icon(QStringLiteral("browser-file"));
    model_ = new SampleTreeModel(this);
    model_->setIcons(Theme::icon(QStringLiteral("browser-folder")), fileIcon);
    tree_ = browserView(this);
    tree_->setModel(model_);
    tree_->setAnimated(true);

    // Search: every matching file below the folder, as one list. A separate
    // view, so the tree keeps its open folders while searching.
    resultsModel_ = new SearchResultsModel(this);
    resultsModel_->setFileIcon(fileIcon);
    results_ = browserView(this);
    results_->setModel(resultsModel_);
    results_->setRootIsDecorated(false);
    results_->hide();

    emptyHint_ = new QLabel(tr("Add a folder with your samples using +."), this);
    emptyHint_->setObjectName(QStringLiteral("panelHint"));
    emptyHint_->setWordWrap(true);
    emptyHint_->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    // Only take keyboard focus when clicked: otherwise the search box would
    // grab it at start-up and Space would type instead of playing.
    for (QWidget* w : std::initializer_list<QWidget*>{folders_, search_, tree_, results_})
        w->setFocusPolicy(Qt::ClickFocus);

    auto* folderRow = new QHBoxLayout;
    folderRow->setSpacing(4);
    folderRow->addWidget(folders_, 1);
    folderRow->addWidget(addButton);
    folderRow->addWidget(removeButton_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 6, 6);
    layout->setSpacing(8);
    layout->addWidget(title);
    layout->addLayout(folderRow);
    layout->addWidget(search_);
    layout->addWidget(tree_, 1);
    layout->addWidget(results_, 1);
    layout->addWidget(emptyHint_, 1);
    showFolder(QString());
}

void BrowserPanel::setFolders(const QStringList& folders, const QString& current)
{
    {
        const QSignalBlocker block(folders_);
        folders_->clear();
        for (const QString& path : folders)
            folders_->addItem(QDir(path).dirName().isEmpty() ? path : QDir(path).dirName(), path);
        const int index = folders_->findData(current);
        folders_->setCurrentIndex(index >= 0 ? index : 0);
    }
    removeButton_->setEnabled(folders_->count() > 0);
    showFolder(folders_->currentData().toString());
}

void BrowserPanel::showFolder(const QString& path)
{
    folders_->setToolTip(path);
    // With no folder, show a hint: an empty root path would list the whole disk.
    const bool hasFolder = !path.isEmpty();
    root_ = hasFolder ? path : QString();
    search_->setEnabled(hasFolder);
    emptyHint_->setVisible(!hasFolder);
    model_->setRootPath(root_);
    applySearch(); // a search in progress follows the new folder
}

void BrowserPanel::applySearch()
{
    searchDelay_->stop();
    const QString text = search_->text().trimmed();
    const bool searching = !root_.isEmpty() && !text.isEmpty();
    if (searching)
        resultsModel_->search(root_, text);
    else
        resultsModel_->clear();
    tree_->setVisible(!root_.isEmpty() && !searching);
    results_->setVisible(searching);
}

void BrowserPanel::addFolder()
{
    const QString path = QFileDialog::getExistingDirectory(this, tr("Add a sample folder"), QDir::homePath());
    if (path.isEmpty())
        return;
    const QString clean = QDir::cleanPath(path);
    int index = folders_->findData(clean);
    if (index < 0) {
        const QSignalBlocker block(folders_);
        folders_->addItem(QDir(clean).dirName().isEmpty() ? clean : QDir(clean).dirName(), clean);
        index = folders_->count() - 1;
    }
    removeButton_->setEnabled(true);
    {
        const QSignalBlocker block(folders_);
        folders_->setCurrentIndex(index);
    }
    showFolder(clean);
    emitFolders();
}

void BrowserPanel::removeFolder()
{
    const int index = folders_->currentIndex();
    if (index < 0)
        return;
    {
        const QSignalBlocker block(folders_);
        folders_->removeItem(index);
    }
    removeButton_->setEnabled(folders_->count() > 0);
    showFolder(folders_->currentData().toString());
    emitFolders();
}

void BrowserPanel::emitFolders()
{
    QStringList folders;
    for (int i = 0; i < folders_->count(); ++i)
        folders << folders_->itemData(i).toString();
    emit foldersChanged(folders, folders_->currentData().toString());
}

} // namespace vloomdaw
