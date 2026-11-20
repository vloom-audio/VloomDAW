// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/AnalyzerEditor.hpp"

#include "core/ParameterScale.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/DisplayHelpers.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"
#include "ui/plugins/builtin/PillToggle.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <vloom/dsp/effects/Analyzer.hpp>
#include <vloom/engine/plugin/PluginInstance.hpp>

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <span>
#include <utility>

namespace vloomdaw {

namespace {

namespace style = editor_style;
using Analyzer = vloom::dsp::Analyzer;

constexpr double kTopDb = 0.0; ///< the level at the top of the grid
constexpr double kTopY = 24.0; ///< where kTopDb is, below the frequency labels
constexpr double kColumn = 2.0; ///< px per point of the spectrum line

QString dbText(double db) { return QString::fromStdString(formatParameterValue("dB", db)); }
QString hzText(double hz) { return QString::fromStdString(formatParameterValue("Hz", hz)); }

/// The step of the dB grid for a display `range` dB tall: about six lines.
double gridStep(double range) { return range <= 36.0 ? 6.0 : range <= 84.0 ? 12.0 : 24.0; }

/// The frames the scope can give for Resolution `resolution`.
int analysedSize(double resolution)
{
    const int wanted = Analyzer::fftSize(static_cast<int>(std::lround(resolution)));
    return std::min(wanted, static_cast<int>(vloom::PluginInstance::kScopeFrames));
}

} // namespace

/// The analyzer's picture: the spectrum on a log axis with its dB grid, the
/// held line and a cursor readout.
class AnalyzerDisplay : public QWidget {
public:
    AnalyzerDisplay(const AnalyzerSpectrum& spectrum, QWidget* parent)
        : QWidget(parent)
        , spectrum_(spectrum)
    {
        setMouseTracking(true);
    }

    std::function<void()> onReset; ///< a double-click: clear the held line

    void setRange(double range)
    {
        range_ = std::max(range, 1.0);
        update();
    }

    /// Where the cursor readout is (for a render without a mouse); none hides it.
    void setCursorX(std::optional<double> x)
    {
        cursor_ = x;
        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), style::background());
        const QRectF area(0.0, 0.0, width(), height());
        display::drawFrequencyGrid(p, area, 14.0);
        paintLevels(p);

        // The spectrum, filled, and the held line over it.
        if (spectrum_.hasSignal() || spectrum_.peakHold()) {
            QPainterPath line;
            QPainterPath peak;
            bool anyPeak = false;
            for (double x = 0.0; x <= width(); x += kColumn) {
                const double from = display::hzAtX(x - kColumn / 2.0, 0.0, width());
                const double to = display::hzAtX(x + kColumn / 2.0, 0.0, width());
                const QPointF at(x, yOf(spectrum_.levelDb(from, to)));
                const double held = spectrum_.peakDb(from, to);
                anyPeak = anyPeak || held > AnalyzerSpectrum::kFloorDb;
                const QPointF heldAt(x, yOf(held));
                // Not isEmpty(): a path holding only its moveTo() counts as empty.
                if (line.elementCount() == 0) {
                    line.moveTo(at);
                    peak.moveTo(heldAt);
                } else {
                    line.lineTo(at);
                    peak.lineTo(heldAt);
                }
            }
            QPainterPath fill = line;
            fill.lineTo(width(), height());
            fill.lineTo(0.0, height());
            fill.closeSubpath();
            QColor shade = style::accent();
            shade.setAlphaF(0.24f);
            p.setPen(Qt::NoPen);
            p.setBrush(shade);
            p.drawPath(fill);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(style::accent(), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(line);
            if (anyPeak) {
                QColor held = style::text();
                held.setAlphaF(0.7f);
                p.setPen(QPen(held, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.drawPath(peak);
            }
        }
        paintCursor(p);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        setCursorX(event->position().x());
        QWidget::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        setCursorX(std::nullopt);
        QWidget::leaveEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (onReset)
            onReset();
        QWidget::mouseDoubleClickEvent(event);
    }

private:
    [[nodiscard]] double yOf(double db) const
    {
        const double share = std::clamp((kTopDb - db) / range_, 0.0, 1.0);
        return kTopY + share * (height() - kTopY);
    }

    /// The dB grid, labelled on the right.
    void paintLevels(QPainter& p) const
    {
        const double step = gridStep(range_);
        p.setFont(style::font(font(), 10));
        for (double db = kTopDb - step; db > kTopDb - range_ + 0.5 * step; db -= step) {
            const double y = std::round(yOf(db)) + 0.5;
            p.setPen(QPen(style::grid(), 1.0));
            p.drawLine(QPointF(0.0, y), QPointF(width(), y));
            p.setPen(style::muted());
            p.drawText(QRectF(width() - 48.0, y - 15.0, 40.0, 14.0), Qt::AlignRight | Qt::AlignBottom,
                QStringLiteral("−%1").arg(std::lround(-db)));
        }
    }

    /// The level under the mouse: a dashed line, a dot on the spectrum and
    /// a box with the frequency and the level.
    void paintCursor(QPainter& p) const
    {
        if (!cursor_ || !spectrum_.hasSignal())
            return;
        const double x = std::clamp(*cursor_, 0.0, static_cast<double>(width()));
        const double hz = display::hzAtX(x, 0.0, width());
        const double db = spectrum_.levelDb(
            display::hzAtX(x - kColumn / 2.0, 0.0, width()), display::hzAtX(x + kColumn / 2.0, 0.0, width()));
        QColor guide = style::text();
        guide.setAlphaF(0.5f);
        QPen dashed(guide, 1.0);
        dashed.setDashPattern({3.0, 3.0});
        p.setPen(dashed);
        p.drawLine(QPointF(x, 0.0), QPointF(x, height()));
        p.setPen(Qt::NoPen);
        p.setBrush(style::text());
        p.drawEllipse(QPointF(x, yOf(db)), 5.0, 5.0);

        const QString text = db > AnalyzerSpectrum::kFloorDb
            ? QStringLiteral("%1   %2").arg(hzText(hz), dbText(db))
            : QStringLiteral("%1   –").arg(hzText(hz));
        const QFont boxFont = style::font(font(), 11, QFont::DemiBold, true);
        const double boxWidth = QFontMetricsF(boxFont).horizontalAdvance(text) + 20.0;
        const double left = x + 10.0 + boxWidth > width() ? x - 10.0 - boxWidth : x + 10.0;
        const QRectF box(left, 30.0, boxWidth, 26.0);
        p.setPen(QPen(style::strong(), 1.0));
        p.setBrush(style::faint());
        p.drawRoundedRect(box, 6.0, 6.0);
        p.setFont(boxFont);
        p.setPen(style::text());
        p.drawText(box, Qt::AlignCenter, text);
    }

    const AnalyzerSpectrum& spectrum_;
    double range_ = 72.0;
    std::optional<double> cursor_;
};

AnalyzerEditor::AnalyzerEditor(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate, QWidget* parent)
    : BuiltinEditorWindow(std::move(instance), title, sampleRate,
          {
              {tr("Mixing (Pink Tilt)"), {{Analyzer::kTilt, 4.5}, {Analyzer::kSpeed, 1.0}}},
              {tr("Flat"), {{Analyzer::kTilt, 0.0}}},
              {tr("Fast Detail"),
                  {{Analyzer::kSpeed, 2.0}, {Analyzer::kResolution, 3.0}, {Analyzer::kRange, 96.0}}},
              {tr("Slow Overview"), {{Analyzer::kSpeed, 0.0}, {Analyzer::kRange, 48.0}}},
          },
          parent)
    , spectrum_(BuiltinEditorWindow::sampleRate())
{
    resize(900, 440);
    display_ = new AnalyzerDisplay(spectrum_, nullptr);
    display_->setAccessibleName(
        tr("The spectrum from 20 Hz to 20 kHz with its held peaks; double-click to clear them"));
    display_->onReset = [this] {
        spectrum_.clearPeaks();
        display_->update();
    };
    setDisplay(display_);

    const std::vector<vloom::ReadoutInfo> readouts = this->instance().processor().readouts();
    for (std::size_t i = 0; i < readouts.size(); ++i)
        if (readouts[i].id == "peak_db")
            peakIndex_ = static_cast<int>(i);

    // Freeze, in the header as the design has it.
    freeze_ = new QToolButton;
    freeze_->setText(tr("Freeze"));
    freeze_->setCheckable(true);
    freeze_->setFocusPolicy(Qt::NoFocus);
    freeze_->setFixedHeight(26);
    freeze_->setToolTip(tr("Hold the picture and the peak still"));
    freeze_->setStyleSheet(
        QStringLiteral("QToolButton { border: 1px solid %1; border-radius: 7px; background: none; color: %2;"
                       " font-size: 11px; padding: 0 12px; }"
                       "QToolButton:checked { border-color: %3; color: %3; }")
            .arg(style::line().name(), style::text().name(), style::accent().name()));
    connect(
        freeze_, &QToolButton::clicked, this, [this](bool on) { change(Analyzer::kFreeze, on ? 1.0 : 0.0); });
    bind(Analyzer::kFreeze, [this](double v) { freeze_->setChecked(v >= 0.5); });
    addHeaderControl(freeze_);

    panel()->add(knob(Analyzer::kSpeed));
    auto* tilt = knob(Analyzer::kTilt, tr("Tilt"), ParameterScale::Linear);
    tilt->setFormatter([](double v) { return QStringLiteral("%1 dB/oct").arg(v, 0, 'f', 1); });
    tilt->setToolTip(
        tr("Tilt the picture up towards the highs (0 dB at 1 kHz): pink noise reads flat at 3 dB/oct"));
    panel()->add(tilt);
    auto* range = knob(Analyzer::kRange, tr("Range"), ParameterScale::Linear);
    range->setFormatter([](double v) { return QStringLiteral("%1 dB").arg(std::lround(v)); });
    panel()->add(range);
    panel()->addSpacing(8);
    panel()->add(toggle(Analyzer::kPeakHold, tr("Peak hold")));

    resolution_ = readout(Analyzer::kResolution, tr("Resolution"));
    resolution_->setWidestText(QStringLiteral("16384"));
    footer()->addLeft(resolution_);
    auto* channel = readout(Analyzer::kChannel, tr("Channel"));
    channel->setWidestText(QStringLiteral("Left + right"));
    channel->setToolTip(tr("The side the spectrum and the peak are of (click to change)"));
    footer()->addLeft(channel);
    peak_ = readout(tr("Peak"));
    peak_->setWidestText(dbText(-60.0));
    peak_->setText(QStringLiteral("–"));
    footer()->addRight(peak_);

    for (const std::uint32_t id : {Analyzer::kSpeed, Analyzer::kTilt, Analyzer::kRange, Analyzer::kPeakHold,
             Analyzer::kFreeze, Analyzer::kResolution})
        parameterChanged(id, value(id));
    startMetering();
}

void AnalyzerEditor::applyResolution(double resolution)
{
    const int size = analysedSize(resolution);
    spectrum_.setFftSize(size);
    const auto frames = static_cast<std::size_t>(size);
    left_.assign(frames, 0.0f);
    right_.assign(frames, 0.0f);
    signal_.assign(frames, 0.0f);
    const int wanted = Analyzer::fftSize(static_cast<int>(std::lround(resolution)));
    resolution_->setToolTip(wanted > size
            ? tr("FFT size. The engine keeps the newest %1 frames, so %2 is analysed at %1.")
                  .arg(size)
                  .arg(wanted)
            : tr("FFT size: finer in the lows, slower to follow (click to change)"));
}

void AnalyzerEditor::parameterChanged(std::uint32_t id, double v)
{
    switch (id) {
    case Analyzer::kSpeed:
        spectrum_.setAveragingSeconds(Analyzer::averagingSeconds(static_cast<int>(std::lround(v))));
        break;
    case Analyzer::kTilt:
        spectrum_.setTiltDbPerOctave(v);
        break;
    case Analyzer::kRange:
        display_->setRange(v);
        break;
    case Analyzer::kPeakHold:
        spectrum_.setPeakHold(v >= 0.5);
        break;
    case Analyzer::kFreeze:
        spectrum_.setFrozen(v >= 0.5);
        break;
    case Analyzer::kResolution:
        applyResolution(v);
        break;
    default:
        break;
    }
    display_->update();
}

void AnalyzerEditor::meter(const Meter& reading)
{
    const double elapsed
        = sinceAnalysis_.isValid() ? static_cast<double>(sinceAnalysis_.restart()) / 1000.0 : 0.0;
    if (!sinceAnalysis_.isValid())
        sinceAnalysis_.start();
    if (reading.running) {
        const std::size_t count = instance().readScopeStereo(left_, right_);
        const auto channel = static_cast<Analyzer::Channel>(std::lround(value(Analyzer::kChannel)));
        for (std::size_t i = 0; i < count; ++i) {
            if (channel == Analyzer::Channel::Left)
                signal_[i] = left_[i];
            else if (channel == Analyzer::Channel::Right)
                signal_[i] = right_[i];
            else
                signal_[i] = 0.5f * (left_[i] + right_[i]);
        }
        spectrum_.analyse(std::span<const float>(signal_.data(), count), elapsed);
    } else if (spectrum_.hasSignal()) {
        spectrum_.analyse({}, elapsed); // falls away at the chosen speed
    }

    // The processor's peak (it stands still while frozen, and does not run while bypassed).
    const double peak = peakIndex_ >= 0 && !isBypassed()
        ? static_cast<double>(instance().processor().readoutValue(peakIndex_))
        : Analyzer::kFloorDb;
    peak_->setText(peak > Analyzer::kFloorDb ? dbText(peak) : QStringLiteral("–"));
    display_->update();
}

} // namespace vloomdaw
