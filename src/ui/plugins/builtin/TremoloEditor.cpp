// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/TremoloEditor.hpp"

#include "core/TimeEffects.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <vloom/dsp/NoteValue.hpp>
#include <vloom/dsp/effects/Tremolo.hpp>

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <utility>

namespace vloomdaw {

namespace {

namespace style = editor_style;
using Tremolo = vloom::dsp::Tremolo;

constexpr double kTop = 40.0; ///< 0 dB
constexpr double kBottomRoom = 36.0; ///< under silence, for the time labels
constexpr double kBadgeRadius = 9.0;

} // namespace

/// TremoloEditor's display: the gain of each side over time.
class GainDisplay : public QWidget {
public:
    struct State {
        bool autoPan = false;
        LfoWave wave = LfoWave::Sine;
        double depth = 0.5; ///< 0 .. 1
        double stereoPhase = 0.0; ///< cycles
        double rateHz = 4.0;
    };

    GainDisplay() { setAccessibleName(QObject::tr("Gain over time for the left and right channels")); }

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
        const double bottom = silence();
        const double window = lfoWindowSeconds(state_.rateHz);
        p.setFont(style::font(font(), 10));
        time_effects::paintTimeAxis(p, 0.0, width(), window, kTop, bottom, bottom + 16.0);
        p.setPen(QPen(style::line(), 1.0));
        p.drawLine(QPointF(0, kTop + 0.5), QPointF(width(), kTop + 0.5));
        p.setPen(QPen(style::grid(), 1.0));
        p.drawLine(QPointF(0, std::round((kTop + bottom) / 2.0) + 0.5),
            QPointF(width(), std::round((kTop + bottom) / 2.0) + 0.5));
        p.drawLine(QPointF(0, std::round(bottom) + 0.5), QPointF(width(), std::round(bottom) + 0.5));
        p.setPen(style::muted());
        p.drawText(QRectF(width() - 108, kTop - 16, 100, 14), Qt::AlignRight | Qt::AlignBottom,
            QStringLiteral("0 dB"));
        p.drawText(QRectF(width() - 108, bottom - 16, 100, 14), Qt::AlignRight | Qt::AlignBottom,
            QObject::tr("Silence"));

        p.setRenderHint(QPainter::Antialiasing);
        const auto trace = [&](bool right) {
            QPainterPath path;
            for (double x = 0.0; x <= width() + 2.0; x += 2.0) {
                const QPointF point(x, yAt(right, x, window));
                if (path.elementCount() == 0)
                    path.moveTo(point);
                else
                    path.lineTo(point);
            }
            return path;
        };
        const QColor leftColour = time_effects::voiceColour(0);
        const QColor rightColour = time_effects::voiceColour(2);
        const QPainterPath left = trace(false);
        QPainterPath area = left;
        area.lineTo(width() + 2.0, bottom);
        area.lineTo(0.0, bottom);
        area.closeSubpath();
        QColor fill = leftColour;
        fill.setAlphaF(0.1f);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawPath(area);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(rightColour, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(trace(true));
        p.setPen(QPen(leftColour, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(left);

        p.setFont(style::font(font(), 10, QFont::Bold));
        for (const bool right : {false, true}) {
            const double x = right ? 44.0 : 20.0;
            const QPointF centre(x, yAt(right, x, window));
            p.setPen(Qt::NoPen);
            p.setBrush(right ? rightColour : leftColour);
            p.drawEllipse(centre, kBadgeRadius, kBadgeRadius);
            p.setPen(style::background());
            p.drawText(QRectF(centre.x() - kBadgeRadius, centre.y() - kBadgeRadius, 2 * kBadgeRadius,
                           2 * kBadgeRadius),
                Qt::AlignCenter, right ? QStringLiteral("R") : QStringLiteral("L"));
        }
    }

private:
    [[nodiscard]] double silence() const
    {
        return std::max(kTop + 60.0, height() - time_effects::panelReach() - kBottomRoom);
    }
    [[nodiscard]] double yAt(bool right, double x, double window) const
    {
        const double phase = state_.rateHz * x / std::max(1.0, static_cast<double>(width())) * window;
        const auto [l, r]
            = tremoloGains(state_.autoPan, state_.wave, state_.depth, state_.stereoPhase, phase);
        return silence() - (right ? r : l) * (silence() - kTop);
    }

    State state_;
};

TremoloEditor::TremoloEditor(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate, QWidget* parent)
    : BuiltinEditorWindow(std::move(instance), title, sampleRate,
          {
              {tr("Gentle Pulse"), {{Tremolo::kRate, 3.0}, {Tremolo::kDepth, 30.0}}},
              {tr("Choppy Sixteenths"),
                  {{Tremolo::kSync, 1.0}, {Tremolo::kNote, vloom::dsp::note::kSixteenth},
                      {Tremolo::kShape, 2.0}, {Tremolo::kDepth, 100.0}}},
              {tr("Stereo Wobble"),
                  {{Tremolo::kRate, 5.0}, {Tremolo::kDepth, 60.0}, {Tremolo::kStereoPhase, 180.0}}},
              {tr("Slow Auto-Pan"),
                  {{Tremolo::kMode, 1.0}, {Tremolo::kSync, 1.0}, {Tremolo::kNote, vloom::dsp::note::kWhole},
                      {Tremolo::kDepth, 80.0}}},
              {tr("Fast Pan"),
                  {{Tremolo::kMode, 1.0}, {Tremolo::kRate, 8.0}, {Tremolo::kShape, 1.0},
                      {Tremolo::kDepth, 100.0}}},
          },
          parent)
{
    display_ = new GainDisplay;
    setDisplay(display_);

    auto* mode = new SegmentedChoice(tr("Mode"), {tr("Tremolo"), tr("Pan")});
    mode->setToolTip(tr("Tremolo moves the level; Pan moves the sound between left and right"));
    connect(mode, &SegmentedChoice::chosen, this, [this](int index) { change(Tremolo::kMode, index); });
    panel()->add(mode);
    bind(Tremolo::kMode, [mode](double v) { mode->setCurrent(v >= 0.5 ? 1 : 0); });
    panel()->addSpacing(8);

    ArcKnob* rate = knob(Tremolo::kRate);
    ArcKnob* note = knob(Tremolo::kNote, tr("Rate"));
    note->setAccessibleName(tr("Rate in notes"));
    panel()->add(rate);
    panel()->add(note);
    bind(Tremolo::kSync, [rate, note](double on) {
        rate->setVisible(on < 0.5);
        note->setVisible(on >= 0.5);
    });
    panel()->add(knob(Tremolo::kDepth));
    panel()->add(knob(Tremolo::kShape));
    ArcKnob* phase = knob(Tremolo::kStereoPhase);
    phase->setFormatter(time_effects::degreesText);
    panel()->add(phase);
    bind(Tremolo::kMode, [phase](double v) {
        const bool pan = v >= 0.5;
        phase->setEnabled(!pan);
        phase->setToolTip(pan ? tr("Pan does not use the stereo phase")
                              : tr("Puts the right side's swing this far after the left's"));
    });

    PillToggle* sync = toggle(Tremolo::kSync, tr("Sync"), PillToggle::Layout::Inline);
    sync->setToolTip(tr("Swing once per note at the song's tempo"));
    footer()->addLeft(sync);
    Readout* output = readout(tr("Output"));
    output_.emplace(output);
    footer()->addRight(output);

    refresh();
    startMetering();
}

void TremoloEditor::refresh()
{
    const bool synced = value(Tremolo::kSync) >= 0.5;
    const int shape = static_cast<int>(std::lround(value(Tremolo::kShape)));
    display_->setState(GainDisplay::State{.autoPan = value(Tremolo::kMode) >= 0.5,
        .wave = shape == 1 ? LfoWave::Triangle
            : shape == 2   ? LfoWave::Square
                           : LfoWave::Sine,
        .depth = value(Tremolo::kDepth) / 100.0,
        .stereoPhase = value(Tremolo::kStereoPhase) / 360.0,
        .rateHz = synced ? vloom::dsp::noteHz(static_cast<int>(value(Tremolo::kNote)), tempo())
                         : value(Tremolo::kRate)});
}

void TremoloEditor::parameterChanged(std::uint32_t /*id*/, double /*value*/) { refresh(); }

void TremoloEditor::tempoChanged(double /*bpm*/) { refresh(); }

void TremoloEditor::meter(const Meter& reading) { output_->push(reading.running, reading.outputDb); }

} // namespace vloomdaw
