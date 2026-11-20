// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/AnalyzerEditor.hpp"

#include "core/ParameterScale.hpp"
#include "ui/plugins/builtin/AnimationPaint.hpp"
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
#include <QPolygonF>
#include <QToolButton>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <span>
#include <utility>
#include <vector>

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
///
/// Animated (docs/builtin-editors.md, "Animation"): each analysis leaves an
/// afterimage that sinks and fades as a mist under the live line (faster at
/// a faster Speed), the held line sparkles where a new peak is set, the
/// loudest point and the cursor's dot glow with the level. Freeze holds it
/// all still; no sound: it fades out with the spectrum.
class AnalyzerDisplay : public QWidget, public Animated {
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

    /// The spectrum was analysed again: reads its columns (animated only:
    /// the static picture reads them as it paints) and repaints.
    void refresh()
    {
        if (animationOn()) {
            readColumns(true);
            fresh_ = true;
        }
        if (!ticking_)
            update(); // else the next frame repaints it
    }

    bool advance(const motion::AudioFeatures& audio, double dt) override
    {
        ticking_ = false;
        if (spectrum_.frozen())
            return false; // Freeze: the picture holds still, the mist and sparks too
        ticking_ = true;
        level_ = audio.level();
        const QRectF plot(0.0, kTopY, width(), height() - kTopY);
        mist_.setArea(plot);
        // The afterimage falls away with the spectrum's own averaging: a
        // slow Speed lingers, a fast one clears quickly. It sinks as it fades.
        const double seconds = std::clamp(0.5 + 2.0 * spectrum_.averagingSeconds(), 0.5, 2.5);
        mist_.advance(motion::decayFactor(dt, seconds, 40.0),
            QTransform::fromTranslate(0.0, kSinkPxPerSecond * dt), 0.5);
        drift_ += QPointF(6.0 * dt, -10.0 * dt);
        if (fresh_ && spectrum_.hasSignal() && !columns_.empty()) {
            QColor ink = style::accent();
            ink.setAlphaF(static_cast<float>(0.12 + 0.18 * audio.envelope()));
            mapLine(line_);
            mist_.ink([&](QPainter& p) {
                p.setRenderHint(QPainter::Antialiasing);
                QPen pen(ink, 1.0); // one buffer pixel: three display pixels, softened up
                pen.setCosmetic(true);
                p.setPen(pen);
                p.drawPolyline(line_);
            });
        }
        const bool analysed = fresh_;
        fresh_ = false;
        sparks_.advance(dt, [](auto& spark, double step) { spark.state.lift += kSparkRise * step; });
        const double cursorTarget = cursor_ && spectrum_.hasSignal() ? 0.35 + 0.65 * level_ : 0.0;
        cursorGlow_ = motion::approach(cursorGlow_, cursorTarget, dt, 0.08);
        const bool moving = spectrum_.hasSignal() || mist_.isVisible() || sparks_.alive() > 0
            || cursorGlow_ > kRest || level_ > kRest;
        // Repainted at the rate the spectrum comes (about 30 a second): the
        // whole plot is redrawn, so every other frame would double the cost
        // for motion that is mostly the spectrum's own.
        sincePaint_ += dt;
        if (analysed || sincePaint_ >= kPaintSeconds) {
            sincePaint_ = 0.0;
            update();
        }
        return moving;
    }

    void rest() override
    {
        ticking_ = false;
        mist_.clear();
        sparks_.clear();
        cursorGlow_ = 0.0;
        level_ = 0.0;
        fresh_ = false;
        update();
    }

    void animationSwitched(bool on) override
    {
        if (on)
            readColumns(false);
        rest();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        if (!animationOn()) {
            paintStatic();
            return;
        }
        const AnimationPaintTimer timer;
        QPainter p(this);
        grid_.draw(p, *this, motion_paint::keyWith(0, range_), [this](QPainter& layer) {
            layer.setRenderHint(QPainter::Antialiasing);
            layer.fillRect(rect(), style::background());
            display::drawFrequencyGrid(layer, QRectF(0.0, 0.0, width(), height()), 14.0);
            paintLevels(layer);
        });
        if (columns_.size() != columnCount())
            readColumns(false);

        mist_.paint(p, 0.9);
        if (spectrum_.hasSignal() || spectrum_.peakHold()) {
            mapLine(line_);
            // The area under the line: not antialiased (its edge is the line).
            const auto count = static_cast<qsizetype>(line_.size());
            fill_.resize(count + 2);
            std::copy(line_.cbegin(), line_.cend(), fill_.begin());
            fill_[count] = QPointF(width(), height());
            fill_[count + 1] = QPointF(0.0, height());
            QColor shade = style::accent();
            shade.setAlphaF(0.2f);
            p.setPen(Qt::NoPen);
            p.setBrush(shade);
            p.drawPolygon(fill_);
            p.setBrush(Qt::NoBrush);

            // The loudest point glows with the level.
            if (spectrum_.hasSignal() && level_ > kRest) {
                const auto loudest = std::min_element(
                    line_.cbegin(), line_.cend(), [](QPointF a, QPointF b) { return a.y() < b.y(); });
                if (loudest != line_.cend())
                    motion_paint::glowDot(p, *loudest, 22.0, style::accent(), 0.45 * level_);
            }

            // The live line, 1 px (a wide pen goes through the stroker).
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(style::accent(), 1.0));
            p.drawPolyline(line_);

            if (anyPeak_) {
                mapPeak(peak_);
                p.setPen(QPen(motion_paint::faded(style::text(), 0.7), 1.0));
                p.drawPolyline(peak_);
            }
        }
        paintSparks(p);
        if (cursorGlow_ > kRest && cursor_) {
            const double x = std::clamp(*cursor_, 0.0, static_cast<double>(width()));
            motion_paint::glowDot(p, QPointF(x, yOf(cursorDb(x))), 16.0, style::text(), 0.6 * cursorGlow_);
        }
        paintCursor(p);
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        if (animationOn())
            readColumns(false);
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
    static constexpr double kSinkPxPerSecond = 18.0; ///< the afterimage sinks
    static constexpr double kSparkRise = 14.0; ///< px a second a spark floats up
    static constexpr double kSparkLife = 0.6; ///< s
    static constexpr double kSparkRiseDb = 1.0; ///< a held peak up this much: a spark
    static constexpr int kSparkSpacing = 10; ///< columns between two sparks of one analysis
    static constexpr std::size_t kSparksPerAnalysis = 6;
    static constexpr double kRest = 0.003;
    static constexpr double kPaintSeconds = 0.04; ///< the longest between two repaints

    struct Spark {
        double x = 0.0;
        double db = 0.0; ///< where the held line was set
        double lift = 0.0; ///< px it has floated up
        double strength = 0.0; ///< 0 .. 1
    };

    [[nodiscard]] double yOf(double db) const
    {
        const double share = std::clamp((kTopDb - db) / range_, 0.0, 1.0);
        return kTopY + share * (height() - kTopY);
    }

    [[nodiscard]] std::size_t columnCount() const
    {
        return static_cast<std::size_t>(std::floor(width() / kColumn)) + 1;
    }

    /// The level at column x.
    [[nodiscard]] double cursorDb(double x) const
    {
        return spectrum_.levelDb(
            display::hzAtX(x - kColumn / 2.0, 0.0, width()), display::hzAtX(x + kColumn / 2.0, 0.0, width()));
    }

    /// Reads the live and held levels of every column (each analysis, ~30 a
    /// second, not each frame), and with `sparkle` spawns sparks where the
    /// held line rose most.
    void readColumns(bool sparkle)
    {
        const std::size_t count = columnCount();
        if (columns_.size() != count) {
            columns_.assign(count, AnalyzerSpectrum::kFloorDb);
            held_.assign(count, AnalyzerSpectrum::kFloorDb);
            line_.resize(static_cast<qsizetype>(count));
            peak_.resize(static_cast<qsizetype>(count));
            sparkle = false; // a new size: no old line to compare with
        }
        std::array<std::pair<double, std::size_t>, kSparksPerAnalysis> best{};
        anyPeak_ = false;
        for (std::size_t i = 0; i < count; ++i) {
            const double x = static_cast<double>(i) * kColumn;
            const double from = display::hzAtX(x - kColumn / 2.0, 0.0, width());
            const double to = display::hzAtX(x + kColumn / 2.0, 0.0, width());
            columns_[i] = spectrum_.levelDb(from, to);
            const double held = spectrum_.peakDb(from, to);
            anyPeak_ = anyPeak_ || held > AnalyzerSpectrum::kFloorDb;
            const double rise = held - held_[i];
            held_[i] = held;
            if (!sparkle || rise < kSparkRiseDb || held <= kTopDb - range_)
                continue;
            // Keep the biggest rises, apart from each other: a neighbour's
            // slot if one is near, else the weakest.
            std::size_t slot = 0;
            for (std::size_t k = 0; k < best.size(); ++k) {
                if (best[k].first > 0.0 && i - best[k].second < static_cast<std::size_t>(kSparkSpacing)) {
                    slot = k;
                    break;
                }
                if (best[k].first < best[slot].first)
                    slot = k;
            }
            if (rise > best[slot].first)
                best[slot] = {rise, i};
        }
        for (const auto& [rise, index] : best) {
            if (rise <= 0.0)
                continue;
            sparks_.spawn(Spark{.x = static_cast<double>(index) * kColumn,
                              .db = held_[index],
                              .lift = 0.0,
                              .strength = std::min(1.0, 0.4 + rise / 12.0)},
                kSparkLife);
        }
    }

    void mapLine(QPolygonF& out) const
    {
        for (std::size_t i = 0; i < columns_.size(); ++i)
            out[static_cast<qsizetype>(i)] = QPointF(static_cast<double>(i) * kColumn, yOf(columns_[i]));
    }
    void mapPeak(QPolygonF& out) const
    {
        for (std::size_t i = 0; i < held_.size(); ++i)
            out[static_cast<qsizetype>(i)] = QPointF(static_cast<double>(i) * kColumn, yOf(held_[i]));
    }

    void paintSparks(QPainter& p) const
    {
        if (sparks_.alive() == 0)
            return;
        motion_paint::GlowBatch glow(p, style::text());
        sparks_.forEach([&](const auto& spark) {
            const double fade = 1.0 - spark.progress();
            const QPointF at(spark.state.x, yOf(spark.state.db) - spark.state.lift);
            glow.dot(at, 7.0, 0.55 * spark.state.strength * fade);
            glow.dot(at, 2.5, spark.state.strength * fade);
        });
    }

    /// The picture with the animation off (as the design has it).
    void paintStatic()
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
        if (animationOn()) {
            // Painted each frame: on whole pixels, not antialiased (cheaper).
            p.setRenderHint(QPainter::Antialiasing, false);
            const double column = std::floor(x) + 0.5;
            p.drawLine(QPointF(column, 0.0), QPointF(column, height()));
            p.setRenderHint(QPainter::Antialiasing);
        } else {
            p.drawLine(QPointF(x, 0.0), QPointF(x, height()));
        }
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

    // Animated only: the columns read at each analysis, and the motion.
    std::vector<double> columns_; ///< the live level of each column, dB
    std::vector<double> held_; ///< the held level of each column, dB
    bool anyPeak_ = false;
    bool fresh_ = false; ///< an analysis not yet inked into the mist
    bool ticking_ = false; ///< frames come (and repaint): refresh() need not
    QPolygonF line_; ///< reused each paint: no allocation a frame
    QPolygonF peak_;
    QPolygonF fill_;
    double level_ = 0.0; ///< the output's level (AudioFeatures::level())
    double cursorGlow_ = 0.0;
    double sincePaint_ = 0.0; ///< s
    QPointF drift_; ///< the mist's noise, drifting
    motion_paint::TrailBuffer mist_{0.25};
    motion::ParticlePool<Spark, 48> sparks_;
    motion_paint::LayerCache grid_;
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
        display_->refresh();
    };
    setDisplay(display_);
    animate(display_);

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
    display_->refresh();
}

} // namespace vloomdaw
