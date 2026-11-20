// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/ReverbEditor.hpp"

#include "core/TimeEffects.hpp"
#include "ui/plugins/builtin/Animation.hpp"
#include "ui/plugins/builtin/AnimationPaint.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <vloom/dsp/effects/Reverb.hpp>

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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
constexpr std::size_t kRibbonFrames = 1024; ///< samples of the live ribbon (21 ms at 48 kHz)
constexpr double kRibbonWidth = 120.0; ///< px the live ribbon spans
const double kHandleDecayShare = std::log10(1.0 / kHandleLevel) / 3.0; ///< of the decay time

} // namespace

/// ReverbEditor's display: the dry hit, the pre-delay, the reflections and
/// the tail over time, with a handle on the tail for the decay.
///
/// Animated (docs/builtin-editors.md, "Animation"), on top of that picture:
/// the live output as a glowing ribbon where the sound enters the reverb (at
/// the pre-delay), which leaves a mist drifting right along the time axis at
/// its real speed, fading 30 dB in the Decay time (Freeze holds it and lets
/// it swirl slowly). Size widens the cloud, Damping fades and darkens its
/// top, Diffusion smooths it. An onset sends a pulse along the time axis
/// that lights each reflection as it reaches it, and a few sparks into the
/// mist. Without sound everything fades in AudioFeatures::kSettleSeconds.
class DecayDisplay : public HandleDisplay, public Animated {
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
        reflections_ = reverbReflections(state_.preDelay, state_.size, state_.decay, state_.diffusion);
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

    bool advance(const motion::AudioFeatures& audio, double dt) override;
    void rest() override
    {
        trail_.clear();
        pulses_.clear();
        sparks_.clear();
        level_ = 0.0;
        update();
    }
    void animationSwitched(bool /*on*/) override { rest(); }

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
        const AnimationPaintTimer timer;
        QPainter p(this);
        const std::uint64_t key = stateKey();
        back_.draw(p, *this, key, [this](QPainter& layer) { paintBack(layer); });
        if (animationOn())
            paintMotion(p);
        front_.draw(p, *this, key, [this](QPainter& layer) { paintFront(layer); });
    }

private:
    /// A pulse an onset sends along the time axis.
    struct Pulse {
        double strength = 0.0;
    };
    /// A spark of an onset drifting into the mist.
    struct Spark {
        QPointF at;
        QPointF velocity; ///< px/s
        double brightness = 0.0;
    };

    /// Everything that follows the parameters: kept in back_.
    void paintBack(QPainter& p) const
    {
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
        const QPainterPath tail = curve(state_.decay);
        QPainterPath area = tail;
        area.lineTo(right() + 3.0, base);
        area.lineTo(xOf(tailStart()), base);
        area.closeSubpath();
        QColor fill = style::accent();
        fill.setAlphaF(0.24f);
        p.setBrush(fill);
        p.setPen(Qt::NoPen);
        p.drawPath(area);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(style::accent(), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(tail);

        // The first trips round the lines.
        QColor reflection = style::accent();
        reflection.setAlphaF(0.9f);
        p.setPen(Qt::NoPen);
        p.setBrush(reflection);
        for (const ReverbReflection& each : reflections_) {
            const double h = full * kReflectionHeight * each.level;
            p.drawRoundedRect(QRectF(xOf(each.seconds) - 1.5, base - h, 3.0, h), 1, 1);
        }

        if (!state_.frozen) {
            QColor dash = style::text();
            dash.setAlphaF(0.6f);
            QPen pen(dash, 1.5);
            pen.setDashPattern({4.0 / 1.5, 4.0 / 1.5});
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawPath(curve(highDecay()));
        }
    }

    /// The words and the handle, over the motion: kept in front_.
    void paintFront(QPainter& p) const
    {
        p.setRenderHint(QPainter::Antialiasing);
        p.setFont(style::font(font(), 10));
        const double start = tailStart();
        if (state_.frozen) {
            p.setPen(style::text());
            p.drawText(QPointF(xOf(start) + 12, baseline() - tailHeight() - 8), QObject::tr("Frozen"));
        } else {
            // Named where the highs are down to half.
            const double labelX = xOf(start + highDecay() * std::log10(2.0) / 3.0) + 8.0;
            QColor words = style::text();
            words.setAlphaF(0.8f);
            p.setPen(words);
            p.drawText(QPointF(std::min(labelX, width() - 120.0), baseline() - tailHeight() * 0.5 + 14.0),
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

    /// The mist, the ribbon, the pulses and the sparks.
    void paintMotion(QPainter& p) const;

    [[nodiscard]] QPainterPath curve(double decay) const
    {
        const double base = baseline();
        const double start = tailStart();
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
    }

    [[nodiscard]] double highDecay() const
    {
        return reverbDecayAt(kHighHz, state_.decay, state_.damping, state_.size, sampleRate_);
    }
    [[nodiscard]] std::uint64_t stateKey() const
    {
        std::uint64_t key = 1469598103934665603ULL;
        for (const double v : {state_.preDelay, state_.size, state_.decay, state_.diffusion, state_.damping,
                 state_.frozen ? 1.0 : 0.0, span_, isDragging() ? 1.0 : 0.0})
            key = motion_paint::keyWith(key, v);
        return key;
    }

    [[nodiscard]] double right() const { return std::max(kDryX + 50.0, width() - kRightMargin); }
    [[nodiscard]] double xOf(double seconds) const { return kDryX + seconds / span_ * (right() - kDryX); }
    [[nodiscard]] double baseline() const
    {
        return std::max(kTop + 60.0, height() - time_effects::panelReach() - kBottomRoom);
    }
    [[nodiscard]] double tailHeight() const { return (baseline() - kTop) * kTailHeight; }
    [[nodiscard]] double tailStart() const { return reverbTailStart(state_.preDelay, state_.size); }
    /// Where the sound enters the reverb: the end of the pre-delay.
    [[nodiscard]] double entryX() const
    {
        return std::max(xOf(state_.preDelay), kDryX + kBarWidth / 2 + 4.0);
    }
    /// The mist's centre line, and how far it reaches up and down at first.
    [[nodiscard]] double mistY() const { return baseline() - tailHeight() * 0.42; }
    [[nodiscard]] double mistReach() const { return tailHeight() * (0.25 + 0.2 * state_.size / 100.0); }

    /// Where anything moves: from the dry bar to the right, the top to the baseline.
    [[nodiscard]] QRectF motionArea() const
    {
        return QRectF(kDryX - 4.0, kTop - 24.0, width() - kDryX + 4.0, baseline() - kTop + 26.0);
    }

    /// Spans the pre-delay and the tail until it is down to a few per cent.
    void fit() { span_ = std::max(0.1, tailStart() + 0.6 * state_.decay); }

    /// A pseudo-random number in 0 .. 1 (sparks' spread; fixed seed).
    double random()
    {
        seed_ ^= seed_ << 13U;
        seed_ ^= seed_ >> 17U;
        seed_ ^= seed_ << 5U;
        return static_cast<double>(seed_ & 0xFFFFU) / 65535.0;
    }

    double sampleRate_;
    State state_;
    std::vector<ReverbReflection> reflections_;
    double span_ = 1.5; ///< seconds from the dry sound to the right end
    motion_paint::LayerCache back_;
    motion_paint::LayerCache front_;

    // The motion (animationOn()).
    motion_paint::TrailBuffer trail_{0.34};
    motion::ParticlePool<Pulse, 8> pulses_;
    motion::ParticlePool<Spark, 64> sparks_;
    std::array<float, kRibbonFrames> ribbon_{}; ///< the newest output
    double level_ = 0.0; ///< the output's level (0 .. 1) times the liveliness
    double time_ = 0.0; ///< s the animation ran: the swirl's phase
    QPointF drift_; ///< the mist texture's scroll, px
    std::uint32_t seed_ = 0x9E3779B9U;
};

bool DecayDisplay::advance(const motion::AudioFeatures& audio, double dt)
{
    time_ += dt;
    const double speed = (right() - kDryX) / span_; // px per second of the time axis
    const double y = mistY();
    trail_.setArea(motionArea());

    // The mist: fades 30 dB in the decay time (Freeze: holds), and with the
    // sound gone, goes in time for the clock to rest.
    double keep
        = state_.frozen ? motion::decayFactor(dt, 120.0, 60.0) : motion::decayFactor(dt, state_.decay, 30.0);
    if (!audio.running())
        keep *= std::exp(-dt / 0.45);
    QTransform flow;
    if (state_.frozen) {
        // Held, slowly swirling about its middle.
        const QPointF centre(entryX() + (right() - entryX()) * 0.4, y);
        const double turn = 0.10 * std::sin(time_ * 0.6) * dt;
        flow = QTransform::fromTranslate(-centre.x(), -centre.y()) * QTransform().rotateRadians(turn)
            * QTransform::fromScale(1.0, 1.0 + 0.04 * std::cos(time_ * 0.4) * dt)
            * QTransform::fromTranslate(centre.x(), centre.y());
    } else {
        // Drifting right with time, widening by Size as it goes.
        const double spread = 1.0 + (0.08 + 0.55 * state_.size / 100.0) * dt;
        flow = QTransform::fromTranslate(0.0, -y) * QTransform::fromScale(1.0, spread)
            * QTransform::fromTranslate(speed * dt, y);
    }
    trail_.advance(keep, flow, 0.12 + 0.6 * state_.diffusion / 100.0);

    level_ = audio.level() * audio.liveliness();
    const std::span<const float> wave = audio.waveform();
    std::copy(wave.end() - static_cast<std::ptrdiff_t>(ribbon_.size()), wave.end(), ribbon_.begin());

    // New sound goes into the mist (none while frozen: Freeze lets no more in).
    if (audio.running() && !state_.frozen && level_ > 0.002) {
        const QRectF band(entryX(), y - mistReach(), kRibbonWidth, 2.0 * mistReach());
        // Damping: the higher it is, the duller the new mist.
        QColor ink = editor_style::accent();
        ink = QColor::fromRgbF(ink.redF() * static_cast<float>(1.0 - 0.35 * state_.damping / 100.0),
            ink.greenF() * static_cast<float>(1.0 - 0.35 * state_.damping / 100.0),
            ink.blueF() * static_cast<float>(1.0 - 0.2 * state_.damping / 100.0));
        const double strength = 0.05 + 0.22 * level_;
        trail_.ink([&](QPainter& p) {
            motion_paint::paintWaveformRibbon(
                p, ribbon_, band, 24, motion_paint::faded(ink, strength), Qt::transparent);
        });
    }

    if (audio.onset() && !state_.frozen) {
        const double strength = std::max(0.3, audio.onsetStrength()) * std::max(level_, 0.3);
        pulses_.spawn(Pulse{.strength = strength}, span_);
        const int count = 3 + static_cast<int>(std::lround(9.0 * strength));
        for (int i = 0; i < count; ++i) {
            const double spreadY = (random() - 0.5) * 2.0 * mistReach();
            sparks_.spawn(Spark{.at = QPointF(entryX() + random() * kRibbonWidth, y + spreadY),
                              .velocity = QPointF(speed * (0.7 + 0.6 * random()),
                                  spreadY * (0.3 + state_.size / 100.0) + (random() - 0.5) * 20.0),
                              .brightness = strength},
                std::min(state_.decay, span_) * (0.5 + 0.5 * random()));
        }
    }
    pulses_.advance(dt);
    const std::size_t sparks = sparks_.advance(dt, [dt](auto& spark, double) {
        spark.state.at += spark.state.velocity * dt;
        spark.state.velocity *= std::exp(-dt / 1.5); // slowing in the mist
    });
    drift_ += QPointF(speed * 0.25 * dt, -5.0 * dt);

    update(motionArea().toAlignedRect()); // the rest is still
    return trail_.isVisible() || pulses_.alive() > 0 || sparks > 0 || level_ > 0.002;
}

void DecayDisplay::paintMotion(QPainter& p) const
{
    p.setRenderHint(QPainter::Antialiasing);
    const QColor accent = editor_style::accent();
    const double base = baseline();

    // The mist: broken up by noise (less with more diffusion), its top
    // faded by damping.
    trail_.paint(p, 0.95, 0.75 - 0.45 * state_.diffusion / 100.0, drift_, 0.85 * state_.damping / 100.0);

    // Each onset's pulse along the time axis: a faint front, and each
    // reflection lit as it passes.
    pulses_.forEach([&](const auto& pulse) {
        const double x = xOf(pulse.age);
        const double fade = pulse.state.strength * motion::decayFactor(pulse.age, state_.decay, 30.0);
        if (fade < 0.01)
            return;
        QLinearGradient front(QPointF(x, kTop), QPointF(x, base));
        front.setColorAt(0.0, motion_paint::faded(accent, 0.0));
        front.setColorAt(1.0, motion_paint::faded(accent, 0.35 * fade));
        p.fillRect(QRectF(x - 1.0, kTop, 2.0, base - kTop), front);
        for (const ReverbReflection& each : reflections_) {
            const double dx = (xOf(each.seconds) - x) / 10.0;
            const double light = fade * std::exp(-dx * dx);
            if (light < 0.02)
                continue;
            const double h = (base - kTop) * kReflectionHeight * each.level;
            const double rx = xOf(each.seconds);
            motion_paint::glowLine(
                p, QLineF(rx, base, rx, base - h), motion_paint::faded(style::text(), light), light, 4.0);
            motion_paint::glowDot(p, QPointF(rx, base - h), 9.0, accent, light);
        }
    });

    // Sparks drifting into the mist.
    {
        motion_paint::GlowBatch glow(p, accent);
        sparks_.forEach([&](const auto& spark) {
            const double light = spark.state.brightness * (1.0 - spark.progress());
            glow.dot(spark.state.at, 5.0 + 4.0 * light, light);
        });
    }

    // The live output: a glowing ribbon where the sound enters.
    if (level_ > 0.002) {
        const QRectF band(entryX(), mistY() - mistReach(), kRibbonWidth, 2.0 * mistReach());
        motion_paint::paintWaveformRibbon(p, ribbon_, band, 60,
            motion_paint::faded(accent, 0.2 + 0.3 * level_),
            motion_paint::faded(accent.lighter(160), 0.5 + 0.5 * level_), 0.6 * level_);
    }
}

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
    animate(display_);

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
