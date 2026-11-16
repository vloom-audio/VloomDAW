// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/dialogs/SettingsDialog/StemSettingsPage.hpp"

#include "core/StemModels.hpp"
#include "ui/dialogs/SettingsDialog/SettingsCard.hpp"
#include "ui/widgets/ToggleSwitch/ToggleSwitch.hpp"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <string_view>

namespace vloomdaw {

namespace {

QString text(std::string_view s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }

} // namespace

StemSettingsPage::StemSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    model_ = new QComboBox;
    model_->setAccessibleName(tr("Stem model in use"));
    connect(model_, &QComboBox::currentIndexChanged, this, [this](int index) {
        describeModel();
        if (index >= 0 && !model_->itemData(index).toString().isEmpty())
            emit modelChosen(model_->itemData(index).toString());
    });

    auto* store = new QPushButton(tr("Open Model Store…"));
    store->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(store, &QPushButton::clicked, this, &StemSettingsPage::storeRequested);

    auto* modelCard = new SettingsCard(tr("Model"), this);
    modelAbout_ = modelCard->addRow(tr("Model in use"), QString(), model_);
    storeAbout_ = modelCard->addRow(tr("Model store"), QString(), store);

    // What follows applies to the models ONNX Runtime runs; demucs.cpp's
    // have neither a graphics-card path nor an overlap to set.
    bool anyGpu = false;
    bool anyOnnx = false;
    for (const StemModel& each : stemModels()) {
        anyGpu = anyGpu || each.gpu;
        anyOnnx = anyOnnx || each.engine == StemModel::Engine::Onnx;
    }
    SettingsCard* runCard = anyOnnx ? new SettingsCard(tr("Processing"), this) : nullptr;
    if (anyGpu) {
        gpu_ = new ToggleSwitch;
        gpu_->setAccessibleName(tr("Use the graphics card"));
        connect(gpu_, &ToggleSwitch::toggled, this, &StemSettingsPage::useGpuChanged);
        runCard->addRow(tr("Graphics card"),
            tr("Run the models that can on the graphics card: any DirectX 12 card (NVIDIA, AMD or Intel), "
               "several times faster. The processor takes over when the card cannot run a model or gives a "
               "wrong result."),
            gpu_);
    }
    if (anyOnnx) {
        overlap_ = new QComboBox;
        overlap_->setAccessibleName(tr("Overlap"));
        overlap_->addItem(tr("25% (fastest)"), 25);
        overlap_->addItem(tr("50% (1.5 times as long)"), 50);
        overlap_->addItem(tr("75% (3 times as long)"), 75);
        connect(overlap_, &QComboBox::currentIndexChanged, this,
            [this](int index) { emit overlapChanged(overlap_->itemData(index).toInt()); });
        runCard->addRow(tr("Overlap"),
            tr("The song goes through the model in pieces; this is how much of each is shared with the "
               "next. More gives smoother joins and takes longer. Not for the \"compact\" models."),
            overlap_);
        runCard->addRow(tr("Segment"),
            tr("Fixed by each model: 7.8 s for the Demucs models, 5.9 s for MDX-Net (11.9 s for "
               "De-Reverb). These model files take pieces of that length only."),
            nullptr);
    }

    mute_ = new ToggleSwitch;
    mute_->setAccessibleName(tr("Mute the original clip"));
    connect(mute_, &ToggleSwitch::toggled, this, &StemSettingsPage::muteOriginalChanged);
    auto* afterCard = new SettingsCard(tr("After extracting"), this);
    afterCard->addRow(tr("Mute the original clip"),
        tr("The stems are placed on new tracks under the clip and play in its place. Off: the clip keeps "
           "playing along with them."),
        mute_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    layout->addWidget(modelCard);
    if (runCard != nullptr)
        layout->addWidget(runCard);
    layout->addWidget(afterCard);
    layout->addStretch();

    setModels({}, QString());
}

void StemSettingsPage::setModels(const QStringList& installed, const QString& chosen)
{
    {
        const QSignalBlocker block(model_);
        model_->clear();
        for (const StemModel& each : stemModels())
            if (installed.contains(text(each.id)))
                model_->addItem(text(each.name), text(each.id));
        model_->setEnabled(model_->count() > 0);
        if (model_->count() == 0)
            model_->addItem(tr("None downloaded yet"), QString());
        // An unknown or missing choice shows as none picked rather than as the first model.
        model_->setCurrentIndex(model_->isEnabled() ? model_->findData(chosen) : 0);
    }
    storeAbout_->setText(tr("Download or remove models. %1 of %2 are on this computer.")
            .arg(model_->isEnabled() ? model_->count() : 0)
            .arg(stemModels().size()));
    storeAbout_->setVisible(true); // a row made without a description hides its label
    describeModel();
}

void StemSettingsPage::setUseGpu(bool on)
{
    if (gpu_ == nullptr)
        return;
    const QSignalBlocker block(gpu_);
    gpu_->setChecked(on);
}

void StemSettingsPage::setOverlap(int percent)
{
    if (overlap_ == nullptr)
        return;
    const QSignalBlocker block(overlap_);
    overlap_->setCurrentIndex(percent >= 63 ? 2 : percent >= 38 ? 1 : 0);
}

void StemSettingsPage::setMuteOriginal(bool on)
{
    const QSignalBlocker block(mute_);
    mute_->setChecked(on);
}

void StemSettingsPage::describeModel()
{
    const StemModel* model = model_->currentIndex() >= 0
        ? findStemModel(model_->currentData().toString().toStdString())
        : nullptr;
    QString about;
    if (!model_->isEnabled())
        about = tr("Open the model store to download one.");
    else if (model == nullptr)
        about = tr("Pick the model the stem extractor uses.");
    else {
        QStringList stems;
        for (const std::string_view stem : model->stems)
            stems << text(stem);
        about = tr("%1 Gives: %2.").arg(text(model->about), stems.join(QStringLiteral(", ")));
    }
    modelAbout_->setText(about);
    modelAbout_->setVisible(true);
}

} // namespace vloomdaw
