// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/FlangerEditor.hpp"

#include "core/ParameterScale.hpp"
#include "core/TimeEffects.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <vloom/dsp/NoteValue.hpp>
#include <vloom/dsp/effects/Flanger.hpp>

#include <algorithm>
#include <utility>

namespace vloomdaw {

namespace {

using Flanger = vloom::dsp::Flanger;

constexpr double kTopHz = 8000.0; ///< the display's right end

ResponseDisplay::Axis combAxis()
{
    ResponseDisplay::Axis axis{.log = false, .fromHz = 0.0, .toHz = kTopHz, .grid = {}};
    for (int k = 1; k <= 7; ++k)
        axis.grid.emplace_back(1000.0 * k, QStringLiteral("%1k").arg(k));
    return axis;
}

} // namespace

FlangerEditor::FlangerEditor(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate, QWidget* parent)
    : BuiltinEditorWindow(std::move(instance), title, sampleRate,
          {
              {tr("Jet Sweep"),
                  {{Flanger::kRate, 0.1}, {Flanger::kDepth, 90.0}, {Flanger::kDelay, 1.0},
                      {Flanger::kFeedback, 75.0}}},
              {tr("Subtle Motion"),
                  {{Flanger::kRate, 0.3}, {Flanger::kDepth, 40.0}, {Flanger::kDelay, 2.0},
                      {Flanger::kFeedback, 20.0}, {Flanger::kMix, 35.0}}},
              {tr("Metallic"),
                  {{Flanger::kRate, 0.5}, {Flanger::kDepth, 30.0}, {Flanger::kDelay, 0.5},
                      {Flanger::kFeedback, 90.0}}},
              {tr("Hollow"),
                  {{Flanger::kRate, 0.2}, {Flanger::kDepth, 70.0}, {Flanger::kDelay, 1.5},
                      {Flanger::kFeedback, -70.0}}},
              {tr("Synced Bar"),
                  {{Flanger::kSync, 1.0}, {Flanger::kNote, vloom::dsp::note::kWhole}, {Flanger::kDepth, 80.0},
                      {Flanger::kFeedback, 60.0}}},
          },
          parent)
{
    display_ = new ResponseDisplay(combAxis());
    display_->setAccessibleName(tr("Flanger response: the comb of notches and where it sweeps to"));
    display_->onHandleDragged = [this](QPointF point) {
        change(Flanger::kDelay, 500.0 / std::max(display_->hzAt(point.x()), 1.0));
    };
    display_->onHandleReset = [this] {
        if (const vloom::ParameterInfo* delay = parameter(Flanger::kDelay))
            change(Flanger::kDelay, delay->defaultValue);
    };
    setDisplay(display_);

    ArcKnob* rate = knob(Flanger::kRate);
    ArcKnob* note = knob(Flanger::kNote, tr("Rate"));
    note->setAccessibleName(tr("Rate in notes"));
    panel()->add(rate);
    panel()->add(note);
    bind(Flanger::kSync, [rate, note](double on) {
        rate->setVisible(on < 0.5);
        note->setVisible(on >= 0.5);
    });
    panel()->add(knob(Flanger::kDepth));
    panel()->add(knob(Flanger::kDelay));
    ArcKnob* feedback = knob(Flanger::kFeedback);
    feedback->setFormatter(time_effects::signedPercentText);
    panel()->add(feedback);
    ArcKnob* phase = knob(Flanger::kStereoPhase);
    phase->setFormatter(time_effects::degreesText);
    phase->setToolTip(tr("Puts the right side's sweep this far after the left's"));
    panel()->add(phase);
    panel()->add(knob(Flanger::kMix));

    PillToggle* sync = toggle(Flanger::kSync, tr("LFO sync"), PillToggle::Layout::Inline);
    sync->setToolTip(tr("Sweep once per note at the song's tempo"));
    footer()->addLeft(sync);
    Readout* output = readout(tr("Output"));
    output_.emplace(output);
    footer()->addRight(output);

    refresh();
    startMetering();
}

void FlangerEditor::refresh()
{
    const double delay = value(Flanger::kDelay);
    const double deepest = delay + value(Flanger::kDepth) / 100.0 * Flanger::kMaxSwingMs;
    const double feedback = value(Flanger::kFeedback) / 100.0;
    const double mix = value(Flanger::kMix) / 100.0;
    const double rate = sampleRate();
    const auto at = [feedback, mix, rate](double ms) {
        return
            [ms, feedback, mix, rate](double hz) { return flangerResponseDb(hz, ms, feedback, mix, rate); };
    };
    std::vector<ResponseDisplay::Response> sweeps;
    if (deepest > delay + 1e-6)
        sweeps.emplace_back(at(deepest));
    const double notch = flangerNotchHz(delay);
    display_->setCurves(at(delay), std::move(sweeps),
        notch <= kTopHz ? std::optional<double>(notch) : std::nullopt,
        tr("Notch %1").arg(QString::fromStdString(formatParameterValue("Hz", notch))));
}

void FlangerEditor::parameterChanged(std::uint32_t /*id*/, double /*value*/) { refresh(); }

void FlangerEditor::meter(const Meter& reading) { output_->push(reading.running, reading.outputDb); }

} // namespace vloomdaw
