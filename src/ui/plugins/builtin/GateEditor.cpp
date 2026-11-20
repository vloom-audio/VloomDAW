// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/GateEditor.hpp"

#include "core/DynamicsMeter.hpp"
#include "core/ParameterScale.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/LevelHistoryView.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <vloom/dsp/effects/Gate.hpp>

#include <cmath>
#include <utility>

namespace vloomdaw {

namespace {

using Gate = vloom::dsp::Gate;

constexpr double kTopDb = 0.0;
constexpr double kBottomDb = Gate::kMinRangeDb;
/// Turned down less than this, the gate reads as open.
constexpr double kOpenBelowDb = 1.0;

QString dbText(double db) { return QString::fromStdString(formatParameterValue("dB", db)); }

} // namespace

GateEditor::GateEditor(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate, QWidget* parent)
    : BuiltinEditorWindow(std::move(instance), title, sampleRate,
          {
              {tr("Drum Tight"),
                  {{Gate::kThreshold, -30.0}, {Gate::kAttack, 0.2}, {Gate::kHold, 30.0},
                      {Gate::kRelease, 80.0}}},
              {tr("Vocal Clean"),
                  {{Gate::kThreshold, -45.0}, {Gate::kHysteresis, 6.0}, {Gate::kRange, -20.0},
                      {Gate::kAttack, 2.0}, {Gate::kHold, 80.0}, {Gate::kRelease, 200.0}}},
              {tr("Gentle Expander"),
                  {{Gate::kThreshold, -40.0}, {Gate::kRange, -10.0}, {Gate::kAttack, 5.0},
                      {Gate::kRelease, 300.0}}},
              {tr("Kick Only"),
                  {{Gate::kThreshold, -24.0}, {Gate::kAttack, 0.1}, {Gate::kHold, 20.0},
                      {Gate::kRelease, 60.0}, {Gate::kSidechainFilter, 1.0},
                      {Gate::kSidechainFrequency, 60.0}}},
          },
          parent)
{
    levels_ = new LevelHistoryView(LevelHistoryView::Look{
        .inputArea = true, .outputArea = true, .outputLine = true, .outputAccent = true, .reduction = false});
    levels_->setRange(kTopDb, kBottomDb, {-12.0, -24.0, -36.0, -48.0, -60.0, -72.0});
    levels_->setAccessibleName(tr("Levels: the input, what the gate lets through, and the threshold"));
    levels_->onMarkerDragged = [this](double db) { change(Gate::kThreshold, db); };
    levels_->onMarkerReset = [this] {
        if (const vloom::ParameterInfo* threshold = parameter(Gate::kThreshold))
            change(Gate::kThreshold, threshold->defaultValue);
    };
    setDisplay(levels_);
    bind(Gate::kThreshold, [this](double db) { levels_->setMarker(db, tr("Threshold %1").arg(dbText(db))); });

    panel()->add(knob(Gate::kThreshold));
    ArcKnob* range = knob(Gate::kRange);
    // At its bottom the gate closes to silence.
    range->setFormatter(
        [](double db) { return db <= Gate::kMinRangeDb ? QStringLiteral("−inf dB") : dbText(db); });
    panel()->add(range);
    panel()->add(knob(Gate::kAttack));
    panel()->add(knob(Gate::kHold));
    panel()->add(knob(Gate::kRelease));
    panel()->add(knob(Gate::kHysteresis));

    auto* lowCut = toggle(Gate::kSidechainFilter, tr("Detector low cut"), PillToggle::Layout::Inline);
    lowCut->setToolTip(tr("Filter the low end out of what opens the gate"));
    footer()->addLeft(lowCut);
    auto* lowCutFrequency = readout(Gate::kSidechainFrequency);
    lowCutFrequency->setLabel({}); // reads after its switch
    lowCutFrequency->setAccessibleName(tr("Detector low cut frequency"));
    lowCutFrequency->setWidestText(QStringLiteral("2.00 kHz"));
    footer()->addLeft(lowCutFrequency);
    auto* listen = toggle(Gate::kSidechainListen, tr("Listen"), PillToggle::Layout::Inline);
    listen->setToolTip(tr("Hear what opens the gate in place of the output"));
    footer()->addLeft(listen);

    state_ = readout(tr("Gate"));
    state_->setWidestText(tr("Closed"));
    state_->setText(QStringLiteral("–"));
    state_->setToolTip(tr("Open while the gate turns the sound down by less than 1 dB"));
    footer()->addRight(state_);

    startMetering();
}

void GateEditor::meter(const Meter& reading)
{
    double input = kNoLevel;
    if (reading.running && value(Gate::kSidechainListen) < 0.5)
        input
            = isBypassed() ? reading.outputDb : inputFromOutputDb(reading.outputDb, reading.reductionDb, 0.0);
    levels_->push(input, reading.outputDb, kNoLevel);
    if (!std::isfinite(reading.reductionDb))
        state_->setText(QStringLiteral("–"));
    else
        state_->setText(reading.reductionDb < kOpenBelowDb ? tr("Open") : tr("Closed"));
}

} // namespace vloomdaw
