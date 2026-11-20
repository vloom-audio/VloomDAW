// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/ReverbEditor.hpp"

#include "core/TimeEffects.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <vloom/dsp/effects/Reverb.hpp>

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace vloomdaw {

namespace {

namespace style = editor_style;
using Reverb = vloom::dsp::Reverb;

constexpr double kDryX = 60.0; ///< the dry bar's centre: time 0
constexpr double kBarWidth = 12.0;
constexpr double kRightMargin = 20.0;
constexpr double kTop = 36.0;
constexpr double kBottomRoom = 44.0; ///< between the baseline and the panel
constexpr double kTailHeight = 0.75; ///< the tail's start, of the full height
constexpr double kReflectionHeight = 0.9;
constexpr double kHandleLevel = 0.25; ///< where on the tail the handle sits
constexpr double kHighHz = 8000.0; ///< the dashed curve's frequency
const double kHandleDecayShare = std::log10(1.0 / kHandleLevel) / 3.0; ///< of the decay time

} // namespace

/// ReverbEditor's display: the dry hit, the pre-delay, the reflections and
/// the tail over time, with a handle on the tail for the decay.
class DecayDisplay : public HandleDisplay {
public:
    struct State {
        double preDelay = 0.01; ///< s
        double size = 50.0; ///< %
        double decay = 2.0; ///< s
        double diffusion = 70.0; ///< %
        double damping = 50.0; ///< %
        bool frozen = false;
    };

    explicit DecayDisplay(double sampleRate)
        : sampleRate_(sampleRate)
    {
        setAccessibleName(
            QObject::tr("Decay: the dry hit, the pre-delay, the reflections and the reverb's tail"));
        setHandleCursor(Qt::SizeHorCursor);
        onDragFinished = [this] {
            fit();
            update();
        };
    }

    void setState(const State& state)
    {
        state_ = state;
        if (!isDragging())
            fit();
        update();
    }

    [[nodiscard]] double secondsAt(double x) const { return (x - kDryX) / (right() - kDryX) * span_; }

    /// The decay that puts the handle at `x`.
    [[nodiscard]] double decayAt(double x) const
    {
        return std::max(0.0, secondsAt(x) - tailStart()) / kHandleDecayShare;
    }

protected:
    std::optional<QPointF> handle() const override
    {
        if (state_.frozen)
            return std::nullopt; // the decay does nothing while frozen
        return QPointF(
            xOf(tailStart() + state_.decay * kHandleDecayShare), baseline() - tailHeight() * kHandleLevel);
    }

    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter p(this);
        p.fillRect(rect(), style::background());
        const double base = baseline();
        const double full = base - kTop;
        p.setFont(style::font(font(), 10));
        time_effects::paintTimeAxis(p, kDryX, right(), span_, 0.0, height(), 16.0);
        p.setPen(QPen(style::line(), 1.0));
        p.drawLine(QPointF(0, std::round(base) + 0.5), QPointF(width(), std::round(base) + 0.5));

        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(style::text());
        p.drawRoundedRect(QRectF(kDryX - kBarWidth / 2, kTop, kBarWidth, full), 3, 3);
        p.setPen(style::muted());
        p.drawText(QPointF(kDryX + kBarWidth / 2 + 8, kTop + 12), QObject::tr("Dry"));

        // The pre-delay, under the baseline at the start of it all.
        const double preEnd = std::max(xOf(state_.preDelay), kDryX + kBarWidth / 2);
        const double bracketY = base + 12.5;
        p.setPen(QPen(style::muted(), 1.0));
        p.drawLine(QPointF(kDryX, bracketY), QPointF(preEnd, bracketY));
        p.drawLine(QPointF(preEnd, bracketY - 5), QPointF(preEnd, bracketY + 5));
        p.drawText(QPointF(preEnd + 8, bracketY + 4),
            QObject::tr("Pre-delay %1").arg(time_effects::timeText(state_.preDelay)));

        // The tail and, dashed, its high frequencies.
        const double start = tailStart();
        const auto curve = [&](double decay) {
            QPainterPath path;
            for (double x = xOf(start);; x += 3.0) {
                const double at = std::min(x, right() + 3.0);
                const double level = reverbLevel(secondsAt(at) - start, decay, state_.frozen);
                const QPointF point(at, base - tailHeight() * level);
                if (path.elementCount() == 0)
                    path.moveTo(point);
                else
                    path.lineTo(point);
                if (at >= right() + 3.0)
                    break;
            }
            return path;
        };
        const QPainterPath tail = curve(state_.decay);
        QPainterPath area = tail;
        area.lineTo(right() + 3.0, base);
        area.lineTo(xOf(start), base);
        area.closeSubpath();
        QColor fill = style::accent();
        fill.setAlphaF(0.24f);
        p.setBrush(fill);
        p.drawPath(area);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(style::accent(), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(tail);

        // The first trips round the lines.
        QColor reflection = style::accent();
        reflection.setAlphaF(0.9f);
        p.setPen(Qt::NoPen);
        p.setBrush(reflection);
        for (const ReverbReflection& each :
            reverbReflections(state_.preDelay, state_.size, state_.decay, state_.diffusion)) {
            const double h = full * kReflectionHeight * each.level;
            p.drawRoundedRect(QRectF(xOf(each.seconds) - 1.5, base - h, 3.0, h), 1, 1);
        }

        if (state_.frozen) {
            p.setPen(style::text());
            p.drawText(QPointF(xOf(start) + 12, base - tailHeight() - 8), QObject::tr("Frozen"));
        } else {
            const double high
                = reverbDecayAt(kHighHz, state_.decay, state_.damping, state_.size, sampleRate_);
            QColor dash = style::text();
            dash.setAlphaF(0.6f);
            QPen pen(dash, 1.5);
            pen.setDashPattern({4.0 / 1.5, 4.0 / 1.5});
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawPath(curve(high));
            // Named where the highs are down to half.
            const double labelX = xOf(start + high * std::log10(2.0) / 3.0) + 8.0;
            QColor words = style::text();
            words.setAlphaF(0.8f);
            p.setPen(words);
            p.drawText(QPointF(std::min(labelX, width() - 120.0), base - tailHeight() * 0.5 + 14.0),
                QObject::tr("High frequencies"));
            p.setFont(style::font(font(), 10, QFont::DemiBold, true));
            p.setPen(style::text());
            if (const auto at = handle())
                p.drawText(QPointF(std::min(at->x() + 14.0, width() - 90.0), at->y() - 10.0),
                    QObject::tr("Decay %1").arg(time_effects::timeText(state_.decay)));
        }

        if (const auto at = handle(); at && at->x() <= width())
            paintHandle(p, *at);
    }

private:
    [[nodiscard]] double right() const { return std::max(kDryX + 50.0, width() - kRightMargin); }
    [[nodiscard]] double xOf(double seconds) const { return kDryX + seconds / span_ * (right() - kDryX); }
    [[nodiscard]] double baseline() const
    {
        return std::max(kTop + 60.0, height() - time_effects::panelReach() - kBottomRoom);
    }
    [[nodiscard]] double tailHeight() const { return (baseline() - kTop) * kTailHeight; }
    [[nodiscard]] double tailStart() const { return reverbTailStart(state_.preDelay, state_.size); }

    /// Spans the pre-delay and the tail until it is down to a few per cent.
    void fit() { span_ = std::max(0.1, tailStart() + 0.6 * state_.decay); }

    double sampleRate_;
    State state_;
    double span_ = 1.5; ///< seconds from the dry sound to the right end
};

ReverbEditor::ReverbEditor(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate, QWidget* parent)
    : BuiltinEditorWindow(std::move(instance), title, sampleRate,
          {
              {tr("Small Room"),
                  {{Reverb::kPreDelay, 5.0}, {Reverb::kSize, 25.0}, {Reverb::kDecay, 0.6},
                      {Reverb::kDiffusion, 60.0}, {Reverb::kDamping, 60.0}, {Reverb::kMix, 20.0}}},
              {tr("Vocal Plate"),
                  {{Reverb::kPreDelay, 25.0}, {Reverb::kSize, 45.0}, {Reverb::kDecay, 1.8},
                      {Reverb::kDiffusion, 85.0}, {Reverb::kDamping, 35.0}, {Reverb::kLowCut, 150.0},
                      {Reverb::kMix, 25.0}}},
              {tr("Large Hall"),
                  {{Reverb::kPreDelay, 30.0}, {Reverb::kSize, 85.0}, {Reverb::kDecay, 4.5},
                      {Reverb::kDiffusion, 80.0}, {Reverb::kMix, 30.0}}},
              {tr("Dark Chamber"),
                  {{Reverb::kSize, 60.0}, {Reverb::kDecay, 2.5}, {Reverb::kDamping, 80.0},
                      {Reverb::kHighCut, 5000.0}}},
              {tr("Ambient Wash"),
                  {{Reverb::kPreDelay, 60.0}, {Reverb::kSize, 100.0}, {Reverb::kDecay, 12.0},
                      {Reverb::kDiffusion, 100.0}, {Reverb::kDamping, 40.0}, {Reverb::kLowCut, 200.0},
                      {Reverb::kMix, 45.0}}},
          },
          parent)
{
    display_ = new DecayDisplay(this->sampleRate());
    display_->onHandleDragged
        = [this](QPointF point) { change(Reverb::kDecay, display_->decayAt(point.x())); };
    display_->onHandleReset = [this] {
        if (const vloom::ParameterInfo* decay = parameter(Reverb::kDecay))
            change(Reverb::kDecay, decay->defaultValue);
    };
    setDisplay(display_);

    panel()->add(knob(Reverb::kSize));
    panel()->add(knob(Reverb::kDecay));
    panel()->add(knob(Reverb::kPreDelay));
    panel()->add(knob(Reverb::kDiffusion));
    panel()->add(knob(Reverb::kDamping));
    panel()->add(knob(Reverb::kWidth));
    panel()->add(knob(Reverb::kMix));
    PillToggle* freeze = toggle(Reverb::kFreeze);
    freeze->setToolTip(tr("Hold the tail as it is and let no more sound in"));
    panel()->add(freeze);

    Readout* lowCut = readout(Reverb::kLowCut, tr("Low cut"));
    lowCut->setWidestText(QStringLiteral("1.00 kHz"));
    lowCut->setToolTip(tr("Takes the lows out of the reverb (not of the dry sound)"));
    footer()->addLeft(lowCut);
    Readout* highCut = readout(Reverb::kHighCut, tr("High cut"));
    highCut->setWidestText(QStringLiteral("20.00 kHz"));
    highCut->setToolTip(tr("Takes the highs out of the reverb (not of the dry sound)"));
    footer()->addLeft(highCut);
    Readout* output = readout(tr("Output"));
    output_.emplace(output);
    footer()->addRight(output);

    refresh();
    startMetering();
}

void ReverbEditor::refresh()
{
    display_->setState(DecayDisplay::State{.preDelay = value(Reverb::kPreDelay) / 1000.0,
        .size = value(Reverb::kSize),
        .decay = value(Reverb::kDecay),
        .diffusion = value(Reverb::kDiffusion),
        .damping = value(Reverb::kDamping),
        .frozen = value(Reverb::kFreeze) >= 0.5});
}

void ReverbEditor::parameterChanged(std::uint32_t /*id*/, double /*value*/) { refresh(); }

void ReverbEditor::meter(const Meter& reading) { output_->push(reading.running, reading.outputDb); }

} // namespace vloomdaw
