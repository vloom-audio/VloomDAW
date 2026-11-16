// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/PluginsPanel.hpp"

#include "core/PluginMetadata.hpp"
#include "ui/FilePaths.hpp"
#include "ui/Theme.hpp"
#include "ui/plugins/PluginDrag.hpp"
#include "ui/plugins/PluginStore.hpp"
#include "ui/plugins/PluginThumbnails.hpp"

#include <QButtonGroup>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHelpEvent>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <map>

namespace vloomdaw {

namespace {

constexpr QLatin1StringView kGroupByKey{"plugins/groupBy"};
constexpr QLatin1StringView kTabKey{"plugins/tab"};
constexpr QLatin1StringView kShowThumbsKey{"plugins/showThumbnails"};
constexpr int kIndexRole = Qt::UserRole + 1; ///< index into plugins_, on plugin & thumbnail rows
constexpr int kFormatColWidth = 54; ///< fixed width for "VST3" / "CLAP" / "Built-in" tag column

QString utf8(const std::string& text) { return QString::fromStdString(text); }

/// The plugin list: plugin rows (and their inline thumbnails) drag out as
/// plugins (PluginDrag), to drop on a mixer insert or the channel rack.
/// Tooltips are built lazily on hover so populating 300+ rows stays O(N) fast.
class PluginTree : public QTreeWidget {
public:
    explicit PluginTree(const PluginsPanel* panel, QWidget* parent = nullptr)
        : QTreeWidget(parent)
        , panel_(panel)
    {
    }

protected:
    [[nodiscard]] QStringList mimeTypes() const override
    {
        return {QStringLiteral("application/x-vloomdaw-plugin")};
    }

    [[nodiscard]] QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override
    {
        if (items.empty() || !items.front()->data(0, kIndexRole).isValid())
            return nullptr; // a group row
        return pluginMimeData(static_cast<std::size_t>(items.front()->data(0, kIndexRole).toULongLong()));
    }

    bool viewportEvent(QEvent* event) override
    {
        if (event->type() == QEvent::ToolTip && panel_ != nullptr) {
            auto* help = static_cast<QHelpEvent*>(event);
            if (QTreeWidgetItem* item = itemAt(help->pos());
                item != nullptr && item->data(0, kIndexRole).isValid()) {
                const auto idx = static_cast<std::size_t>(item->data(0, kIndexRole).toULongLong());
                const QString tip = panel_->tooltipForPlugin(idx);
                if (!tip.isEmpty()) {
                    QToolTip::showText(help->globalPos(), tip, viewport(), visualItemRect(item));
                    return true;
                }
            }
            QToolTip::hideText();
            return true;
        }
        return QTreeWidget::viewportEvent(event);
    }

private:
    const PluginsPanel* panel_ = nullptr;
};

/// Category groups in the browser's order, vendors A-Z with "Unknown" last.
int groupRank(PluginsPanel::GroupBy by, PluginsPanel::Tab tab, const QString& group)
{
    if (by == PluginsPanel::GroupBy::Category) {
        if (tab == PluginsPanel::Tab::Instruments) {
            const auto& subs = pluginSubCategories();
            const auto it = std::find(subs.begin(), subs.end(), group.toStdString());
            return static_cast<int>(it - subs.begin());
        }
        const auto& order = pluginCategories();
        const auto it = std::find(order.begin(), order.end(), group.toStdString());
        return static_cast<int>(it - order.begin());
    }
    if (by == PluginsPanel::GroupBy::Vendor)
        return group.isEmpty() ? 1 : 0;
    return 0;
}

} // namespace

PluginsPanel::PluginsPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("pluginsPanel"));
    setAttribute(Qt::WA_StyledBackground);

    QSettings settings;
    const int savedTab = settings.value(kTabKey, static_cast<int>(Tab::Instruments)).toInt();
    tab_ = savedTab == static_cast<int>(Tab::Effects) ? Tab::Effects : Tab::Instruments;
    showThumbnails_ = settings.value(kShowThumbsKey, true).toBool();

    auto* title = new QLabel(tr("PLUGINS"), this);
    title->setObjectName(QStringLiteral("panelTitle"));

    auto* manage = new QToolButton(this);
    manage->setIcon(Theme::icon(QStringLiteral("sliders")));
    manage->setIconSize(QSize(14, 14));
    manage->setToolTip(tr("Plugin Manager... (scan, folders)"));
    manage->setFocusPolicy(Qt::NoFocus);
    connect(manage, &QToolButton::clicked, this, &PluginsPanel::managerRequested);

    // Segmented Studio One-style top tab selector: Instruments | FX
    tabBarWidget_ = new QWidget(this);
    tabBarWidget_->setObjectName(QStringLiteral("pluginTabStrip"));
    auto* tabLayout = new QHBoxLayout(tabBarWidget_);
    tabLayout->setContentsMargins(2, 2, 2, 2);
    tabLayout->setSpacing(3);

    instTabBtn_ = new QPushButton(tr("Instruments"), tabBarWidget_);
    instTabBtn_->setObjectName(QStringLiteral("pluginTabButton"));
    instTabBtn_->setCheckable(true);
    instTabBtn_->setChecked(tab_ == Tab::Instruments);
    instTabBtn_->setFocusPolicy(Qt::NoFocus);
    instTabBtn_->setFixedHeight(26);

    fxTabBtn_ = new QPushButton(tr("FX"), tabBarWidget_);
    fxTabBtn_->setObjectName(QStringLiteral("pluginTabButton"));
    fxTabBtn_->setCheckable(true);
    fxTabBtn_->setChecked(tab_ == Tab::Effects);
    fxTabBtn_->setFocusPolicy(Qt::NoFocus);
    fxTabBtn_->setFixedHeight(26);

    auto* tabGroup = new QButtonGroup(this);
    tabGroup->setExclusive(true);
    tabGroup->addButton(instTabBtn_, static_cast<int>(Tab::Instruments));
    tabGroup->addButton(fxTabBtn_, static_cast<int>(Tab::Effects));
    connect(
        tabGroup, &QButtonGroup::idClicked, this, [this](int id) { setCurrentTab(static_cast<Tab>(id)); });
    tabLayout->addWidget(instTabBtn_, 1);
    tabLayout->addWidget(fxTabBtn_, 1);

    // Sort / Group bar + Show Thumbnails toggle button
    controlsRow_ = new QWidget(this);
    auto* ctrlLayout = new QHBoxLayout(controlsRow_);
    ctrlLayout->setContentsMargins(0, 0, 0, 0);
    ctrlLayout->setSpacing(6);

    groupBy_ = new QComboBox(controlsRow_);
    groupBy_->setToolTip(tr("Sort / Group plugins by"));
    groupBy_->addItem(tr("By type / category"), static_cast<int>(GroupBy::Category));
    groupBy_->addItem(tr("By vendor"), static_cast<int>(GroupBy::Vendor));
    groupBy_->addItem(tr("By format"), static_cast<int>(GroupBy::Format));
    groupBy_->addItem(tr("Flat (A-Z)"), static_cast<int>(GroupBy::Flat));
    groupBy_->setFocusPolicy(Qt::ClickFocus);
    const int savedGroup
        = groupBy_->findData(settings.value(kGroupByKey, static_cast<int>(GroupBy::Category)));
    groupBy_->setCurrentIndex(savedGroup >= 0 ? savedGroup : 0);
    connect(groupBy_, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue(kGroupByKey, groupBy_->currentData());
        markAllDirty();
        ensureCurrentTabBuilt();
    });

    thumbToggleBtn_ = new QToolButton(controlsRow_);
    thumbToggleBtn_->setObjectName(QStringLiteral("pluginThumbToggle"));
    thumbToggleBtn_->setCheckable(true);
    thumbToggleBtn_->setChecked(showThumbnails_);
    thumbToggleBtn_->setIcon(Theme::icon(QStringLiteral("thumbnail")));
    thumbToggleBtn_->setIconSize(QSize(15, 15));
    thumbToggleBtn_->setFixedSize(28, 28);
    thumbToggleBtn_->setFocusPolicy(Qt::NoFocus);
    thumbToggleBtn_->setToolTip(tr("Show / Hide inline plug-in thumbnails"));
    connect(thumbToggleBtn_, &QToolButton::toggled, this, [this](bool on) {
        showThumbnails_ = on;
        QSettings().setValue(kShowThumbsKey, on);
        markAllDirty();
        ensureCurrentTabBuilt();
    });

    ctrlLayout->addWidget(groupBy_, 1);
    ctrlLayout->addWidget(thumbToggleBtn_);

    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("browserSearch"));
    search_->setPlaceholderText(tr("Search name or vendor (Esc to leave)"));
    search_->setClearButtonEnabled(true);
    search_->setFocusPolicy(Qt::ClickFocus);
    connect(search_, &QLineEdit::textChanged, this, &PluginsPanel::applySearch);
    auto* leaveSearch
        = new QShortcut(QKeySequence(Qt::Key_Escape), search_, nullptr, nullptr, Qt::WidgetShortcut);
    connect(leaveSearch, &QShortcut::activated, search_, [this] { search_->clearFocus(); });

    treeStack_ = new QStackedWidget(this);
    instTree_ = makePluginTree();
    fxTree_ = makePluginTree();
    treeStack_->addWidget(instTree_);
    treeStack_->addWidget(fxTree_);
    treeStack_->setCurrentIndex(static_cast<int>(tab_));

    emptyHint_ = new QLabel(
        tr("No plugins yet. Plugins can be used after a scan of your plugin folders; nothing is scanned "
           "automatically."),
        this);
    emptyHint_->setObjectName(QStringLiteral("panelHint"));
    emptyHint_->setWordWrap(true);

    scanButton_ = new QPushButton(tr("Open Plugin Manager..."), this);
    scanButton_->setFocusPolicy(Qt::NoFocus);
    connect(scanButton_, &QPushButton::clicked, this, &PluginsPanel::managerRequested);

    // Bottom Studio One-style Plugin Inspector Card
    inspectorCard_ = new QWidget(this);
    inspectorCard_->setObjectName(QStringLiteral("pluginInspectorCard"));
    auto* inspOuter = new QVBoxLayout(inspectorCard_);
    inspOuter->setContentsMargins(8, 8, 8, 8);
    inspOuter->setSpacing(6);

    inspectorThumb_ = new QLabel(inspectorCard_);
    inspectorThumb_->setObjectName(QStringLiteral("pluginInspectorThumb"));
    inspectorThumb_->setAlignment(Qt::AlignCenter);
    inspectorThumb_->setVisible(false);
    inspOuter->addWidget(inspectorThumb_);

    auto* inspHeader = new QHBoxLayout;
    inspHeader->setContentsMargins(0, 0, 0, 0);
    inspHeader->setSpacing(8);

    inspectorBadge_ = new QLabel(inspectorCard_);
    inspectorBadge_->setObjectName(QStringLiteral("pluginInspectorBadge"));
    inspectorBadge_->setAlignment(Qt::AlignCenter);
    inspectorBadge_->setFixedSize(40, 36);
    inspHeader->addWidget(inspectorBadge_, 0, Qt::AlignTop);

    auto* inspMeta = new QVBoxLayout;
    inspMeta->setContentsMargins(0, 0, 0, 0);
    inspMeta->setSpacing(2);

    inspectorName_ = new QLabel(inspectorCard_);
    inspectorName_->setObjectName(QStringLiteral("pluginInspectorName"));
    inspectorName_->setWordWrap(true);
    inspMeta->addWidget(inspectorName_);

    inspectorVendor_ = new QLabel(inspectorCard_);
    inspectorVendor_->setObjectName(QStringLiteral("panelHint"));
    inspMeta->addWidget(inspectorVendor_);

    inspectorCategory_ = new QLabel(inspectorCard_);
    inspectorCategory_->setObjectName(QStringLiteral("panelHint"));
    inspectorCategory_->setWordWrap(true);
    inspMeta->addWidget(inspectorCategory_);

    inspHeader->addLayout(inspMeta, 1);
    inspOuter->addLayout(inspHeader);
    inspectorCard_->setVisible(false);

    footer_ = new QLabel(this);
    footer_->setObjectName(QStringLiteral("panelHint"));
    footer_->setWordWrap(true);

    auto* titleRow = new QHBoxLayout;
    titleRow->addWidget(title, 1);
    titleRow->addWidget(manage);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 6, 6);
    layout->setSpacing(6);
    layout->addLayout(titleRow);
    layout->addWidget(tabBarWidget_);
    layout->addWidget(controlsRow_);
    layout->addWidget(search_);
    layout->addWidget(treeStack_, 1);
    layout->addWidget(emptyHint_);
    layout->addWidget(scanButton_);
    layout->addStretch(0);
    layout->addWidget(inspectorCard_);
    layout->addWidget(footer_);
}

QTreeWidget* PluginsPanel::makePluginTree()
{
    auto* tree = new PluginTree(this, treeStack_);
    tree->setDragEnabled(true);
    tree->setDragDropMode(QAbstractItemView::DragOnly);
    tree->setObjectName(QStringLiteral("browserTree"));
    tree->setHeaderHidden(true);
    tree->setColumnCount(2); // name, format tag
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    // Fixed column width avoids O(N^2) ResizeToContents font-metric sweeps on 300+ FX rows.
    tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    tree->setColumnWidth(1, kFormatColWidth);
    tree->setIconSize(QSize(168, 72));
    tree->setFrameShape(QFrame::NoFrame);
    tree->setIndentation(14);
    tree->setUniformRowHeights(false);
    tree->setFocusPolicy(Qt::ClickFocus);
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree, &QTreeWidget::customContextMenuRequested, this,
        [this, tree](const QPoint& pos) { showContextMenu(tree, pos); });
    connect(tree, &QTreeWidget::currentItemChanged, this,
        [this, tree](QTreeWidgetItem* current, QTreeWidgetItem* /*previous*/) {
            if (tree != activeTree())
                return;
            if (current != nullptr && current->data(0, kIndexRole).isValid())
                updateInspector(static_cast<std::size_t>(current->data(0, kIndexRole).toULongLong()));
        });
    connect(tree, &QTreeWidget::itemDoubleClicked, this,
        [this](QTreeWidgetItem* item, int /*column*/) { onItemActivated(item); });
    return tree;
}

QTreeWidget* PluginsPanel::activeTree() const { return tab_ == Tab::Instruments ? instTree_ : fxTree_; }

void PluginsPanel::markAllDirty()
{
    instDirty_ = true;
    fxDirty_ = true;
}

void PluginsPanel::setCurrentTab(Tab tab)
{
    if (tab_ == tab)
        return;
    tab_ = tab;
    {
        const QSignalBlocker b1(instTabBtn_);
        const QSignalBlocker b2(fxTabBtn_);
        instTabBtn_->setChecked(tab_ == Tab::Instruments);
        fxTabBtn_->setChecked(tab_ == Tab::Effects);
    }
    QSettings().setValue(kTabKey, static_cast<int>(tab_));
    treeStack_->setCurrentIndex(static_cast<int>(tab_));
    ensureCurrentTabBuilt();
}

PluginsPanel::GroupBy PluginsPanel::groupBy() const
{
    return static_cast<GroupBy>(groupBy_->currentData().toInt());
}

QString PluginsPanel::tooltipForPlugin(std::size_t index) const
{
    if (index >= plugins_.size())
        return {};
    const PluginInfo& info = plugins_[index];
    QStringList tip;
    if (!info.vendor.empty())
        tip << tr("Vendor: %1").arg(utf8(info.vendor));
    if (!info.version.empty())
        tip << tr("Version: %1").arg(utf8(info.version));
    const std::string category = effectiveCategory(info);
    const std::string type = effectiveInstrumentType(info);
    QString categoryText
        = type.empty() ? utf8(category) : QStringLiteral("%1 / %2").arg(utf8(category), utf8(type));
    if (!info.userCategory.empty())
        categoryText += QLatin1Char(' ') + tr("(set by you)");
    tip << tr("Category: %1").arg(categoryText);
    tip << tr("Sub-Type: %1").arg(utf8(detectPluginSubCategory(info)));
    if (info.probe == PluginInfo::Probe::Failed)
        tip << tr("Could not be read: %1").arg(utf8(info.probeError));
    else if (info.probe == PluginInfo::Probe::NotProbed)
        tip << tr("Not read yet: vendor and category come from a scan with plugin reading on");
    if (info.format != PluginFormat::Builtin)
        tip << QDir::toNativeSeparators(toQString(info.path));
    return tip.join(QLatin1Char('\n'));
}

void PluginsPanel::updateTabCounts()
{
    std::size_t instCount = 0;
    std::size_t fxCount = 0;
    for (const PluginInfo& info : plugins_) {
        if (!info.enabled)
            continue;
        if (pluginMainGroup(info) == "Instrument")
            ++instCount;
        else
            ++fxCount;
    }
    instTabBtn_->setText(tr("Instruments (%1)").arg(instCount));
    fxTabBtn_->setText(tr("FX (%1)").arg(fxCount));
}

void PluginsPanel::setStore(const PluginStore& store)
{
    plugins_ = store.plugins;
    updateTabCounts();

    const bool scanned = store.lastScan.isValid();
    const bool anyEnabled
        = std::any_of(plugins_.begin(), plugins_.end(), [](const PluginInfo& info) { return info.enabled; });

    tabBarWidget_->setVisible(anyEnabled);
    controlsRow_->setVisible(anyEnabled);
    search_->setVisible(anyEnabled);
    scanButton_->setVisible(!anyEnabled);

    footer_->setText(scanned
            ? tr("Last scan: %1").arg(QLocale().toString(store.lastScan.toLocalTime(), QLocale::ShortFormat))
            : QString());
    footer_->setVisible(scanned);

    markAllDirty();
    if (!anyEnabled) {
        instTree_->clear();
        fxTree_->clear();
        treeStack_->setVisible(false);
        inspectorCard_->setVisible(false);
        emptyHint_->setVisible(true);
        if (!plugins_.empty()) {
            emptyHint_->setText(
                tr("All plugins are currently disabled. Enable plugins in the Plugin Manager."));
        } else {
            emptyHint_->setText(scanned
                    ? tr("The last scan found no plugins. Add the folders your plugins are in, "
                         "then scan again.")
                    : tr("No plugins yet. Plugins can be used after a scan of your plugin folders; "
                         "nothing is scanned automatically."));
        }
        return;
    }

    ensureCurrentTabBuilt();
    // Pre-build the inactive tab on the next event-loop turn so even the very
    // first click between Instruments and FX is an instant 0 ms page flip.
    QTimer::singleShot(0, this, [this] {
        const bool stillAny = std::any_of(
            plugins_.begin(), plugins_.end(), [](const PluginInfo& info) { return info.enabled; });
        if (!stillAny)
            return;
        if (instDirty_) {
            populateTree(instTree_, Tab::Instruments);
            instDirty_ = false;
            applySearchToTree(instTree_, search_->text());
        }
        if (fxDirty_) {
            populateTree(fxTree_, Tab::Effects);
            fxDirty_ = false;
            applySearchToTree(fxTree_, search_->text());
        }
    });
}

void PluginsPanel::refreshThumbnails()
{
    markAllDirty();
    ensureCurrentTabBuilt();
}

void PluginsPanel::ensureCurrentTabBuilt()
{
    const bool anyEnabled
        = std::any_of(plugins_.begin(), plugins_.end(), [](const PluginInfo& info) { return info.enabled; });
    if (!anyEnabled)
        return;

    if (tab_ == Tab::Instruments) {
        if (instDirty_) {
            populateTree(instTree_, Tab::Instruments);
            instDirty_ = false;
        }
        applySearchToTree(instTree_, search_->text());
    } else {
        if (fxDirty_) {
            populateTree(fxTree_, Tab::Effects);
            fxDirty_ = false;
        }
        applySearchToTree(fxTree_, search_->text());
    }
    updateTabState();
}

void PluginsPanel::updateTabState()
{
    const bool empty = (tab_ == Tab::Instruments) ? instEmpty_ : fxEmpty_;
    if (empty) {
        treeStack_->setVisible(false);
        inspectorCard_->setVisible(false);
        emptyHint_->setVisible(true);
        emptyHint_->setText(tab_ == Tab::Instruments ? tr("No instrument plugins enabled. Switch to FX or "
                                                          "scan instrument folders in the Plugin Manager.")
                                                     : tr("No effect plugins enabled. Switch to Instruments "
                                                          "or scan FX folders in the Plugin Manager."));
        return;
    }

    emptyHint_->setVisible(false);
    treeStack_->setVisible(true);

    QTreeWidget* tree = activeTree();
    if (QTreeWidgetItem* cur = tree->currentItem(); cur != nullptr && cur->data(0, kIndexRole).isValid()) {
        updateInspector(static_cast<std::size_t>(cur->data(0, kIndexRole).toULongLong()));
    } else {
        updateInspector(std::nullopt);
    }
}

void PluginsPanel::populateTree(QTreeWidget* tree, Tab tab)
{
    const GroupBy by = groupBy();
    const QColor dim = Theme::colors().textDim;
    const std::string wantedMainGroup = (tab == Tab::Instruments) ? "Instrument" : "Effect";

    std::vector<const PluginInfo*> tabPlugins;
    std::vector<const PluginInfo*> favorites;
    std::map<QString, std::vector<const PluginInfo*>> groups;

    for (const PluginInfo& info : plugins_) {
        if (!info.enabled)
            continue;
        if (pluginMainGroup(info) != wantedMainGroup)
            continue;
        tabPlugins.push_back(&info);
        if (info.favorite)
            favorites.push_back(&info);

        if (by == GroupBy::Flat)
            continue;

        QString key;
        switch (by) {
        case GroupBy::Category:
            key = (tab == Tab::Instruments) ? utf8(detectPluginSubCategory(info))
                                            : utf8(effectiveCategory(info));
            break;
        case GroupBy::Vendor:
            key = utf8(info.vendor);
            break;
        case GroupBy::Format:
            key = QString::fromUtf8(formatName(info.format));
            break;
        case GroupBy::Flat:
            break;
        }
        groups[key].push_back(&info);
    }

    bool& emptyFlag = (tab == Tab::Instruments) ? instEmpty_ : fxEmpty_;
    emptyFlag = tabPlugins.empty();
    if (emptyFlag) {
        tree->clear();
        return;
    }

    std::vector<std::pair<QString, std::vector<const PluginInfo*>>> ordered(groups.begin(), groups.end());
    std::stable_sort(ordered.begin(), ordered.end(), [by, tab](const auto& a, const auto& b) {
        const int ra = groupRank(by, tab, a.first);
        const int rb = groupRank(by, tab, b.first);
        if (ra != rb)
            return ra < rb;
        return a.first.compare(b.first, Qt::CaseInsensitive) < 0;
    });

    const qreal dpr = devicePixelRatioF();

    // Build all items completely detached from `tree` so Qt never recomputes
    // row layouts per insertion.
    const auto makePluginItem = [&](const PluginInfo* info) -> QTreeWidgetItem* {
        const auto pluginIdx = static_cast<std::size_t>(info - plugins_.data());
        auto* item = new QTreeWidgetItem();
        item->setText(0, utf8(info->name));
        item->setText(1, QString::fromUtf8(formatName(info->format)));
        item->setForeground(1, dim);
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setData(0, Qt::UserRole, utf8(info->vendor)); // searched too
        item->setData(0, kIndexRole, static_cast<qulonglong>(pluginIdx));
        if (info->probe == PluginInfo::Probe::Failed)
            item->setForeground(0, dim);

        if (showThumbnails_) {
            const std::string thumbId = pluginThumbnailId(*info);
            const QPixmap thumb = loadPluginThumbnail(thumbId, 168, 72, dpr);
            if (!thumb.isNull()) {
                auto* thumbItem = new QTreeWidgetItem(item);
                thumbItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
                thumbItem->setData(0, Qt::UserRole, utf8(info->vendor));
                thumbItem->setData(0, kIndexRole, static_cast<qulonglong>(pluginIdx));
                thumbItem->setFirstColumnSpanned(true);
                thumbItem->setData(0, Qt::DecorationRole, QIcon(thumb));
                const QSize logicalThumbSize = thumb.deviceIndependentSize().toSize();
                thumbItem->setSizeHint(0, QSize(logicalThumbSize.width() + 8, logicalThumbSize.height() + 8));
            }
        }
        return item;
    };

    const auto makeGroupItem = [](const QString& label, std::size_t count) -> QTreeWidgetItem* {
        auto* group = new QTreeWidgetItem();
        group->setFlags(Qt::ItemIsEnabled);
        group->setText(0, QStringLiteral("%1  (%2)").arg(label).arg(count));
        return group;
    };

    const auto byName = [](const PluginInfo* a, const PluginInfo* b) {
        return QString::fromStdString(a->name).compare(QString::fromStdString(b->name), Qt::CaseInsensitive)
            < 0;
    };

    QList<QTreeWidgetItem*> topItems;
    if (!favorites.empty()) {
        std::sort(favorites.begin(), favorites.end(), byName);
        QTreeWidgetItem* favGroup = makeGroupItem(tr("Favorites"), favorites.size());
        for (const PluginInfo* info : favorites)
            favGroup->addChild(makePluginItem(info));
        topItems.append(favGroup);
    }

    if (by == GroupBy::Flat) {
        std::sort(tabPlugins.begin(), tabPlugins.end(), byName);
        topItems.reserve(topItems.size() + static_cast<qsizetype>(tabPlugins.size()));
        for (const PluginInfo* info : tabPlugins)
            topItems.append(makePluginItem(info));
    } else {
        topItems.reserve(topItems.size() + static_cast<qsizetype>(ordered.size()));
        for (auto& [name, members] : ordered) {
            std::sort(members.begin(), members.end(), byName);
            QTreeWidgetItem* group
                = makeGroupItem(name.isEmpty() ? tr("Unknown vendor") : name, members.size());
            for (const PluginInfo* info : members)
                group->addChild(makePluginItem(info));
            topItems.append(group);
        }
    }

    tree->setUpdatesEnabled(false);
    {
        const QSignalBlocker block(tree);
        tree->clear();
        tree->addTopLevelItems(topItems);
        for (QTreeWidgetItem* top : topItems) {
            if (!top->data(0, kIndexRole).isValid()) {
                top->setFirstColumnSpanned(true);
                top->setExpanded(true);
                for (int i = 0; i < top->childCount(); ++i) {
                    if (top->child(i)->childCount() > 0)
                        top->child(i)->setExpanded(true);
                }
            } else if (top->childCount() > 0) {
                top->setExpanded(true);
            }
        }
    }
    tree->setUpdatesEnabled(true);
}

void PluginsPanel::updateInspector(std::optional<std::size_t> pluginIndex)
{
    selectedPluginIndex_ = pluginIndex;
    if (!pluginIndex.has_value() || *pluginIndex >= plugins_.size()) {
        inspectorCard_->setVisible(false);
        return;
    }

    const PluginInfo& plugin = plugins_[*pluginIndex];
    const bool isInst = pluginMainGroup(plugin) == "Instrument";
    const QString fmt = QString::fromUtf8(formatName(plugin.format));
    inspectorBadge_->setText(QStringLiteral("%1\n%2").arg(isInst ? tr("INST") : tr("FX"), fmt));

    inspectorName_->setText(utf8(plugin.name));
    inspectorVendor_->setText(
        tr("Vendor: %1").arg(plugin.vendor.empty() ? tr("Unknown") : utf8(plugin.vendor)));
    inspectorCategory_->setText(tr("Category: %1 · %2").arg(fmt, utf8(detectPluginSubCategory(plugin))));

    const QPixmap preview = loadPluginThumbnail(pluginThumbnailId(plugin), 200, 108, devicePixelRatioF());
    if (!preview.isNull()) {
        inspectorThumb_->setPixmap(preview);
        inspectorThumb_->setVisible(true);
    } else {
        inspectorThumb_->clear();
        inspectorThumb_->setVisible(false);
    }

    inspectorCard_->setVisible(true);
}

void PluginsPanel::onItemActivated(QTreeWidgetItem* item)
{
    if (item == nullptr || !item->data(0, kIndexRole).isValid())
        return;
    const auto index = static_cast<std::size_t>(item->data(0, kIndexRole).toULongLong());
    if (index >= plugins_.size())
        return;
    if (pluginMainGroup(plugins_[index]) == "Instrument")
        emit addToRackRequested(index);
    else
        emit addToMixerRequested(index);
}

void PluginsPanel::showContextMenu(QTreeWidget* tree, const QPoint& pos)
{
    if (tree == nullptr)
        return;
    QTreeWidgetItem* item = tree->itemAt(pos);
    if (item == nullptr || !item->data(0, kIndexRole).isValid())
        return; // a group row
    const auto index = static_cast<std::size_t>(item->data(0, kIndexRole).toULongLong());
    if (index >= plugins_.size())
        return;
    const PluginInfo& plugin = plugins_[index];
    const QString current = utf8(effectiveCategory(plugin));
    const QString currentType = utf8(effectiveInstrumentType(plugin));

    QMenu menu(this);
    menu.addAction(tr("Add to Channel Rack"), this, [this, index] { emit addToRackRequested(index); });
    menu.addAction(tr("Add to Mixer…"), this, [this, index] { emit addToMixerRequested(index); });
    if (plugin.format == PluginFormat::Builtin) {
        menu.exec(tree->viewport()->mapToGlobal(pos));
        return;
    }
    menu.addSeparator();
    menu.addAction(plugin.favorite ? tr("Remove Favorite") : tr("Mark as Favorite"), this,
        [this, index, fav = !plugin.favorite] { emit favoriteToggled(index, fav); });

    const std::string thumbId = pluginThumbnailId(plugin);
    if (hasPluginThumbnail(thumbId)) {
        menu.addAction(
            Theme::icon(QStringLiteral("thumbnail")), tr("Remove Plug-in Thumbnail"), this, [this, thumbId] {
                if (removePluginThumbnail(thumbId))
                    refreshThumbnails();
            });
    }

    QMenu* setMenu = menu.addMenu(tr("Set category"));
    for (const std::string& category : pluginCategories()) {
        if (category == "Uncategorized")
            continue;
        const QString name = utf8(category);
        if (category == "Instrument") {
            QMenu* types = setMenu->addMenu(name);
            for (const std::string& type : instrumentTypes()) {
                QAction* action = types->addAction(utf8(type), this, [this, index, type] {
                    emit userCategoryChosen(index, QStringLiteral("Instrument"), utf8(type));
                });
                action->setCheckable(true);
                action->setChecked(current == QLatin1String("Instrument") && currentType == utf8(type));
            }
            continue;
        }
        QAction* action = setMenu->addAction(
            name, this, [this, index, name] { emit userCategoryChosen(index, name, QString()); });
        action->setCheckable(true);
        action->setChecked(current == name);
    }
    QAction* reset = menu.addAction(tr("Reset to plugin's category (%1)").arg(utf8(plugin.category)), this,
        [this, index] { emit userCategoryChosen(index, QString(), QString()); });
    reset->setEnabled(!plugin.userCategory.empty());
    menu.exec(tree->viewport()->mapToGlobal(pos));
}

void PluginsPanel::applySearchToTree(QTreeWidget* tree, const QString& text)
{
    if (tree == nullptr)
        return;
    const QString needle = text.trimmed();
    tree->setUpdatesEnabled(false);
    // Plugins match on name or vendor; inline thumbnail child rows follow their
    // parent plugin item; a group shows while any plugin in it matches.
    const auto filter = [&](auto& self, QTreeWidgetItem* item) -> bool {
        if (item->data(0, kIndexRole).isValid()) {
            const bool match = needle.isEmpty() || item->text(0).contains(needle, Qt::CaseInsensitive)
                || item->data(0, Qt::UserRole).toString().contains(needle, Qt::CaseInsensitive);
            item->setHidden(!match);
            for (int c = 0; c < item->childCount(); ++c)
                item->child(c)->setHidden(!match);
            return match;
        }
        if (item->childCount() == 0) {
            item->setHidden(true);
            return false;
        }
        bool any = false;
        for (int i = 0; i < item->childCount(); ++i)
            any = self(self, item->child(i)) || any;
        item->setHidden(!any);
        return any;
    };
    for (int g = 0; g < tree->topLevelItemCount(); ++g)
        filter(filter, tree->topLevelItem(g));
    tree->setUpdatesEnabled(true);
}

void PluginsPanel::applySearch(const QString& text) { applySearchToTree(activeTree(), text); }

} // namespace vloomdaw
