// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/SaturatorEditor.hpp"

#include "core/EffectPreview.hpp"
#include "core/ParameterScale.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ChoiceButtons.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/DisplayHelpers.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <vloom/dsp/effects/Saturator.hpp>
#include <vloom/engine/plugin/PluginInstance.hpp>

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace vloomdaw {

namespace {

namespace style = editor_style;
using Saturator = vloom::dsp::Saturator;

constexpr double kSpan = 1.25; ///< the curve's axes run -kSpan .. kSpan
constexpr double kHandleInput = 0.5; ///< -6 dB in: where the handle sits
constexpr double kBoxTop = 16.0;
constexpr double kBoxSize = 220.0;
constexpr double kHandleRadius = 8.0;
constexpr double kWaveGap = 80.0; ///< the curve's box to the wave

QString dbText(double db) { return QString::fromStdString(formatParameterValue("dB", db)); }

} // namespace

/// The saturator's picture: the curve with its drive handle, and a sine
/// before and after.
class SaturatorDisplay : public QWidget {
public:
    SaturatorDisplay(std::function<std::vector<double>()> settings, double sampleRate, QWidget* parent)
        : QWidget(parent)
        , settings_(std::move(settings))
        , sampleRate_(sampleRate)
    {
        setMouseTracking(true);
        refresh();
    }

    /// The drive was dragged to `db`; the default asked for (double-click).
    std::function<void(double db)> onDrive;
    std::function<void()> onDriveReset;
    std::function<double()> drive;

    /// The settings changed: work the pictures out again.
    void refresh()
    {
        values_ = settings_();
        wave_ = saturatorPreview(values_, sampleRate_);
        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), style::background());
        p.setFont(style::font(font(), 10));

        // The curve.
        const QRectF box = boxRect();
        const QPointF centre = box.center();
        const double unit = box.width() / 2.0 / kSpan;
        p.setPen(QPen(style::faint(), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(box, 8.0, 8.0);
        p.drawLine(QPointF(centre.x(), box.top()), QPointF(centre.x(), box.bottom()));
        p.drawLine(QPointF(box.left(), centre.y()), QPointF(box.right(), centre.y()));
        QPen fullScale(style::grid(), 1.0, Qt::DashLine);
        p.setPen(fullScale);
        p.drawRect(QRectF(centre.x() - unit, centre.y() - unit, 2.0 * unit, 2.0 * unit));
        QPen diagonal(style::strong(), 1.0);
        diagonal.setDashPattern({4.0, 5.0});
        p.setPen(diagonal);
        p.drawLine(box.bottomLeft(), box.topRight());

        p.save();
        p.setClipRect(box);
        QPainterPath curve;
        constexpr int kPoints = 110;
        for (int i = 0; i <= kPoints; ++i) {
            const double x = -kSpan + 2.0 * kSpan * i / kPoints;
            const QPointF point(centre.x() + x * unit, centre.y() - saturatorCurve(values_, x) * unit);
            if (i == 0)
                curve.moveTo(point);
            else
                curve.lineTo(point);
        }
        p.setPen(QPen(style::accent(), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(curve);
        p.restore();

        p.setPen(QPen(style::accent(), 2.5));
        p.setBrush(style::background());
        p.drawEllipse(handle(), kHandleRadius, kHandleRadius);
        p.setPen(style::muted());
        p.drawText(QPointF(box.left() + 6.0, box.top() + 14.0), tr("Out"));
        p.drawText(QRectF(box.left(), box.bottom() - 18.0, box.width() - 6.0, 14.0),
            Qt::AlignRight | Qt::AlignVCenter, tr("In"));

        // The sine before and after.
        const QRectF wave = waveRect();
        if (wave.width() > 40.0 && !wave_.dry.empty()) {
            p.setPen(QPen(style::line(), 1.0));
            p.drawLine(QPointF(wave.left(), centre.y()), QPointF(wave.right(), centre.y()));
            p.save();
            p.setClipRect(wave.adjusted(0, -2, 0, 2));
            const QPainterPath dry = display::wavePath(wave_.dry, wave, centre.y(), unit, false);
            const QPainterPath wet = display::wavePath(wave_.wet, wave, centre.y(), unit, false);
            QPainterPath area = wet;
            area.lineTo(wave.right(), centre.y());
            area.lineTo(wave.left(), centre.y());
            area.closeSubpath();
            QColor shade = style::accent();
            shade.setAlphaF(0.18f);
            p.setPen(Qt::NoPen);
            p.setBrush(shade);
            p.drawPath(area);
            QColor dryColour = style::text();
            dryColour.setAlphaF(0.45f);
            QPen dryPen(dryColour, 1.5);
            dryPen.setDashPattern({4.0 / 1.5, 4.0 / 1.5});
            p.setPen(dryPen);
            p.setBrush(Qt::NoBrush);
            p.drawPath(dry);
            p.setPen(QPen(style::accent(), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(wet);
            p.restore();
            p.setPen(style::muted());
            p.drawText(QPointF(wave.left(), box.top() + 14.0), tr("Before (dashed) and after"));
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || !boxRect().contains(event->position()) || !drive) {
            QWidget::mousePressEvent(event);
            return;
        }
        dragging_ = true;
        dragStartY_ = event->position().y();
        dragStartDb_ = drive();
        setCursor(Qt::SizeVerCursor);
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!dragging_) {
            setCursor(boxRect().contains(event->position()) ? Qt::SizeVerCursor : Qt::ArrowCursor);
            setToolTip(boxRect().contains(event->position()) ? tr("Drag up or down for more or less drive")
                                                             : QString());
            return;
        }
        const double fine = (event->modifiers() & Qt::ShiftModifier) != 0 ? kFineFactor : 1.0;
        const double range = 36.0; // the drive's range, dB
        const double db = dragStartDb_ + (dragStartY_ - event->position().y()) / kDragPixels * range * fine;
        if (onDrive)
            onDrive(db);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        dragging_ = false;
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (boxRect().contains(event->position()) && onDriveReset)
            onDriveReset();
    }

private:
    [[nodiscard]] QRectF boxRect() const
    {
        const double size = std::clamp(height() - 148.0, 120.0, kBoxSize);
        return {std::round(width() * 120.0 / 900.0) + 0.5, kBoxTop + 0.5, size, size};
    }

    [[nodiscard]] QRectF waveRect() const
    {
        const QRectF box = boxRect();
        return {box.right() + kWaveGap, box.top(), width() - 20.0 - (box.right() + kWaveGap), box.height()};
    }

    [[nodiscard]] QPointF handle() const
    {
        const QRectF box = boxRect();
        const double unit = box.width() / 2.0 / kSpan;
        const double y = std::clamp(saturatorCurve(values_, kHandleInput), -kSpan, kSpan);
        return {box.center().x() + kHandleInput * unit, box.center().y() - y * unit};
    }

    std::function<std::vector<double>()> settings_;
    double sampleRate_;
    std::vector<double> values_;
    WavePreview wave_;
    bool dragging_ = false;
    double dragStartY_ = 0.0;
    double dragStartDb_ = 0.0;
};

SaturatorEditor::SaturatorEditor(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate, QWidget* parent)
    : BuiltinEditorWindow(std::move(instance), title, sampleRate,
          {
              {tr("Warm Tape"),
                  {{Saturator::kCurve, 2.0}, {Saturator::kDrive, 9.0}, {Saturator::kTone, -20.0},
                      {Saturator::kOutput, -2.0}}},
              {tr("Tube Drive"),
                  {{Saturator::kCurve, 1.0}, {Saturator::kDrive, 15.0}, {Saturator::kTone, 10.0},
                      {Saturator::kOutput, -5.0}}},
              {tr("Hard Clip"),
                  {{Saturator::kCurve, 3.0}, {Saturator::kDrive, 12.0}, {Saturator::kOutput, -6.0}}},
              {tr("Parallel Grit"),
                  {{Saturator::kDrive, 24.0}, {Saturator::kTone, 30.0}, {Saturator::kOutput, -8.0},
                      {Saturator::kMix, 35.0}}},
              {tr("Gentle Warmth"),
                  {{Saturator::kCurve, 2.0}, {Saturator::kDrive, 4.0}, {Saturator::kTone, -10.0}}},
          },
          parent)
{
    resize(900, 440);
    display_ = new SaturatorDisplay([this] { return display::settingsOf(*this, Saturator::kParameterCount); },
        this->sampleRate(), nullptr);
    display_->setAccessibleName(tr("Shaping curve, and a sine before and after"));
    display_->drive = [this] { return value(Saturator::kDrive); };
    display_->onDrive = [this](double db) { change(Saturator::kDrive, db); };
    display_->onDriveReset = [this] {
        if (const vloom::ParameterInfo* drive = parameter(Saturator::kDrive))
            change(Saturator::kDrive, drive->defaultValue);
    };
    setDisplay(display_);

    auto* character
        = new ChoiceButtons(tr("Character"), display::choiceLabels(parameter(Saturator::kCurve)), 172);
    character->onChosen = [this](int index) { change(Saturator::kCurve, index); };
    bind(Saturator::kCurve,
        [character](double curve) { character->setCurrent(static_cast<int>(std::lround(curve))); });
    panel()->add(character);
    panel()->addSpacing(10);
    panel()->add(knob(Saturator::kDrive));
    panel()->add(knob(Saturator::kTone));
    panel()->add(knob(Saturator::kOutput));
    panel()->add(knob(Saturator::kMix));

    auto* oversampling = readout(Saturator::kOversampling, tr("Oversampling"));
    oversampling->setWidestText(QStringLiteral("4x"));
    oversampling->setToolTip(tr("Runs the curve at 2 or 4 times the rate, so its harmonics do not alias"));
    footer()->addLeft(oversampling);
    latency_ = readout(tr("Latency"));
    latency_->setWidestText(QStringLiteral("10.0 ms"));
    latency_->setToolTip(tr("How long the oversampling delays the sound (the project makes up for it)"));
    footer()->addLeft(latency_);
    showLatency();

    peak_ = readout(tr("Output peak"));
    peak_->setWidestText(dbText(-60.0));
    peak_->setText(QStringLiteral("–"));
    footer()->addRight(peak_);

    startMetering();
}

void SaturatorEditor::showLatency()
{
    const double ms = 1000.0 * instance().latencyFrames() / sampleRate();
    latency_->setText(QString::fromStdString(formatParameterValue("ms", ms)));
}

void SaturatorEditor::parameterChanged(std::uint32_t /*id*/, double /*value*/)
{
    display_->refresh();
    showLatency();
}

void SaturatorEditor::meter(const Meter& reading)
{
    showLatency(); // the engine reads the latency again after a change
    peak_->setText(
        std::isfinite(reading.outputDb) && reading.running ? dbText(reading.outputDb) : QStringLiteral("–"));
}

} // namespace vloomdaw
