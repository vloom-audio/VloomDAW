// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/CompressorEditor.hpp"

#include "core/DynamicsMeter.hpp"
#include "core/ParameterScale.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/LevelHistoryView.hpp"
#include "ui/plugins/builtin/MeterBars.hpp"
#include "ui/plugins/builtin/Readout.hpp"
#include "ui/plugins/builtin/TransferCurve.hpp"

#include <vloom/dsp/effects/Compressor.hpp>

#include <QHBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace vloomdaw {

namespace {

using Compressor = vloom::dsp::Compressor;

constexpr double kTopDb = 0.0;
constexpr double kBottomDb = -60.0;
constexpr double kReductionRangeDb = 24.0; ///< the GR meter's full height
constexpr int kInsetMargin = 16;

enum Bar : std::size_t { kIn, kGr, kOut };

QString dbText(double db) { return QString::fromStdString(formatParameterValue("dB", db)); }

} // namespace

CompressorEditor::CompressorEditor(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate, QWidget* parent)
    : BuiltinEditorWindow(std::move(instance), title, sampleRate,
          {
              {tr("Gentle Glue"),
                  {{Compressor::kThreshold, -20.0}, {Compressor::kRatio, 2.0}, {Compressor::kAttack, 30.0},
                      {Compressor::kRelease, 200.0}, {Compressor::kKnee, 10.0},
                      {Compressor::kAutoMakeup, 1.0}}},
              {tr("Vocal Leveler"),
                  {{Compressor::kThreshold, -24.0}, {Compressor::kRatio, 3.0}, {Compressor::kAttack, 5.0},
                      {Compressor::kRelease, 120.0}, {Compressor::kKnee, 6.0}, {Compressor::kDetector, 1.0},
                      {Compressor::kMakeup, 4.0}}},
              {tr("Drum Punch"),
                  {{Compressor::kThreshold, -18.0}, {Compressor::kRatio, 4.0}, {Compressor::kAttack, 25.0},
                      {Compressor::kRelease, 80.0}, {Compressor::kKnee, 3.0}, {Compressor::kMakeup, 3.0}}},
              {tr("Parallel Smash"),
                  {{Compressor::kThreshold, -36.0}, {Compressor::kRatio, 10.0}, {Compressor::kAttack, 1.0},
                      {Compressor::kRelease, 60.0}, {Compressor::kKnee, 0.0}, {Compressor::kMakeup, 10.0},
                      {Compressor::kMix, 40.0}}},
              {tr("Bass Control"),
                  {{Compressor::kThreshold, -22.0}, {Compressor::kRatio, 4.0}, {Compressor::kAttack, 15.0},
                      {Compressor::kRelease, 150.0}, {Compressor::kDetector, 1.0},
                      {Compressor::kMakeup, 2.0}}},
          },
          parent)
{
    // The display: the scrolling levels, the transfer curve over them and the meters.
    auto* display = new QWidget;
    auto* row = new QHBoxLayout(display);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    levels_ = new LevelHistoryView(LevelHistoryView::Look{}, display);
    levels_->setRange(kTopDb, kBottomDb, {-12.0, -24.0, -36.0, -48.0});
    levels_->setAccessibleName(tr("Levels: input, output and gain reduction, with the threshold"));
    levels_->onMarkerDragged = [this](double db) { change(Compressor::kThreshold, db); };
    levels_->onMarkerReset = [this] {
        if (const vloom::ParameterInfo* threshold = parameter(Compressor::kThreshold))
            change(Compressor::kThreshold, threshold->defaultValue);
    };
    row->addWidget(levels_, 1);
    meters_ = new MeterBars({{tr("In"), false, {}}, {tr("GR"), true, {}}, {tr("Out"), false, {}}}, kBottomDb,
        kReductionRangeDb, display);
    row->addWidget(meters_);
    curve_ = new TransferCurve(
        [this](double input) {
            return compressorOutputDb(input, value(Compressor::kThreshold), value(Compressor::kRatio),
                value(Compressor::kKnee), value(Compressor::kMakeup), value(Compressor::kAutoMakeup) >= 0.5);
        },
        kBottomDb, kTopDb, levels_);
    curve_->move(kInsetMargin, kInsetMargin);
    levels_->setMarkerLabelX(kInsetMargin + curve_->width() + 10); // clear of the inset
    curve_->setAccessibleName(tr("Transfer curve"));
    setDisplay(display);
    animate(levels_);
    animate(curve_);

    bind(Compressor::kThreshold, [this](double db) {
        levels_->setMarker(
            db, tr("Threshold %1").arg(QString::fromStdString(formatParameterValue("dB", db))));
    });

    panel()->add(knob(Compressor::kThreshold));
    panel()->add(knob(Compressor::kRatio));
    panel()->add(knob(Compressor::kKnee));
    panel()->add(knob(Compressor::kAttack));
    panel()->add(knob(Compressor::kRelease));
    panel()->add(knob(Compressor::kMakeup));
    panel()->add(knob(Compressor::kMix));
    panel()->add(toggle(Compressor::kAutoMakeup, tr("Auto makeup")));

    // The detector: its low cut is the sidechain filter (on the key, or on
    // the input when no key is routed in).
    auto* lowCut = toggle(Compressor::kSidechainFilter, tr("Detector low cut"), PillToggle::Layout::Inline);
    lowCut->setToolTip(tr("Filter the low end out of what the detector hears"));
    footer()->addLeft(lowCut);
    auto* lowCutFrequency = readout(Compressor::kSidechainFrequency);
    lowCutFrequency->setLabel({}); // reads after its switch
    lowCutFrequency->setAccessibleName(tr("Detector low cut frequency"));
    lowCutFrequency->setWidestText(QStringLiteral("500 Hz"));
    footer()->addLeft(lowCutFrequency);
    auto* listen = toggle(Compressor::kSidechainListen, tr("Listen"), PillToggle::Layout::Inline);
    listen->setToolTip(tr("Hear what the detector hears in place of the output"));
    footer()->addLeft(listen);
    auto* detector = readout(Compressor::kDetector, tr("Detector"));
    detector->setWidestText(QStringLiteral("Peak"));
    footer()->addLeft(detector);
    auto* link = readout(Compressor::kStereoLink, tr("Stereo link"));
    link->setWidestText(QStringLiteral("100 %"));
    footer()->addLeft(link);

    reduction_ = readout(tr("Gain reduction"));
    reduction_->setWidestText(dbText(-24.0));
    reduction_->setText(QStringLiteral("–"));
    footer()->addRight(reduction_);

    startMetering();
}

double CompressorEditor::makeupDb() const
{
    return compressorMakeupDb(value(Compressor::kThreshold), value(Compressor::kRatio),
        value(Compressor::kKnee), value(Compressor::kMakeup), value(Compressor::kAutoMakeup) >= 0.5);
}

void CompressorEditor::parameterChanged(std::uint32_t id, double /*value*/)
{
    switch (id) {
    case Compressor::kThreshold:
    case Compressor::kRatio:
    case Compressor::kKnee:
    case Compressor::kMakeup:
    case Compressor::kAutoMakeup:
        curve_->refresh();
        break;
    default:
        break;
    }
}

void CompressorEditor::meter(const Meter& reading)
{
    double input = kNoLevel;
    if (reading.running) {
        if (isBypassed())
            input = reading.outputDb; // the dry sound
        // Only the wet sound tells the input: no dry blended in, not the key.
        else if (value(Compressor::kMix) >= 100.0 && value(Compressor::kSidechainListen) < 0.5)
            input = inputFromOutputDb(reading.outputDb, reading.reductionDb, makeupDb());
    }
    levels_->push(input, reading.outputDb, reading.reductionDb);
    meters_->setValue(kIn, input);
    meters_->setValue(kGr, reading.reductionDb);
    meters_->setValue(kOut, reading.outputDb);
    curve_->setInput(input);
    // The input rose over the threshold: the line flashes.
    const double threshold = value(Compressor::kThreshold);
    if (std::isfinite(input) && std::isfinite(lastInput_) && lastInput_ < threshold && input >= threshold)
        levels_->pulseMarker(std::clamp((input - threshold) / 6.0 + 0.5, 0.5, 1.0));
    lastInput_ = input;
    reduction_->setText(
        std::isfinite(reading.reductionDb) ? dbText(-reading.reductionDb) : QStringLiteral("–"));
}

} // namespace vloomdaw
