// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/mixer/MixerPanel.hpp"

#include "ui/Theme.hpp"
#include "ui/widgets/ColorMenu/ColorMenu.hpp"
#include "ui/widgets/DetachButton/DetachButton.hpp"
#include "ui/widgets/LevelMeter/LevelMeter.hpp"
#include "ui/widgets/ToolGroup/ToolGroup.hpp"

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
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
constexpr QLatin1StringView kFxSlotRowsKey{"mixer/fxSlotRows"};

} // namespace

MixerPanel::MixerPanel(std::size_t insertCount, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("mixerPanel"));
    setAttribute(Qt::WA_StyledBackground);
    setMinimumHeight(396);

    QSettings settings;
    wideMode_ = settings.value(kWideStripsKey, false).toBool();
    showFxRack_ = settings.value(kShowFxRackKey, true).toBool();
    fxSlotRows_
        = std::clamp(settings.value(kFxSlotRowsKey, 5).toInt(), 0, static_cast<int>(MixerModel::kMaxEffects));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(5);

    // Header: title, + Bus / + FX / + Aux / + Insert buttons, detach / dock and close.
    auto* headerBox = new QWidget(this);
    headerBox->setObjectName(QStringLiteral("mixerHeader"));
    header_ = new QHBoxLayout(headerBox);
    header_->setContentsMargins(0, 0, 0, 0);
    header_->setSpacing(6);
    auto* title = new QLabel(tr("MIXER"), headerBox);
    title->setObjectName(QStringLiteral("panelTitle"));
    header_->addWidget(title);
    header_->addStretch();

    const auto headerButton = [headerBox](const QString& icon, const QString& text, const QString& tip) {
        auto* button = new QToolButton(headerBox);
        button->setObjectName(QStringLiteral("panelButton"));
        if (!icon.isEmpty())
            button->setIcon(Theme::icon(icon));
        button->setText(text);
        button->setToolButtonStyle(icon.isEmpty()
                ? Qt::ToolButtonTextOnly
                : (text.isEmpty() ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon));
        button->setProperty("iconOnly", text.isEmpty() && !icon.isEmpty());
        button->setToolTip(tip);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    };

    auto* addBusBtn = headerButton(QStringLiteral("plus"), tr("Bus"), tr("Add a Bus submix channel"));
    connect(addBusBtn, &QToolButton::clicked, this, [this] { emit addChannelRoleRequested(MixerRole::Bus); });

    auto* addFxChBtn = headerButton(QStringLiteral("plus"), tr("FX"), tr("Add an FX return channel"));
    connect(addFxChBtn, &QToolButton::clicked, this, [this] { emit addChannelRoleRequested(MixerRole::Fx); });

    auto* addAuxBtn = headerButton(QStringLiteral("plus"), tr("Aux"), tr("Add an Aux channel"));
    connect(addAuxBtn, &QToolButton::clicked, this, [this] { emit addChannelRoleRequested(MixerRole::Aux); });

    header_->addWidget(makePanelButtonGroup({addBusBtn, addFxChBtn, addAuxBtn}));

    auto* removeInsert = headerButton(QStringLiteral("minus"), QString(), tr("Delete the last channel"));
    connect(removeInsert, &QToolButton::clicked, this, [this] {
        if (trackCount() > 1)
            emit removeInsertRequested(trackCount() - 1);
    });
    auto* addInsert = headerButton(
        QStringLiteral("plus"), tr("Channel"), tr("Add an insert channel (right-click for Bus / FX / Aux)"));
    addInsert->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(addInsert, &QToolButton::clicked, this, &MixerPanel::addInsertRequested);
    connect(addInsert, &QWidget::customContextMenuRequested, this, [this, addInsert](const QPoint& pos) {
        QMenu menu(this);
        populateAddChannelActions(menu);
        menu.exec(addInsert->mapToGlobal(pos));
    });
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

    // Studio One console body: scrolling channels on the left, Main (Master) anchored on the right!
    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 2);
    body->setSpacing(6);

    auto* master = new MixerStrip(MixerStrip::Kind::Master, tr("Main"), QStringLiteral("M"), this);
    master->setWideMode(wideMode_);
    master->setFxRackVisible(showFxRack_);
    master->setUserFxSlotRows(fxSlotRows_);
    connectStrip(master);
    master->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(master, &QWidget::customContextMenuRequested, this, [this, master](const QPoint& at) {
        QMenu menu(this);
        QAction* monoAct
            = menu.addAction(tr("Mono Mix Check"), this, [this](bool mono) { emit monoToggled(0, mono); });
        monoAct->setCheckable(true);
        monoAct->setChecked(master->isMono());
        menu.addSeparator();
        populateAddChannelActions(menu);
        menu.addSeparator();
        addColorMenu(menu, master->color(),
            [this](std::optional<std::uint32_t> color) { emit colorChanged(0, color); });
        menu.exec(master->mapToGlobal(at));
    });
    strips_.append(master);
    master->setSelected(true);

    inserts_ = new QWidget;
    inserts_->setObjectName(QStringLiteral("mixerInserts"));
    inserts_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(inserts_, &QWidget::customContextMenuRequested, this,
        [this](const QPoint& pos) { showEmptySpaceContextMenu(inserts_->mapToGlobal(pos)); });

    insertRow_ = new QHBoxLayout(inserts_);
    insertRow_->setContentsMargins(0, 0, 0, 0);
    insertRow_->setSpacing(3);
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
    scroll_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(scroll_, &QWidget::customContextMenuRequested, this,
        [this](const QPoint& pos) { showEmptySpaceContextMenu(scroll_->mapToGlobal(pos)); });
    body->addWidget(scroll_, 1);

    auto* masterSep = new QFrame(this);
    masterSep->setFrameShape(QFrame::VLine);
    masterSep->setObjectName(QStringLiteral("mixerFooterSep"));
    body->addWidget(masterSep);
    body->addWidget(master);

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
    auto* allBtn = makeFooterPill(tr("All"), tr("Show all mixer channels"), true);
    auto* audioBtn = makeFooterPill(tr("Audio"), tr("Show Audio channels"), true);
    auto* instBtn = makeFooterPill(tr("Instruments"), tr("Show Instrument channels"), true);
    auto* busFxBtn = makeFooterPill(tr("Bus / FX / Aux"), tr("Show Bus, FX Return and Aux channels"), true);
    auto* fxFilterBtn = makeFooterPill(tr("With FX"), tr("Show channels with active insert effects"), true);
    allBtn->setChecked(true);
    filterGroup->addButton(allBtn, static_cast<int>(StripFilter::All));
    filterGroup->addButton(audioBtn, static_cast<int>(StripFilter::Audio));
    filterGroup->addButton(instBtn, static_cast<int>(StripFilter::Instrument));
    filterGroup->addButton(busFxBtn, static_cast<int>(StripFilter::BusFxAux));
    filterGroup->addButton(fxFilterBtn, static_cast<int>(StripFilter::Effects));
    connect(filterGroup, &QButtonGroup::idClicked, this, [this](int id) {
        filter_ = static_cast<StripFilter>(id);
        applyStripFilter();
    });
    footerRow->addWidget(allBtn);
    footerRow->addWidget(audioBtn);
    footerRow->addWidget(instBtn);
    footerRow->addWidget(busFxBtn);
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

    auto* fxVisBtn = makeFooterPill(tr("Inserts & Sends"),
        tr("Show / Hide Inserts & Sends racks for full-height precision faders"), true);
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

void MixerPanel::populateAddChannelActions(QMenu& menu)
{
    const bool hasRoom = trackCount() <= MixerModel::kMaxInserts;
    QAction* busAct = menu.addAction(
        tr("Add Bus Channel"), this, [this] { emit addChannelRoleRequested(MixerRole::Bus); });
    busAct->setEnabled(hasRoom);

    QAction* fxAct
        = menu.addAction(tr("Add FX Channel"), this, [this] { emit addChannelRoleRequested(MixerRole::Fx); });
    fxAct->setEnabled(hasRoom);

    QAction* auxAct = menu.addAction(
        tr("Add Aux Channel"), this, [this] { emit addChannelRoleRequested(MixerRole::Aux); });
    auxAct->setEnabled(hasRoom);

    menu.addSeparator();
    QAction* dlgAct = menu.addAction(
        Theme::icon(QStringLiteral("plus")), tr("Add Tracks…"), this, &MixerPanel::addTracksDialogRequested);
    dlgAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    menu.addAction(Theme::icon(QStringLiteral("track-audio")), tr("Add Audio Track"), this,
        &MixerPanel::addAudioTrackRequested);
    menu.addAction(Theme::icon(QStringLiteral("track-instrument")), tr("Add Instrument Track"), this,
        &MixerPanel::addInstrumentTrackRequested);
}

void MixerPanel::showEmptySpaceContextMenu(const QPoint& globalPos)
{
    QMenu menu(this);
    if (selectedTrack_ > 0 && selectedTrack_ < trackCount()) {
        const QString selTitle = names_.value(static_cast<qsizetype>(selectedTrack_)).isEmpty()
            ? tr("Insert %1").arg(selectedTrack_)
            : names_.value(static_cast<qsizetype>(selectedTrack_));
        menu.addAction(tr("Add Bus for “%1”").arg(selTitle), this,
            [this] { emit addBusForTrackRequested(selectedTrack_); });
        menu.addAction(tr("Add FX Send for “%1”").arg(selTitle), this,
            [this] { emit addFxSendForTrackRequested(selectedTrack_); });
        menu.addSeparator();
    }
    populateAddChannelActions(menu);
    menu.addSeparator();
    QAction* bypassAct = menu.addAction(tr("Bypass All FX"), this, [this](bool checked) {
        setAllEffectsBypassed(checked);
        emit bypassAllEffectsToggled(checked);
    });
    bypassAct->setCheckable(true);
    bypassAct->setChecked(bypassAllFxBtn_ != nullptr && bypassAllFxBtn_->isChecked());
    menu.addAction(tr("Clear Solo"), this, &MixerPanel::clearAllSoloRequested);
    menu.addAction(tr("Clear Mute"), this, &MixerPanel::clearAllMuteRequested);
    menu.addAction(tr("Reset All Peaks"), this, &MixerPanel::resetAllPeaks);
    menu.exec(globalPos);
}

void MixerPanel::showStripContextMenu(MixerStrip* strip, const QPoint& globalPos)
{
    const auto i = strips_.indexOf(strip);
    if (i <= 0)
        return;
    const auto index = static_cast<std::size_t>(i);
    setSelectedTrack(index);
    emit trackSelected(index);

    const QString custom = names_.value(i);
    const QString label = custom.isEmpty() ? tr("Insert %1").arg(index) : custom;

    QMenu menu(this);
    menu.addAction(tr("Rename"), this, [strip] { strip->startInlineRename(); });
    addColorMenu(menu, strip->color(),
        [this, index](std::optional<std::uint32_t> color) { emit colorChanged(index, color); });
    QAction* monoAct
        = menu.addAction(tr("Mono Mode"), this, [this, index](bool mono) { emit monoToggled(index, mono); });
    monoAct->setCheckable(true);
    monoAct->setChecked(strip->isMono());

    if (strip->hasInstrumentSource()) {
        menu.addSeparator();
        menu.addAction(Theme::icon(QStringLiteral("track-instrument")), tr("Open Instrument Editor…"), this,
            [this, index] { emit instrumentClicked(index); });
        menu.addAction(tr("Instrument Options…"), this,
            [this, index, globalPos] { emit instrumentMenuRequested(index, globalPos); });
    }

    menu.addSeparator();
    menu.addAction(tr("Output Routing…"), this,
        [this, index, globalPos] { emit outputRoutingRequested(index, globalPos); });
    menu.addAction(
        tr("Add Send…"), this, [this, index, globalPos] { emit addSendMenuRequested(index, globalPos); });

    menu.addSeparator();
    menu.addAction(
        tr("Copy Channel Settings"), this, [this, index] { emit copyChannelSettingsRequested(index); });
    QAction* pasteAct = menu.addAction(
        tr("Paste Channel Settings"), this, [this, index] { emit pasteChannelSettingsRequested(index); });
    pasteAct->setEnabled(canPasteChannelSettings_);

    menu.addSeparator();
    menu.addAction(
            tr("Add Bus for Selected Channel"), this, [this, index] { emit addBusForTrackRequested(index); })
        ->setEnabled(trackCount() <= MixerModel::kMaxInserts);
    menu.addAction(tr("Add FX Send for Selected Channel"), this,
            [this, index] { emit addFxSendForTrackRequested(index); })
        ->setEnabled(trackCount() <= MixerModel::kMaxInserts);

    menu.addSeparator();
    populateAddChannelActions(menu);

    menu.addSeparator();
    menu.addAction(tr("Move Left"), this, [this, index] { emit moveInsertRequested(index, index - 1); })
        ->setEnabled(index > 1);
    menu.addAction(tr("Move Right"), this, [this, index] { emit moveInsertRequested(index, index + 1); })
        ->setEnabled(index + 1 < trackCount());
    menu.addSeparator();
    menu.addAction(tr("Delete “%1”").arg(label), this, [this, index] { emit removeInsertRequested(index); });
    menu.exec(globalPos);
}

MixerStrip* MixerPanel::makeInsertStrip(std::size_t track)
{
    auto* strip = new MixerStrip(
        MixerStrip::Kind::Insert, tr("Insert %1").arg(track), QString::number(track), inserts_);
    strip->setWideMode(wideMode_);
    strip->setFxRackVisible(showFxRack_);
    strip->setUserFxSlotRows(fxSlotRows_);
    connectStrip(strip);
    strip->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(strip, &QWidget::customContextMenuRequested, this,
        [this, strip](const QPoint& at) { showStripContextMenu(strip, strip->mapToGlobal(at)); });
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
    connect(strip, &MixerStrip::monoToggled, this, [this, strip](bool mono) {
        if (const auto i = strips_.indexOf(strip); i >= 0)
            emit monoToggled(static_cast<std::size_t>(i), mono);
    });
    connect(strip, &MixerStrip::userFxSlotRowsChanged, this, [this](int rows) {
        const int clamped = std::clamp(rows, 0, static_cast<int>(MixerModel::kMaxEffects));
        if (fxSlotRows_ == clamped)
            return;
        fxSlotRows_ = clamped;
        QSettings().setValue(kFxSlotRowsKey, fxSlotRows_);
        for (MixerStrip* s : strips_)
            s->setUserFxSlotRows(fxSlotRows_);
    });
    connect(strip, &MixerStrip::renameRequested, this, [this, strip] {
        if (const auto i = strips_.indexOf(strip); i > 0)
            emit renameRequested(static_cast<std::size_t>(i));
    });
    connect(strip, &MixerStrip::nameEdited, this, [this, strip](const QString& name) {
        if (const auto i = strips_.indexOf(strip); i > 0)
            emit trackNameEdited(static_cast<std::size_t>(i), name);
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

    connect(strip, &MixerStrip::instrumentClicked, this, [this, strip] {
        if (const auto i = strips_.indexOf(strip); i > 0)
            emit instrumentClicked(static_cast<std::size_t>(i));
    });
    connect(strip, &MixerStrip::instrumentMenuRequested, this, [this, strip](const QPoint& at) {
        if (const auto i = strips_.indexOf(strip); i > 0)
            emit instrumentMenuRequested(static_cast<std::size_t>(i), at);
    });
    connect(strip, &MixerStrip::sourceClicked, this, [this, strip](const QPoint& at) {
        if (const auto i = strips_.indexOf(strip); i > 0)
            emit sourceClicked(static_cast<std::size_t>(i), at);
    });
    connect(strip, &MixerStrip::outputRoutingRequested, this, [this, strip](const QPoint& at) {
        if (const auto i = strips_.indexOf(strip); i > 0)
            emit outputRoutingRequested(static_cast<std::size_t>(i), at);
    });
    connect(strip, &MixerStrip::addSendMenuRequested, this, [this, strip](const QPoint& at) {
        if (const auto i = strips_.indexOf(strip); i > 0)
            emit addSendMenuRequested(static_cast<std::size_t>(i), at);
    });
    connect(strip, &MixerStrip::sendLevelChanged, this, [this, strip](std::size_t slot, double level) {
        if (const auto i = strips_.indexOf(strip); i > 0)
            emit sendLevelChanged(static_cast<std::size_t>(i), slot, level);
    });
    connect(strip, &MixerStrip::sendMuteToggled, this, [this, strip](std::size_t slot, bool muted) {
        if (const auto i = strips_.indexOf(strip); i > 0)
            emit sendMuteToggled(static_cast<std::size_t>(i), slot, muted);
    });
    connect(strip, &MixerStrip::sendPreFaderToggled, this, [this, strip](std::size_t slot, bool preFader) {
        if (const auto i = strips_.indexOf(strip); i > 0)
            emit sendPreFaderToggled(static_cast<std::size_t>(i), slot, preFader);
    });
    connect(strip, &MixerStrip::sendMenuRequested, this, [this, strip](std::size_t slot, const QPoint& at) {
        if (const auto i = strips_.indexOf(strip); i > 0)
            emit sendMenuRequested(static_cast<std::size_t>(i), slot, at);
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

void MixerPanel::setTrackSends(std::size_t track, const std::vector<MixerSendDisplay>& sends)
{
    if (track > 0 && track < trackCount())
        strips_[static_cast<qsizetype>(track)]->setSends(sends);
}

void MixerPanel::setTrackRole(std::size_t track, MixerRole role)
{
    if (track > 0 && track < trackCount()) {
        strips_[static_cast<qsizetype>(track)]->setRole(role);
        if (filter_ != StripFilter::All)
            applyStripFilter();
    }
}

void MixerPanel::setTrackMono(std::size_t track, bool mono)
{
    if (track < trackCount())
        strips_[static_cast<qsizetype>(track)]->setMono(mono);
}

void MixerPanel::startTrackRename(std::size_t track)
{
    if (track > 0 && track < trackCount())
        strips_[static_cast<qsizetype>(track)]->startInlineRename();
}

void MixerPanel::setTrackChannelRouting(std::size_t track, bool hasAudio, bool hasInstrument,
    const QString& instrumentName, const QString& sourceLabel, int outputTarget, const QString& outputLabel)
{
    if (track == 0 || track >= trackCount())
        return;
    strips_[static_cast<qsizetype>(track)]->setChannelRoutingInfo(
        hasAudio, hasInstrument, instrumentName, sourceLabel, outputTarget, outputLabel);
    if (filter_ != StripFilter::All)
        applyStripFilter();
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
            show = !strip->isBusOrFxRole() && strip->hasAudioSource();
            break;
        case StripFilter::Instrument:
            show = !strip->isBusOrFxRole() && strip->hasInstrumentSource();
            break;
        case StripFilter::BusFxAux:
            show = strip->isBusOrFxRole();
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
        selectedLabel_->setText(tr("Selected: Main"));
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

QSize MixerPanel::minimumSizeHint() const { return {300, 396}; }

QSize MixerPanel::sizeHint() const { return {980, 440}; }

} // namespace vloomdaw
