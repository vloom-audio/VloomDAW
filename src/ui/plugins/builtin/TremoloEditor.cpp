// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/TremoloEditor.hpp"

#include "core/TimeEffects.hpp"
#include "ui/plugins/builtin/AnimationPaint.hpp"
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
#include <cstdint>
#include <utility>
#include <vector>

namespace vloomdaw {

namespace {

namespace style = editor_style;
using Tremolo = vloom::dsp::Tremolo;

constexpr double kTop = 40.0; ///< 0 dB
constexpr double kBottomRoom = 36.0; ///< under silence, for the time labels
constexpr double kBadgeRadius = 9.0;
constexpr double kPlayhead = 0.62; ///< where "now" is while it moves, of the width

} // namespace

/// TremoloEditor's display: the gain of each side over time.
///
/// Animated: the gain curves scroll past a playhead ("now") at the LFO's
/// real rate (the note at the song's tempo while synced). The playhead
/// glows with the sound, a glow on each side's curve as bright as the gain
/// it gives now (in Pan, left and right take turns), and behind it the
/// output's level trails off to the left at the same speed, so the sound's
/// own pulsing lines up with the curve that made it. The plugin does not
/// report its LFO's phase: the display runs its own at the same rate (the
/// plugin's runs free too, so only where in the cycle it is can differ).
class GainDisplay : public QWidget, public Animated {
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
        ++key_;
        update();
    }

    [[nodiscard]] QSize minimumSizeHint() const override { return {320, 200}; }

    bool advance(const motion::AudioFeatures& audio, double dt) override
    {
        live_ = motion::approach(live_, audio.liveliness(), dt, 0.3);
        const double cycles = state_.rateHz * dt * live_;
        phase_ = std::fmod(phase_ + cycles, 1.0);
        glow_ = audio.level() * audio.liveliness();

        // The level's trail moves left as far as the curves scrolled.
        const double window = lfoWindowSeconds(state_.rateHz);
        const double dx = cycles / std::max(state_.rateHz * window, 1e-9) * width();
        const double head = playhead();
        trail_.setArea(QRectF(0.0, kTop, head + 1.0, silence() - kTop));
        trail_.advance(motion::decayFactor(dt, std::max(0.5, 0.8 * window), 30.0),
            QTransform::fromTranslate(-dx, 0.0), 0.2);
        if (audio.level() > 0.0) {
            const double y = silence() - audio.level() * (silence() - kTop);
            const QColor ink = motion_paint::faded(time_effects::voiceColour(0), 0.22);
            trail_.ink([&](QPainter& p) {
                // Replacing, not adding: the columns of two frames overlap.
                p.setCompositionMode(QPainter::CompositionMode_Source);
                p.fillRect(QRectF(head - dx - 1.0, kTop, dx + 2.0, y - kTop), Qt::transparent);
                p.fillRect(QRectF(head - dx - 1.0, y, dx + 2.0, silence() - y), ink);
            });
        }
        update();
        return live_ > 0.002 || glow_ > 0.002 || trail_.isVisible();
    }

    void rest() override
    {
        live_ = 0.0;
        glow_ = 0.0;
        trail_.clear();
        update();
    }

    void animationSwitched(bool /*on*/) override
    {
        phase_ = 0.0;
        rest();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const AnimationPaintTimer timer;
        QPainter p(this);
        if (!animationOn()) {
            paintBack(p);
            paintStill(p);
            return;
        }
        back_.draw(p, *this, key_, [this](QPainter& layer) { paintBack(layer); });
        paintMoving(p);
    }

private:
    void paintBack(QPainter& p) const
    {
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
    }

    void paintStill(QPainter& p) const
    {
        const double bottom = silence();
        const double window = lfoWindowSeconds(state_.rateHz);
        p.setRenderHint(QPainter::Antialiasing);
        const auto trace = [&](bool right) {
            QPainterPath path;
            for (double x = 0.0; x <= width() + 2.0; x += 2.0) {
                const QPointF point(x, yAt(right, x, window, 0.0));
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
        paintBadges(p, window, 0.0);
    }

    void paintMoving(QPainter& p)
    {
        const double bottom = silence();
        const double window = lfoWindowSeconds(state_.rateHz);
        // From the playhead: the curve there is the gain now.
        const double origin
            = phase_ - state_.rateHz * playhead() / std::max(1.0, static_cast<double>(width())) * window;
        trail_.paint(p, 1.0);

        const QColor leftColour = time_effects::voiceColour(0);
        const QColor rightColour = time_effects::voiceColour(2);
        left_.clear();
        right_.clear();
        for (double x = 0.0; x <= width() + 3.0; x += 3.0) {
            left_.emplace_back(x, yAt(false, x, window, origin));
            right_.emplace_back(x, yAt(true, x, window, origin));
        }
        // The left side's area: one polygon, not antialiased (cheap).
        const std::size_t count = left_.size();
        left_.emplace_back(left_.back().x(), bottom);
        left_.emplace_back(0.0, bottom);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(Qt::NoPen);
        p.setBrush(motion_paint::faded(leftColour, 0.1));
        p.drawPolygon(left_.data(), static_cast<int>(left_.size()));
        p.setBrush(Qt::NoBrush);
        left_.resize(count);
        p.setRenderHint(QPainter::Antialiasing);
        const auto line = [&p](const std::vector<QPointF>& points, const QColor& colour) {
            // A fainter copy a pixel lower: reads as 2 px, both on Qt's fast path.
            p.setPen(QPen(motion_paint::faded(colour, 0.5), 1.0));
            p.translate(0.0, 1.0);
            p.drawPolyline(points.data(), static_cast<int>(points.size()));
            p.translate(0.0, -1.0);
            p.setPen(QPen(colour, 1.0));
            p.drawPolyline(points.data(), static_cast<int>(points.size()));
        };
        line(right_, rightColour);
        line(left_, leftColour);

        // The playhead and each side's gain now, glowing with the sound.
        const double head = std::round(playhead()) + 0.5;
        motion_paint::glowLine(p, QLineF(head, kTop, head, bottom), style::text(), 0.15 + 0.45 * glow_, 6.0);
        const double phaseNow = phase_;
        const auto [l, r]
            = tremoloGains(state_.autoPan, state_.wave, state_.depth, state_.stereoPhase, phaseNow);
        if (glow_ > 0.01) {
            motion_paint::glowDot(p, QPointF(head, bottom - r * (bottom - kTop)), 10.0 + 14.0 * r,
                rightColour, glow_ * (0.25 + 0.75 * r));
            motion_paint::glowDot(p, QPointF(head, bottom - l * (bottom - kTop)), 10.0 + 14.0 * l, leftColour,
                glow_ * (0.25 + 0.75 * l));
        }
        paintBadges(p, window, origin);
    }

    void paintBadges(QPainter& p, double window, double origin) const
    {
        p.setFont(style::font(font(), 10, QFont::Bold));
        for (const bool right : {false, true}) {
            const double x = right ? 44.0 : 20.0;
            const QPointF centre(x, yAt(right, x, window, origin));
            p.setPen(Qt::NoPen);
            p.setBrush(time_effects::voiceColour(right ? 2 : 0));
            p.drawEllipse(centre, kBadgeRadius, kBadgeRadius);
            p.setPen(style::background());
            p.drawText(QRectF(centre.x() - kBadgeRadius, centre.y() - kBadgeRadius, 2 * kBadgeRadius,
                           2 * kBadgeRadius),
                Qt::AlignCenter, right ? QStringLiteral("R") : QStringLiteral("L"));
        }
    }

    [[nodiscard]] double silence() const
    {
        return std::max(kTop + 60.0, height() - time_effects::panelReach() - kBottomRoom);
    }
    [[nodiscard]] double playhead() const { return kPlayhead * static_cast<double>(width()); }
    /// The gain of a side at `x`, where the LFO is at `origin` (cycles) at x 0.
    [[nodiscard]] double yAt(bool right, double x, double window, double origin) const
    {
        const double phase
            = origin + state_.rateHz * x / std::max(1.0, static_cast<double>(width())) * window;
        const auto [l, r]
            = tremoloGains(state_.autoPan, state_.wave, state_.depth, state_.stereoPhase, phase);
        return silence() - (right ? r : l) * (silence() - kTop);
    }

    State state_;
    double phase_ = 0.0; ///< the display's own LFO at the playhead, cycles
    double live_ = 0.0; ///< eases in and out with the sound
    double glow_ = 0.0; ///< level x liveliness
    std::uint64_t key_ = 1; ///< changes with what paintBack() draws
    motion_paint::LayerCache back_;
    motion_paint::TrailBuffer trail_{0.34};
    std::vector<QPointF> left_; ///< the traces (kept: no allocation a frame)
    std::vector<QPointF> right_;
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
    animate(display_);
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
