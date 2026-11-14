// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/mixer/MixerPanel.hpp"

#include "ui/Theme.hpp"
#include "ui/mixer/MixerStrip.hpp"
#include "ui/widgets/ColorMenu/ColorMenu.hpp"
#include "ui/widgets/DetachButton/DetachButton.hpp"
#include "ui/widgets/ToolGroup/ToolGroup.hpp"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace vloomdaw {

MixerPanel::MixerPanel(std::size_t insertCount, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("mixerPanel"));
    setAttribute(Qt::WA_StyledBackground);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 10);
    layout->setSpacing(6);

    // Header: title, detach / dock and close. A widget of its own, so a
    // detached window can be dragged by it.
    auto* headerBox = new QWidget(this);
    headerBox->setObjectName(QStringLiteral("mixerHeader"));
    header_ = new QHBoxLayout(headerBox);
    header_->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel(tr("MIXER"), headerBox);
    title->setObjectName(QStringLiteral("panelTitle"));
    header_->addWidget(title);
    header_->addStretch();
    const auto headerButton = [headerBox](const QString& icon, const QString& text, const QString& tip) {
        auto* button = new QToolButton(headerBox);
        button->setObjectName(QStringLiteral("panelButton"));
        button->setIcon(Theme::icon(icon));
        button->setText(text);
        button->setToolButtonStyle(text.isEmpty() ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
        button->setProperty("iconOnly", text.isEmpty()); // even padding (Theme)
        button->setToolTip(tip);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    };
    auto* removeInsert = headerButton(QStringLiteral("minus"), QString(), tr("Delete the last insert"));
    connect(removeInsert, &QToolButton::clicked, this, [this] {
        if (trackCount() > 1)
            emit removeInsertRequested(trackCount() - 1);
    });
    auto* addInsert = headerButton(QStringLiteral("plus"), tr("Insert"), tr("Add an insert"));
    connect(addInsert, &QToolButton::clicked, this, &MixerPanel::addInsertRequested);
    // "+ Insert" and "−" together, like the playlist's "+ Track" and "−".
    header_->addWidget(makePanelButtonGroup({addInsert, removeInsert}));
    detach_ = new DetachButton(tr("mixer"), headerBox);
    connect(detach_, &QToolButton::clicked, this, &MixerPanel::detachRequested);
    header_->addWidget(detach_);
    close_ = new QToolButton(headerBox);
    close_->setObjectName(QStringLiteral("panelClose"));
    close_->setIcon(Theme::icon(QStringLiteral("window-close")));
    close_->setIconSize(QSize(14, 14));
    close_->setFocusPolicy(Qt::NoFocus);
    close_->setToolTip(tr("Close the mixer (F9)"));
    close_->setAccessibleName(tr("Close the mixer"));
    connect(close_, &QToolButton::clicked, this, &MixerPanel::closeRequested);
    header_->addWidget(close_);
    layout->addWidget(headerBox);

    // Master stays put; the inserts scroll beside it.
    auto* body = new QHBoxLayout;
    body->setSpacing(8);

    auto* master = new MixerStrip(MixerStrip::Kind::Master, tr("Master"), tr("M"), this);
    connectStrip(master);
    master->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(master, &QWidget::customContextMenuRequested, this, [this, master](const QPoint& at) {
        QMenu menu(this);
        addColorMenu(menu, master->color(),
            [this](std::optional<std::uint32_t> color) { emit colorChanged(0, color); });
        menu.exec(master->mapToGlobal(at));
    });
    strips_.append(master);
    body->addWidget(master);

    inserts_ = new QWidget;
    inserts_->setObjectName(QStringLiteral("mixerInserts"));
    insertRow_ = new QHBoxLayout(inserts_);
    insertRow_->setContentsMargins(0, 0, 0, 0);
    insertRow_->setSpacing(2);
    for (std::size_t i = 1; i <= insertCount; ++i) {
        MixerStrip* strip = makeInsertStrip(i);
        strips_.append(strip);
        insertRow_->addWidget(strip);
    }
    insertRow_->addStretch();

    scroll_ = new QScrollArea(this);
    scroll_->setObjectName(QStringLiteral("mixerScroll"));
    scroll_->setWidget(inserts_);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    body->addWidget(scroll_, 1);

    layout->addLayout(body, 1);
}

MixerStrip* MixerPanel::makeInsertStrip(std::size_t track)
{
    auto* strip = new MixerStrip(
        MixerStrip::Kind::Insert, tr("Insert %1").arg(track), QString::number(track), inserts_);
    connectStrip(strip);
    strip->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(strip, &QWidget::customContextMenuRequested, this, [this, strip](const QPoint& at) {
        const auto index = static_cast<std::size_t>(strips_.indexOf(strip));
        QMenu menu(this);
        menu.addAction(tr("Rename…"), this, [this, index] { emit renameRequested(index); });
        addColorMenu(menu, strip->color(),
            [this, index](std::optional<std::uint32_t> color) { emit colorChanged(index, color); });
        menu.addSeparator();
        menu.addAction(tr("Move Left"), this, [this, index] { emit moveInsertRequested(index, index - 1); })
            ->setEnabled(index > 1);
        menu.addAction(tr("Move Right"), this, [this, index] { emit moveInsertRequested(index, index + 1); })
            ->setEnabled(index + 1 < trackCount());
        menu.addSeparator();
        menu.addAction(
            tr("Delete Insert %1").arg(index), this, [this, index] { emit removeInsertRequested(index); });
        menu.exec(strip->mapToGlobal(at));
    });
    return strip;
}

void MixerPanel::connectStrip(MixerStrip* strip)
{
    // The track is looked up when the strip emits: deleting an insert
    // renumbers the strips after it.
    connect(strip, &MixerStrip::faderChanged, this, [this, strip](double p) {
        if (const auto i = strips_.indexOf(strip); i >= 0)
            emit faderChanged(static_cast<std::size_t>(i), p);
    });
    connect(strip, &MixerStrip::panChanged, this, [this, strip](double p) {
        if (const auto i = strips_.indexOf(strip); i >= 0)
            emit panChanged(static_cast<std::size_t>(i), p);
    });
    connect(strip, &MixerStrip::muteToggled, this, [this, strip](bool on) {
        if (const auto i = strips_.indexOf(strip); i >= 0)
            emit muteToggled(static_cast<std::size_t>(i), on);
    });
    connect(strip, &MixerStrip::soloToggled, this, [this, strip](bool on) {
        if (const auto i = strips_.indexOf(strip); i >= 0)
            emit soloToggled(static_cast<std::size_t>(i), on);
    });
    connect(strip, &MixerStrip::renameRequested, this, [this, strip] {
        if (const auto i = strips_.indexOf(strip); i > 0)
            emit renameRequested(static_cast<std::size_t>(i));
    });
    connect(strip, &MixerStrip::effectsMenuRequested, this, [this, strip](const QPoint& at) {
        if (const auto i = strips_.indexOf(strip); i >= 0)
            emit effectsMenuRequested(static_cast<std::size_t>(i), at);
    });
    connect(strip, &MixerStrip::pluginDropped, this, [this, strip](std::size_t plugin) {
        if (const auto i = strips_.indexOf(strip); i >= 0)
            emit pluginDropped(static_cast<std::size_t>(i), plugin);
    });
}

void MixerPanel::setEffectCount(std::size_t track, std::size_t count)
{
    if (track < static_cast<std::size_t>(strips_.size()))
        strips_[static_cast<qsizetype>(track)]->setEffectCount(count);
}

void MixerPanel::addInsert()
{
    MixerStrip* strip = makeInsertStrip(trackCount());
    strips_.append(strip);
    insertRow_->insertWidget(insertRow_->count() - 1, strip); // before the stretch
    // Show the new strip once the row has its new width.
    QTimer::singleShot(0, this,
        [this] { scroll_->horizontalScrollBar()->setValue(scroll_->horizontalScrollBar()->maximum()); });
}

void MixerPanel::removeInsert(std::size_t track)
{
    if (track == 0 || track >= trackCount())
        return;
    MixerStrip* strip = strips_.takeAt(static_cast<qsizetype>(track));
    insertRow_->removeWidget(strip);
    strip->hide();
    strip->deleteLater(); // not now: its context menu may be what asked
    if (track < static_cast<std::size_t>(names_.size()))
        names_.removeAt(static_cast<qsizetype>(track));
    for (std::size_t i = track; i < trackCount(); ++i) {
        const QString name = names_.value(static_cast<qsizetype>(i));
        strips_[static_cast<qsizetype>(i)]->setLabel(
            name.isEmpty() ? tr("Insert %1").arg(i) : name, QString::number(i));
    }
}

void MixerPanel::moveInsert(std::size_t from, std::size_t to)
{
    if (from == 0 || to == 0 || from >= trackCount() || to >= trackCount() || from == to)
        return;
    MixerStrip* strip = strips_.takeAt(static_cast<qsizetype>(from));
    strips_.insert(static_cast<qsizetype>(to), strip);
    insertRow_->removeWidget(strip);
    insertRow_->insertWidget(static_cast<int>(to) - 1, strip); // the row holds inserts only (and a stretch)
    while (static_cast<std::size_t>(names_.size()) < trackCount())
        names_.append(QString());
    names_.move(static_cast<qsizetype>(from), static_cast<qsizetype>(to));
    for (std::size_t i = 1; i < trackCount(); ++i) {
        const QString name = names_.value(static_cast<qsizetype>(i));
        strips_[static_cast<qsizetype>(i)]->setLabel(
            name.isEmpty() ? tr("Insert %1").arg(i) : name, QString::number(i));
    }
}

void MixerPanel::setTrackValues(std::size_t track, double fader, double pan, bool muted, bool soloed)
{
    if (track < trackCount())
        strips_[static_cast<qsizetype>(track)]->setValues(fader, pan, muted, soloed);
}

void MixerPanel::setTrackColor(std::size_t track, std::optional<std::uint32_t> color)
{
    if (track < trackCount())
        strips_[static_cast<qsizetype>(track)]->setColor(color);
}

void MixerPanel::setTrackName(std::size_t track, const QString& name)
{
    if (track == 0 || track >= trackCount())
        return;
    while (static_cast<std::size_t>(names_.size()) <= track)
        names_.append(QString());
    names_[static_cast<qsizetype>(track)] = name == tr("Insert %1").arg(track) ? QString() : name;
    strips_[static_cast<qsizetype>(track)]->setLabel(name, QString::number(track));
}

LevelMeter* MixerPanel::meter(std::size_t track) const
{
    return track < trackCount() ? strips_[static_cast<qsizetype>(track)]->meter() : nullptr;
}

void MixerPanel::setDetached(bool detached)
{
    detach_->setDetached(detached);
    // A detached window has its own close button.
    close_->setVisible(!detached);
}

void MixerPanel::addHeaderWidget(QWidget* widget) { header_->addWidget(widget); }

void MixerPanel::setSilenced(std::size_t track, bool silenced)
{
    if (track < trackCount())
        strips_[static_cast<qsizetype>(track)]->setSilenced(silenced);
}

} // namespace vloomdaw
