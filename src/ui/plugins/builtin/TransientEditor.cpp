// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/TransientEditor.hpp"

#include "core/EffectPreview.hpp"
#include "core/ParameterScale.hpp"
#include "ui/plugins/builtin/Animation.hpp"
#include "ui/plugins/builtin/AnimationPaint.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/DisplayHelpers.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <vloom/dsp/effects/TransientShaper.hpp>

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>

namespace vloomdaw {

namespace {

namespace style = editor_style;
using Shaper = vloom::dsp::TransientShaper;

constexpr double kCentre = 126.0;
constexpr double kHalf = 110.0; ///< px from the centre to full scale (the hits peak at half of it)
constexpr double kPointPixels = 3.0;
/// The live level's head: px in from the right edge.
constexpr double kHeadInset = 10.0;
/// How fast the live level scrolls away from the head, px a second.
constexpr double kScrollPerSecond = 150.0;
/// Columns of the live level kept for the afterimage (one a frame).
constexpr std::size_t kColumns = 160;

QString dbText(double db) { return QString::fromStdString(formatParameterValue("dB", db)); }

} // namespace

/// The transient shaper's picture: hits before and after, mirrored.
///
/// Animated: the playing sound's level runs in from the right, mirrored
/// like the hits, and leaves an afterimage that lasts longer and spreads
/// more the more sustain; each onset flashes at the head and throws sparks,
/// more and faster the more attack.
class TransientDisplay : public QWidget, public Animated {
public:
    TransientDisplay(std::function<std::vector<double>()> settings, double sampleRate, QWidget* parent)
        : QWidget(parent)
        , settings_(std::move(settings))
        , sampleRate_(sampleRate)
    {
    }

    void refresh()
    {
        const auto points = static_cast<std::size_t>(std::max(16.0, std::ceil(width() / kPointPixels)));
        preview_ = transientPreview(settings_(), sampleRate_, points);
        ++generation_;
        update();
    }

    /// Attack and sustain (-100 .. +100 %): what the motion follows.
    void setShape(double attack, double sustain)
    {
        attack_ = std::clamp((attack + 100.0) / 200.0, 0.0, 1.0);
        sustain_ = std::clamp((sustain + 100.0) / 200.0, 0.0, 1.0);
    }

    bool advance(const motion::AudioFeatures& audio, double dt) override
    {
        const double live = audio.liveliness();
        const double centre = centreY();
        const double half = halfHeight();
        // The afterimage: the columns age; they last longer the more sustain.
        tail_ = 0.15 + 1.2 * sustain_;
        bool trailing = false;
        for (Column& each : columns_) {
            each.age += dt;
            trailing = trailing || (each.level > 0.002 && each.age < tail_);
        }

        level_ = motion::normalisedLevel(audio.peakDb(), -48.0) * live;
        const double headX = width() - kHeadInset;
        if (audio.onset()) {
            const double punch = audio.onsetStrength() * (0.3 + 0.7 * attack_) * live;
            flash_ = std::max(flash_, std::min(1.0, 0.25 + punch));
            const int count = static_cast<int>(std::lround(4.0 + 22.0 * attack_ * (0.4 + 0.6 * punch)));
            const double speed = 60.0 + 260.0 * attack_;
            for (int i = 0; i < count; ++i) {
                const double side = next() < 0.5 ? -1.0 : 1.0;
                const Spark spark{.x = headX,
                    .y = centre + side * level_ * half * next(),
                    .vx = -speed * (0.3 + 0.9 * next()),
                    .vy = side * speed * (0.15 + 0.8 * next()),
                    .size = 2.0 + 3.0 * next()};
                sparks_.spawn(spark, 0.2 + 0.25 * next() + 0.3 * attack_);
            }
        }
        flash_ *= std::exp(-dt / 0.12);
        if (level_ > 0.002 || trailing) {
            newest_ = (newest_ + 1) % kColumns;
            columns_[newest_] = Column{.level = level_, .bright = flash_, .age = 0.0};
        }
        const std::size_t alive = sparks_.advance(dt, [](Sparks::Particle& each, double time) {
            each.state.x += each.state.vx * time;
            each.state.y += each.state.vy * time;
            const double drag = std::exp(-time / 0.25);
            each.state.vx *= drag;
            each.state.vy *= drag;
        });
        const bool moving = alive > 0 || flash_ > 0.01 || level_ > 0.002 || trailing;
        if (moving || wasMoving_)
            update();
        wasMoving_ = moving;
        return moving;
    }

    void rest() override
    {
        columns_.fill(Column{});
        sparks_.clear();
        flash_ = 0.0;
        level_ = 0.0;
        wasMoving_ = false;
        update();
    }

    void animationSwitched(bool /*on*/) override { rest(); }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        refresh();
    }

    void paintEvent(QPaintEvent* /*event*/) override
    {
        if (!animationOn()) {
            QPainter p(this);
            paintPicture(p);
            return;
        }
        const AnimationPaintTimer timer;
        QPainter p(this);
        picture_.draw(p, *this, generation_, [this](QPainter& layer) { paintPicture(layer); });
        paintAfterimage(p);
        const QColor accent = style::accent();
        const double headX = width() - kHeadInset;
        const double centre = centreY();
        if (flash_ > 0.01) {
            const double reach = halfHeight() * (0.4 + 0.6 * flash_);
            motion_paint::glowLine(
                p, QLineF(headX, centre - reach, headX, centre + reach), accent, flash_, 10.0);
            motion_paint::glowDot(p, QPointF(headX, centre), 16.0 + 26.0 * flash_, accent, flash_);
        }
        if (level_ > 0.002) {
            const double extent = level_ * halfHeight();
            motion_paint::glowDot(p, QPointF(headX, centre - extent), 6.0, style::text(), level_);
            motion_paint::glowDot(p, QPointF(headX, centre + extent), 6.0, style::text(), level_);
        }
        motion_paint::GlowBatch glow(p, accent);
        sparks_.forEach([&glow](const Sparks::Particle& each) {
            const double fade = 1.0 - each.progress();
            glow.dot(QPointF(each.state.x, each.state.y), each.state.size, fade);
        });
    }

    /// The live level's columns, scrolling left from the head and fading
    /// (and spreading, the more sustain) as they age.
    void paintAfterimage(QPainter& p) const
    {
        const QColor accent = style::accent();
        const double headX = width() - kHeadInset;
        const double centre = centreY();
        const double half = halfHeight();
        const double columnWidth = std::max(2.0, kScrollPerSecond / 60.0 + 0.5);
        for (const Column& each : columns_) {
            if (each.level <= 0.002 || each.age >= tail_)
                continue;
            const double fade = motion::decayFactor(each.age, tail_, 40.0);
            const double extent = each.level * half * (1.0 + 0.3 * sustain_ * each.age / tail_);
            const double x = headX - each.age * kScrollPerSecond;
            p.fillRect(QRectF(x, centre - extent, columnWidth, 2.0 * extent),
                motion_paint::faded(accent, fade * (0.3 + 0.5 * each.bright)));
        }
    }

    /// The display without the motion.
    void paintPicture(QPainter& p) const
    {
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), style::background());
        const double centre = centreY();
        const double half = halfHeight();
        p.setPen(QPen(style::line(), 1.0));
        p.drawLine(QPointF(0.0, centre + 0.5), QPointF(width(), centre + 0.5));

        QColor before = style::text();
        before.setAlphaF(0.12f);
        QColor beforeLine = style::text();
        beforeLine.setAlphaF(0.35f);
        p.setPen(QPen(beforeLine, 1.0));
        p.setBrush(before);
        p.drawPath(mirrored(preview_.dry, centre, half));
        QColor after = style::accent();
        after.setAlphaF(0.2f);
        p.setPen(QPen(style::accent(), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(after);
        p.drawPath(mirrored(preview_.wet, centre, half));

        p.setPen(style::muted());
        p.setFont(style::font(font(), 10));
        p.drawText(QPointF(12.0, 24.0), tr("Input (grey) and output"));
    }

private:
    struct Column {
        double level = 0.0; ///< 0 .. 1
        double bright = 0.0; ///< the flash when it came in
        double age = 0.0; ///< s
    };

    struct Spark {
        double x = 0.0;
        double y = 0.0;
        double vx = 0.0;
        double vy = 0.0;
        double size = 0.0;
    };
    using Sparks = motion::ParticlePool<Spark, 64>;

    [[nodiscard]] double centreY() const { return std::min(kCentre, height() / 2.0); }
    [[nodiscard]] double halfHeight() const { return std::min(kHalf, centreY() - 6.0); }

    /// 0 .. 1, the same sequence every run.
    double next()
    {
        seed_ = seed_ * 1664525U + 1013904223U;
        return static_cast<double>(seed_ >> 8U) / 16777216.0;
    }

    /// The levels as a shape mirrored about `centre`; full scale at `half`
    /// px, louder cut off.
    [[nodiscard]] QPainterPath mirrored(const std::vector<float>& levels, double centre, double half) const
    {
        QPainterPath path;
        if (levels.empty())
            return path;
        const double step = width() / static_cast<double>(std::max<std::size_t>(levels.size() - 1, 1));
        const auto height = [&](float level) {
            return std::clamp(std::isfinite(level) ? static_cast<double>(level) : 0.0, 0.0, 1.0) * half;
        };
        path.moveTo(0.0, centre - height(levels.front()));
        for (std::size_t i = 1; i < levels.size(); ++i)
            path.lineTo(step * static_cast<double>(i), centre - height(levels[i]));
        for (std::size_t i = levels.size(); i-- > 0;)
            path.lineTo(step * static_cast<double>(i), centre + height(levels[i]));
        path.closeSubpath();
        return path;
    }

    std::function<std::vector<double>()> settings_;
    double sampleRate_;
    WavePreview preview_;

    // The motion (animationOn()).
    motion_paint::LayerCache picture_;
    std::uint64_t generation_ = 0; ///< changes with the preview
    std::array<Column, kColumns> columns_{};
    std::size_t newest_ = 0;
    double tail_ = 0.75; ///< s the afterimage lasts
    Sparks sparks_;
    double attack_ = 0.5; ///< 0 .. 1 (-100 .. +100 %)
    double sustain_ = 0.5;
    double level_ = 0.0; ///< the live level, 0 .. 1
    double flash_ = 0.0;
    std::uint32_t seed_ = 0x9e3779b9U;
    bool wasMoving_ = false;
};

TransientEditor::TransientEditor(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate, QWidget* parent)
    : BuiltinEditorWindow(std::move(instance), title, sampleRate,
          {
              {tr("Punchy Drums"), {{Shaper::kAttack, 60.0}, {Shaper::kSustain, -20.0}}},
              {tr("Tight Kick"),
                  {{Shaper::kAttack, 40.0}, {Shaper::kSustain, -60.0}, {Shaper::kSpeed, 70.0}}},
              {tr("Snappy Snare"),
                  {{Shaper::kAttack, 80.0}, {Shaper::kSustain, -10.0}, {Shaper::kSpeed, 80.0},
                      {Shaper::kOutput, -3.0}}},
              {tr("More Room"), {{Shaper::kAttack, -10.0}, {Shaper::kSustain, 60.0}, {Shaper::kSpeed, 30.0}}},
              {tr("Soften Attack"), {{Shaper::kAttack, -60.0}, {Shaper::kSpeed, 60.0}}},
          },
          parent)
{
    resize(900, 440);
    display_ = new TransientDisplay(
        [this] { return display::settingsOf(*this, Shaper::kParameterCount); }, this->sampleRate(), nullptr);
    display_->setAccessibleName(tr("The level of four hits before and after shaping"));
    setDisplay(display_);
    display_->setShape(value(Shaper::kAttack), value(Shaper::kSustain));
    animate(display_);

    panel()->add(knob(Shaper::kAttack));
    panel()->add(knob(Shaper::kSustain));
    panel()->add(knob(Shaper::kOutput));
    panel()->add(knob(Shaper::kMix));

    auto* speed = readout(Shaper::kSpeed, tr("Speed"));
    speed->setWidestText(QStringLiteral("100 %"));
    speed->setToolTip(tr("How short the attack and the start of the tail are: high for clicks, low for a "
                         "fuller punch"));
    footer()->addLeft(speed);

    peak_ = readout(tr("Output peak"));
    peak_->setWidestText(dbText(-60.0));
    peak_->setText(QStringLiteral("–"));
    footer()->addRight(peak_);

    startMetering();
}

void TransientEditor::parameterChanged(std::uint32_t /*id*/, double /*value*/)
{
    display_->setShape(value(Shaper::kAttack), value(Shaper::kSustain));
    display_->refresh();
}

void TransientEditor::meter(const Meter& reading)
{
    peak_->setText(
        std::isfinite(reading.outputDb) && reading.running ? dbText(reading.outputDb) : QStringLiteral("–"));
}

} // namespace vloomdaw
