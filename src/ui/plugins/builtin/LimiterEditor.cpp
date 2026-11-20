// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/LimiterEditor.hpp"

#include "core/DynamicsMeter.hpp"
#include "core/ParameterScale.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"
#include "ui/plugins/builtin/LevelHistoryView.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <vloom/dsp/effects/Limiter.hpp>
#include <vloom/engine/plugin/PluginInstance.hpp>

#include <QEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace vloomdaw {

namespace {

using Limiter = vloom::dsp::Limiter;

constexpr double kTopDb = 0.0;
constexpr double kBottomDb = -36.0;

QString dbText(double db) { return QString::fromStdString(formatParameterValue("dB", db)); }

/// LevelHistoryView's mapping of a level to y (its padding above the top
/// level and below the bottom one).
constexpr double kViewPadding = 20.0;
/// How far the history scrolls a second (LevelHistoryView::kStep a reading,
/// about 30 readings a second).
constexpr double kScrollPerSecond = static_cast<double>(LevelHistoryView::kStep) * 30.0;

} // namespace

/// Over the level display (animated only): where the peaks hit the ceiling
/// it flares, and sparks fly off the line and scroll away with the history.
class CeilingFlare : public QWidget, public Animated {
public:
    explicit CeilingFlare(QWidget* view)
        : QWidget(view)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        view->installEventFilter(this);
        setGeometry(view->rect());
    }

    void setCeiling(double db)
    {
        ceilingDb_ = db;
        update();
    }

    bool advance(const motion::AudioFeatures& audio, double dt) override
    {
        const double live = audio.liveliness();
        const double reduction = audio.reductionDb() * live;
        // Peaks hitting the ceiling: the reduction rising (a new peak caught).
        const double rise = std::max(0.0, reduction - lastReduction_);
        lastReduction_ = reduction;
        flare_ = std::max(flare_ * std::exp(-dt / 0.18), std::min(1.0, rise / 1.5 + reduction / 12.0));
        const double y = ceilingY();
        if (rise > 0.05 && live > 0.0) {
            const int count = std::clamp(static_cast<int>(rise * 3.0), 1, 6);
            for (int i = 0; i < count; ++i) {
                const Spark spark{.x = width() - 4.0 - 6.0 * next(),
                    .y = y,
                    .vx = -kScrollPerSecond * (0.8 + 0.4 * next()),
                    .vy = -(30.0 + 90.0 * next()) * std::min(1.0, 0.3 + rise / 3.0),
                    .size = 2.5 + 3.0 * next()};
                sparks_.spawn(spark, 0.35 + 0.4 * next());
            }
        }
        const std::size_t alive = sparks_.advance(dt, [](Sparks::Particle& each, double step) {
            each.state.x += each.state.vx * step;
            each.state.y += each.state.vy * step;
            each.state.vy += 140.0 * step; // falls back to the line
        });
        if (alive > 0 || flare_ > 0.01 || wasMoving_)
            update(QRect(0, static_cast<int>(y) - 90, width(), 120));
        wasMoving_ = alive > 0 || flare_ > 0.01;
        return wasMoving_;
    }

    void rest() override
    {
        sparks_.clear();
        flare_ = 0.0;
        lastReduction_ = 0.0;
        wasMoving_ = false;
        update();
    }

    void animationSwitched(bool /*on*/) override { rest(); }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == parentWidget() && event->type() == QEvent::Resize)
            setGeometry(parentWidget()->rect());
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent* /*event*/) override
    {
        if (!animationOn() || (flare_ <= 0.01 && sparks_.alive() == 0))
            return;
        const AnimationPaintTimer timer;
        QPainter p(this);
        const QColor accent = editor_style::accent();
        const double y = std::round(ceilingY()) + 0.5;
        if (flare_ > 0.01) {
            // The newest stretch of the line, where the peaks meet it, burns.
            const double reach = 60.0 + 140.0 * flare_;
            motion_paint::glowLine(
                p, QLineF(width() - reach, y, width(), y), motion_paint::faded(accent, 0.8), flare_, 7.0);
            motion_paint::glowDot(p, QPointF(width() - 6.0, y), 10.0 + 14.0 * flare_, accent, flare_);
            motion_paint::glowDot(p, QPointF(width() - 6.0, y), 4.0, editor_style::text(), flare_);
        }
        motion_paint::GlowBatch glow(p, accent);
        sparks_.forEach([&glow](const Sparks::Particle& each) {
            const double fade = 1.0 - each.progress();
            glow.dot(QPointF(each.state.x, each.state.y), each.state.size * (0.6 + 0.4 * fade), fade);
        });
    }

private:
    struct Spark {
        double x = 0.0;
        double y = 0.0;
        double vx = 0.0;
        double vy = 0.0;
        double size = 0.0;
    };
    using Sparks = motion::ParticlePool<Spark, 48>;

    [[nodiscard]] double ceilingY() const
    {
        const double perDb = std::max(1.0, height() - 2.0 * kViewPadding) / (kTopDb - kBottomDb);
        return kViewPadding + (kTopDb - std::clamp(ceilingDb_, kBottomDb, kTopDb)) * perDb;
    }

    /// 0 .. 1, the same sequence every run.
    double next()
    {
        seed_ = seed_ * 1664525U + 1013904223U;
        return static_cast<double>(seed_ >> 8U) / 16777216.0;
    }

    Sparks sparks_;
    double ceilingDb_ = -1.0;
    double flare_ = 0.0;
    double lastReduction_ = 0.0;
    std::uint32_t seed_ = 0x51ed2701U;
    bool wasMoving_ = false;
};

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
    flare_ = new CeilingFlare(levels_);
    bind(Limiter::kCeiling, [this](double db) {
        levels_->setMarker(db, tr("Ceiling %1").arg(dbText(db)));
        flare_->setCeiling(db);
    });
    animate(levels_);
    animate(flare_);

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
