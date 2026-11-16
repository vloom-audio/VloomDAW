// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/dialogs/AddTracksDialog/AddTracksDialog.hpp"

#include "core/MixerModel.hpp"
#include "core/PluginMetadata.hpp"
#include "core/Project.hpp"
#include "core/TrackRouting.hpp"
#include "ui/Theme.hpp"
#include "ui/plugins/PluginStore.hpp"
#include "ui/widgets/ColorMenu/ColorMenu.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace vloomdaw {

AddTracksDialog::AddTracksDialog(const Project& project, const MixerModel& mixer, const PluginStore& plugins,
    AddTracksSpec::Kind initialKind, QWidget* parent)
    : QDialog(parent)
    , project_(project)
    , plugins_(plugins)
    , kind_(initialKind)
    , nextColorIndex_(project.tracks().size())
    , customColor_(trackPaletteColor(project.tracks().size()))
{
    setObjectName(QStringLiteral("addTracksDialog"));
    setWindowTitle(tr("Add Tracks"));
    setWindowModality(Qt::WindowModal);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setFixedWidth(460);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 14, 14, 14);
    outer->setSpacing(12);

    // Top Studio One-style mode cards: Audio | Instrument
    auto* typeStrip = new QFrame(this);
    typeStrip->setObjectName(QStringLiteral("addTracksTypeStrip"));
    auto* typeRow = new QHBoxLayout(typeStrip);
    typeRow->setContentsMargins(0, 0, 0, 0);
    typeRow->setSpacing(1);

    const auto makeTypeCard = [&](const QString& iconName, const QString& label, AddTracksSpec::Kind k) {
        auto* btn = new QToolButton(typeStrip);
        btn->setObjectName(QStringLiteral("addTracksTypeCard"));
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setIcon(Theme::icon(iconName));
        btn->setIconSize(QSize(28, 28));
        btn->setText(label);
        btn->setCheckable(true);
        btn->setChecked(kind_ == k);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        btn->setFixedHeight(74);
        return btn;
    };

    audioCardBtn_ = makeTypeCard(QStringLiteral("track-audio"), tr("Audio"), AddTracksSpec::Kind::Audio);
    instCardBtn_
        = makeTypeCard(QStringLiteral("track-instrument"), tr("Instrument"), AddTracksSpec::Kind::Instrument);

    auto* cardGroup = new QButtonGroup(this);
    cardGroup->setExclusive(true);
    cardGroup->addButton(audioCardBtn_, static_cast<int>(AddTracksSpec::Kind::Audio));
    cardGroup->addButton(instCardBtn_, static_cast<int>(AddTracksSpec::Kind::Instrument));
    connect(cardGroup, &QButtonGroup::idClicked, this,
        [this](int id) { setKind(static_cast<AddTracksSpec::Kind>(id)); });

    typeRow->addWidget(audioCardBtn_, 1);
    typeRow->addWidget(instCardBtn_, 1);
    outer->addWidget(typeStrip);

    // Main settings card
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("addTracksCard"));
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 16, 16, 16);
    cardLayout->setSpacing(12);

    auto* commonGrid = new QGridLayout;
    commonGrid->setContentsMargins(0, 0, 0, 0);
    commonGrid->setHorizontalSpacing(14);
    commonGrid->setVerticalSpacing(10);
    commonGrid->setColumnMinimumWidth(0, 82);
    commonGrid->setColumnStretch(1, 1);

    // Row 0: Name
    auto* nameLbl = new QLabel(tr("Name"), card);
    nameEdit_ = new QLineEdit(defaultBaseName(kind_), card);
    nameEdit_->setMaxLength(static_cast<int>(Project::kMaxTrackNameLength));
    nameEdit_->selectAll();
    connect(nameEdit_, &QLineEdit::textEdited, this, [this] { nameEditedByUser_ = true; });
    commonGrid->addWidget(nameLbl, 0, 0);
    commonGrid->addWidget(nameEdit_, 0, 1);

    // Row 1: Count + Pack Folder
    auto* countLbl = new QLabel(tr("Count"), card);
    auto* countRow = new QHBoxLayout;
    countRow->setContentsMargins(0, 0, 0, 0);
    countRow->setSpacing(14);

    countSpin_ = new QSpinBox(card);
    countSpin_->setRange(1, 64);
    countSpin_->setValue(1);
    countSpin_->setFixedWidth(96);
    countSpin_->setAlignment(Qt::AlignCenter);

    packFolderCheck_ = new QCheckBox(tr("Pack Folder"), card);
    packFolderCheck_->setToolTip(tr("Place the created tracks inside a new parent folder track"));

    countRow->addWidget(countSpin_);
    countRow->addWidget(packFolderCheck_);
    countRow->addStretch(1);
    commonGrid->addWidget(countLbl, 1, 0);
    commonGrid->addLayout(countRow, 1, 1);

    // Row 2: Color + Auto-Color
    auto* colorLbl = new QLabel(tr("Color"), card);
    auto* colorRow = new QHBoxLayout;
    colorRow->setContentsMargins(0, 0, 0, 0);
    colorRow->setSpacing(14);

    colorSwatchBtn_ = new QPushButton(card);
    colorSwatchBtn_->setObjectName(QStringLiteral("addTracksColorSwatch"));
    colorSwatchBtn_->setFixedSize(96, 26);
    colorSwatchBtn_->setCursor(Qt::PointingHandCursor);
    colorSwatchBtn_->setToolTip(tr("Click to choose a custom track color"));

    autoColorCheck_ = new QCheckBox(tr("Auto-Color"), card);
    autoColorCheck_->setChecked(true);
    autoColorCheck_->setToolTip(tr("Assign vivid spectrum colors automatically to each created track"));

    connect(colorSwatchBtn_, &QPushButton::clicked, this, [this] {
        showColorPicker(this, colorSwatchBtn_->mapToGlobal(QPoint(0, colorSwatchBtn_->height() + 2)),
            customColor_, [this](std::optional<std::uint32_t> picked) {
                if (picked.has_value()) {
                    customColor_ = *picked;
                    autoColorCheck_->setChecked(false);
                } else {
                    autoColorCheck_->setChecked(true);
                }
                updateColorSwatch();
            });
    });
    connect(autoColorCheck_, &QCheckBox::toggled, this, [this] { updateColorSwatch(); });

    colorRow->addWidget(colorSwatchBtn_);
    colorRow->addWidget(autoColorCheck_);
    colorRow->addStretch(1);
    commonGrid->addWidget(colorLbl, 2, 0);
    commonGrid->addLayout(colorRow, 2, 1);

    cardLayout->addLayout(commonGrid);

    auto* sep = new QFrame(card);
    sep->setObjectName(QStringLiteral("settingsSeparator"));
    sep->setFixedHeight(1);
    cardLayout->addWidget(sep);

    // Mode-specific stacked section
    modeStack_ = new QStackedWidget(card);

    // ---- Page 0: Audio Track Options ----
    auto* audioPage = new QWidget(modeStack_);
    auto* audioGrid = new QGridLayout(audioPage);
    audioGrid->setContentsMargins(0, 0, 0, 0);
    audioGrid->setHorizontalSpacing(14);
    audioGrid->setVerticalSpacing(10);
    audioGrid->setColumnMinimumWidth(0, 82);
    audioGrid->setColumnStretch(1, 1);

    auto* formatLbl = new QLabel(tr("Format"), audioPage);
    formatCombo_ = new QComboBox(audioPage);
    formatCombo_->addItem(Theme::icon(QStringLiteral("track-audio")), tr("Stereo"), false);
    formatCombo_->addItem(Theme::icon(QStringLiteral("track-audio")), tr("Mono"), true);
    audioGrid->addWidget(formatLbl, 0, 0);
    audioGrid->addWidget(formatCombo_, 0, 1);

    auto* fxLbl = new QLabel(tr("FX Chain"), audioPage);
    fxChainCombo_ = new QComboBox(audioPage);
    fxChainCombo_->addItem(tr("No Preset"), static_cast<int>(AddTracksSpec::FxPreset::None));
    fxChainCombo_->addItem(tr("Built-in EQ"), static_cast<int>(AddTracksSpec::FxPreset::BuiltinEq));
    fxChainCombo_->addItem(tr("Built-in Gain"), static_cast<int>(AddTracksSpec::FxPreset::BuiltinGain));
    fxChainCombo_->addItem(
        tr("Built-in EQ + Gain"), static_cast<int>(AddTracksSpec::FxPreset::BuiltinEqAndGain));
    audioGrid->addWidget(fxLbl, 1, 0);
    audioGrid->addWidget(fxChainCombo_, 1, 1);

    auto* audioOutLbl = new QLabel(tr("Output"), audioPage);
    auto* audioOutRow = new QHBoxLayout;
    audioOutRow->setContentsMargins(0, 0, 0, 0);
    audioOutRow->setSpacing(10);

    audioOutputCombo_ = new QComboBox(audioPage);
    audioOutputCombo_->addItem(tr("New Dedicated Insert"), -1);
    audioOutputCombo_->addItem(tr("Master (Direct)"), 0);
    for (std::size_t i = 1; i < mixer.size(); ++i) {
        const MixerTrack& mt = mixer.track(i);
        const QString label = mt.named ? tr("Insert %1 · %2").arg(i).arg(QString::fromStdString(mt.name))
                                       : tr("Insert %1").arg(i);
        audioOutputCombo_->addItem(label, static_cast<int>(i));
    }

    audioAscendingCheck_ = new QCheckBox(tr("Ascending"), audioPage);
    audioAscendingCheck_->setToolTip(tr("Route each added track to the next consecutive Mixer Insert"));

    audioOutRow->addWidget(audioOutputCombo_, 1);
    audioOutRow->addWidget(audioAscendingCheck_);
    audioGrid->addWidget(audioOutLbl, 2, 0);
    audioGrid->addLayout(audioOutRow, 2, 1);
    audioGrid->setRowStretch(3, 1);
    modeStack_->addWidget(audioPage);

    // ---- Page 1: Instrument Track Options ----
    auto* instPage = new QWidget(modeStack_);
    auto* instGrid = new QGridLayout(instPage);
    instGrid->setContentsMargins(0, 0, 0, 0);
    instGrid->setHorizontalSpacing(14);
    instGrid->setVerticalSpacing(8);
    instGrid->setColumnMinimumWidth(0, 82);
    instGrid->setColumnStretch(1, 1);

    auto* instOutLbl = new QLabel(tr("Output"), instPage);
    newInstRadio_ = new QRadioButton(tr("New Instrument"), instPage);
    newInstRadio_->setChecked(true);

    newInstCombo_ = new QComboBox(instPage);
    newInstCombo_->addItem(tr("None (Empty MIDI Channel)"), -1);

    // Populate instrument plugins: built-ins first (e.g. Synth), then scanned A-Z.
    std::vector<std::size_t> builtinInsts;
    std::vector<std::size_t> scannedInsts;
    for (std::size_t i = 0; i < plugins.plugins.size(); ++i) {
        const PluginInfo& info = plugins.plugins[i];
        if (!info.enabled || pluginMainGroup(info) != "Instrument")
            continue;
        if (info.format == PluginFormat::Builtin)
            builtinInsts.push_back(i);
        else
            scannedInsts.push_back(i);
    }
    const auto byPluginName = [&plugins](std::size_t a, std::size_t b) {
        return QString::fromStdString(plugins.plugins[a].name)
                   .compare(QString::fromStdString(plugins.plugins[b].name), Qt::CaseInsensitive)
            < 0;
    };
    std::sort(builtinInsts.begin(), builtinInsts.end(), byPluginName);
    std::sort(scannedInsts.begin(), scannedInsts.end(), byPluginName);

    int defaultInstComboIdx = 0;
    for (std::size_t idx : builtinInsts) {
        const PluginInfo& info = plugins.plugins[idx];
        newInstCombo_->addItem(
            tr("%1 (Built-in)").arg(QString::fromStdString(info.name)), static_cast<qulonglong>(idx));
        if (defaultInstComboIdx == 0)
            defaultInstComboIdx = newInstCombo_->count() - 1;
    }
    for (std::size_t idx : scannedInsts) {
        const PluginInfo& info = plugins.plugins[idx];
        const QString label = info.vendor.empty()
            ? QString::fromStdString(info.name)
            : QStringLiteral("%1 — %2").arg(
                  QString::fromStdString(info.name), QString::fromStdString(info.vendor));
        newInstCombo_->addItem(label, static_cast<qulonglong>(idx));
    }
    newInstCombo_->setCurrentIndex(defaultInstComboIdx);

    openEditorCheck_ = new QCheckBox(tr("Open Instrument Editor"), instPage);
    openEditorCheck_->setChecked(true);

    existingInstRadio_ = new QRadioButton(tr("Existing Instrument"), instPage);
    auto* existingRow = new QHBoxLayout;
    existingRow->setContentsMargins(0, 0, 0, 0);
    existingRow->setSpacing(10);

    existingInstCombo_ = new QComboBox(instPage);
    for (std::size_t c = 0; c < project.channels().size(); ++c) {
        const Channel& ch = project.channels()[c];
        if (!ch.samplePath.empty() && !ch.instrument.has_value())
            continue; // pure audio sample channel
        const QString suffix
            = ch.instrument.has_value() ? QString::fromStdString(ch.instrument->name) : tr("MIDI");
        existingInstCombo_->addItem(QStringLiteral("%1 · %2").arg(QString::fromStdString(ch.name), suffix),
            static_cast<qulonglong>(c));
    }
    const bool hasExistingInst = existingInstCombo_->count() > 0;
    if (!hasExistingInst) {
        existingInstCombo_->addItem(tr("None"), -1);
        existingInstRadio_->setEnabled(false);
    }

    instAscendingCheck_ = new QCheckBox(tr("Ascending"), instPage);
    instAscendingCheck_->setToolTip(tr("Assign each added track to the next existing instrument channel"));

    existingRow->addWidget(existingInstCombo_, 1);
    existingRow->addWidget(instAscendingCheck_);

    instGrid->addWidget(instOutLbl, 0, 0, Qt::AlignVCenter);
    instGrid->addWidget(newInstRadio_, 0, 1);
    instGrid->addWidget(newInstCombo_, 1, 1);
    instGrid->addWidget(openEditorCheck_, 2, 1);
    instGrid->addWidget(existingInstRadio_, 3, 1);
    instGrid->addLayout(existingRow, 4, 1);
    modeStack_->addWidget(instPage);

    cardLayout->addWidget(modeStack_);
    outer->addWidget(card);

    // Dialog footer: OK | Cancel
    auto* footerRow = new QHBoxLayout;
    footerRow->setContentsMargins(0, 0, 0, 0);
    footerRow->setSpacing(8);
    footerRow->addStretch(1);

    auto* okBtn = new QPushButton(tr("OK"), this);
    okBtn->setObjectName(QStringLiteral("accentButton"));
    okBtn->setDefault(true);
    okBtn->setMinimumWidth(88);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);

    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    cancelBtn->setMinimumWidth(88);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    footerRow->addWidget(okBtn);
    footerRow->addWidget(cancelBtn);
    outer->addLayout(footerRow);

    connect(countSpin_, &QSpinBox::valueChanged, this, [this] { updateControlStates(); });
    connect(audioOutputCombo_, &QComboBox::currentIndexChanged, this, [this] { updateControlStates(); });
    connect(newInstRadio_, &QRadioButton::toggled, this, [this] { updateControlStates(); });
    connect(newInstCombo_, &QComboBox::currentIndexChanged, this, [this] {
        updateControlStates();
        if (!nameEditedByUser_ && kind_ == AddTracksSpec::Kind::Instrument
            && !existingInstRadio_->isChecked()) {
            const QSignalBlocker block(nameEdit_);
            nameEdit_->setText(defaultBaseName(kind_));
        }
    });

    setKind(initialKind);
    updateColorSwatch();
    updateControlStates();
    nameEdit_->setFocus();
}

QString AddTracksDialog::defaultBaseName(AddTracksSpec::Kind kind) const
{
    const std::size_t num = project_.tracks().size() + 1;
    if (kind == AddTracksSpec::Kind::Instrument && newInstCombo_ != nullptr) {
        const QVariant itemData = newInstCombo_->currentData();
        if (itemData.isValid() && itemData.toLongLong() >= 0) {
            const auto idx = static_cast<std::size_t>(itemData.toLongLong());
            if (idx < plugins_.plugins.size()) {
                const QString pluginName = QString::fromStdString(plugins_.plugins[idx].name).trimmed();
                if (!pluginName.isEmpty())
                    return pluginName;
            }
        }
        return tr("Instrument %1").arg(num);
    }
    return tr("Audio %1").arg(num);
}

void AddTracksDialog::setKind(AddTracksSpec::Kind kind)
{
    kind_ = kind;
    {
        const QSignalBlocker b1(audioCardBtn_);
        const QSignalBlocker b2(instCardBtn_);
        audioCardBtn_->setChecked(kind_ == AddTracksSpec::Kind::Audio);
        instCardBtn_->setChecked(kind_ == AddTracksSpec::Kind::Instrument);
    }
    modeStack_->setCurrentIndex(static_cast<int>(kind_));
    if (!nameEditedByUser_) {
        const QSignalBlocker block(nameEdit_);
        nameEdit_->setText(defaultBaseName(kind_));
        nameEdit_->selectAll();
    }
    updateControlStates();
}

void AddTracksDialog::updateColorSwatch()
{
    const std::uint32_t rgb
        = autoColorCheck_->isChecked() ? trackPaletteColor(nextColorIndex_) : customColor_;
    const QColor col = toQColor(rgb);
    const QColor borderCol = col.lighter(125);
    colorSwatchBtn_->setStyleSheet(QStringLiteral("QPushButton#addTracksColorSwatch {"
                                                  "  background-color: %1;"
                                                  "  border: 1px solid %2;"
                                                  "  border-radius: 5px;"
                                                  "}")
            .arg(col.name(), borderCol.name()));
}

void AddTracksDialog::updateControlStates()
{
    const int count = countSpin_->value();
    const int outVal = audioOutputCombo_->currentData().toInt();
    audioAscendingCheck_->setEnabled(count > 1 && outVal > 0);

    const bool useNew = newInstRadio_->isChecked();
    newInstCombo_->setEnabled(useNew);
    const bool hasNewPlugin = useNew && newInstCombo_->currentData().toLongLong() >= 0;
    openEditorCheck_->setEnabled(hasNewPlugin);

    const bool useExisting = existingInstRadio_->isChecked() && existingInstRadio_->isEnabled();
    existingInstCombo_->setEnabled(useExisting);
    instAscendingCheck_->setEnabled(useExisting && count > 1 && existingInstCombo_->count() > 1);
}

AddTracksSpec AddTracksDialog::spec() const
{
    AddTracksSpec out;
    out.kind = kind_;
    out.baseName = nameEdit_->text().trimmed();
    out.count = countSpin_->value();
    out.packFolder = packFolderCheck_->isChecked();
    out.autoColor = autoColorCheck_->isChecked();
    out.customColor = customColor_;

    out.mono = formatCombo_->currentData().toBool();
    out.fxPreset = static_cast<AddTracksSpec::FxPreset>(fxChainCombo_->currentData().toInt());
    out.audioOutputInsert = audioOutputCombo_->currentData().toInt();
    out.audioOutputAscending = audioAscendingCheck_->isEnabled() && audioAscendingCheck_->isChecked();

    out.useExistingInstrument = existingInstRadio_->isChecked() && existingInstRadio_->isEnabled();
    if (!out.useExistingInstrument) {
        const long long pluginIdx = newInstCombo_->currentData().toLongLong();
        if (pluginIdx >= 0)
            out.newInstrumentPlugin = static_cast<std::size_t>(pluginIdx);
        out.openInstrumentEditor = openEditorCheck_->isEnabled() && openEditorCheck_->isChecked();
    } else {
        const long long chIdx = existingInstCombo_->currentData().toLongLong();
        if (chIdx >= 0)
            out.existingChannel = static_cast<std::size_t>(chIdx);
        out.instrumentAscending = instAscendingCheck_->isEnabled() && instAscendingCheck_->isChecked();
    }
    return out;
}

} // namespace vloomdaw
