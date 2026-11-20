// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/TransientEditor.hpp"

#include "core/EffectPreview.hpp"
#include "core/ParameterScale.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/DisplayHelpers.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <vloom/dsp/effects/TransientShaper.hpp>

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace vloomdaw {

namespace {

namespace style = editor_style;
using Shaper = vloom::dsp::TransientShaper;

constexpr double kCentre = 126.0;
constexpr double kHalf = 110.0; ///< px from the centre to full scale (the hits peak at half of it)
constexpr double kPointPixels = 3.0;

QString dbText(double db) { return QString::fromStdString(formatParameterValue("dB", db)); }

} // namespace

/// The transient shaper's picture: hits before and after, mirrored.
class TransientDisplay : public QWidget {
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
        update();
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        refresh();
    }

    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), style::background());
        const double centre = std::min(kCentre, height() / 2.0);
        const double half = std::min(kHalf, centre - 6.0);
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

void TransientEditor::parameterChanged(std::uint32_t /*id*/, double /*value*/) { display_->refresh(); }

void TransientEditor::meter(const Meter& reading)
{
    peak_->setText(
        std::isfinite(reading.outputDb) && reading.running ? dbText(reading.outputDb) : QStringLiteral("–"));
}

} // namespace vloomdaw
