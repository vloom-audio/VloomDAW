// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/mixer/MixerPanel.hpp"

#include "ui/Theme.hpp"
#include "ui/mixer/MixerStrip.hpp"
#include "ui/widgets/ColorMenu/ColorMenu.hpp"
#include "ui/widgets/DetachButton/DetachButton.hpp"
#include "ui/widgets/LevelMeter/LevelMeter.hpp"
#include "ui/widgets/ToolGroup/ToolGroup.hpp"

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace vloomdaw {

namespace {

constexpr QLatin1StringView kWideStripsKey{"mixer/wideStrips"};
constexpr QLatin1StringView kShowFxRackKey{"mixer/showFxRack"};

} // namespace

MixerPanel::MixerPanel(std::size_t insertCount, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("mixerPanel"));
    setAttribute(Qt::WA_StyledBackground);
    setMinimumHeight(308);

    QSettings settings;
    wideMode_ = settings.value(kWideStripsKey, false).toBool();
    showFxRack_ = settings.value(kShowFxRackKey, true).toBool();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(5);

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
        button->setProperty("iconOnly", text.isEmpty());
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
    master->setWideMode(wideMode_);
    master->setFxRackVisible(showFxRack_);
    connectStrip(master);
    master->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(master, &QWidget::customContextMenuRequested, this, [this, master](const QPoint& at) {
        QMenu menu(this);
        addColorMenu(menu, master->color(),
            [this](std::optional<std::uint32_t> color) { emit colorChanged(0, color); });
        menu.exec(master->mapToGlobal(at));
    });
    strips_.append(master);
    master->setSelected(true);
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

    // Studio One-style Bottom Console Toolbar (#mixerFooter)
    auto* footerBox = new QFrame(this);
    footerBox->setObjectName(QStringLiteral("mixerFooter"));
    auto* footerRow = new QHBoxLayout(footerBox);
    footerRow->setContentsMargins(8, 3, 8, 3);
    footerRow->setSpacing(5);

    auto* showLbl = new QLabel(tr("Channels:"), footerBox);
    showLbl->setObjectName(QStringLiteral("panelHint"));
    footerRow->addWidget(showLbl);

    const auto makeFooterPill = [footerBox](const QString& text, const QString& tip, bool checkable) {
        auto* btn = new QPushButton(text, footerBox);
        btn->setObjectName(QStringLiteral("mixerFooterPill"));
        btn->setCheckable(checkable);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setFixedHeight(22);
        btn->setToolTip(tip);
        return btn;
    };

    auto* filterGroup = new QButtonGroup(this);
    filterGroup->setExclusive(true);
    auto* allBtn = makeFooterPill(tr("All"), tr("Show all insert channels"), true);
    auto* audioBtn = makeFooterPill(tr("Audio"), tr("Show insert channels carrying audio / stems"), true);
    auto* instBtn
        = makeFooterPill(tr("Instruments"), tr("Show insert channels carrying instrument plugins"), true);
    auto* fxFilterBtn
        = makeFooterPill(tr("With FX"), tr("Show insert channels that have active FX slots"), true);
    allBtn->setChecked(true);
    filterGroup->addButton(allBtn, static_cast<int>(StripFilter::All));
    filterGroup->addButton(audioBtn, static_cast<int>(StripFilter::Audio));
    filterGroup->addButton(instBtn, static_cast<int>(StripFilter::Instrument));
    filterGroup->addButton(fxFilterBtn, static_cast<int>(StripFilter::Effects));
    connect(filterGroup, &QButtonGroup::idClicked, this, [this](int id) {
        filter_ = static_cast<StripFilter>(id);
        applyStripFilter();
    });
    footerRow->addWidget(allBtn);
    footerRow->addWidget(audioBtn);
    footerRow->addWidget(instBtn);
    footerRow->addWidget(fxFilterBtn);

    auto* sep1 = new QFrame(footerBox);
    sep1->setFrameShape(QFrame::VLine);
    sep1->setObjectName(QStringLiteral("mixerFooterSep"));
    footerRow->addWidget(sep1);

    auto* viewLbl = new QLabel(tr("View:"), footerBox);
    viewLbl->setObjectName(QStringLiteral("panelHint"));
    footerRow->addWidget(viewLbl);

    auto* wideBtn = makeFooterPill(tr("Wide Strips"), tr("Toggle wider mixer strips and FX slots"), true);
    wideBtn->setChecked(wideMode_);
    connect(wideBtn, &QPushButton::toggled, this, [this](bool wide) {
        wideMode_ = wide;
        QSettings().setValue(kWideStripsKey, wide);
        for (MixerStrip* strip : strips_)
            strip->setWideMode(wide);
    });
    footerRow->addWidget(wideBtn);

    auto* fxVisBtn = makeFooterPill(
        tr("FX Rack"), tr("Show / Hide FX insert racks for full-height precision faders"), true);
    fxVisBtn->setChecked(showFxRack_);
    connect(fxVisBtn, &QPushButton::toggled, this, [this](bool showFx) {
        showFxRack_ = showFx;
        QSettings().setValue(kShowFxRackKey, showFx);
        for (MixerStrip* strip : strips_)
            strip->setFxRackVisible(showFx);
    });
    footerRow->addWidget(fxVisBtn);

    footerRow->addStretch(1);

    selectedLabel_ = new QLabel(footerBox);
    selectedLabel_->setObjectName(QStringLiteral("mixerSelectedReadout"));
    footerRow->addWidget(selectedLabel_);

    auto* addFxBtn = makeFooterPill(
        tr("+ Add FX"), tr("Add an effect plug-in to the currently selected mixer channel"), false);
    connect(addFxBtn, &QPushButton::clicked, this,
        [this, addFxBtn] { emit effectsMenuRequested(selectedTrack_, addFxBtn->mapToGlobal(QPoint(0, 0))); });
    footerRow->addWidget(addFxBtn);

    bypassAllFxBtn_ = makeFooterPill(tr("Bypass All FX"),
        tr("Temporarily bypass all mixer insert and master effects (A/B compare)"), true);
    bypassAllFxBtn_->setIcon(Theme::icon(QStringLiteral("power")));
    bypassAllFxBtn_->setIconSize(QSize(13, 13));
    connect(bypassAllFxBtn_, &QPushButton::toggled, this, &MixerPanel::bypassAllEffectsToggled);
    footerRow->addWidget(bypassAllFxBtn_);

    auto* clearSoloBtn = makeFooterPill(tr("Clear Solo"), tr("Unsolo all soloed mixer channels"), false);
    connect(clearSoloBtn, &QPushButton::clicked, this, &MixerPanel::clearAllSoloRequested);
    footerRow->addWidget(clearSoloBtn);

    auto* clearMuteBtn = makeFooterPill(tr("Clear Mute"), tr("Unmute all muted mixer channels"), false);
    connect(clearMuteBtn, &QPushButton::clicked, this, &MixerPanel::clearAllMuteRequested);
    footerRow->addWidget(clearMuteBtn);

    auto* resetPeaksBtn
        = makeFooterPill(tr("Reset Peaks"), tr("Reset peak hold and clip indicators on all meters"), false);
    connect(resetPeaksBtn, &QPushButton::clicked, this, &MixerPanel::resetAllPeaks);
    footerRow->addWidget(resetPeaksBtn);

    layout->addWidget(footerBox);
    updateSelectedLabel();
}

MixerStrip* MixerPanel::makeInsertStrip(std::size_t track)
{
    auto* strip = new MixerStrip(
        MixerStrip::Kind::Insert, tr("Insert %1").arg(track), QString::number(track), inserts_);
    strip->setWideMode(wideMode_);
    strip->setFxRackVisible(showFxRack_);
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
    connect(strip, &MixerStrip::clicked, this, [this, strip] {
        if (const auto i = strips_.indexOf(strip); i >= 0) {
            const auto idx = static_cast<std::size_t>(i);
            if (selectedTrack_ != idx) {
                setSelectedTrack(idx);
                emit trackSelected(idx);
            }
        }
    });
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
    connect(strip, &MixerStrip::colorPickRequested, this, [this, strip](const QPoint& at) {
        if (const auto i = strips_.indexOf(strip); i >= 0) {
            const auto track = static_cast<std::size_t>(i);
            showColorPicker(this, at, strip->color(),
                [this, track](std::optional<std::uint32_t> color) { emit colorChanged(track, color); });
        }
    });
    connect(strip, &MixerStrip::effectsMenuRequested, this, [this, strip](const QPoint& at) {
        if (const auto i = strips_.indexOf(strip); i >= 0)
            emit effectsMenuRequested(static_cast<std::size_t>(i), at);
    });
    connect(strip, &MixerStrip::effectBypassToggled, this, [this, strip](std::size_t slot, bool bypassed) {
        if (const auto i = strips_.indexOf(strip); i >= 0)
            emit effectBypassToggled(static_cast<std::size_t>(i), slot, bypassed);
    });
    connect(strip, &MixerStrip::effectClicked, this, [this, strip](std::size_t slot) {
        if (const auto i = strips_.indexOf(strip); i >= 0)
            emit effectClicked(static_cast<std::size_t>(i), slot);
    });
    connect(strip, &MixerStrip::effectMenuRequested, this, [this, strip](std::size_t slot, const QPoint& at) {
        if (const auto i = strips_.indexOf(strip); i >= 0)
            emit effectMenuRequested(static_cast<std::size_t>(i), slot, at);
    });
    connect(
        strip, &MixerStrip::pluginDroppedOnSlot, this, [this, strip](std::size_t slot, std::size_t plugin) {
            if (const auto i = strips_.indexOf(strip); i >= 0)
                emit pluginDroppedOnSlot(static_cast<std::size_t>(i), slot, plugin);
        });
    connect(strip, &MixerStrip::pluginDropped, this, [this, strip](std::size_t plugin) {
        if (const auto i = strips_.indexOf(strip); i >= 0)
            emit pluginDropped(static_cast<std::size_t>(i), plugin);
    });
}

void MixerPanel::setTrackEffects(std::size_t track, const std::vector<PluginSlot>& effects)
{
    if (track < static_cast<std::size_t>(strips_.size())) {
        strips_[static_cast<qsizetype>(track)]->setEffects(effects);
        if (filter_ == StripFilter::Effects)
            applyStripFilter();
    }
}

void MixerPanel::setTrackSourceFlags(std::size_t track, bool hasAudio, bool hasInstrument)
{
    if (track == 0 || track >= trackCount())
        return;
    strips_[static_cast<qsizetype>(track)]->setSourceFlags(hasAudio, hasInstrument);
    if (filter_ == StripFilter::Audio || filter_ == StripFilter::Instrument)
        applyStripFilter();
}

void MixerPanel::applyStripFilter()
{
    for (qsizetype i = 1; i < strips_.size(); ++i) {
        MixerStrip* strip = strips_[i];
        bool show = true;
        switch (filter_) {
        case StripFilter::All:
            show = true;
            break;
        case StripFilter::Audio:
            show = strip->hasAudioSource();
            break;
        case StripFilter::Instrument:
            show = strip->hasInstrumentSource();
            break;
        case StripFilter::Effects:
            show = strip->hasEffects();
            break;
        }
        strip->setVisible(show);
    }
}

void MixerPanel::resetAllPeaks()
{
    for (MixerStrip* strip : strips_) {
        if (LevelMeter* m = strip->meter())
            m->reset();
    }
}

void MixerPanel::setAllEffectsBypassed(bool bypassed)
{
    if (bypassAllFxBtn_ == nullptr)
        return;
    const QSignalBlocker blocker(bypassAllFxBtn_);
    bypassAllFxBtn_->setChecked(bypassed);
}

void MixerPanel::updateSelectedLabel()
{
    if (selectedLabel_ == nullptr)
        return;
    if (selectedTrack_ == 0) {
        selectedLabel_->setText(tr("Selected: Master"));
        return;
    }
    const QString custom = names_.value(static_cast<qsizetype>(selectedTrack_));
    const QString title = custom.isEmpty() ? tr("Insert %1").arg(selectedTrack_) : custom;
    selectedLabel_->setText(tr("Selected: %1").arg(title));
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
    insertRow_->insertWidget(insertRow_->count() - 1, strip);
    applyStripFilter();
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
    strip->deleteLater();
    if (track < static_cast<std::size_t>(names_.size()))
        names_.removeAt(static_cast<qsizetype>(track));
    for (std::size_t i = track; i < trackCount(); ++i) {
        const QString name = names_.value(static_cast<qsizetype>(i));
        strips_[static_cast<qsizetype>(i)]->setLabel(
            name.isEmpty() ? tr("Insert %1").arg(i) : name, QString::number(i));
    }
    if (selectedTrack_ >= trackCount())
        setSelectedTrack(trackCount() - 1);
    else
        setSelectedTrack(selectedTrack_);
}

void MixerPanel::moveInsert(std::size_t from, std::size_t to)
{
    if (from == 0 || to == 0 || from >= trackCount() || to >= trackCount() || from == to)
        return;
    MixerStrip* strip = strips_.takeAt(static_cast<qsizetype>(from));
    strips_.insert(static_cast<qsizetype>(to), strip);
    insertRow_->removeWidget(strip);
    insertRow_->insertWidget(static_cast<int>(to) - 1, strip);
    while (static_cast<std::size_t>(names_.size()) < trackCount())
        names_.append(QString());
    names_.move(static_cast<qsizetype>(from), static_cast<qsizetype>(to));
    for (std::size_t i = 1; i < trackCount(); ++i) {
        const QString name = names_.value(static_cast<qsizetype>(i));
        strips_[static_cast<qsizetype>(i)]->setLabel(
            name.isEmpty() ? tr("Insert %1").arg(i) : name, QString::number(i));
    }
    if (selectedTrack_ == from)
        setSelectedTrack(to);
    updateSelectedLabel();
}

void MixerPanel::setSelectedTrack(std::size_t track)
{
    if (strips_.isEmpty())
        return;
    selectedTrack_ = std::min(track, trackCount() - 1);
    for (qsizetype i = 0; i < strips_.size(); ++i)
        strips_[i]->setSelected(static_cast<std::size_t>(i) == selectedTrack_);
    if (selectedTrack_ > 0 && scroll_ != nullptr
        && strips_[static_cast<qsizetype>(selectedTrack_)]->isVisible()) {
        scroll_->ensureWidgetVisible(strips_[static_cast<qsizetype>(selectedTrack_)], 12, 0);
    }
    updateSelectedLabel();
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
    if (track == selectedTrack_)
        updateSelectedLabel();
}

LevelMeter* MixerPanel::meter(std::size_t track) const
{
    return track < trackCount() ? strips_[static_cast<qsizetype>(track)]->meter() : nullptr;
}

void MixerPanel::setDetached(bool detached)
{
    detach_->setDetached(detached);
    close_->setVisible(!detached);
}

void MixerPanel::addHeaderWidget(QWidget* widget) { header_->addWidget(widget); }

void MixerPanel::setSilenced(std::size_t track, bool silenced)
{
    if (track < trackCount())
        strips_[static_cast<qsizetype>(track)]->setSilenced(silenced);
}

QSize MixerPanel::minimumSizeHint() const { return {200, 308}; }

QSize MixerPanel::sizeHint() const { return {800, 392}; }

} // namespace vloomdaw
