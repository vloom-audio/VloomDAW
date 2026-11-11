// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/PluginsPanel.hpp"

#include "ui/Theme.hpp"
#include "ui/plugins/PluginStore.hpp"

#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QShortcut>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace vloomdaw {

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
    manage->setToolTip(tr("Plugin Manager… (scan, folders)"));
    manage->setFocusPolicy(Qt::NoFocus);
    connect(manage, &QToolButton::clicked, this, &PluginsPanel::managerRequested);

    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("browserSearch"));
    search_->setPlaceholderText(tr("Search (Esc to leave)"));
    search_->setClearButtonEnabled(true);
    search_->setFocusPolicy(Qt::ClickFocus);
    connect(search_, &QLineEdit::textChanged, this, &PluginsPanel::applySearch);
    auto* leaveSearch
        = new QShortcut(QKeySequence(Qt::Key_Escape), search_, nullptr, nullptr, Qt::WidgetShortcut);
    connect(leaveSearch, &QShortcut::activated, search_, [this] { search_->clearFocus(); });

    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("browserTree"));
    tree_->setHeaderHidden(true);
    tree_->setColumnCount(1);
    tree_->setFrameShape(QFrame::NoFrame);
    tree_->setIndentation(14);
    tree_->setUniformRowHeights(true);
    tree_->setFocusPolicy(Qt::ClickFocus);

    emptyHint_ = new QLabel(
        tr("No plugins yet. Plugins can be used after a scan of your plugin folders; nothing is scanned "
           "automatically."),
        this);
    emptyHint_->setObjectName(QStringLiteral("panelHint"));
    emptyHint_->setWordWrap(true);

    scanButton_ = new QPushButton(tr("Open Plugin Manager…"), this);
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
    layout->addWidget(search_);
    layout->addWidget(tree_, 1);
    layout->addWidget(emptyHint_);
    layout->addWidget(scanButton_);
    layout->addStretch(0);
    layout->addWidget(footer_);
}

void PluginsPanel::setStore(const PluginStore& store)
{
    tree_->clear();
    for (const PluginFormat format : {PluginFormat::Vst3, PluginFormat::Clap}) {
        auto* group = new QTreeWidgetItem(tree_);
        group->setFlags(Qt::ItemIsEnabled);
        int count = 0;
        for (const PluginInfo& info : store.plugins) {
            if (info.format != format)
                continue;
            auto* item = new QTreeWidgetItem(group, {QString::fromStdString(info.name)});
            item->setToolTip(0, QString::fromStdWString(info.path.wstring()));
            ++count;
        }
        group->setText(
            0, QStringLiteral("%1  (%2)").arg(QString::fromUtf8(formatName(format)), QString::number(count)));
        group->setExpanded(true);
        group->setHidden(count == 0);
    }

    const bool scanned = store.lastScan.isValid();
    const bool any = !store.plugins.empty();
    tree_->setVisible(any);
    search_->setVisible(any);
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
    applySearch(search_->text());
    tree_->scrollToTop();
}

void PluginsPanel::applySearch(const QString& text)
{
    const QString needle = text.trimmed();
    for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
        QTreeWidgetItem* group = tree_->topLevelItem(g);
        if (group->childCount() == 0)
            continue;
        int shown = 0;
        for (int i = 0; i < group->childCount(); ++i) {
            QTreeWidgetItem* item = group->child(i);
            const bool match = needle.isEmpty() || item->text(0).contains(needle, Qt::CaseInsensitive);
            item->setHidden(!match);
            shown += match ? 1 : 0;
        }
        group->setHidden(shown == 0);
    }
}

} // namespace vloomdaw
