// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/LimiterEditor.hpp"

#include "core/DynamicsMeter.hpp"
#include "core/ParameterScale.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/LevelHistoryView.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <vloom/dsp/effects/Limiter.hpp>
#include <vloom/engine/plugin/PluginInstance.hpp>

#include <cmath>
#include <utility>

namespace vloomdaw {

namespace {

using Limiter = vloom::dsp::Limiter;

constexpr double kTopDb = 0.0;
constexpr double kBottomDb = -36.0;

QString dbText(double db) { return QString::fromStdString(formatParameterValue("dB", db)); }

} // namespace

LimiterEditor::LimiterEditor(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate, QWidget* parent)
    : BuiltinEditorWindow(std::move(instance), title, sampleRate,
          {
              {tr("Transparent"),
                  {{Limiter::kRelease, 120.0}, {Limiter::kLookahead, 5.0}, {Limiter::kCeiling, -1.0}}},
              {tr("Loud Master"),
                  {{Limiter::kInputGain, 6.0}, {Limiter::kRelease, 50.0}, {Limiter::kLookahead, 3.0},
                      {Limiter::kCeiling, -0.3}}},
              {tr("Streaming (-1 dBTP)"),
                  {{Limiter::kInputGain, 3.0}, {Limiter::kRelease, 80.0}, {Limiter::kTruePeak, 1.0},
                      {Limiter::kCeiling, -1.0}}},
              {tr("Fast Catch"),
                  {{Limiter::kRelease, 10.0}, {Limiter::kLookahead, 0.5}, {Limiter::kTruePeak, 0.0},
                      {Limiter::kCeiling, -0.1}}},
          },
          parent)
{
    levels_ = new LevelHistoryView(
        LevelHistoryView::Look{.inputArea = true, .outputArea = true, .outputLine = true, .reduction = true});
    levels_->setRange(kTopDb, kBottomDb, {-6.0, -12.0, -18.0, -24.0});
    levels_->setAccessibleName(tr("Levels: input, limited output and gain reduction, with the ceiling"));
    levels_->onMarkerDragged = [this](double db) { change(Limiter::kCeiling, db); };
    levels_->onMarkerReset = [this] {
        if (const vloom::ParameterInfo* ceiling = parameter(Limiter::kCeiling))
            change(Limiter::kCeiling, ceiling->defaultValue);
    };
    setDisplay(levels_);
    bind(Limiter::kCeiling, [this](double db) { levels_->setMarker(db, tr("Ceiling %1").arg(dbText(db))); });

    panel()->add(knob(Limiter::kInputGain, tr("Gain")));
    panel()->add(knob(Limiter::kCeiling));
    panel()->add(knob(Limiter::kRelease));
    panel()->add(knob(Limiter::kLookahead));
    panel()->add(toggle(Limiter::kTruePeak, tr("True peak")));

    latency_ = readout(tr("Latency"));
    latency_->setWidestText(QStringLiteral("10.0 ms"));
    latency_->setToolTip(tr("How long the limiter delays the sound (the project makes up for it)"));
    footer()->addLeft(latency_);
    auto* link = readout(Limiter::kStereoLink, tr("Stereo link"));
    link->setWidestText(QStringLiteral("100 %"));
    footer()->addLeft(link);

    reduction_ = readout(tr("Gain reduction"));
    reduction_->setWidestText(dbText(-24.0));
    reduction_->setText(QStringLiteral("–"));
    footer()->addRight(reduction_);

    showLatency();
    startMetering();
}

void LimiterEditor::showLatency()
{
    // What the engine compensates: the plugin's latency as last read (0
    // while the plugin is not running).
    const double ms = 1000.0 * instance().latencyFrames() / sampleRate();
    latency_->setText(QString::fromStdString(formatParameterValue("ms", ms)));
}

void LimiterEditor::parameterChanged(std::uint32_t id, double /*value*/)
{
    if (id == Limiter::kLookahead || id == Limiter::kTruePeak)
        showLatency(); // the engine reads it again soon: meter() follows
}

void LimiterEditor::meter(const Meter& reading)
{
    double input = kNoLevel;
    if (reading.running)
        input = isBypassed()
            ? reading.outputDb
            : inputFromOutputDb(reading.outputDb, reading.reductionDb, value(Limiter::kInputGain));
    levels_->push(input, reading.outputDb, reading.reductionDb);
    reduction_->setText(
        std::isfinite(reading.reductionDb) ? dbText(-reading.reductionDb) : QStringLiteral("–"));
    showLatency();
}

} // namespace vloomdaw
