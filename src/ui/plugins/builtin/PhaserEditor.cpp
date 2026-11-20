// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/PhaserEditor.hpp"

#include "core/ParameterScale.hpp"
#include "core/TimeEffects.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <vloom/dsp/NoteValue.hpp>
#include <vloom/dsp/effects/Phaser.hpp>

#include <cmath>
#include <numbers>
#include <utility>

namespace vloomdaw {

namespace {

using Phaser = vloom::dsp::Phaser;

ResponseDisplay::Axis spectrumAxis()
{
    return ResponseDisplay::Axis{.log = true,
        .fromHz = 20.0,
        .toHz = 20000.0,
        .grid = {{50.0, QStringLiteral("50")}, {100.0, QStringLiteral("100")}, {200.0, QStringLiteral("200")},
            {500.0, QStringLiteral("500")}, {1000.0, QStringLiteral("1k")}, {2000.0, QStringLiteral("2k")},
            {5000.0, QStringLiteral("5k")}, {10000.0, QStringLiteral("10k")}}};
}

} // namespace

PhaserEditor::PhaserEditor(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate, QWidget* parent)
    : BuiltinEditorWindow(std::move(instance), title, sampleRate,
          {
              {tr("Slow Swirl"),
                  {{Phaser::kRate, 0.15}, {Phaser::kDepth, 90.0}, {Phaser::kStages, 8.0},
                      {Phaser::kFrequency, 600.0}, {Phaser::kFeedback, 50.0}}},
              {tr("Deep Twelve"),
                  {{Phaser::kRate, 0.25}, {Phaser::kDepth, 80.0}, {Phaser::kStages, 12.0},
                      {Phaser::kFeedback, 70.0}}},
              {tr("Funky Quarter"),
                  {{Phaser::kSync, 1.0}, {Phaser::kNote, vloom::dsp::note::kQuarter}, {Phaser::kStages, 6.0},
                      {Phaser::kFrequency, 1200.0}, {Phaser::kFeedback, 40.0}}},
              {tr("Subtle"),
                  {{Phaser::kRate, 0.3}, {Phaser::kDepth, 40.0}, {Phaser::kStages, 2.0},
                      {Phaser::kFeedback, 0.0}, {Phaser::kMix, 35.0}}},
              {tr("Wide Stereo"),
                  {{Phaser::kRate, 0.4}, {Phaser::kStages, 6.0}, {Phaser::kStereoPhase, 180.0}}},
          },
          parent)
{
    display_ = new ResponseDisplay(spectrumAxis());
    display_->setAccessibleName(tr("Phaser response: the notches and where they sweep to"));
    display_->onHandleDragged
        = [this](QPointF point) { change(Phaser::kFrequency, display_->hzAt(point.x())); };
    display_->onHandleReset = [this] {
        if (const vloom::ParameterInfo* centre = parameter(Phaser::kFrequency))
            change(Phaser::kFrequency, centre->defaultValue);
    };
    setDisplay(display_);

    ArcKnob* rate = knob(Phaser::kRate);
    ArcKnob* note = knob(Phaser::kNote, tr("Rate"));
    note->setAccessibleName(tr("Rate in notes"));
    panel()->add(rate);
    panel()->add(note);
    bind(Phaser::kSync, [rate, note](double on) {
        rate->setVisible(on < 0.5);
        note->setVisible(on >= 0.5);
    });
    panel()->add(knob(Phaser::kDepth));
    panel()->add(knob(Phaser::kFrequency, tr("Centre")));
    ArcKnob* feedback = knob(Phaser::kFeedback);
    feedback->setFormatter(time_effects::signedPercentText);
    panel()->add(feedback);
    ArcKnob* phase = knob(Phaser::kStereoPhase);
    phase->setFormatter(time_effects::degreesText);
    phase->setToolTip(tr("Puts the right side's sweep this far after the left's"));
    panel()->add(phase);
    panel()->add(knob(Phaser::kMix));

    Readout* stages = readout(Phaser::kStages);
    stages->setWidestText(QStringLiteral("12"));
    stages->setToolTip(tr("All-pass stages: every two make a notch (an odd count rounds up)"));
    footer()->addLeft(stages);
    PillToggle* sync = toggle(Phaser::kSync, tr("LFO sync"), PillToggle::Layout::Inline);
    sync->setToolTip(tr("Sweep once per note at the song's tempo"));
    footer()->addLeft(sync);
    Readout* output = readout(tr("Output"));
    output_.emplace(output);
    footer()->addRight(output);

    refresh();
    animate(display_);
}

void PhaserEditor::refresh()
{
    const double centre = value(Phaser::kFrequency);
    // An odd count rounds up (dsp::Phaser).
    const int stages = 2 * static_cast<int>(std::ceil(value(Phaser::kStages) / 2.0));
    const double feedback = value(Phaser::kFeedback) / 100.0;
    const double mix = value(Phaser::kMix) / 100.0;
    const double rate = sampleRate();
    const auto at = [stages, feedback, mix, rate](double hz) {
        return [hz, stages, feedback, mix, rate](
                   double f) { return phaserResponseDb(f, hz, stages, feedback, mix, rate); };
    };
    const auto [low, high] = phaserSweepHz(centre, value(Phaser::kDepth) / 100.0, rate);
    std::vector<ResponseDisplay::Response> sweeps;
    if (high > low * 1.001) {
        sweeps.emplace_back(at(low));
        sweeps.emplace_back(at(high));
    }
    display_->setCurves(at(centre), std::move(sweeps), centre,
        tr("Centre %1").arg(QString::fromStdString(formatParameterValue("Hz", centre))));
    // dsp::Phaser: the notches' frequency moves sin(2 pi phase) of the sweep
    // in octaves about the centre (to the ends drawn dashed), the LFO
    // free-running at the rate.
    const double down = std::log2(low / centre);
    const double up = std::log2(high / centre);
    display_->setMotion(
        [centre, down, up, stages, feedback, mix, rate](double hz, double amount) {
            const double octaves = amount >= 0.0 ? amount * up : -amount * down;
            return phaserResponseDb(hz, centre * std::exp2(octaves), stages, feedback, mix, rate);
        },
        [](double phase) { return std::sin(2.0 * std::numbers::pi * phase); }, lfoHz());
}

double PhaserEditor::lfoHz() const
{
    return value(Phaser::kSync) >= 0.5 ? vloom::dsp::noteHz(static_cast<int>(value(Phaser::kNote)), tempo())
                                       : value(Phaser::kRate);
}

void PhaserEditor::parameterChanged(std::uint32_t /*id*/, double /*value*/) { refresh(); }

void PhaserEditor::tempoChanged(double /*bpm*/) { refresh(); }

void PhaserEditor::meter(const Meter& reading) { output_->push(reading.running, reading.outputDb); }

} // namespace vloomdaw
