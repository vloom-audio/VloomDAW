// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/DelayEditor.hpp"

#include "core/ParameterScale.hpp"
#include "core/TimeEffects.hpp"
#include "ui/plugins/builtin/AnimationPaint.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <vloom/dsp/effects/Delay.hpp>

#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace vloomdaw {

namespace {

namespace style = editor_style;
using Delay = vloom::dsp::Delay;

constexpr double kDryX = 58.0; ///< the dry bar's centre
constexpr double kBarWidth = 12.0;
constexpr double kRightMargin = 24.0;
constexpr double kTop = 24.0;
constexpr double kLabelRoom = 26.0; ///< under the echoes, for their numbers
constexpr double kEchoesShown = 8.6; ///< echo intervals the display spans
constexpr double kRangeDb = 42.0; ///< from a full bar down to none
constexpr double kFlashSeconds = 0.45; ///< an echo's glow as its pulse reaches it
constexpr double kTrailSeconds = 0.7; ///< a pulse's trail falls 60 dB in this

} // namespace

/// DelayEditor's display: the dry hit and its echoes, left above the line
/// and right below it, the decay dashed, and a handle on the second echo.
///
/// Animated: each hit in the sound (an onset) sends a glowing pulse along
/// the time axis at real time, as tall as the feedback's decay lets the
/// echoes be by then, leaving a fading trail; each echo flashes (above, on
/// the left; below, on the right; ping-pong bouncing) as the pulse reaches
/// it, as bright as the hit and that echo's level.
class EchoDisplay : public HandleDisplay, public Animated {
public:
    EchoDisplay()
    {
        setAccessibleName(QObject::tr("Echoes: the dry hit, then each echo on the left and the right"));
        setHandleCursor(Qt::SizeAllCursor);
        onDragFinished = [this] {
            fit();
            taps_ = echoTaps(echo_, span_);
            ++key_;
            update();
        };
    }

    void setState(const EchoSettings& echo, double mix)
    {
        echo_ = echo;
        mix_ = std::clamp(mix, 0.0, 1.0);
        if (!isDragging())
            fit();
        taps_ = echoTaps(echo_, span_);
        ++key_;
        update();
    }

    bool advance(const motion::AudioFeatures& audio, double dt) override
    {
        if (audio.onset() && wet() > 0.0) {
            const double strength = std::clamp(audio.onsetStrength(), 0.25, 1.0);
            pulses_.spawn(Pulse{.strength = strength, .previousAge = 0.0}, span_ + 0.05);
            flashes_.spawn(Flash{.x = kDryX,
                               .top = base() - heightOf((1.0 - mix_) / scale()),
                               .bottom = base() + heightOf((1.0 - mix_) / scale()),
                               .intensity = strength},
                kFlashSeconds);
        }
        flashes_.advance(dt);
        trail_.setArea(QRectF(0.0, 0.0, width(), height()));
        trail_.advance(motion::decayFactor(dt, kTrailSeconds, 60.0), QTransform(), 0.35);
        const double mid = base();
        pulses_.advance(dt, [&](auto& pulse, double /*step*/) {
            Pulse& state = pulse.state;
            // The echoes the pulse reached since the last frame flash.
            for (const EchoTap& tap : taps_) {
                if (tap.seconds > state.previousAge && tap.seconds <= pulse.age) {
                    const double up = heightOf(tap.left * wet());
                    const double down = heightOf(tap.right * wet());
                    const double level = std::max(up, down) / halfHeight();
                    if (level > 0.0)
                        flashes_.spawn(Flash{.x = xOf(tap.seconds),
                                           .top = up >= 0.5 ? mid - up : mid,
                                           .bottom = down >= 0.5 ? mid + down : mid,
                                           .intensity = state.strength * (0.4 + 0.6 * level)},
                            kFlashSeconds);
                }
            }
            // Its front: from where it was to where it is, as tall as the
            // echoes may be by then (the feedback's decay).
            const double from = xOf(state.previousAge);
            const double to = xOf(pulse.age);
            const double h = heightOf(echoLevelAt(pulse.age));
            if (h >= 0.5 && to > from) {
                QColor ink = style::accent();
                ink.setAlphaF(static_cast<float>(0.16 * state.strength));
                trail_.ink([&](QPainter& p) { p.fillRect(QRectF(from, mid - h, to - from, 2.0 * h), ink); });
            }
            state.previousAge = pulse.age;
        });
        update();
        return pulses_.alive() > 0 || flashes_.alive() > 0 || trail_.isVisible();
    }

    void rest() override
    {
        pulses_ = motion::ParticlePool<Pulse, 8>{};
        flashes_ = motion::ParticlePool<Flash, 48>{};
        trail_.clear();
        update();
    }

    void animationSwitched(bool /*on*/) override { rest(); }

    /// When the second echo comes for these settings.
    [[nodiscard]] double secondEcho() const
    {
        return echo_.pingPong ? echo_.leftSeconds + echo_.rightSeconds : 2.0 * echo_.leftSeconds;
    }

    [[nodiscard]] double secondsAt(double x) const { return (x - kDryX) / (right() - kDryX) * span_; }

    /// The feedback that puts the second echo at `y`; nullopt with no echoes (mix 0).
    [[nodiscard]] std::optional<double> feedbackAt(double y) const
    {
        if (!(wet() > 0.0))
            return std::nullopt;
        return std::clamp(levelOf(base() - y) / wet(), 0.0, 1.0);
    }

protected:
    std::optional<QPointF> handle() const override
    {
        return QPointF(xOf(secondEcho()), base() - heightOf(wet() * echo_.feedback));
    }

    void paintEvent(QPaintEvent* /*event*/) override
    {
        const AnimationPaintTimer timer;
        QPainter p(this);
        if (!animationOn()) {
            paintStatic(p);
            return;
        }
        back_.draw(p, *this, key_, [this](QPainter& layer) { paintStatic(layer); });
        trail_.paint(p, 1.0);
        const double mid = base();
        // Each pulse's front: a glowing edge where it is now.
        pulses_.forEach([&](const auto& pulse) {
            const double h = heightOf(echoLevelAt(pulse.age));
            if (h < 0.5)
                return;
            const double x = std::round(xOf(pulse.age)) + 0.5;
            motion_paint::glowLine(p, QLineF(x, mid - h, x, mid + h), style::accent(),
                0.6 * pulse.state.strength * (1.0 - pulse.progress()), 7.0);
        });
        motion_paint::GlowBatch glow(p, style::accent());
        flashes_.forEach([&](const auto& flash) {
            const double fade = 1.0 - flash.progress();
            const double intensity = flash.state.intensity * fade * fade;
            const double radius = 10.0 + 10.0 * flash.state.intensity;
            glow.dot(QPointF(flash.state.x, flash.state.top), radius, intensity);
            if (flash.state.bottom > flash.state.top + 1.0)
                glow.dot(QPointF(flash.state.x, flash.state.bottom), radius, intensity);
        });
    }

    void paintStatic(QPainter& p) const
    {
        p.fillRect(rect(), style::background());
        const double mid = base();
        const double half = halfHeight();
        p.setPen(QPen(style::line(), 1.0));
        p.drawLine(QPointF(0, std::round(mid) + 0.5), QPointF(width(), std::round(mid) + 0.5));

        const QFont small = style::font(font(), 10);
        p.setFont(small);
        p.setPen(style::muted());
        p.drawText(QPointF(12, kTop), QObject::tr("Left"));
        p.drawText(QPointF(12, mid + half + 8), QObject::tr("Right"));

        p.setRenderHint(QPainter::Antialiasing);
        // The dry sound, as loud as the mix leaves it.
        const double dry = heightOf((1.0 - mix_) / scale());
        p.setPen(Qt::NoPen);
        p.setBrush(style::text());
        if (dry > 0.5)
            p.drawRoundedRect(QRectF(kDryX - kBarWidth / 2, mid - dry, kBarWidth, 2.0 * dry), 3, 3);
        p.setPen(style::muted());
        p.drawText(QPointF(kDryX + kBarWidth / 2 + 8, mid - half + 12), QObject::tr("Dry"));

        // The echoes.
        const std::vector<EchoTap>& taps = taps_;
        const QColor accent = style::accent();
        const double numberY = mid + half + kLabelRoom - 6;
        double lastNumberX = -100.0;
        for (std::size_t i = 0; i < taps.size(); ++i) {
            const EchoTap& tap = taps[i];
            const double x = xOf(tap.seconds);
            if (x > width())
                break;
            const double up = heightOf(tap.left * wet());
            const double down = heightOf(tap.right * wet());
            if (std::max(up, down) < 0.5)
                continue; // under the display's range
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(QPen(style::grid(), 1.0));
            p.drawLine(QPointF(std::round(x) + 0.5, 0), QPointF(std::round(x) + 0.5, height()));
            p.setRenderHint(QPainter::Antialiasing);
            QColor fill = accent;
            fill.setAlphaF(static_cast<float>(std::max(0.35, 1.0 - 0.06 * static_cast<double>(i))));
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            if (up >= 0.5)
                p.drawRoundedRect(QRectF(x - kBarWidth / 2, mid - up, kBarWidth, up), 3, 3);
            if (down >= 0.5)
                p.drawRoundedRect(QRectF(x - kBarWidth / 2, mid, kBarWidth, down), 3, 3);
            if (x - lastNumberX >= 20.0) {
                p.setPen(style::muted());
                p.drawText(QRectF(x - 15, numberY - 11, 30, 14), Qt::AlignCenter, QString::number(i + 1));
                lastNumberX = x;
            }
        }

        // How the feedback lets them fall: one step of it per echo interval,
        // from the first echo.
        if (echo_.feedback > 0.0 && wet() > 0.0) {
            const double start = echo_.leftSeconds;
            const double interval
                = echo_.pingPong ? 0.5 * (echo_.leftSeconds + echo_.rightSeconds) : echo_.leftSeconds;
            QPainterPath decay;
            for (double x = xOf(start); x <= right() + 4.0; x += 3.0) {
                const double steps = (secondsAt(x) - start) / interval;
                const double level = wet() * std::pow(echo_.feedback, steps);
                if (heightOf(level) <= 0.0)
                    break;
                const QPointF point(x, mid - heightOf(level));
                if (decay.elementCount() == 0)
                    decay.moveTo(point);
                else
                    decay.lineTo(point);
            }
            QColor dash = accent;
            dash.setAlphaF(0.5f);
            QPen pen(dash, 1.0);
            pen.setDashPattern({4.0, 4.0});
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawPath(decay);
        }

        if (const auto at = handle(); at && at->x() <= width())
            paintHandle(p, *at);
    }

private:
    /// The right end of the time axis.
    [[nodiscard]] double right() const { return std::max(kDryX + 50.0, width() - kRightMargin); }
    [[nodiscard]] double xOf(double seconds) const { return kDryX + seconds / span_ * (right() - kDryX); }
    /// The line between left and right.
    [[nodiscard]] double base() const { return kTop + halfHeight(); }
    /// From the line to the top of a bar at full level.
    [[nodiscard]] double halfHeight() const
    {
        const double bottom = height() - time_effects::panelReach() - kLabelRoom;
        return std::max(20.0, (bottom - kTop) / 2.0);
    }
    /// Levels are drawn against the louder of the dry sound and the first echo.
    [[nodiscard]] double scale() const { return std::max({1.0 - mix_, mix_, 1e-6}); }
    /// The level of a first echo, which every later one is a part of.
    [[nodiscard]] double wet() const { return mix_ / scale(); }
    /// How tall a bar of `level` (1: the louder of the dry sound and the
    /// first echo) stands: in dB, kRangeDb down to nothing.
    [[nodiscard]] double heightOf(double level) const
    {
        if (!(level > 0.0))
            return 0.0;
        return halfHeight() * std::clamp(1.0 + 20.0 * std::log10(level) / kRangeDb, 0.0, 1.0);
    }
    /// The level of a bar `height` tall.
    [[nodiscard]] double levelOf(double height) const
    {
        const double fraction = std::clamp(height / halfHeight(), 0.0, 1.0);
        return fraction <= 0.0 ? 0.0 : std::pow(10.0, (fraction - 1.0) * kRangeDb / 20.0);
    }

    /// How loud an echo can be `seconds` after the hit: the first echo's
    /// level until it comes, then one step of the feedback per interval.
    [[nodiscard]] double echoLevelAt(double seconds) const
    {
        const double start = echo_.leftSeconds;
        if (seconds <= start)
            return wet();
        const double interval
            = echo_.pingPong ? 0.5 * (echo_.leftSeconds + echo_.rightSeconds) : echo_.leftSeconds;
        return wet() * std::pow(echo_.feedback, (seconds - start) / std::max(interval, 1e-6));
    }

    /// Spans enough time for about eight echo intervals.
    void fit()
    {
        const double interval = echo_.pingPong ? 0.5 * (echo_.leftSeconds + echo_.rightSeconds)
                                               : std::max(echo_.leftSeconds, echo_.rightSeconds);
        span_ = std::max(0.01, kEchoesShown * interval);
    }

    /// A hit's pulse travelling along the echoes.
    struct Pulse {
        double strength = 0.0; ///< of the onset, 0 .. 1
        double previousAge = 0.0; ///< where it was last frame, s
    };
    /// An echo glowing as a pulse reaches it.
    struct Flash {
        double x = 0.0;
        double top = 0.0; ///< the left bar's top (the line if none)
        double bottom = 0.0; ///< the right bar's bottom
        double intensity = 0.0;
    };

    EchoSettings echo_;
    double mix_ = 0.3;
    double span_ = 3.0; ///< seconds from the dry sound to the right end
    std::vector<EchoTap> taps_; ///< for these settings over span_
    std::uint64_t key_ = 1; ///< changes with the settings (the cached picture)
    motion_paint::LayerCache back_;
    motion_paint::TrailBuffer trail_{0.34};
    motion::ParticlePool<Pulse, 8> pulses_;
    motion::ParticlePool<Flash, 48> flashes_;
};

DelayEditor::DelayEditor(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate, QWidget* parent)
    : BuiltinEditorWindow(std::move(instance), title, sampleRate,
          {
              {tr("Slapback"),
                  {{Delay::kSync, 0.0}, {Delay::kTime, 90.0}, {Delay::kFeedback, 10.0},
                      {Delay::kHighCut, 6000.0}, {Delay::kWidth, 60.0}, {Delay::kMix, 25.0}}},
              {tr("Dotted Eighth"),
                  {{Delay::kNote, 7.0}, {Delay::kFeedback, 40.0}, {Delay::kLowCut, 150.0},
                      {Delay::kHighCut, 8000.0}, {Delay::kMix, 25.0}}},
              {tr("Ping-Pong Quarter"),
                  {{Delay::kMode, 1.0}, {Delay::kNote, 8.0}, {Delay::kFeedback, 45.0}, {Delay::kMix, 30.0}}},
              {tr("Dub Echo"),
                  {{Delay::kMode, 1.0}, {Delay::kNote, 10.0}, {Delay::kFeedback, 70.0},
                      {Delay::kLowCut, 200.0}, {Delay::kHighCut, 2500.0}, {Delay::kMix, 35.0}}},
              {tr("Wide Doubler"),
                  {{Delay::kSync, 0.0}, {Delay::kTime, 25.0}, {Delay::kStereoOffset, 30.0},
                      {Delay::kFeedback, 0.0}, {Delay::kMix, 35.0}}},
          },
          parent)
{
    display_ = new EchoDisplay;
    display_->onHandleDragged
        = [this](QPointF point) { dragged(display_->secondsAt(point.x()), display_->feedbackAt(point.y())); };
    display_->onHandleReset = [this] {
        for (const std::uint32_t id : {Delay::kTime, Delay::kNote, Delay::kFeedback})
            if (const vloom::ParameterInfo* known = parameter(id))
                change(id, known->defaultValue);
    };
    setDisplay(display_);

    // Sync and Ping-pong, one over the other.
    auto* switches = new QWidget;
    auto* column = new QVBoxLayout(switches);
    column->setContentsMargins(0, 0, 8, 0);
    column->setSpacing(10);
    PillToggle* sync = toggle(Delay::kSync, tr("Sync"), PillToggle::Layout::Inline);
    sync->setToolTip(tr("Time the echoes in notes at the song's tempo"));
    PillToggle* pingPong = toggle(Delay::kMode, tr("Ping-pong"), PillToggle::Layout::Inline);
    pingPong->setToolTip(tr("Bounce the echoes between left and right"));
    column->addWidget(sync, 0, Qt::AlignRight);
    column->addWidget(pingPong, 0, Qt::AlignRight);
    panel()->add(switches);

    ArcKnob* time = knob(Delay::kTime);
    ArcKnob* note = knob(Delay::kNote, tr("Time"));
    note->setAccessibleName(tr("Time in notes"));
    panel()->add(time);
    panel()->add(note);
    bind(Delay::kSync, [time, note](double on) {
        time->setVisible(on < 0.5);
        note->setVisible(on >= 0.5);
    });
    panel()->add(knob(Delay::kFeedback));
    panel()->add(knob(Delay::kLowCut));
    panel()->add(knob(Delay::kHighCut));
    panel()->add(knob(Delay::kWidth));
    panel()->add(knob(Delay::kMix));

    tempo_ = readout(tr("Tempo"));
    tempo_->setWidestText(QStringLiteral("999.0 BPM"));
    tempo_->setToolTip(tr("The song's tempo, which synced times follow"));
    footer()->addLeft(tempo_);
    delay_ = readout(tr("Delay"));
    delay_->setWidestText(QStringLiteral("L 1.00 s · R 1.00 s"));
    delay_->setToolTip(tr("How long each side waits before it echoes"));
    footer()->addLeft(delay_);
    Readout* offset = readout(Delay::kStereoOffset, tr("Stereo offset"));
    offset->setFormatter(time_effects::signedPercentText);
    offset->setWidestText(QStringLiteral("+50 %"));
    offset->setToolTip(tr("Makes the right side's delay longer or shorter than the left's"));
    footer()->addLeft(offset);
    Readout* output = readout(tr("Output"));
    output_.emplace(output);
    footer()->addRight(output);

    refresh();
    animate(display_);
}

double DelayEditor::delaySeconds(bool right) const
{
    const double left = value(Delay::kSync) >= 0.5
        ? Delay::noteBeats(static_cast<int>(value(Delay::kNote))) * 60.0 / tempo()
        : value(Delay::kTime) / 1000.0;
    const double seconds = right ? left * (1.0 + value(Delay::kStereoOffset) / 100.0) : left;
    return std::min(seconds, Delay::kMaxSeconds); // the plugin keeps it there
}

void DelayEditor::refresh()
{
    const double left = delaySeconds(false);
    const double right = delaySeconds(true);
    display_->setState(EchoSettings{.leftSeconds = left,
                           .rightSeconds = right,
                           .pingPong = value(Delay::kMode) >= 0.5,
                           .feedback = value(Delay::kFeedback) / 100.0,
                           .width = value(Delay::kWidth) / 100.0},
        value(Delay::kMix) / 100.0);
    tempo_->setText(
        QStringLiteral("%1 BPM").arg(QString::number(tempo(), 'f', std::round(tempo()) == tempo() ? 0 : 1)));
    const QString leftText = time_effects::timeText(left);
    delay_->setText(std::abs(right - left) < 1e-6
            ? leftText
            : QStringLiteral("L %1 · R %2").arg(leftText, time_effects::timeText(right)));
}

void DelayEditor::dragged(double seconds, std::optional<double> feedback)
{
    // The second echo comes after the left delay and then the right one
    // (ping-pong) or the left one again.
    const double ratio = 1.0 + value(Delay::kStereoOffset) / 100.0;
    const double left = seconds / (value(Delay::kMode) >= 0.5 ? 1.0 + ratio : 2.0);
    if (value(Delay::kSync) >= 0.5) {
        std::vector<double> beats;
        for (int i = 0; i < Delay::kNoteCount; ++i)
            beats.push_back(Delay::noteBeats(i));
        change(Delay::kNote, nearestNote(std::max(left, 1e-3), tempo(), beats));
    } else {
        change(Delay::kTime, left * 1000.0);
    }
    if (feedback)
        change(Delay::kFeedback, *feedback * 100.0);
}

void DelayEditor::parameterChanged(std::uint32_t /*id*/, double /*value*/) { refresh(); }

void DelayEditor::tempoChanged(double /*bpm*/) { refresh(); }

void DelayEditor::meter(const Meter& reading) { output_->push(reading.running, reading.outputDb); }

} // namespace vloomdaw
