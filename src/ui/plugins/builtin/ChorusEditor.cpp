// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/ChorusEditor.hpp"

#include "core/ParameterScale.hpp"
#include "core/TimeEffects.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <vloom/dsp/NoteValue.hpp>
#include <vloom/dsp/effects/Chorus.hpp>

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <utility>

namespace vloomdaw {

namespace {

namespace style = editor_style;
using Chorus = vloom::dsp::Chorus;

constexpr double kTop = 34.0;
constexpr double kBottomRoom = 36.0; ///< under the plot, for the time labels
constexpr double kBadgeRadius = 9.0;
constexpr double kMsAbove = 1.0; ///< shown over the deepest sweep
constexpr double kMsBelow = 1.0; ///< shown under the delay

QString msText(double ms) { return QString::fromStdString(formatParameterValue("ms", ms)); }

} // namespace

/// ChorusEditor's display: each voice's delay over time.
class VoiceDisplay : public QWidget {
public:
    struct State {
        double delayMs = 10.0;
        double depth = 0.5; ///< 0 .. 1
        int voices = 2;
        double rateHz = 0.8;
    };

    VoiceDisplay()
    {
        setAccessibleName(QObject::tr("Voices: the delay time of each voice swinging over time"));
    }

    void setState(const State& state)
    {
        state_ = state;
        update();
    }

    [[nodiscard]] QSize minimumSizeHint() const override { return {320, 200}; }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter p(this);
        p.fillRect(rect(), style::background());
        const double bottom = plotBottom();
        const double window = lfoWindowSeconds(state_.rateHz);
        p.setFont(style::font(font(), 10));
        time_effects::paintTimeAxis(p, 0.0, width(), window, kTop - 12.0, bottom, bottom + 16.0);

        // The delay the voices start from and how far they reach.
        const double swing = Chorus::kMaxSwingMs * state_.depth;
        const double low = std::round(yOf(state_.delayMs)) + 0.5;
        const double high = std::round(yOf(state_.delayMs + swing)) + 0.5;
        p.setPen(QPen(style::line(), 1.0));
        p.drawLine(QPointF(0, low), QPointF(width(), low));
        p.setPen(style::muted());
        p.drawText(QRectF(width() - 208, low + 2, 200, 14), Qt::AlignRight | Qt::AlignTop,
            QObject::tr("Delay %1").arg(msText(state_.delayMs)));
        if (swing > 0.05) {
            p.setPen(QPen(style::grid(), 1.0));
            p.drawLine(QPointF(0, high), QPointF(width(), high));
            p.setPen(style::muted());
            p.drawText(QRectF(width() - 208, high - 16, 200, 14), Qt::AlignRight | Qt::AlignBottom,
                msText(state_.delayMs + swing));
        }

        p.setRenderHint(QPainter::Antialiasing);
        const int voices = std::clamp(state_.voices, 1, Chorus::kMaxVoices);
        // The last voice under the first.
        for (int v = voices - 1; v >= 0; --v) {
            QPainterPath path;
            for (double x = 0.0; x <= width() + 2.0; x += 2.0) {
                const QPointF point(x, yAt(v, x, window));
                if (path.elementCount() == 0)
                    path.moveTo(point);
                else
                    path.lineTo(point);
            }
            p.setPen(QPen(time_effects::voiceColour(v), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        }
        p.setFont(style::font(font(), 10, QFont::Bold));
        for (int v = 0; v < voices; ++v) {
            const double x = 20.0 + 24.0 * v;
            const QPointF centre(x, yAt(v, x, window));
            p.setPen(Qt::NoPen);
            p.setBrush(time_effects::voiceColour(v));
            p.drawEllipse(centre, kBadgeRadius, kBadgeRadius);
            p.setPen(style::background());
            p.drawText(QRectF(centre.x() - kBadgeRadius, centre.y() - kBadgeRadius, 2 * kBadgeRadius,
                           2 * kBadgeRadius),
                Qt::AlignCenter, QString::number(v + 1));
        }
    }

private:
    [[nodiscard]] double plotBottom() const
    {
        return std::max(kTop + 60.0, height() - time_effects::panelReach() - kBottomRoom);
    }
    /// Milliseconds from the delay's to the deepest sweep's, with a margin.
    [[nodiscard]] double yOf(double ms) const
    {
        const double from = state_.delayMs - kMsBelow;
        const double to = state_.delayMs + Chorus::kMaxSwingMs + kMsAbove;
        return plotBottom() - (ms - from) / (to - from) * (plotBottom() - kTop);
    }
    [[nodiscard]] double yAt(int voice, double x, double window) const
    {
        const double phase = state_.rateHz * x / std::max(1.0, static_cast<double>(width())) * window;
        return yOf(chorusDelayMs(state_.delayMs, state_.depth, voice, phase));
    }

    State state_;
};

ChorusEditor::ChorusEditor(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate, QWidget* parent)
    : BuiltinEditorWindow(std::move(instance), title, sampleRate,
          {
              {tr("Subtle Double"),
                  {{Chorus::kRate, 0.5}, {Chorus::kDepth, 25.0}, {Chorus::kDelay, 12.0},
                      {Chorus::kMix, 35.0}}},
              {tr("Lush Ensemble"),
                  {{Chorus::kRate, 0.35}, {Chorus::kDepth, 70.0}, {Chorus::kDelay, 18.0},
                      {Chorus::kVoices, 4.0}, {Chorus::kSpread, 100.0}}},
              {tr("Vibrato"),
                  {{Chorus::kRate, 5.0}, {Chorus::kDepth, 30.0}, {Chorus::kDelay, 3.0},
                      {Chorus::kVoices, 1.0}, {Chorus::kSpread, 0.0}, {Chorus::kMix, 100.0}}},
              {tr("Synced Shimmer"),
                  {{Chorus::kSync, 1.0}, {Chorus::kNote, vloom::dsp::note::kWhole}, {Chorus::kDepth, 60.0},
                      {Chorus::kVoices, 3.0}, {Chorus::kFeedback, 30.0}, {Chorus::kMix, 45.0}}},
          },
          parent)
{
    display_ = new VoiceDisplay;
    setDisplay(display_);

    ArcKnob* rate = knob(Chorus::kRate);
    ArcKnob* note = knob(Chorus::kNote, tr("Rate"));
    note->setAccessibleName(tr("Rate in notes"));
    panel()->add(rate);
    panel()->add(note);
    bind(Chorus::kSync, [rate, note](double on) {
        rate->setVisible(on < 0.5);
        note->setVisible(on >= 0.5);
    });
    panel()->add(knob(Chorus::kDepth));
    panel()->add(knob(Chorus::kDelay));
    panel()->add(knob(Chorus::kFeedback));
    panel()->add(knob(Chorus::kWidth));
    panel()->add(knob(Chorus::kMix));

    Readout* voices = readout(Chorus::kVoices);
    voices->setWidestText(QStringLiteral("4"));
    voices->setToolTip(tr("How many copies of the sound sweep (drag or scroll)"));
    footer()->addLeft(voices);
    Readout* spread = readout(Chorus::kSpread);
    spread->setWidestText(QStringLiteral("100 %"));
    spread->setToolTip(tr("Puts the right side's sweep up to a quarter cycle after the left's"));
    footer()->addLeft(spread);
    PillToggle* sync = toggle(Chorus::kSync, tr("LFO sync"), PillToggle::Layout::Inline);
    sync->setToolTip(tr("Sweep once per note at the song's tempo"));
    footer()->addLeft(sync);
    Readout* output = readout(tr("Output"));
    output_.emplace(output);
    footer()->addRight(output);

    refresh();
    startMetering();
}

void ChorusEditor::refresh()
{
    const bool synced = value(Chorus::kSync) >= 0.5;
    display_->setState(VoiceDisplay::State{.delayMs = value(Chorus::kDelay),
        .depth = value(Chorus::kDepth) / 100.0,
        .voices = static_cast<int>(std::lround(value(Chorus::kVoices))),
        .rateHz = synced ? vloom::dsp::noteHz(static_cast<int>(value(Chorus::kNote)), tempo())
                         : value(Chorus::kRate)});
}

void ChorusEditor::parameterChanged(std::uint32_t /*id*/, double /*value*/) { refresh(); }

void ChorusEditor::tempoChanged(double /*bpm*/) { refresh(); }

void ChorusEditor::meter(const Meter& reading) { output_->push(reading.running, reading.outputDb); }

} // namespace vloomdaw
