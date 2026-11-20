// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/UtilityEditor.hpp"

#include "core/EffectPreview.hpp"
#include "core/ParameterScale.hpp"
#include "ui/plugins/builtin/Animation.hpp"
#include "ui/plugins/builtin/AnimationPaint.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/DisplayHelpers.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"
#include "ui/plugins/builtin/PillToggle.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <vloom/dsp/effects/Utility.hpp>

#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <numbers>
#include <span>
#include <utility>

namespace vloomdaw {

namespace {

namespace style = editor_style;
using Utility = vloom::dsp::Utility;

constexpr double kCentreY = 126.0;
constexpr double kRadius = 104.0;
constexpr double kUnitShare = 0.8; ///< of the radius: a sound at 0 dB
constexpr int kSources = 13; ///< the fan of sounds, hard left to hard right
constexpr double kBlockWidth = 220.0;
constexpr double kBarHeight = 12.0;
constexpr double kGainTopDb = 24.0;
constexpr double kGainBottomDb = Utility::kMinGainDb;
constexpr std::size_t kCloud = 96; ///< the newest samples drawn as the live dots

QString dbText(double db) { return QString::fromStdString(formatParameterValue("dB", db)); }

QString panText(double pan)
{
    const long rounded = std::lround(pan);
    if (rounded == 0)
        return QStringLiteral("C");
    return rounded < 0 ? QStringLiteral("%1 L").arg(-rounded) : QStringLiteral("%1 R").arg(rounded);
}

} // namespace

/// The utility's picture: gain per side, the stereo field and the
/// balance, width and polarity.
///
/// Animated: the stereo field's fan glows with the level, and the sound
/// heard now shows as a cloud of dots on it, with a glow at the centre that
/// swells with the envelope. The scope is mono today (left and right
/// averaged), so the dots lie on the Mid axis.
// TODO(stereo-scope): once motion::AudioInput's left and right are filled
// (BuiltinEditorWindow::frame()) and AudioFeatures keeps them, plot each
// sample at ((R - L) / sqrt 2, (L + R) / sqrt 2): a real goniometer cloud.
class UtilityDisplay : public QWidget, public Animated {
public:
    UtilityDisplay(std::function<std::vector<double>()> settings, double sampleRate, QWidget* parent)
        : QWidget(parent)
        , settings_(std::move(settings))
        , sampleRate_(sampleRate)
    {
        refresh();
    }

    void refresh()
    {
        values_ = settings_();
        matrix_ = utilityMatrix(values_, sampleRate_);
        key_ = 0;
        for (const double v : values_)
            key_ = motion_paint::keyWith(key_, v);
        update();
    }

    bool advance(const motion::AudioFeatures& audio, double dt) override
    {
        const double live = audio.liveliness();
        glow_ = audio.level() * live;
        swell_ = motion::approach(swell_, audio.envelope() * live, dt, 0.05);
        const std::span<const float> wave = audio.waveform();
        // Every other one of the newest samples: about 4 ms of sound.
        for (std::size_t i = 0; i < kCloud; ++i) {
            const std::size_t at = wave.size() >= 2 * kCloud ? wave.size() - 2 * kCloud + 2 * i : 0;
            cloud_[i] = at < wave.size() ? wave[at] * static_cast<float>(live) : 0.0F;
        }
        update(fieldRect().toAlignedRect());
        return glow_ > 0.002 || swell_ > 0.002;
    }

    void rest() override
    {
        glow_ = 0.0;
        swell_ = 0.0;
        cloud_.fill(0.0F);
        update();
    }

    void animationSwitched(bool /*on*/) override { rest(); }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const AnimationPaintTimer timer;
        QPainter p(this);
        back_.draw(p, *this, key_, [this](QPainter& l) {
            l.setRenderHint(QPainter::Antialiasing);
            l.fillRect(rect(), style::background());
            const double scale = std::min(1.0, width() / 900.0);
            paintGains(l, 40.0 * scale);
            paintField(l, centre());
            paintSettings(l, width() - (kBlockWidth + 50.0) * scale);
        });
        if (animationOn())
            paintMotion(p);
    }

    void paintMotion(QPainter& p)
    {
        if (glow_ <= 0.002 && swell_ <= 0.002)
            return;
        const QPointF c = centre();
        const double r = radius();
        const QColor accent = style::accent();
        // The fan's edges, hard left and hard right, glow with the level.
        motion_paint::glowStroke(p, edges_, accent, 0.7 * glow_, 6.0, 5.0);
        // The sound now: mono sits on the Mid axis.
        motion_paint::glowDot(p, c, 10.0 + 0.6 * r * swell_, accent, 0.5 * swell_);
        {
            motion_paint::GlowBatch dots(p, style::text());
            for (const float sample : cloud_) {
                const double v = std::clamp(static_cast<double>(sample), -1.25, 1.25);
                if (std::abs(v) < 1e-4)
                    continue;
                dots.dot(QPointF(c.x(), c.y() - v * kUnitShare * r), 3.0, 0.25 + 0.5 * glow_);
            }
        }
    }

private:
    [[nodiscard]] QPointF centre() const { return {width() / 2.0, std::min(kCentreY, height() / 2.0)}; }
    [[nodiscard]] double radius() const { return std::min(kRadius, centre().y() - 18.0); }
    /// What the motion covers: the field and its glow.
    [[nodiscard]] QRectF fieldRect() const
    {
        const double r = radius() + 16.0;
        return {centre().x() - r, centre().y() - r, 2.0 * r, 2.0 * r};
    }

    [[nodiscard]] double setting(std::uint32_t id) const { return id < values_.size() ? values_[id] : 0.0; }

    void bar(QPainter& p, const QRectF& well, double from, double to, const QColor& fill) const
    {
        p.setPen(Qt::NoPen);
        p.setBrush(style::faint());
        p.drawRoundedRect(well, 3.0, 3.0);
        if (to > from) {
            p.setBrush(fill);
            p.drawRoundedRect(QRectF(well.left() + from * well.width(), well.top(),
                                  (to - from) * well.width(), well.height()),
                3.0, 3.0);
        }
    }

    void paintGains(QPainter& p, double left) const
    {
        const double barLeft = left + 18.0;
        p.setFont(style::font(font(), 10));
        p.setPen(style::muted());
        p.drawText(QPointF(left, 92.0), tr("Gain, a sound in the middle"));
        const auto share = [](double gain) {
            if (!(gain > 0.0))
                return 0.0;
            const double db = 20.0 * std::log10(gain);
            return std::clamp((db - kGainBottomDb) / (kGainTopDb - kGainBottomDb), 0.0, 1.0);
        };
        const std::array<std::pair<QString, double>, 2> sides{{
            {tr("L"), std::abs(matrix_.ll + matrix_.lr)},
            {tr("R"), std::abs(matrix_.rl + matrix_.rr)},
        }};
        double top = 108.0;
        for (const auto& [name, gain] : sides) {
            p.setFont(style::font(font(), 11, QFont::DemiBold));
            p.setPen(style::text());
            p.drawText(
                QRectF(left, top - 2.0, 18.0, kBarHeight + 4.0), Qt::AlignLeft | Qt::AlignVCenter, name);
            bar(p, QRectF(barLeft, top, kBlockWidth, kBarHeight), 0.0, share(gain), style::level());
            top += 26.0;
        }
        const double zero = barLeft + share(1.0) * kBlockWidth;
        p.setPen(QPen(style::strong(), 1.0));
        p.drawLine(QPointF(zero, 104.0), QPointF(zero, top - 10.0));
        p.setFont(style::font(font(), 10));
        p.setPen(style::muted());
        p.drawText(QPointF(barLeft, top + 4.0), QStringLiteral("−60"));
        p.drawText(QRectF(zero - 20.0, top - 8.0, 40.0, 14.0), Qt::AlignCenter, QStringLiteral("0"));
        p.drawText(QRectF(barLeft, top - 8.0, kBlockWidth, 14.0), Qt::AlignRight | Qt::AlignVCenter,
            QStringLiteral("+24 dB"));
    }

    void paintField(QPainter& p, const QPointF& centre)
    {
        const double r = std::min(kRadius, centre.y() - 18.0);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(style::faint(), 1.0));
        p.drawEllipse(centre, r, r);
        p.drawLine(QPointF(centre.x(), centre.y() - r), QPointF(centre.x(), centre.y() + r));
        p.drawLine(QPointF(centre.x() - r, centre.y()), QPointF(centre.x() + r, centre.y()));
        p.setPen(QPen(style::grid(), 1.0));
        p.drawEllipse(centre, r / 2.0, r / 2.0);
        const double d = r / std::numbers::sqrt2;
        p.drawLine(QPointF(centre.x() - d, centre.y() - d), QPointF(centre.x() + d, centre.y() + d));
        p.drawLine(QPointF(centre.x() + d, centre.y() - d), QPointF(centre.x() - d, centre.y() + d));
        p.setFont(style::font(font(), 10));
        p.setPen(style::muted());
        p.drawText(QRectF(centre.x() - 30.0, centre.y() - r - 18.0, 60.0, 14.0), Qt::AlignCenter, tr("Mid"));
        p.drawText(QPointF(centre.x() - d - 12.0, centre.y() - d - 4.0), tr("L"));
        p.drawText(QPointF(centre.x() + d + 6.0, centre.y() - d - 4.0), tr("R"));
        p.drawText(QPointF(centre.x() + r + 6.0, centre.y() + 4.0), tr("Side"));

        // Where the settings put sounds panned hard left .. hard right.
        std::array<QPointF, kSources> ends{};
        for (int k = 0; k < kSources; ++k) {
            const double angle = std::numbers::pi / 2.0 * k / (kSources - 1);
            const double inLeft = std::cos(angle);
            const double inRight = std::sin(angle);
            const double outLeft = matrix_.ll * inLeft + matrix_.lr * inRight;
            const double outRight = matrix_.rl * inLeft + matrix_.rr * inRight;
            QPointF end(
                (outRight - outLeft) / std::numbers::sqrt2, -(outLeft + outRight) / std::numbers::sqrt2);
            end *= kUnitShare * r;
            const double length = std::hypot(end.x(), end.y());
            if (length > r)
                end *= r / length;
            ends[static_cast<std::size_t>(k)] = end;
        }
        // Two wedges: each sound swings to both sides of the centre.
        QPainterPath fan;
        for (const double side : {1.0, -1.0}) {
            fan.moveTo(centre);
            for (const QPointF& end : ends)
                fan.lineTo(centre + side * end);
            fan.closeSubpath();
        }
        QColor shade = style::accent();
        shade.setAlphaF(0.12f);
        p.setPen(Qt::NoPen);
        p.setBrush(shade);
        p.drawPath(fan);
        QColor line = style::accent();
        line.setAlphaF(0.7f);
        p.setPen(QPen(line, 1.0));
        for (const QPointF& end : ends)
            p.drawLine(centre - end, centre + end);
        p.setPen(QPen(style::accent(), 2.0, Qt::SolidLine, Qt::RoundCap));
        edges_ = QPainterPath();
        for (const QPointF& end : {ends.front(), ends.back()}) {
            p.drawLine(centre - end, centre + end);
            edges_.moveTo(centre - end);
            edges_.lineTo(centre + end);
        }
    }

    void paintSettings(QPainter& p, double left) const
    {
        const QRectF balance(left, 108.0, kBlockWidth, kBarHeight);
        const QRectF spreadBar(left, 178.0, kBlockWidth, kBarHeight);
        p.setFont(style::font(font(), 10));
        p.setPen(style::muted());
        p.drawText(QPointF(left, 92.0), tr("Balance"));
        p.drawText(QPointF(left, 162.0), tr("Width"));

        // Balance, from the middle.
        const double pan = std::clamp(setting(Utility::kPan) / 100.0, -1.0, 1.0);
        QColor fill = style::accent();
        fill.setAlphaF(0.45f);
        bar(p, balance, std::min(0.5, 0.5 + pan / 2.0), std::max(0.5, 0.5 + pan / 2.0), fill);
        const double mid = balance.center().x();
        p.setPen(QPen(style::strong(), 1.0));
        p.drawLine(QPointF(mid, balance.top() - 4.0), QPointF(mid, balance.bottom() + 4.0));
        const double at = balance.left() + (0.5 + pan / 2.0) * balance.width();
        p.setPen(Qt::NoPen);
        p.setBrush(style::accent());
        p.drawRoundedRect(QRectF(at - 2.5, balance.top() - 4.0, 5.0, balance.height() + 8.0), 2.0, 2.0);

        // Width: 0 .. 200 %, mono at 0.
        const bool mono = setting(Utility::kMono) >= 0.5;
        const double spread = mono ? 0.0 : std::clamp(setting(Utility::kWidth) / 200.0, 0.0, 1.0);
        bar(p, spreadBar, 0.0, spread, fill);
        const double unity = spreadBar.center().x();
        p.setPen(QPen(style::strong(), 1.0));
        p.drawLine(QPointF(unity, spreadBar.top() - 4.0), QPointF(unity, spreadBar.bottom() + 4.0));

        p.setPen(style::muted());
        p.drawText(QPointF(balance.left(), balance.bottom() + 20.0), QStringLiteral("L"));
        p.drawText(
            QRectF(mid - 20.0, balance.bottom() + 8.0, 40.0, 14.0), Qt::AlignCenter, QStringLiteral("C"));
        p.drawText(QRectF(balance.left(), balance.bottom() + 8.0, balance.width(), 14.0),
            Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("R"));
        p.drawText(QPointF(spreadBar.left(), spreadBar.bottom() + 20.0), QStringLiteral("0"));
        p.drawText(QRectF(unity - 30.0, spreadBar.bottom() + 8.0, 60.0, 14.0), Qt::AlignCenter,
            QStringLiteral("100"));
        p.drawText(QRectF(spreadBar.left(), spreadBar.bottom() + 8.0, spreadBar.width(), 14.0),
            Qt::AlignRight | Qt::AlignVCenter, mono ? tr("Mono") : QStringLiteral("200 %"));

        // Polarity of each side, after the swap.
        const double polarityY = spreadBar.bottom() + 46.0;
        p.drawText(QPointF(left, polarityY), tr("Polarity"));
        double x = left + 64.0;
        for (const auto& [name, id] :
            {std::pair{tr("L"), Utility::kInvertLeft}, std::pair{tr("R"), Utility::kInvertRight}}) {
            const bool inverted = setting(id) >= 0.5;
            p.setPen(style::text());
            p.setFont(style::font(font(), 11, QFont::DemiBold));
            p.drawText(QPointF(x, polarityY), name);
            p.setPen(inverted ? style::accent() : style::muted());
            p.drawText(QPointF(x + 14.0, polarityY), inverted ? QStringLiteral("Ø") : QStringLiteral("+"));
            x += 52.0;
        }
        if (setting(Utility::kSwap) >= 0.5) {
            p.setPen(style::accent());
            p.setFont(style::font(font(), 10, QFont::DemiBold));
            p.drawText(QPointF(x + 8.0, polarityY), tr("Swapped"));
        }
    }

    std::function<std::vector<double>()> settings_;
    double sampleRate_;
    std::vector<double> values_;
    StereoMatrix matrix_;

    // The motion (animationOn()).
    std::uint64_t key_ = 0;
    motion_paint::LayerCache back_;
    QPainterPath edges_; ///< the fan's outer lines as drawn (paintField())
    std::array<float, kCloud> cloud_{};
    double glow_ = 0.0;
    double swell_ = 0.0;
};

UtilityEditor::UtilityEditor(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate, QWidget* parent)
    : BuiltinEditorWindow(std::move(instance), title, sampleRate,
          {
              {tr("Mono Check"), {{Utility::kMono, 1.0}}},
              {tr("Wider"), {{Utility::kWidth, 150.0}}},
              {tr("Narrower"), {{Utility::kWidth, 60.0}}},
              {tr("Swap Sides"), {{Utility::kSwap, 1.0}}},
              {tr("Fix Polarity (L)"), {{Utility::kInvertLeft, 1.0}}},
              {tr("Clean Up"), {{Utility::kDcBlock, 1.0}}},
              {tr("Down 6 dB"), {{Utility::kGain, -6.0}}},
          },
          parent)
{
    resize(900, 440);
    display_ = new UtilityDisplay(
        [this] { return display::settingsOf(*this, Utility::kParameterCount); }, this->sampleRate(), nullptr);
    display_->setAccessibleName(tr("Gain per side, the stereo field, balance, width and polarity"));
    setDisplay(display_);
    animate(display_);

    panel()->add(knob(Utility::kGain));
    auto* pan = knob(Utility::kPan);
    pan->setFormatter(&panText);
    panel()->add(pan);
    panel()->add(knob(Utility::kWidth));
    panel()->addSpacing(12);
    auto* switches = new QWidget;
    auto* column = new QVBoxLayout(switches);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(8);
    for (const auto& [id, label] :
        {std::pair{Utility::kMono, tr("Mono")}, std::pair{Utility::kInvertLeft, tr("Invert L")},
            std::pair{Utility::kInvertRight, tr("Invert R")}}) {
        auto* pill = toggle(id, label, PillToggle::Layout::Inline);
        column->addWidget(pill, 0, Qt::AlignRight);
    }
    panel()->add(switches);

    auto* swap = toggle(Utility::kSwap, tr("Swap"), PillToggle::Layout::Inline);
    swap->setToolTip(tr("Swap left and right"));
    footer()->addLeft(swap);
    auto* dc = toggle(Utility::kDcBlock, tr("DC block"), PillToggle::Layout::Inline);
    dc->setToolTip(tr("Take out a DC offset (a 5 Hz high-pass)"));
    footer()->addLeft(dc);

    peak_ = readout(tr("Output peak"));
    peak_->setWidestText(dbText(-60.0));
    peak_->setText(QStringLiteral("–"));
    footer()->addRight(peak_);

    startMetering();
}

void UtilityEditor::parameterChanged(std::uint32_t /*id*/, double /*value*/) { display_->refresh(); }

void UtilityEditor::meter(const Meter& reading)
{
    peak_->setText(
        std::isfinite(reading.outputDb) && reading.running ? dbText(reading.outputDb) : QStringLiteral("–"));
}

} // namespace vloomdaw
