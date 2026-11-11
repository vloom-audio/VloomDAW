// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/dialogs/SettingsDialog/SettingsDialog.hpp"

#include "ui/Theme.hpp"
#include "ui/dialogs/SettingsDialog/AudioSettingsPage.hpp"
#include "ui/dialogs/SettingsDialog/GeneralSettingsPage.hpp"
#include "ui/dialogs/SettingsDialog/ThemeSettingsPage.hpp"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace vloomdaw {

namespace {
constexpr int kNavItemVerticalPadding = 18; ///< total, above and below the text
constexpr int kNavWidth = 190;
} // namespace

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(tr("Settings"));
    setMinimumSize(760, 520);
    resize(880, 700);

    // Navigation: a heading and one entry per page, with an icon.
    auto* navBox = new QWidget(this);
    navBox->setObjectName(QStringLiteral("settingsNavBox"));
    navBox->setAttribute(Qt::WA_StyledBackground);
    navBox->setFixedWidth(kNavWidth);
    auto* navLayout = new QVBoxLayout(navBox);
    navLayout->setContentsMargins(12, 18, 12, 12);
    navLayout->setSpacing(10);
    auto* navTitle = new QLabel(tr("SETTINGS"), navBox);
    navTitle->setObjectName(QStringLiteral("panelTitle"));
    navTitle->setContentsMargins(8, 0, 0, 0);
    navLayout->addWidget(navTitle);
    nav_ = new QListWidget(navBox);
    nav_->setObjectName(QStringLiteral("settingsNav"));
    nav_->setFrameShape(QFrame::NoFrame);
    nav_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    nav_->setIconSize(QSize(18, 18));
    navLayout->addWidget(nav_, 1);

    pages_ = new QStackedWidget(this);
    // Follow the selection, not just the current row: screen readers and UI
    // automation select an item without making it current.
    nav_->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(nav_, &QListWidget::itemSelectionChanged, this, [this] {
        const auto selected = nav_->selectedItems();
        if (!selected.isEmpty())
            pages_->setCurrentIndex(nav_->row(selected.constFirst()));
    });

    // Order must match the Page enum.
    general_ = new GeneralSettingsPage(this);
    addPage(QStringLiteral("sliders"), tr("General"), tr("How VloomDAW starts and behaves."), general_);
    audio_ = new AudioSettingsPage(this);
    addPage(QStringLiteral("speaker"), tr("Audio"), tr("The device VloomDAW plays through and its format."), audio_);
    theme_ = new ThemeSettingsPage(this);
    addPage(QStringLiteral("palette"), tr("Theme"), tr("Colours of the whole app. Changes show at once."), theme_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto* content = new QVBoxLayout;
    content->setContentsMargins(28, 22, 24, 16);
    content->setSpacing(12);
    content->addWidget(pages_, 1);
    content->addWidget(buttons);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(navBox);
    layout->addLayout(content, 1);

    nav_->setCurrentRow(0);
}

void SettingsDialog::addPage(const QString& icon, const QString& title, const QString& subtitle, QWidget* page)
{
    // Each page: a heading and a one-line summary above its cards, scrolling
    // when the window is short.
    auto* holder = new QWidget;
    holder->setObjectName(QStringLiteral("settingsPage"));
    auto* layout = new QVBoxLayout(holder);
    layout->setContentsMargins(0, 0, 8, 0);
    layout->setSpacing(4);
    auto* heading = new QLabel(title, holder);
    heading->setObjectName(QStringLiteral("settingsHeading"));
    layout->addWidget(heading);
    auto* summary = new QLabel(subtitle, holder);
    summary->setObjectName(QStringLiteral("settingsSubtitle"));
    layout->addWidget(summary);
    layout->addSpacing(14);
    layout->addWidget(page);
    layout->addStretch();

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("settingsScroll"));
    scroll->setWidget(holder);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded); // only if the window is very narrow
    pages_->addWidget(scroll);

    // The style sheet's item padding is drawn but not counted in the row
    // height, so rows would overlap; size them from the font instead.
    auto* item = new QListWidgetItem(Theme::icon(icon), title, nav_);
    item->setSizeHint(QSize(0, nav_->fontMetrics().height() + kNavItemVerticalPadding));
}

void SettingsDialog::showPage(Page page)
{
    nav_->setCurrentRow(static_cast<int>(page));
    show();
    raise();
    activateWindow();
}

} // namespace vloomdaw
