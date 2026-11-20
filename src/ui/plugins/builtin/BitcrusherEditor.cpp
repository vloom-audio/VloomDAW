// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/BitcrusherEditor.hpp"

#include "core/EffectPreview.hpp"
#include "core/ParameterScale.hpp"
#include "ui/plugins/builtin/Animation.hpp"
#include "ui/plugins/builtin/AnimationPaint.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/DisplayHelpers.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <vloom/dsp/effects/Bitcrusher.hpp>

#include <QPainter>
#include <QPolygonF>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>

namespace vloomdaw {

namespace {

namespace style = editor_style;
using Bitcrusher = vloom::dsp::Bitcrusher;

constexpr double kUnit = 110.0; ///< px for full scale
constexpr double kCentre = 126.0; ///< the wave's zero line, from the top
constexpr double kMinLevelGap = 6.0; ///< px: closer quantizer levels get no grid lines
constexpr std::size_t kLiveFrames = 1024; ///< the newest samples the live trace spans
constexpr std::size_t kSearchFrames = 512; ///< how far back a rising zero crossing is looked for
constexpr std::size_t kSparks = 64;
constexpr int kMaxGlowStamps = 90; ///< glow sprites along the live trace, at most

QString dbText(double db) { return QString::fromStdString(formatParameterValue("dB", db)); }

} // namespace

/// The bitcrusher's picture: a sine before and after.
///
/// Animated: the sound coming out runs across it as glowing stair steps,
/// held for the downsampling and rounded to the bits' levels (the effect's
/// own settings), and each hit throws out square pixel sparks, more the
/// harder it crushes.
class BitcrusherDisplay : public QWidget, public Animated {
public:
    BitcrusherDisplay(std::function<std::vector<double>()> settings, double sampleRate, QWidget* parent)
        : QWidget(parent)
        , settings_(std::move(settings))
        , sampleRate_(sampleRate)
    {
        refresh();
    }

    void refresh()
    {
        values_ = settings_();
        wave_ = crusherPreview(values_, sampleRate_);
        key_ = 0;
        for (const double v : values_)
            key_ = motion_paint::keyWith(key_, v);
        update();
    }

    bool advance(const motion::AudioFeatures& audio, double dt) override
    {
        const double live = audio.liveliness();
        glow_ = audio.level() * live;
        const std::span<const float> wave = audio.waveform();
        // Steady like a scope: start at a rising zero crossing.
        std::size_t from = wave.size() >= kLiveFrames ? wave.size() - kLiveFrames : 0;
        for (std::size_t back = 0; back < kSearchFrames && from > back + 1; ++back) {
            const std::size_t i = from - back;
            if (wave[i - 1] < 0.0F && wave[i] >= 0.0F) {
                from = i;
                break;
            }
        }
        live_.fill(0.0F);
        for (std::size_t i = 0; i < kLiveFrames && from + i < wave.size(); ++i)
            live_[i] = wave[from + i] * static_cast<float>(live);

        const auto spread = [this] {
            seed_ = seed_ * 1664525U + 1013904223U;
            return static_cast<double>(seed_ >> 8) / 16777216.0;
        };
        sparks_.advance(dt, [](auto& spark, double step) {
            spark.state.position += spark.state.velocity * step;
            spark.state.velocity.ry() += 260.0 * step; // they fall
        });
        if (audio.onset() && live > 0.0 && trace_.size() > 1) {
            const auto count = static_cast<int>(std::lround((3.0 + 26.0 * crush()) * audio.onsetStrength()));
            for (int k = 0; k < count; ++k) {
                const auto index = std::min(
                    trace_.size() - 1, static_cast<qsizetype>(spread() * static_cast<double>(trace_.size())));
                const double angle = spread() * 6.283185307;
                const double speed = 60.0 + 180.0 * spread() * (0.5 + crush());
                sparks_.spawn(
                    Spark{.position = trace_[index],
                        .velocity = QPointF(std::cos(angle) * speed, std::sin(angle) * speed - 80.0)},
                    0.4 + 0.5 * spread());
            }
        }
        update(band().toAlignedRect());
        return glow_ > 0.002 || sparks_.alive() > 0;
    }

    void rest() override
    {
        glow_ = 0.0;
        live_.fill(0.0F);
        sparks_.clear();
        trace_.clear();
        update();
    }

    void animationSwitched(bool /*on*/) override { rest(); }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const AnimationPaintTimer timer;
        QPainter p(this);
        back_.draw(p, *this, key_, [this](QPainter& layer) { paintStatic(layer); });
        if (animationOn())
            paintMotion(p);
    }

    void paintMotion(QPainter& p)
    {
        const double centre = std::min(kCentre, height() / 2.0);
        const double unit = std::min(kUnit, centre - 12.0);
        const double step = Bitcrusher::stepFor(setting(Bitcrusher::kBits, 24.0));
        const auto hold = std::max<std::size_t>(
            1, static_cast<std::size_t>(std::lround(setting(Bitcrusher::kDownsample, 1.0))));
        const bool rounds = step * unit >= 2.0;
        const double perFrame = width() / static_cast<double>(kLiveFrames);

        // The stair steps: each held value flat, a riser to the next.
        trace_.resize(0);
        if (glow_ > 0.002) {
            double last = 0.0;
            for (std::size_t i = 0; i < kLiveFrames; i += hold) {
                double v = std::clamp(static_cast<double>(live_[i]), -1.1, 1.1);
                if (rounds)
                    v = std::round(v / step) * step;
                const double yv = std::round(centre - v * unit) + 0.5;
                const double x = std::round(static_cast<double>(i) * perFrame) + 0.5;
                if (i > 0)
                    trace_.append(QPointF(x, last));
                trace_.append(QPointF(x, yv));
                last = yv;
            }
            trace_.append(QPointF(width(), last));
            const QColor accent = style::accent();
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(QPen(motion_paint::faded(accent, 0.35 + 0.65 * glow_), 1.0));
            p.drawPolyline(trace_);
            // A soft glow on the steps' corners, spread along the trace.
            const qsizetype every = std::max<qsizetype>(1, trace_.size() / kMaxGlowStamps);
            motion_paint::GlowBatch glow(p, accent);
            for (qsizetype i = 0; i < trace_.size(); i += every)
                glow.dot(trace_[i], 5.0, 0.35 * glow_);
        }
        if (sparks_.alive() == 0)
            return;
        // Pixels as big as one level of the quantizer (2 .. 6 px).
        const double size = std::clamp(step * unit, 2.0, 6.0);
        p.setRenderHint(QPainter::Antialiasing, false);
        const QColor accent = style::accent();
        const QColor text = style::text();
        sparks_.forEach([&](const auto& spark) {
            const double fade = 1.0 - spark.progress();
            const QPointF at(std::round(spark.state.position.x() / size) * size,
                std::round(spark.state.position.y() / size) * size);
            p.fillRect(QRectF(at.x(), at.y(), size, size),
                motion_paint::faded(spark.progress() < 0.25 ? text : accent, fade));
        });
    }

    void paintStatic(QPainter& p)
    {
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), style::background());
        const double centre = std::min(kCentre, height() / 2.0);
        const double unit = std::min(kUnit, centre - 12.0);

        // The levels the quantizer rounds to (drive and output aside).
        const double bits = values_.size() > Bitcrusher::kBits ? values_[Bitcrusher::kBits] : 24.0;
        const double step = Bitcrusher::stepFor(bits);
        if (step * unit >= kMinLevelGap) {
            p.setPen(QPen(style::grid(), 1.0));
            for (double level = step; level <= 1.0 + 1e-9; level += step)
                for (const double y : {centre - level * unit, centre + level * unit})
                    p.drawLine(QPointF(0.0, std::round(y) + 0.5), QPointF(width(), std::round(y) + 0.5));
        }
        p.setPen(QPen(style::line(), 1.0));
        p.drawLine(QPointF(0.0, centre + 0.5), QPointF(width(), centre + 0.5));

        if (!wave_.dry.empty()) {
            const QRectF area(0.0, 0.0, width(), height());
            p.save();
            p.setClipRect(QRectF(0.0, centre - unit - 14.0, width(), 2.0 * unit + 28.0));
            const QPainterPath dry = display::wavePath(wave_.dry, area, centre, unit, false);
            // Held samples show as steps; not downsampled, every sample is a
            // new one and the line joins them.
            const double factor
                = values_.size() > Bitcrusher::kDownsample ? values_[Bitcrusher::kDownsample] : 1.0;
            const QPainterPath wet = display::wavePath(wave_.wet, area, centre, unit, factor > 1.0);
            QPainterPath fill = wet;
            fill.lineTo(width(), centre);
            fill.lineTo(0.0, centre);
            fill.closeSubpath();
            QColor shade = style::accent();
            shade.setAlphaF(0.16f);
            p.setPen(Qt::NoPen);
            p.setBrush(shade);
            p.drawPath(fill);
            QColor dryColour = style::text();
            dryColour.setAlphaF(0.5f);
            QPen dryPen(dryColour, 1.5);
            dryPen.setDashPattern({4.0 / 1.5, 4.0 / 1.5});
            p.setPen(dryPen);
            p.setBrush(Qt::NoBrush);
            p.drawPath(dry);
            p.setPen(QPen(style::accent(), 2.5, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
            p.drawPath(wet);
            p.restore();
        }
        p.setPen(style::muted());
        p.setFont(style::font(font(), 10));
        p.drawText(QPointF(12.0, 20.0), tr("Before (dashed) and after"));
    }

private:
    struct Spark {
        QPointF position;
        QPointF velocity; ///< px/s
    };

    [[nodiscard]] double setting(std::uint32_t id, double fallback) const
    {
        return id < values_.size() ? values_[id] : fallback;
    }
    /// How hard it crushes, 0 .. 1: fewer bits and more downsampling.
    [[nodiscard]] double crush() const
    {
        const double bits = std::clamp((16.0 - setting(Bitcrusher::kBits, 24.0)) / 14.0, 0.0, 1.0);
        const double down
            = std::clamp(std::log2(std::max(1.0, setting(Bitcrusher::kDownsample, 1.0))) / 5.0, 0.0, 1.0);
        return 0.6 * bits + 0.4 * down;
    }
    /// The band the live trace and the sparks move in.
    [[nodiscard]] QRectF band() const
    {
        const double centre = std::min(kCentre, height() / 2.0);
        const double unit = std::min(kUnit, centre - 12.0);
        return {0.0, centre - unit - 40.0, static_cast<double>(width()), 2.0 * unit + 80.0};
    }

    std::function<std::vector<double>()> settings_;
    double sampleRate_;
    std::vector<double> values_;
    WavePreview wave_;

    // The motion (animationOn()).
    std::uint64_t key_ = 0;
    motion_paint::LayerCache back_;
    std::array<float, kLiveFrames> live_{}; ///< the newest samples, from a rising zero crossing
    QPolygonF trace_; ///< the stair steps as drawn (reused: no allocation once grown)
    motion::ParticlePool<Spark, kSparks> sparks_;
    std::uint32_t seed_ = 0x2545F491U; ///< spreads the sparks a hit throws (never starts them)
    double glow_ = 0.0;
};

BitcrusherEditor::BitcrusherEditor(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate, QWidget* parent)
    : BuiltinEditorWindow(std::move(instance), title, sampleRate,
          {
              {tr("8-bit Console"), {{Bitcrusher::kBits, 8.0}, {Bitcrusher::kDownsample, 4.0}}},
              {tr("Vintage Sampler"),
                  {{Bitcrusher::kBits, 12.0}, {Bitcrusher::kDownsample, 3.0}, {Bitcrusher::kAntiAlias, 1.0}}},
              {tr("Telephone"),
                  {{Bitcrusher::kBits, 6.0}, {Bitcrusher::kDownsample, 6.0}, {Bitcrusher::kDrive, 6.0},
                      {Bitcrusher::kAntiAlias, 1.0}, {Bitcrusher::kOutput, -4.0}}},
              {tr("Broken Toy"),
                  {{Bitcrusher::kBits, 3.0}, {Bitcrusher::kDownsample, 16.0}, {Bitcrusher::kJitter, 40.0},
                      {Bitcrusher::kDrive, 12.0}, {Bitcrusher::kOutput, -8.0}}},
              {tr("Subtle Grit"),
                  {{Bitcrusher::kBits, 10.0}, {Bitcrusher::kDownsample, 2.0}, {Bitcrusher::kDither, 50.0},
                      {Bitcrusher::kMix, 40.0}}},
          },
          parent)
{
    resize(900, 440);
    display_
        = new BitcrusherDisplay([this] { return display::settingsOf(*this, Bitcrusher::kParameterCount); },
            this->sampleRate(), nullptr);
    display_->setAccessibleName(tr("A sine before and after crushing"));
    setDisplay(display_);
    animate(display_);

    auto* bits = knob(Bitcrusher::kBits, tr("Bits"));
    bits->setFormatter([](double v) { return QString::number(v, 'f', 1); });
    panel()->add(bits);
    auto* downsample = knob(Bitcrusher::kDownsample, tr("Downsample"), ParameterScale::Log);
    downsample->setFormatter([](double v) { return QStringLiteral("x%1").arg(v, 0, 'f', v < 10.0 ? 2 : 1); });
    panel()->add(downsample);
    panel()->add(knob(Bitcrusher::kJitter));
    panel()->add(knob(Bitcrusher::kDrive));
    panel()->add(knob(Bitcrusher::kMix));
    panel()->add(toggle(Bitcrusher::kAntiAlias, tr("Anti-alias")));

    auto* dither = readout(Bitcrusher::kDither, tr("Dither"));
    dither->setFormatter(
        [](double v) { return v <= 0.0 ? tr("Off") : QString::fromStdString(formatParameterValue("%", v)); });
    dither->setWidestText(QStringLiteral("100 %"));
    footer()->addLeft(dither);
    rate_ = readout(tr("Rate"));
    rate_->setWidestText(QStringLiteral("48.00 kHz"));
    rate_->setToolTip(tr("The sample rate the downsampling leaves"));
    footer()->addLeft(rate_);
    bind(Bitcrusher::kDownsample, [this](double factor) {
        rate_->setText(
            QString::fromStdString(formatParameterValue("Hz", this->sampleRate() / std::max(1.0, factor))));
    });

    auto* output = readout(Bitcrusher::kOutput, tr("Output"));
    output->setWidestText(dbText(-24.0));
    footer()->addRight(output);
    peak_ = readout(tr("Peak"));
    peak_->setWidestText(dbText(-60.0));
    peak_->setText(QStringLiteral("–"));
    footer()->addRight(peak_);

    startMetering();
}

void BitcrusherEditor::parameterChanged(std::uint32_t /*id*/, double /*value*/) { display_->refresh(); }

void BitcrusherEditor::meter(const Meter& reading)
{
    peak_->setText(
        std::isfinite(reading.outputDb) && reading.running ? dbText(reading.outputDb) : QStringLiteral("–"));
}

} // namespace vloomdaw
