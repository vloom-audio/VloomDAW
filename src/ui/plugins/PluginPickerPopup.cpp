// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/PluginPickerPopup.hpp"

#include "core/PluginMetadata.hpp"
#include "ui/Theme.hpp"

#include <QButtonGroup>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QScreen>
#include <QScrollArea>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <map>

namespace vloomdaw {

namespace {

constexpr int kPopupWidth = 760;
constexpr int kPopupHeight = 540;
constexpr int kColumnCount = 3;

const QStringList kStandardCategoryOrder = {
    QStringLiteral("Favorites"),
    QStringLiteral("Dynamics"),
    QStringLiteral("EQ"),
    QStringLiteral("Filter"),
    QStringLiteral("Reverb"),
    QStringLiteral("Delay"),
    QStringLiteral("Distortion"),
    QStringLiteral("Amp"),
    QStringLiteral("Modulation"),
    QStringLiteral("Pitch"),
    QStringLiteral("Mastering"),
    QStringLiteral("Spatial"),
    QStringLiteral("Analyzer"),
    QStringLiteral("Restoration"),
    QStringLiteral("Utility"),
    QStringLiteral("Other"),
    QStringLiteral("Uncategorized"),
};

int categoryRank(const QString& name)
{
    const auto idx = static_cast<int>(kStandardCategoryOrder.indexOf(name));
    return idx >= 0 ? idx : 100;
}

} // namespace

PluginPickerPopup::PluginPickerPopup(const PluginStore& store, Filter filter, QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
    , store_(store)
    , filter_(filter)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_StyledBackground);
    setObjectName(QStringLiteral("pluginPickerPopup"));

    setStyleSheet(QStringLiteral("#pluginPickerPopup {"
                                 "  background-color: #1a1d21;"
                                 "  border: 1px solid #343b43;"
                                 "  border-radius: 4px;"
                                 "}"
                                 "QScrollArea {"
                                 "  background: transparent;"
                                 "  border: none;"
                                 "}"
                                 "QScrollBar:vertical {"
                                 "  background: #14171a;"
                                 "  width: 10px;"
                                 "  margin: 0px;"
                                 "}"
                                 "QScrollBar::handle:vertical {"
                                 "  background: #363c44;"
                                 "  min-height: 20px;"
                                 "  border-radius: 4px;"
                                 "}"
                                 "QScrollBar::handle:vertical:hover {"
                                 "  background: #4a535e;"
                                 "}"
                                 "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
                                 "  height: 0px;"
                                 "}"
                                 "QRadioButton {"
                                 "  color: #abb2ba;"
                                 "  font-size: 11px;"
                                 "  spacing: 5px;"
                                 "}"
                                 "QRadioButton:checked {"
                                 "  color: #ffffff;"
                                 "}"
                                 "QRadioButton::indicator {"
                                 "  width: 12px;"
                                 "  height: 12px;"
                                 "}"));

    collectEntries();
    initUi();
}

void PluginPickerPopup::collectEntries()
{
    allEntries_.clear();
    for (std::size_t i = 0; i < store_.plugins.size(); ++i) {
        const auto& p = store_.plugins[i];
        if (!p.enabled)
            continue;
        const std::string cat = effectiveCategory(p);
        if (filter_ == Filter::Effect && cat == "Instrument")
            continue;
        if (filter_ == Filter::Instrument && cat != "Instrument")
            continue;
        if (p.name.empty() || p.probe == PluginInfo::Probe::Failed)
            continue;

        Entry entry;
        entry.index = i;
        entry.name = QString::fromStdString(p.name);
        entry.vendor = QString::fromStdString(p.vendor);
        entry.category = QString::fromStdString(cat.empty() ? "Uncategorized" : cat);
        entry.tags = QString::fromStdString(p.tags);
        entry.favorite = p.favorite;
        allEntries_.push_back(std::move(entry));
    }

    // Sort entries alphabetically
    std::sort(allEntries_.begin(), allEntries_.end(),
        [](const Entry& a, const Entry& b) { return a.name.localeAwareCompare(b.name) < 0; });

    // Group by category (plus Favorites group when any entry is marked favorite)
    std::vector<Entry*> favList;
    std::map<QString, std::vector<Entry*>> catMap;
    for (auto& entry : allEntries_) {
        if (entry.favorite)
            favList.push_back(&entry);
        catMap[entry.category].push_back(&entry);
    }

    groups_.clear();
    if (!favList.empty()) {
        CategoryGroup favGroup;
        favGroup.name = tr("Favorites");
        favGroup.entries = std::move(favList);
        groups_.push_back(std::move(favGroup));
    }
    for (auto& [name, list] : catMap) {
        CategoryGroup group;
        group.name = name;
        group.entries = std::move(list);
        groups_.push_back(std::move(group));
    }

    // Sort groups according to FL Studio standard order, then alphabetically
    std::sort(groups_.begin(), groups_.end(), [](const CategoryGroup& a, const CategoryGroup& b) {
        const int rankA = categoryRank(a.name);
        const int rankB = categoryRank(b.name);
        if (rankA != rankB)
            return rankA < rankB;
        return a.name.localeAwareCompare(b.name) < 0;
    });
}

void PluginPickerPopup::initUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 8);
    mainLayout->setSpacing(8);

    // --- Header ---
    auto* headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);

    auto* moreBtn = new QToolButton(this);
    moreBtn->setText(tr("More plugins…"));
    moreBtn->setIcon(Theme::icon(QStringLiteral("plug")));
    moreBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    moreBtn->setCursor(Qt::PointingHandCursor);
    moreBtn->setFocusPolicy(Qt::NoFocus);
    moreBtn->setStyleSheet(QStringLiteral("QToolButton {"
                                          "  background-color: #2b323a;"
                                          "  color: #cfd4da;"
                                          "  font-size: 11px;"
                                          "  padding: 4px 10px;"
                                          "  border: 1px solid #3c444e;"
                                          "  border-radius: 3px;"
                                          "}"
                                          "QToolButton:hover {"
                                          "  background-color: #38424d;"
                                          "  color: #ffffff;"
                                          "}"));
    connect(moreBtn, &QToolButton::clicked, this, [this] {
        emit morePluginsRequested();
        close();
    });
    headerLayout->addWidget(moreBtn);

    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText(tr("Search plugins… (Ctrl+F)"));
    searchEdit_->setClearButtonEnabled(true);
    searchEdit_->setStyleSheet(QStringLiteral("QLineEdit {"
                                              "  background-color: #14171a;"
                                              "  color: #ffffff;"
                                              "  border: 1px solid #2d333b;"
                                              "  border-radius: 3px;"
                                              "  padding: 4px 8px;"
                                              "  font-size: 12px;"
                                              "}"
                                              "QLineEdit:focus {"
                                              "  border: 1px solid #ff8a3d;"
                                              "}"));
    connect(searchEdit_, &QLineEdit::textChanged, this, &PluginPickerPopup::filterPlugins);
    connect(searchEdit_, &QLineEdit::returnPressed, this, &PluginPickerPopup::selectFirstVisible);
    headerLayout->addWidget(searchEdit_, 1);

    auto* closeBtn = new QToolButton(this);
    closeBtn->setIcon(Theme::icon(QStringLiteral("window-close")));
    closeBtn->setIconSize(QSize(12, 12));
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFocusPolicy(Qt::NoFocus);
    closeBtn->setStyleSheet(QStringLiteral("QToolButton {"
                                           "  background: transparent;"
                                           "  border: none;"
                                           "  padding: 4px;"
                                           "}"
                                           "QToolButton:hover {"
                                           "  background-color: #d32f2f;"
                                           "  border-radius: 2px;"
                                           "}"));
    connect(closeBtn, &QToolButton::clicked, this, &QWidget::close);
    headerLayout->addWidget(closeBtn);

    mainLayout->addLayout(headerLayout);

    // --- Body Views ---
    viewStack_ = new QStackedWidget(this);

    buildCategoriesView();
    buildSimpleView();
    buildTreeView();

    mainLayout->addWidget(viewStack_, 1);

    // --- Footer ---
    auto* footerLayout = new QHBoxLayout;
    footerLayout->setContentsMargins(4, 2, 4, 0);
    footerLayout->setSpacing(12);

    countLabel_ = new QLabel(this);
    countLabel_->setStyleSheet(QStringLiteral("color: #7d8590; font-size: 11px;"));
    countLabel_->setText(tr("%1 plugins").arg(allEntries_.size()));
    footerLayout->addWidget(countLabel_);

    footerLayout->addStretch();

    catRadio_ = new QRadioButton(tr("Categories"), this);
    simpleRadio_ = new QRadioButton(tr("Simple"), this);
    treeRadio_ = new QRadioButton(tr("Tree"), this);
    catRadio_->setChecked(true);

    auto* modeGroup = new QButtonGroup(this);
    modeGroup->addButton(catRadio_, 0);
    modeGroup->addButton(simpleRadio_, 1);
    modeGroup->addButton(treeRadio_, 2);

    connect(modeGroup, &QButtonGroup::idClicked, this, [this](int id) {
        viewStack_->setCurrentIndex(id);
        filterPlugins(searchEdit_->text());
    });

    footerLayout->addWidget(catRadio_);
    footerLayout->addWidget(simpleRadio_);
    footerLayout->addWidget(treeRadio_);

    mainLayout->addLayout(footerLayout);
}

void PluginPickerPopup::buildCategoriesView()
{
    catScroll_ = new QScrollArea(this);
    catScroll_->setWidgetResizable(true);

    auto* container = new QWidget(catScroll_);
    container->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* hLayout = new QHBoxLayout(container);
    hLayout->setContentsMargins(2, 2, 2, 2);
    hLayout->setSpacing(10);

    // Create 3 columns
    std::array<QVBoxLayout*, kColumnCount> colLayouts{};
    for (int c = 0; c < kColumnCount; ++c) {
        auto* colWidget = new QWidget(container);
        colWidget->setStyleSheet(QStringLiteral("background: transparent;"));
        auto* vLayout = new QVBoxLayout(colWidget);
        vLayout->setContentsMargins(0, 0, 0, 0);
        vLayout->setSpacing(8);
        colLayouts[c] = vLayout;
        hLayout->addWidget(colWidget, 1);
    }

    // Estimate items per column to balance
    std::size_t totalWeight = 0;
    for (const auto& g : groups_)
        totalWeight += (g.entries.size() + 2);

    const std::size_t targetPerCol = std::max<std::size_t>(1, totalWeight / kColumnCount);

    int currentCol = 0;
    std::size_t currentColWeight = 0;

    for (auto& group : groups_) {
        auto* groupWidget = new QWidget(container);
        groupWidget->setStyleSheet(QStringLiteral("background: transparent;"));
        auto* gLayout = new QVBoxLayout(groupWidget);
        gLayout->setContentsMargins(0, 0, 0, 4);
        gLayout->setSpacing(1);

        // FL Studio Category Banner
        auto* banner = new QLabel(group.name, groupWidget);
        banner->setStyleSheet(QStringLiteral("QLabel {"
                                             "  background-color: #3b434c;"
                                             "  color: #f0f2f5;"
                                             "  font-weight: bold;"
                                             "  font-size: 11px;"
                                             "  padding: 3px 6px;"
                                             "  border-radius: 2px;"
                                             "}"));
        gLayout->addWidget(banner);

        for (auto* entry : group.entries) {
            auto* btn = new QToolButton(groupWidget);
            btn->setText(entry->name);
            btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
            btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            btn->setFixedHeight(20);
            btn->setCursor(Qt::PointingHandCursor);
            if (!entry->vendor.isEmpty())
                btn->setToolTip(tr("%1 · %2").arg(entry->name, entry->vendor));
            btn->setStyleSheet(QStringLiteral("QToolButton {"
                                              "  background: transparent;"
                                              "  color: #c9ced4;"
                                              "  text-align: left;"
                                              "  padding-left: 8px;"
                                              "  border: none;"
                                              "  font-size: 11px;"
                                              "}"
                                              "QToolButton:hover {"
                                              "  background-color: #2b3540;"
                                              "  color: #ffffff;"
                                              "  border-radius: 2px;"
                                              "}"));

            connect(btn, &QToolButton::clicked, this, [this, idx = entry->index] {
                emit pluginSelected(idx);
                close();
            });

            gLayout->addWidget(btn);
            entry->buttonWidgets.push_back(btn);
        }

        group.groupWidget = groupWidget;

        if (currentCol < kColumnCount - 1 && currentColWeight >= targetPerCol) {
            currentCol++;
            currentColWeight = 0;
        }
        colLayouts[currentCol]->addWidget(groupWidget);
        currentColWeight += (group.entries.size() + 2);
    }

    for (int c = 0; c < kColumnCount; ++c)
        colLayouts[c]->addStretch();

    catScroll_->setWidget(container);
    viewStack_->addWidget(catScroll_);
}

void PluginPickerPopup::buildSimpleView()
{
    simpleScroll_ = new QScrollArea(this);
    simpleScroll_->setWidgetResizable(true);

    auto* container = new QWidget(simpleScroll_);
    container->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* hLayout = new QHBoxLayout(container);
    hLayout->setContentsMargins(2, 2, 2, 2);
    hLayout->setSpacing(10);

    std::array<QVBoxLayout*, kColumnCount> colLayouts{};
    for (int c = 0; c < kColumnCount; ++c) {
        auto* colWidget = new QWidget(container);
        colWidget->setStyleSheet(QStringLiteral("background: transparent;"));
        auto* vLayout = new QVBoxLayout(colWidget);
        vLayout->setContentsMargins(0, 0, 0, 0);
        vLayout->setSpacing(2);
        colLayouts[c] = vLayout;
        hLayout->addWidget(colWidget, 1);
    }

    const std::size_t itemsPerCol
        = std::max<std::size_t>(1, (allEntries_.size() + kColumnCount - 1) / kColumnCount);

    for (std::size_t i = 0; i < allEntries_.size(); ++i) {
        auto& entry = allEntries_[i];
        auto* btn = new QToolButton(container);
        btn->setText(entry.name);
        btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        btn->setFixedHeight(20);
        btn->setCursor(Qt::PointingHandCursor);
        if (!entry.vendor.isEmpty())
            btn->setToolTip(tr("%1 · %2 [%3]").arg(entry.name, entry.vendor, entry.category));
        btn->setStyleSheet(QStringLiteral("QToolButton {"
                                          "  background: transparent;"
                                          "  color: #c9ced4;"
                                          "  text-align: left;"
                                          "  padding-left: 8px;"
                                          "  border: none;"
                                          "  font-size: 11px;"
                                          "}"
                                          "QToolButton:hover {"
                                          "  background-color: #2b3540;"
                                          "  color: #ffffff;"
                                          "  border-radius: 2px;"
                                          "}"));

        connect(btn, &QToolButton::clicked, this, [this, idx = entry.index] {
            emit pluginSelected(idx);
            close();
        });

        const int col = std::min<int>(static_cast<int>(i / itemsPerCol), kColumnCount - 1);
        colLayouts[col]->addWidget(btn);
        entry.simpleWidget = btn;
    }

    for (int c = 0; c < kColumnCount; ++c)
        colLayouts[c]->addStretch();

    simpleScroll_->setWidget(container);
    viewStack_->addWidget(simpleScroll_);
}

void PluginPickerPopup::buildTreeView()
{
    treeWidget_ = new QTreeWidget(this);
    treeWidget_->setHeaderHidden(true);
    treeWidget_->setCursor(Qt::PointingHandCursor);
    treeWidget_->setStyleSheet(QStringLiteral("QTreeWidget {"
                                              "  background: transparent;"
                                              "  border: none;"
                                              "  color: #c9ced4;"
                                              "  font-size: 11px;"
                                              "}"
                                              "QTreeWidget::item {"
                                              "  padding: 2px 4px;"
                                              "  height: 20px;"
                                              "}"
                                              "QTreeWidget::item:hover {"
                                              "  background-color: #2b3540;"
                                              "  color: #ffffff;"
                                              "  border-radius: 2px;"
                                              "}"
                                              "QTreeWidget::item:selected {"
                                              "  background-color: #3b434c;"
                                              "  color: #ffffff;"
                                              "}"));

    for (const auto& group : groups_) {
        auto* catItem = new QTreeWidgetItem(treeWidget_);
        catItem->setText(0, group.name);
        catItem->setExpanded(true);
        QFont f = catItem->font(0);
        f.setBold(true);
        catItem->setFont(0, f);
        catItem->setForeground(0, QColor(0xf0, 0xf2, 0xf5));

        for (const auto* entry : group.entries) {
            auto* item = new QTreeWidgetItem(catItem);
            item->setText(0, entry->name);
            item->setData(0, Qt::UserRole, static_cast<qulonglong>(entry->index));
            if (!entry->vendor.isEmpty())
                item->setToolTip(0, tr("%1 · %2").arg(entry->name, entry->vendor));
        }
    }

    connect(treeWidget_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int /*col*/) {
        const QVariant index = item->data(0, Qt::UserRole);
        if (index.isValid()) {
            emit pluginSelected(index.toULongLong());
            close();
        }
    });

    viewStack_->addWidget(treeWidget_);
}

void PluginPickerPopup::filterPlugins(const QString& rawQuery)
{
    const QString query = rawQuery.trimmed().toLower();
    int visibleCount = 0;

    // Filter Categories View
    for (auto& group : groups_) {
        int visibleInGroup = 0;
        for (auto* entry : group.entries) {
            const bool matches = query.isEmpty() || entry->name.toLower().contains(query)
                || entry->vendor.toLower().contains(query) || entry->tags.toLower().contains(query);
            for (QWidget* btn : entry->buttonWidgets)
                btn->setVisible(matches);
            if (matches)
                visibleInGroup++;
        }
        if (group.groupWidget != nullptr)
            group.groupWidget->setVisible(visibleInGroup > 0);
    }

    // Filter Simple View (and count unique matching plugins)
    for (auto& entry : allEntries_) {
        const bool matches = query.isEmpty() || entry.name.toLower().contains(query)
            || entry.vendor.toLower().contains(query) || entry.tags.toLower().contains(query);
        if (entry.simpleWidget != nullptr)
            entry.simpleWidget->setVisible(matches);
        if (matches)
            visibleCount++;
    }

    // Filter Tree View
    if (treeWidget_ != nullptr) {
        for (int i = 0; i < treeWidget_->topLevelItemCount(); ++i) {
            auto* catItem = treeWidget_->topLevelItem(i);
            int childMatches = 0;
            for (int j = 0; j < catItem->childCount(); ++j) {
                auto* child = catItem->child(j);
                const bool matches = query.isEmpty() || child->text(0).toLower().contains(query)
                    || child->toolTip(0).toLower().contains(query);
                child->setHidden(!matches);
                if (matches)
                    childMatches++;
            }
            catItem->setHidden(childMatches == 0);
            if (!query.isEmpty() && childMatches > 0)
                catItem->setExpanded(true);
        }
    }

    if (countLabel_ != nullptr) {
        if (query.isEmpty())
            countLabel_->setText(tr("%1 plugins").arg(allEntries_.size()));
        else
            countLabel_->setText(tr("%1 of %2 plugins").arg(visibleCount).arg(allEntries_.size()));
    }
}

void PluginPickerPopup::selectFirstVisible()
{
    const QString query = searchEdit_->text().trimmed().toLower();
    for (const auto& entry : allEntries_) {
        if (query.isEmpty() || entry.name.toLower().contains(query) || entry.vendor.toLower().contains(query)
            || entry.tags.toLower().contains(query)) {
            emit pluginSelected(entry.index);
            close();
            return;
        }
    }
}

void PluginPickerPopup::showAt(const QPoint& globalPos)
{
    resize(kPopupWidth, kPopupHeight);

    QScreen* screen = QGuiApplication::screenAt(globalPos);
    if (screen == nullptr)
        screen = QGuiApplication::primaryScreen();

    const QRect screenGeom = screen != nullptr ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);

    int x = globalPos.x();
    int y = globalPos.y();

    // Clamp horizontally to screen
    if (x + kPopupWidth > screenGeom.right() - 10)
        x = screenGeom.right() - kPopupWidth - 10;
    if (x < screenGeom.left() + 10)
        x = screenGeom.left() + 10;

    // Clamp vertically to screen
    if (y + kPopupHeight > screenGeom.bottom() - 10)
        y = screenGeom.bottom() - kPopupHeight - 10;
    if (y < screenGeom.top() + 10)
        y = screenGeom.top() + 10;

    move(x, y);
    show();
    raise();
    activateWindow();
    searchEdit_->setFocus();
}

void PluginPickerPopup::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace vloomdaw
