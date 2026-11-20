// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/BitcrusherEditor.hpp"

#include "core/EffectPreview.hpp"
#include "core/ParameterScale.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/DisplayHelpers.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <vloom/dsp/effects/Bitcrusher.hpp>

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace vloomdaw {

namespace {

namespace style = editor_style;
using Bitcrusher = vloom::dsp::Bitcrusher;

constexpr double kUnit = 110.0; ///< px for full scale
constexpr double kCentre = 126.0; ///< the wave's zero line, from the top
constexpr double kMinLevelGap = 6.0; ///< px: closer quantizer levels get no grid lines

QString dbText(double db) { return QString::fromStdString(formatParameterValue("dB", db)); }

} // namespace

/// The bitcrusher's picture: a sine before and after.
class BitcrusherDisplay : public QWidget {
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
        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter p(this);
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
    std::function<std::vector<double>()> settings_;
    double sampleRate_;
    std::vector<double> values_;
    WavePreview wave_;
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
