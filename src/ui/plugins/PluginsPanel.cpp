// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/PluginsPanel.hpp"

#include "core/PluginMetadata.hpp"
#include "ui/Theme.hpp"
#include "ui/plugins/PluginStore.hpp"

#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <map>

namespace vloomdaw {

namespace {

constexpr QLatin1StringView kGroupByKey{"plugins/groupBy"};
constexpr int kIndexRole = Qt::UserRole + 1; ///< index into plugins_, on plugin rows only

QString utf8(const std::string& text) { return QString::fromStdString(text); }

/// Category groups in the browser's order, vendors A-Z with "Unknown" last.
int groupRank(PluginsPanel::GroupBy by, const QString& group)
{
    if (by == PluginsPanel::GroupBy::Category) {
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

    auto* title = new QLabel(tr("PLUGINS"), this);
    title->setObjectName(QStringLiteral("panelTitle"));

    auto* manage = new QToolButton(this);
    manage->setIcon(Theme::icon(QStringLiteral("sliders")));
    manage->setIconSize(QSize(14, 14));
    manage->setToolTip(tr("Plugin Manager... (scan, folders)"));
    manage->setFocusPolicy(Qt::NoFocus);
    connect(manage, &QToolButton::clicked, this, &PluginsPanel::managerRequested);

    groupBy_ = new QComboBox(this);
    groupBy_->setToolTip(tr("Group plugins by"));
    groupBy_->addItem(tr("By category"), static_cast<int>(GroupBy::Category));
    groupBy_->addItem(tr("By vendor"), static_cast<int>(GroupBy::Vendor));
    groupBy_->addItem(tr("By format"), static_cast<int>(GroupBy::Format));
    groupBy_->setFocusPolicy(Qt::ClickFocus);
    const int saved = groupBy_->findData(QSettings().value(kGroupByKey, static_cast<int>(GroupBy::Category)));
    groupBy_->setCurrentIndex(saved >= 0 ? saved : 0);
    connect(groupBy_, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue(kGroupByKey, groupBy_->currentData());
        rebuild();
    });

    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("browserSearch"));
    search_->setPlaceholderText(tr("Search name or vendor (Esc to leave)"));
    search_->setClearButtonEnabled(true);
    search_->setFocusPolicy(Qt::ClickFocus);
    connect(search_, &QLineEdit::textChanged, this, &PluginsPanel::applySearch);
    auto* leaveSearch
        = new QShortcut(QKeySequence(Qt::Key_Escape), search_, nullptr, nullptr, Qt::WidgetShortcut);
    connect(leaveSearch, &QShortcut::activated, search_, [this] { search_->clearFocus(); });

    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("browserTree"));
    tree_->setHeaderHidden(true);
    tree_->setColumnCount(2); // name, format tag
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->setFrameShape(QFrame::NoFrame);
    tree_->setIndentation(14);
    tree_->setUniformRowHeights(true);
    tree_->setFocusPolicy(Qt::ClickFocus);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QTreeWidget::customContextMenuRequested, this, &PluginsPanel::showContextMenu);

    emptyHint_ = new QLabel(
        tr("No plugins yet. Plugins can be used after a scan of your plugin folders; nothing is scanned "
           "automatically."),
        this);
    emptyHint_->setObjectName(QStringLiteral("panelHint"));
    emptyHint_->setWordWrap(true);

    scanButton_ = new QPushButton(tr("Open Plugin Manager..."), this);
    scanButton_->setFocusPolicy(Qt::NoFocus);
    connect(scanButton_, &QPushButton::clicked, this, &PluginsPanel::managerRequested);

    footer_ = new QLabel(this);
    footer_->setObjectName(QStringLiteral("panelHint"));
    footer_->setWordWrap(true);

    auto* titleRow = new QHBoxLayout;
    titleRow->addWidget(title, 1);
    titleRow->addWidget(manage);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 6, 6);
    layout->setSpacing(8);
    layout->addLayout(titleRow);
    layout->addWidget(groupBy_);
    layout->addWidget(search_);
    layout->addWidget(tree_, 1);
    layout->addWidget(emptyHint_);
    layout->addWidget(scanButton_);
    layout->addStretch(0);
    layout->addWidget(footer_);
}

PluginsPanel::GroupBy PluginsPanel::groupBy() const
{
    return static_cast<GroupBy>(groupBy_->currentData().toInt());
}

void PluginsPanel::setStore(const PluginStore& store)
{
    plugins_ = store.plugins;
    const bool scanned = store.lastScan.isValid();
    const bool any = !plugins_.empty();
    tree_->setVisible(any);
    search_->setVisible(any);
    groupBy_->setVisible(any);
    emptyHint_->setVisible(!any);
    emptyHint_->setText(scanned
            ? tr("The last scan found no plugins. Add the folders your plugins are in, "
                 "then scan again.")
            : tr("No plugins yet. Plugins can be used after a scan of your plugin folders; "
                 "nothing is scanned automatically."));
    scanButton_->setVisible(!any);
    footer_->setText(scanned
            ? tr("Last scan: %1").arg(QLocale().toString(store.lastScan.toLocalTime(), QLocale::ShortFormat))
            : QString());
    footer_->setVisible(scanned);
    rebuild();
}

void PluginsPanel::rebuild()
{
    tree_->clear();
    const GroupBy by = groupBy();
    const QColor dim = Theme::colors().textDim;

    // group name -> plugins, then ordered for display.
    std::map<QString, std::vector<const PluginInfo*>> groups;
    for (const PluginInfo& info : plugins_) {
        QString key;
        switch (by) {
        case GroupBy::Category:
            key = utf8(effectiveCategory(info));
            break;
        case GroupBy::Vendor:
            key = utf8(info.vendor);
            break;
        case GroupBy::Format:
            key = QString::fromUtf8(formatName(info.format));
            break;
        }
        groups[key].push_back(&info);
    }
    std::vector<std::pair<QString, std::vector<const PluginInfo*>>> ordered(groups.begin(), groups.end());
    std::stable_sort(ordered.begin(), ordered.end(), [by](const auto& a, const auto& b) {
        const int ra = groupRank(by, a.first);
        const int rb = groupRank(by, b.first);
        if (ra != rb)
            return ra < rb;
        return a.first.compare(b.first, Qt::CaseInsensitive) < 0;
    });

    // One row per plugin: name, a format tag, details in the tooltip.
    const auto addPlugin = [&](QTreeWidgetItem* parent, const PluginInfo* info) {
        auto* item = new QTreeWidgetItem(parent);
        item->setText(0, utf8(info->name));
        item->setText(1, QString::fromUtf8(formatName(info->format)));
        item->setForeground(1, dim);
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setData(0, Qt::UserRole, utf8(info->vendor)); // searched too
        item->setData(0, kIndexRole, static_cast<qulonglong>(info - plugins_.data()));

        QStringList tip;
        if (!info->vendor.empty())
            tip << tr("Vendor: %1").arg(utf8(info->vendor));
        if (!info->version.empty())
            tip << tr("Version: %1").arg(utf8(info->version));
        const std::string category = effectiveCategory(*info);
        const std::string type = effectiveInstrumentType(*info);
        QString categoryText
            = type.empty() ? utf8(category) : QStringLiteral("%1 / %2").arg(utf8(category), utf8(type));
        if (!info->userCategory.empty())
            categoryText += QLatin1Char(' ') + tr("(set by you)");
        tip << tr("Category: %1").arg(categoryText);
        if (info->probe == PluginInfo::Probe::Failed) {
            tip << tr("Could not be read: %1").arg(utf8(info->probeError));
            item->setForeground(0, dim);
        } else if (info->probe == PluginInfo::Probe::NotProbed) {
            tip << tr("Not read yet: vendor and category come from a scan with plugin reading on");
        }
        tip << QDir::toNativeSeparators(QString::fromStdWString(info->path.wstring()));
        item->setToolTip(0, tip.join(QLatin1Char('\n')));
    };
    const auto addGroup = [](auto* parent, const QString& label, std::size_t count) {
        auto* group = new QTreeWidgetItem(parent);
        group->setFlags(Qt::ItemIsEnabled);
        group->setText(0, QStringLiteral("%1  (%2)").arg(label).arg(count));
        group->setFirstColumnSpanned(true);
        group->setExpanded(true);
        return group;
    };
    const auto byName = [](const PluginInfo* a, const PluginInfo* b) {
        return QString::fromStdString(a->name).compare(QString::fromStdString(b->name), Qt::CaseInsensitive)
            < 0;
    };

    for (auto& [name, members] : ordered) {
        std::sort(members.begin(), members.end(), byName);
        QTreeWidgetItem* group
            = addGroup(tree_, name.isEmpty() ? tr("Unknown vendor") : name, members.size());

        // Instruments get one more level: Synth, Sampler, Drums...
        if (by == GroupBy::Category && name == QLatin1String("Instrument")) {
            for (const std::string& type : instrumentTypes()) {
                std::vector<const PluginInfo*> ofType;
                for (const PluginInfo* info : members)
                    if (effectiveInstrumentType(*info) == type)
                        ofType.push_back(info);
                if (ofType.empty())
                    continue;
                QTreeWidgetItem* sub = addGroup(group, utf8(type), ofType.size());
                for (const PluginInfo* info : ofType)
                    addPlugin(sub, info);
            }
            continue;
        }
        for (const PluginInfo* info : members)
            addPlugin(group, info);
    }
    applySearch(search_->text());
    tree_->scrollToTop();
}

void PluginsPanel::showContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = tree_->itemAt(pos);
    if (item == nullptr || !item->data(0, kIndexRole).isValid())
        return; // a group row
    const auto index = static_cast<std::size_t>(item->data(0, kIndexRole).toULongLong());
    if (index >= plugins_.size())
        return;
    const PluginInfo& plugin = plugins_[index];
    const QString current = utf8(effectiveCategory(plugin));
    const QString currentType = utf8(effectiveInstrumentType(plugin));

    QMenu menu(this);
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
    menu.exec(tree_->viewport()->mapToGlobal(pos));
}

void PluginsPanel::applySearch(const QString& text)
{
    const QString needle = text.trimmed();
    // Plugins match on name or vendor; a group shows while any plugin in it does.
    const auto filter = [&](auto& self, QTreeWidgetItem* item) -> bool {
        if (item->childCount() == 0) {
            const bool match = needle.isEmpty() || item->text(0).contains(needle, Qt::CaseInsensitive)
                || item->data(0, Qt::UserRole).toString().contains(needle, Qt::CaseInsensitive);
            item->setHidden(!match);
            return match;
        }
        bool any = false;
        for (int i = 0; i < item->childCount(); ++i)
            any = self(self, item->child(i)) || any;
        item->setHidden(!any);
        return any;
    };
    for (int g = 0; g < tree_->topLevelItemCount(); ++g)
        filter(filter, tree_->topLevelItem(g));
}

} // namespace vloomdaw
