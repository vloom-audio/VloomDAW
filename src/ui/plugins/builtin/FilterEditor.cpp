// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/FilterEditor.hpp"

#include "core/EffectPreview.hpp"
#include "core/ParameterScale.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ChoiceButtons.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/DisplayHelpers.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <vloom/dsp/effects/Filter.hpp>

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <utility>

namespace vloomdaw {

namespace {

namespace style = editor_style;
using Filter = vloom::dsp::Filter;

constexpr double kZeroShare = 110.0 / 236.0; ///< where 0 dB sits, down the curve's area
constexpr double kPixelsPerDb = 4.4 / 236.0; ///< of the curve's area height
constexpr double kHandleRadius = 11.0;
constexpr double kGrab = 18.0; ///< px around the handle that take it
constexpr double kResonanceDragPixels = 150.0; ///< up: the whole resonance range
constexpr double kPointPixels = 2.0;

QString dbText(double db) { return QString::fromStdString(formatParameterValue("dB", db)); }

} // namespace

/// The filter's picture: its response with a handle at the cutoff.
class FilterDisplay : public QWidget {
public:
    FilterDisplay(std::function<std::vector<double>()> settings, double sampleRate, QWidget* parent)
        : QWidget(parent)
        , settings_(std::move(settings))
        , sampleRate_(sampleRate)
    {
        setMouseTracking(true);
    }

    /// The handle was dragged to `hz` and `resonance` (%); a double-click
    /// on it asks for the defaults.
    std::function<void(double hz, double resonance)> onDragged;
    std::function<void()> onReset;

    void refresh()
    {
        values_ = settings_();
        const auto count = static_cast<std::size_t>(std::max(16.0, std::ceil(width() / kPointPixels))) + 1;
        frequencies_.resize(count);
        for (std::size_t i = 0; i < count; ++i)
            frequencies_[i] = display::hzAtX(
                static_cast<double>(i) * width() / static_cast<double>(count - 1), 0.0, width());
        response_ = filterResponseDb(values_, sampleRate_, frequencies_);
        const std::array<double, 1> cutoff{this->cutoff()};
        cutoffDb_ = filterResponseDb(values_, sampleRate_, cutoff).front();
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
        const QRectF area = curveArea();
        display::drawFrequencyGrid(p, area, area.bottom() - 8.0);
        const double zero = std::round(y(0.0)) + 0.5;
        p.setPen(QPen(style::line(), 1.0));
        p.drawLine(QPointF(0.0, zero), QPointF(width(), zero));

        if (response_.size() >= 2) {
            QPainterPath curve;
            for (std::size_t i = 0; i < response_.size(); ++i) {
                const QPointF point(display::xOfHz(frequencies_[i], 0.0, width()), y(response_[i]));
                if (i == 0)
                    curve.moveTo(point);
                else
                    curve.lineTo(point);
            }
            QPainterPath fill = curve;
            fill.lineTo(width(), area.bottom());
            fill.lineTo(0.0, area.bottom());
            fill.closeSubpath();
            QColor shade = style::accent();
            shade.setAlphaF(0.18f);
            p.setPen(Qt::NoPen);
            p.setBrush(shade);
            p.drawPath(fill);
            p.setPen(QPen(style::accent(), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(curve);
        }

        const QPointF at = handle();
        QColor stem = style::accent();
        stem.setAlphaF(0.35f);
        p.setPen(QPen(stem, 1.0));
        p.drawLine(at, QPointF(at.x(), area.bottom()));
        p.setPen(QPen(style::accent(), 3.0));
        p.setBrush(style::background());
        p.drawEllipse(at, kHandleRadius, kHandleRadius);

        const QString label = QString::fromStdString(formatParameterValue("Hz", cutoff()));
        p.setFont(style::font(font(), 11, QFont::DemiBold, true));
        const double labelWidth = p.fontMetrics().horizontalAdvance(label);
        const double labelX
            = at.x() + 18.0 + labelWidth < width() - 6.0 ? at.x() + 18.0 : at.x() - 18.0 - labelWidth;
        p.setPen(style::text());
        p.drawText(QPointF(labelX, std::max(14.0, at.y() - 8.0)), label);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || !nearHandle(event->position())) {
            QWidget::mousePressEvent(event);
            return;
        }
        dragging_ = true;
        grabOffsetX_ = event->position().x() - handle().x();
        dragStartY_ = event->position().y();
        dragStartResonance_ = resonance();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!dragging_) {
            const bool near = nearHandle(event->position());
            setCursor(near ? Qt::SizeAllCursor : Qt::ArrowCursor);
            setToolTip(near ? tr("Drag sideways for the cutoff, up or down for the resonance") : QString());
            return;
        }
        const double fine = (event->modifiers() & Qt::ShiftModifier) != 0 ? kFineFactor : 1.0;
        const double hz = display::hzAtX(event->position().x() - grabOffsetX_, 0.0, width());
        const double resonance = dragStartResonance_
            + (dragStartY_ - event->position().y()) / kResonanceDragPixels * 100.0 * fine;
        if (onDragged)
            onDragged(hz, std::clamp(resonance, 0.0, 100.0));
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        dragging_ = false;
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (nearHandle(event->position()) && onReset)
            onReset();
    }

private:
    [[nodiscard]] double cutoff() const
    {
        return values_.size() > Filter::kCutoff ? values_[Filter::kCutoff] : 1000.0;
    }
    [[nodiscard]] double resonance() const
    {
        return values_.size() > Filter::kResonance ? values_[Filter::kResonance] : 0.0;
    }

    /// Where the curve is drawn: above the floating panel.
    [[nodiscard]] QRectF curveArea() const
    {
        const double bottom
            = std::max(120.0, height() - static_cast<double>(style::kPanelHeight + style::kPanelMargin));
        return {0.0, 0.0, static_cast<double>(width()), bottom};
    }

    [[nodiscard]] double y(double db) const
    {
        const QRectF area = curveArea();
        const double value = std::isfinite(db) ? db : -1000.0;
        return std::clamp(area.height() * (kZeroShare - value * kPixelsPerDb), 6.0, area.bottom() - 4.0);
    }

    [[nodiscard]] QPointF handle() const { return {display::xOfHz(cutoff(), 0.0, width()), y(cutoffDb_)}; }

    [[nodiscard]] bool nearHandle(const QPointF& point) const
    {
        const QPointF d = point - handle();
        return d.x() * d.x() + d.y() * d.y() <= kGrab * kGrab;
    }

    std::function<std::vector<double>()> settings_;
    double sampleRate_;
    std::vector<double> values_;
    std::vector<double> frequencies_;
    std::vector<double> response_;
    double cutoffDb_ = 0.0;
    bool dragging_ = false;
    double grabOffsetX_ = 0.0;
    double dragStartY_ = 0.0;
    double dragStartResonance_ = 0.0;
};

FilterEditor::FilterEditor(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate, QWidget* parent)
    : BuiltinEditorWindow(std::move(instance), title, sampleRate,
          {
              {tr("Warm Low-pass"), {{Filter::kCutoff, 2500.0}, {Filter::kResonance, 15.0}}},
              {tr("Thin High-pass"),
                  {{Filter::kType, 1.0}, {Filter::kCutoff, 300.0}, {Filter::kResonance, 10.0}}},
              {tr("Resonant Sweep"),
                  {{Filter::kCutoff, 800.0}, {Filter::kResonance, 70.0}, {Filter::kSlope, 1.0}}},
              {tr("Telephone Band"),
                  {{Filter::kType, 2.0}, {Filter::kCutoff, 1500.0}, {Filter::kResonance, 30.0}}},
              {tr("Notch 1 kHz"),
                  {{Filter::kType, 3.0}, {Filter::kCutoff, 1000.0}, {Filter::kResonance, 20.0}}},
              {tr("Gritty Low-pass"),
                  {{Filter::kCutoff, 1200.0}, {Filter::kResonance, 40.0}, {Filter::kSlope, 1.0},
                      {Filter::kDrive, 12.0}}},
          },
          parent)
{
    resize(640, 420);
    display_ = new FilterDisplay(
        [this] { return display::settingsOf(*this, Filter::kParameterCount); }, this->sampleRate(), nullptr);
    display_->setAccessibleName(tr("Filter response; drag the handle for cutoff and resonance"));
    display_->onDragged = [this](double hz, double resonance) {
        change(Filter::kCutoff, hz);
        change(Filter::kResonance, resonance);
    };
    display_->onReset = [this] {
        for (const std::uint32_t id : {Filter::kCutoff, Filter::kResonance})
            if (const vloom::ParameterInfo* info = parameter(id))
                change(id, info->defaultValue);
    };
    setDisplay(display_);

    auto* type = new ChoiceButtons(tr("Response"), display::choiceLabels(parameter(Filter::kType)), 264);
    type->onChosen = [this](int index) { change(Filter::kType, index); };
    bind(Filter::kType, [type](double v) { type->setCurrent(static_cast<int>(std::lround(v))); });
    panel()->add(type);
    panel()->addSpacing(10);
    panel()->add(knob(Filter::kCutoff, tr("Frequency")));
    panel()->add(knob(Filter::kResonance));
    panel()->add(knob(Filter::kDrive));

    auto* slope = readout(Filter::kSlope, tr("Slope"));
    slope->setWidestText(QStringLiteral("24 dB/oct"));
    footer()->addLeft(slope);
    peak_ = readout(tr("Output peak"));
    peak_->setWidestText(dbText(-60.0));
    peak_->setText(QStringLiteral("–"));
    footer()->addRight(peak_);

    startMetering();
}

void FilterEditor::parameterChanged(std::uint32_t /*id*/, double /*value*/) { display_->refresh(); }

void FilterEditor::meter(const Meter& reading)
{
    peak_->setText(
        std::isfinite(reading.outputDb) && reading.running ? dbText(reading.outputDb) : QStringLiteral("–"));
}

} // namespace vloomdaw
