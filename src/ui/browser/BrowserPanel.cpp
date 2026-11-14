// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/browser/BrowserPanel.hpp"

#include "ui/Theme.hpp"
#include "ui/browser/BrowserRoles.hpp"
#include "ui/browser/SampleTreeModel.hpp"
#include "ui/browser/SampleWaveform.hpp"
#include "ui/browser/SearchResultsModel.hpp"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <vector>

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
    auto* refreshButton = smallButton(this, QStringLiteral("refresh"), tr("Refresh (F5)"));
    connect(refreshButton, &QToolButton::clicked, this, &BrowserPanel::refresh);

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

    autoPreview_ = smallButton(this, QStringLiteral("speaker"),
        tr("Auto preview: play each file as you move to it with the arrow keys"));
    autoPreview_->setObjectName(QStringLiteral("autoPreview"));
    autoPreview_->setCheckable(true);
    autoPreview_->setChecked(true);
    connect(autoPreview_, &QToolButton::toggled, this, &BrowserPanel::autoPreviewChanged);

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
    watchForPreview(tree_);
    watchForPreview(results_);
    connect(model_, &QAbstractItemModel::rowsInserted, this, &BrowserPanel::restoreRows);

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
    folderRow->addWidget(refreshButton);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 6, 6);
    layout->setSpacing(8);
    layout->addWidget(title);
    layout->addLayout(folderRow);
    auto* searchRow = new QHBoxLayout;
    searchRow->setSpacing(4);
    searchRow->addWidget(search_, 1);
    searchRow->addWidget(autoPreview_);
    layout->addLayout(searchRow);
    layout->addWidget(tree_, 1);
    layout->addWidget(results_, 1);
    layout->addWidget(emptyHint_, 1);
    // The selected file's waveform and a play button, under the list.
    waveform_ = new SampleWaveform(this);
    layout->addWidget(waveform_);
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

void BrowserPanel::setAutoPreview(bool on)
{
    const QSignalBlocker block(autoPreview_);
    autoPreview_->setChecked(on);
}

bool BrowserPanel::autoPreview() const { return autoPreview_->isChecked(); }

void BrowserPanel::watchForPreview(QAbstractItemView* view)
{
    // A file that becomes current plays: by a click always, by the arrow
    // keys while Auto preview is on.
    connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this,
        [this](const QModelIndex& current) {
            const QString path = current.data(kSampleFileRole).toString();
            if (path.isEmpty() || restoring_)
                return;
            waveform_->setFile(path);
            if (mouseDown_ || autoPreview_->isChecked())
                emit previewRequested(path);
            else
                emit fileShown(path);
        });
    // A click on the file that was already current does not change the
    // current item: toggle it instead.
    connect(view, &QAbstractItemView::clicked, this, [this](const QModelIndex& index) {
        const QString path = index.data(kSampleFileRole).toString();
        if (pressedOnCurrent_ && !path.isEmpty())
            emit previewToggled(path);
    });
    view->viewport()->installEventFilter(this);
    view->installEventFilter(this);
}

bool BrowserPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        // Seen before the view moves its current item to the press.
        for (QAbstractItemView* view :
            {static_cast<QAbstractItemView*>(tree_), static_cast<QAbstractItemView*>(results_)}) {
            if (watched == view->viewport()) {
                const auto* mouse = static_cast<QMouseEvent*>(event);
                const QModelIndex hit = view->indexAt(mouse->position().toPoint());
                pressedOnCurrent_ = hit.isValid() && hit == view->currentIndex();
                mouseDown_ = true;
            }
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        mouseDown_ = false;
    } else if (event->type() == QEvent::ShortcutOverride && (watched == tree_ || watched == results_)
        && static_cast<QKeyEvent*>(event)->key() == Qt::Key_F5) {
        // F5 opens the playlist elsewhere; in the browser's lists it refreshes.
        event->accept();
        return true;
    } else if (event->type() == QEvent::KeyPress && (watched == tree_ || watched == results_)
        && static_cast<QKeyEvent*>(event)->key() == Qt::Key_F5) {
        refresh();
        return true;
    } else if (event->type() == QEvent::KeyPress && (watched == tree_ || watched == results_)) {
        mouseDown_ = false; // a drag can swallow the release
        if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            emit previewStopRequested();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void BrowserPanel::refresh()
{
    if (root_.isEmpty())
        return;
    if (results_->isVisible()) {
        applySearch(); // the same words, read again
        return;
    }
    // Note what is open, selected and scrolled to, read the folder again,
    // then put it all back as the rows arrive (restoreRows()).
    reopen_.clear();
    std::vector<QModelIndex> folders{QModelIndex()};
    while (!folders.empty()) {
        const QModelIndex parent = folders.back();
        folders.pop_back();
        for (int row = 0; row < model_->rowCount(parent); ++row) {
            const QModelIndex child = model_->index(row, 0, parent);
            if (tree_->isExpanded(child)) {
                reopen_ << child.data(kItemPathRole).toString();
                folders.push_back(child);
            }
        }
    }
    reselect_ = tree_->currentIndex().data(kItemPathRole).toString();
    rescroll_ = tree_->verticalScrollBar()->value();
    model_->setRootPath(root_);
}

void BrowserPanel::restoreRows(const QModelIndex& parent, int first, int last)
{
    if (reopen_.isEmpty() && reselect_.isEmpty() && rescroll_ < 0)
        return;
    for (int row = first; row <= last; ++row) {
        const QModelIndex child = model_->index(row, 0, parent);
        const QString path = child.data(kItemPathRole).toString();
        if (reopen_.removeOne(path))
            tree_->expand(child); // reads it; its rows come back through here
        if (!reselect_.isEmpty() && path == reselect_) {
            reselect_.clear();
            restoring_ = true; // putting the selection back is not a click
            tree_->setCurrentIndex(child);
            restoring_ = false;
        }
    }
    if (rescroll_ >= 0) {
        // The list grows as folders are read: go back as far as it allows each time.
        QTimer::singleShot(
            0, this, [this, target = rescroll_] { tree_->verticalScrollBar()->setValue(target); });
        if (reopen_.isEmpty())
            rescroll_ = -1;
    }
}

void BrowserPanel::showFolder(const QString& path)
{
    reopen_.clear(); // a refresh in progress is for the old folder
    reselect_.clear();
    rescroll_ = -1;
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
