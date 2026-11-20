// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/DeEsserEditor.hpp"

#include "core/EffectPreview.hpp"
#include "core/ParameterScale.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/DisplayHelpers.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <vloom/dsp/effects/DeEsser.hpp>
#include <vloom/engine/plugin/PluginInstance.hpp>

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace vloomdaw {

namespace {

namespace style = editor_style;
using DeEsser = vloom::dsp::DeEsser;

constexpr double kTopY = 20.0; ///< where 0 dB sits
constexpr double kBottomDb = -60.0; ///< at the bottom of the area, 16 px up
constexpr double kHeaderHeight = 30.0; ///< the band's reduction strip
constexpr double kHandleRadius = 9.0;
constexpr double kGrab = 16.0;
constexpr double kPointPixels = 3.0;

QString dbText(double db) { return QString::fromStdString(formatParameterValue("dB", db)); }

} // namespace

/// The de-esser's picture: the spectrum, the band and the threshold.
class DeEsserDisplay : public QWidget {
public:
    DeEsserDisplay(const SpectrumAnalyzer& spectrum, double sampleRate, QWidget* parent)
        : QWidget(parent)
        , spectrum_(spectrum)
        , sampleRate_(sampleRate)
    {
        setMouseTracking(true);
    }

    /// The handle was dragged to `hz` and `thresholdDb`; a double-click on it
    /// asks for the defaults.
    std::function<void(double hz, double thresholdDb)> onDragged;
    std::function<void()> onReset;

    void setSettings(double frequency, double thresholdDb, bool splitBand, bool listening)
    {
        frequency_ = frequency;
        threshold_ = thresholdDb;
        split_ = splitBand;
        listening_ = listening;
        update();
    }

    /// The reduction now, dB (>= 0); NaN: none known (no audio).
    void setReduction(double db)
    {
        reduction_ = db;
        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), style::background());
        const QRectF area = this->area();
        const QRectF all(rect());
        display::drawFrequencyGrid(p, all, area.bottom() - 8.0);

        // The band the detector hears.
        const double bandX = display::xOfHz(frequency_, 0.0, width());
        const QRectF band(bandX, all.top(), width() - bandX, all.height());
        QColor shade = style::accent();
        shade.setAlphaF(0.1f);
        p.fillRect(band, shade);
        shade.setAlphaF(0.35f);
        p.fillRect(QRectF(band.left(), band.top(), band.width(), kHeaderHeight), shade);
        p.setPen(QPen(style::accent(), 1.5));
        p.drawLine(QPointF(bandX, all.top()), QPointF(bandX, all.bottom()));
        p.setPen(style::text());
        p.setFont(style::font(font(), 10, QFont::DemiBold, true));
        const bool known = std::isfinite(reduction_);
        p.drawText(QRectF(band.left(), band.top(), band.width(), kHeaderHeight), Qt::AlignCenter,
            known ? dbText(-reduction_) : QStringLiteral("–"));

        // The spectra.
        if (spectrum_.hasSignal()) {
            const int points = std::max(16, static_cast<int>(std::ceil(width() / kPointPixels)));
            QPainterPath out;
            QPainterPath in;
            for (int i = 0; i <= points; ++i) {
                const double x = static_cast<double>(i) * width() / points;
                const double from = display::hzAtX(x - kPointPixels / 2.0, 0.0, width());
                const double to = display::hzAtX(x + kPointPixels / 2.0, 0.0, width());
                const double level = spectrum_.levelDb(from, to);
                const double centre = std::sqrt(from * to);
                const double taken = known && !listening_
                    ? deEsserGainDb(frequency_, split_, reduction_, sampleRate_, centre)
                    : 0.0;
                const QPointF o(x, y(level));
                const QPointF n(x, y(level - taken));
                if (i == 0) {
                    out.moveTo(o);
                    in.moveTo(n);
                } else {
                    out.lineTo(o);
                    in.lineTo(n);
                }
            }
            if (!listening_) {
                QPainterPath fill = in;
                fill.lineTo(width(), all.bottom() + 1.0);
                fill.lineTo(0.0, all.bottom() + 1.0);
                fill.closeSubpath();
                QColor grey = style::text();
                grey.setAlphaF(0.09f);
                QColor greyLine = style::text();
                greyLine.setAlphaF(0.3f);
                p.setPen(QPen(greyLine, 1.0));
                p.setBrush(grey);
                p.drawPath(fill);
            }
            p.setPen(QPen(style::accent(), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(out);
        }

        // The threshold across the band, with its handle.
        const QPointF at = handle();
        QPen dashed(style::text(), 1.0);
        dashed.setDashPattern({4.0, 4.0});
        p.setPen(dashed);
        p.drawLine(QPointF(bandX, at.y()), QPointF(width(), at.y()));
        p.setPen(style::text());
        p.setFont(style::font(font(), 10, QFont::DemiBold));
        p.drawText(QRectF(0.0, at.y() - 18.0, bandX - 14.0, 14.0), Qt::AlignRight | Qt::AlignVCenter,
            tr("Threshold %1").arg(dbText(threshold_)));
        p.setPen(QPen(style::accent(), 2.5));
        p.setBrush(style::background());
        p.drawEllipse(at, kHandleRadius, kHandleRadius);
        if (listening_) {
            p.setPen(style::muted());
            p.setFont(style::font(font(), 10));
            p.drawText(QPointF(12.0, area.top() + 20.0), tr("Listening to the band"));
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || !nearHandle(event->position())) {
            QWidget::mousePressEvent(event);
            return;
        }
        dragging_ = true;
        grab_ = event->position() - handle();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!dragging_) {
            const bool near = nearHandle(event->position());
            setCursor(near ? Qt::SizeAllCursor : Qt::ArrowCursor);
            setToolTip(
                near ? tr("Drag sideways for the frequency, up or down for the threshold") : QString());
            return;
        }
        const QPointF to = event->position() - grab_;
        if (onDragged)
            onDragged(display::hzAtX(to.x(), 0.0, width()), dbAt(to.y()));
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
    /// Above the floating panel.
    [[nodiscard]] QRectF area() const
    {
        const double bottom
            = std::max(140.0, height() - static_cast<double>(style::kPanelHeight + style::kPanelMargin));
        return {0.0, 0.0, static_cast<double>(width()), bottom};
    }

    [[nodiscard]] double pixelsPerDb() const { return (area().bottom() - 16.0 - kTopY) / -kBottomDb; }

    [[nodiscard]] double y(double db) const
    {
        const double value = std::isfinite(db) ? db : -200.0;
        return std::clamp(kTopY - value * pixelsPerDb(), -10.0, height() + 10.0);
    }

    [[nodiscard]] double dbAt(double y) const { return (kTopY - y) / pixelsPerDb(); }

    [[nodiscard]] QPointF handle() const { return {display::xOfHz(frequency_, 0.0, width()), y(threshold_)}; }

    [[nodiscard]] bool nearHandle(const QPointF& point) const
    {
        const QPointF d = point - handle();
        return d.x() * d.x() + d.y() * d.y() <= kGrab * kGrab;
    }

    const SpectrumAnalyzer& spectrum_;
    double sampleRate_;
    double frequency_ = 6000.0;
    double threshold_ = -30.0;
    bool split_ = true;
    bool listening_ = false;
    double reduction_ = std::nan("");
    bool dragging_ = false;
    QPointF grab_;
};

DeEsserEditor::DeEsserEditor(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate, QWidget* parent)
    : BuiltinEditorWindow(std::move(instance), title, sampleRate,
          {
              {tr("Gentle Vocal"), {{DeEsser::kFrequency, 6500.0}, {DeEsser::kThreshold, -28.0}}},
              {tr("Strong Sibilance"),
                  {{DeEsser::kFrequency, 5500.0}, {DeEsser::kThreshold, -36.0}, {DeEsser::kRange, 12.0}}},
              {tr("Bright Voice"),
                  {{DeEsser::kFrequency, 8000.0}, {DeEsser::kThreshold, -30.0}, {DeEsser::kRange, 8.0}}},
              {tr("Deep Voice"),
                  {{DeEsser::kFrequency, 4500.0}, {DeEsser::kThreshold, -30.0}, {DeEsser::kRange, 8.0}}},
              {tr("Cymbal Tamer"),
                  {{DeEsser::kFrequency, 9000.0}, {DeEsser::kThreshold, -24.0}, {DeEsser::kRange, 6.0}}},
              {tr("Wideband Soft"),
                  {{DeEsser::kMode, 0.0}, {DeEsser::kThreshold, -26.0}, {DeEsser::kRange, 4.0}}},
          },
          parent)
    , analyzer_(BuiltinEditorWindow::sampleRate())
    , scope_(SpectrumAnalyzer::kFrames)
{
    resize(900, 440);
    display_ = new DeEsserDisplay(analyzer_, this->sampleRate(), nullptr);
    display_->setAccessibleName(
        tr("Spectrum with the detection band; drag the handle for frequency and threshold"));
    display_->onDragged = [this](double hz, double db) {
        change(DeEsser::kFrequency, hz);
        change(DeEsser::kThreshold, db);
    };
    display_->onReset = [this] {
        for (const std::uint32_t id : {DeEsser::kFrequency, DeEsser::kThreshold})
            if (const vloom::ParameterInfo* info = parameter(id))
                change(id, info->defaultValue);
    };
    setDisplay(display_);
    parameterChanged(DeEsser::kFrequency, value(DeEsser::kFrequency));

    panel()->add(knob(DeEsser::kFrequency));
    panel()->add(knob(DeEsser::kThreshold));
    panel()->add(knob(DeEsser::kRange));
    auto* listen = toggle(DeEsser::kListen, tr("Listen to band"));
    listen->setToolTip(tr("Hear the band the detector listens to, to find the frequency"));
    panel()->add(listen);

    auto* mode = readout(DeEsser::kMode, tr("Mode"));
    mode->setWidestText(QStringLiteral("Split-band"));
    mode->setToolTip(tr("Split-band turns down only the band; Wideband the whole sound"));
    footer()->addLeft(mode);
    reduction_ = readout(tr("Reduction"));
    reduction_->setWidestText(dbText(-24.0));
    reduction_->setText(QStringLiteral("–"));
    footer()->addRight(reduction_);

    startMetering();
}

void DeEsserEditor::parameterChanged(std::uint32_t /*id*/, double /*value*/)
{
    display_->setSettings(value(DeEsser::kFrequency), value(DeEsser::kThreshold),
        value(DeEsser::kMode) >= 0.5, value(DeEsser::kListen) >= 0.5);
}

void DeEsserEditor::meter(const Meter& reading)
{
    if (reading.running) {
        const std::size_t count = instance().readScope(scope_);
        analyzer_.analyse(std::span<const float>(scope_.data(), count));
    } else if (analyzer_.hasSignal()) {
        analyzer_.analyse({}); // falls away
    }
    const double reduction = reading.running ? reading.reductionDb : std::nan("");
    display_->setReduction(reduction);
    reduction_->setText(std::isfinite(reduction) ? dbText(-reduction) : QStringLiteral("–"));
}

} // namespace vloomdaw
