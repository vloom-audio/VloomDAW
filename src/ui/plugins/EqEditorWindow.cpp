// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/EqEditorWindow.hpp"

#include "ui/Theme.hpp"
#include "ui/plugins/EditorMotion.hpp"
#include "ui/plugins/ParameterText.hpp"
#include "ui/plugins/PresetBar.hpp"
#include "ui/plugins/builtin/Animation.hpp"
#include "ui/plugins/builtin/AnimationPaint.hpp"
#include "ui/widgets/Knob/Knob.hpp"
#include "ui/widgets/ToggleSwitch/ToggleSwitch.hpp"

#include <vloom/engine/plugin/PluginInstance.hpp>

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vloomdaw {

namespace {

using Eq = vloom::dsp::Eq;

constexpr double kMinHz = 20.0;
constexpr double kMaxHz = 20000.0;
constexpr double kRangeDb = 24.0; ///< top and bottom of the graph
constexpr double kNodeRadius = 10.0;
constexpr double kGrabRadius = 18.0; ///< how near a click selects a point
constexpr int kPanelMargin = 16; ///< panel to the bottom of the graph
constexpr int kKnobSize = 44;
constexpr double kQStep = 1.2; ///< per wheel notch
constexpr int kSpectrumMs = 33; ///< between two readings of the spectrum
constexpr double kSpectrumTopDb = 0.0; ///< at the top of the graph
constexpr double kSpectrumBottomDb = -90.0; ///< at its bottom
/// Music falls with frequency; tilted this much about 1 kHz it reads level.
constexpr double kSpectrumTiltDb = 4.5; ///< per octave
constexpr int kSpectrumStep = 3; ///< pixels between points of the spectrum
/// A section's node glows with the level of the spectrum this far either
/// side of its frequency (octaves), over this range (dB, tilted).
constexpr double kNodeRegionOctaves = 0.5;
constexpr double kNodeQuietDb = -66.0;
constexpr double kNodeLoudDb = -12.0;
constexpr double kMistSeconds = 0.9; ///< the spectrum's afterimage fades 40 dB in this long
constexpr double kMistRise = 14.0; ///< px per second the mist drifts up
constexpr double kBoostDb = 0.25; ///< the summed curve glows where it boosts more than this

/// A setting that comes with the app: the values that differ from the defaults.
struct FactoryPreset {
    const char* name;
    std::vector<std::pair<std::uint32_t, double>> values;
};

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> kPresets{
        {QT_TRANSLATE_NOOP("vloomdaw::EqEditorWindow", "Default"), {}},
        {QT_TRANSLATE_NOOP("vloomdaw::EqEditorWindow", "Low Cut 80 Hz"),
            {{Eq::kLowCutOn, 1.0}, {Eq::kLowCutFrequency, 80.0}}},
        {QT_TRANSLATE_NOOP("vloomdaw::EqEditorWindow", "Vocal Presence"),
            {{Eq::kLowCutOn, 1.0}, {Eq::kLowCutFrequency, 90.0}, {Eq::kLowMidFrequency, 300.0},
                {Eq::kLowMidGain, -2.5}, {Eq::kLowMidQ, 1.2}, {Eq::kHighMidFrequency, 3500.0},
                {Eq::kHighMidGain, 3.0}, {Eq::kHighShelfFrequency, 10000.0}, {Eq::kHighShelfGain, 2.0}}},
        {QT_TRANSLATE_NOOP("vloomdaw::EqEditorWindow", "Bass Boost"),
            {{Eq::kLowShelfFrequency, 100.0}, {Eq::kLowShelfGain, 4.0}, {Eq::kLowMidFrequency, 350.0},
                {Eq::kLowMidGain, -2.0}}},
        {QT_TRANSLATE_NOOP("vloomdaw::EqEditorWindow", "Mud Cut"),
            {{Eq::kLowMidFrequency, 300.0}, {Eq::kLowMidGain, -4.0}, {Eq::kLowMidQ, 1.4}}},
        {QT_TRANSLATE_NOOP("vloomdaw::EqEditorWindow", "Air"),
            {{Eq::kHighShelfFrequency, 10000.0}, {Eq::kHighShelfGain, 4.0}}},
        {QT_TRANSLATE_NOOP("vloomdaw::EqEditorWindow", "Telephone"),
            {{Eq::kLowCutOn, 1.0}, {Eq::kLowCutFrequency, 300.0}, {Eq::kHighCutOn, 1.0},
                {Eq::kHighCutFrequency, 3400.0}, {Eq::kHighMidFrequency, 1800.0}, {Eq::kHighMidGain, 4.0},
                {Eq::kHighMidQ, 1.2}}},
    };
    return kPresets;
}

constexpr std::array<EqSection, kEqSections> kSections{EqSection::LowCut, EqSection::LowShelf,
    EqSection::LowMid, EqSection::HighMid, EqSection::HighShelf, EqSection::HighCut};

std::size_t indexOf(EqSection section) { return static_cast<std::size_t>(section); }

/// Each section's colour, from the theme's pattern hues; the number on the
/// point and in the panel says which is which without it.
QColor sectionColor(EqSection section)
{
    static constexpr std::array<std::size_t, kEqSections> kHues{5, 1, 3, 6, 4, 2};
    return Theme::patternColor(kHues[indexOf(section)]);
}

QString sectionName(EqSection section)
{
    return QString::fromUtf8(eqParameter(eqSectionParameters(section).frequency).group);
}

QString sectionShape(EqSection section)
{
    switch (section) {
    case EqSection::LowCut:
    case EqSection::HighCut:
        return EqEditorWindow::tr("Cut, 12 dB/oct");
    case EqSection::LowShelf:
    case EqSection::HighShelf:
        return EqEditorWindow::tr("Shelf");
    case EqSection::LowMid:
    case EqSection::HighMid:
        break;
    }
    return EqEditorWindow::tr("Bell");
}

/// A knob's position (0 .. 1) for a value that turns in octaves, and back.
double toPosition(const vloom::dsp::ParameterSpec& parameter, double value)
{
    return std::log(value / parameter.minValue) / std::log(parameter.maxValue / parameter.minValue);
}
double fromPosition(const vloom::dsp::ParameterSpec& parameter, double position)
{
    return parameter.minValue * std::pow(parameter.maxValue / parameter.minValue, position);
}

/// The numbered dot that names a section.
class Badge : public QWidget {
public:
    using QWidget::QWidget;

    void show(EqSection section)
    {
        section_ = section;
        update();
    }
    [[nodiscard]] QSize sizeHint() const override { return {18, 18}; }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(sectionColor(section_));
        p.drawEllipse(QRectF(rect()).adjusted(1, 1, -1, -1));
        QFont f = font();
        f.setBold(true);
        f.setPixelSize(11);
        p.setFont(f);
        p.setPen(Theme::colors().base);
        p.drawText(rect(), Qt::AlignCenter, QString::number(indexOf(section_) + 1));
    }

private:
    EqSection section_ = EqSection::LowCut;
};

} // namespace

/// The response curve and its points. Animated: the spectrum leaves a
/// rising afterimage, each section's point glows with the sound around its
/// frequency and the summed curve glows where it boosts, by the level.
class EqGraph : public QWidget, public Animated {
public:
    EqGraph(const EqResponse& response, const SpectrumAnalyzer& spectrum, QWidget* parent)
        : QWidget(parent)
        , response_(response)
        , spectrum_(spectrum)
    {
        setMouseTracking(true);
        setMinimumSize(560, 300);
        energy_.fill(motion::EnvelopeFollower(0.03, 0.3));
    }

    bool advance(const motion::AudioFeatures& audio, double dt) override
    {
        const double live = audio.liveliness();
        level_ = audio.level() * live;
        bool moving = false;
        for (EqSection section : kSections) {
            double target = 0.0;
            if (spectrum_.hasSignal() && isOn(section)) {
                const double hz = response_.value(eqSectionParameters(section).frequency);
                const double spread = std::exp2(kNodeRegionOctaves);
                const double db
                    = spectrum_.levelDb(hz / spread, hz * spread) + kSpectrumTiltDb * std::log2(hz / 1000.0);
                target = std::clamp((db - kNodeQuietDb) / (kNodeLoudDb - kNodeQuietDb), 0.0, 1.0) * live;
            }
            motion::EnvelopeFollower& energy = energy_[indexOf(section)];
            moving = energy.step(target, dt) > 0.01 || moving;
        }
        // The afterimage: what the spectrum was, fading and rising.
        if (mist_.area() != QRectF(rect()))
            mist_.setArea(QRectF(rect()));
        mist_.advance(motion::decayFactor(dt, kMistSeconds, 40.0),
            QTransform::fromTranslate(0.0, -kMistRise * dt), 0.5);
        drift_ += kMistRise * dt;
        if (spectrum_.hasSignal())
            makeSpectrum(); // once a frame, for the mist and the paint
        if (spectrum_.hasSignal() && level_ > 0.0) {
            const QColor ink = motion_paint::faded(Theme::colors().accent, 0.25 + 0.5 * level_);
            // Inked every frame and kept about 12 frames: faint, it builds up.
            const QColor body = motion_paint::faded(Theme::colors().text, 0.012 + 0.008 * level_);
            mist_.ink([this, &ink, &body](QPainter& p) {
                QPen pen(ink, 1.0);
                pen.setCosmetic(true); // a buffer pixel: soft once smoothed up
                p.setPen(pen);
                p.drawPolyline(spectrumPoints_.data(), static_cast<int>(spectrumPoints_.size()) - 2);
                // The body of the spectrum lives in the mist (a small image: cheap to fill).
                p.setPen(Qt::NoPen);
                p.setBrush(body);
                p.drawPolygon(spectrumPoints_.data(), static_cast<int>(spectrumPoints_.size()));
            });
        }
        update();
        return moving || mist_.isVisible() || level_ > 0.01;
    }

    void rest() override
    {
        mist_.clear();
        for (motion::EnvelopeFollower& energy : energy_)
            energy.reset();
        level_ = 0.0;
        update();
    }
    void animationSwitched(bool /*on*/) override { rest(); }

    std::function<void(std::uint32_t id, double value)> onChange;
    std::function<void(EqSection)> onSelect;
    std::function<void()> onResize;

    void setSelected(EqSection section)
    {
        selected_ = section;
        update();
    }
    /// The response changed: draw it again.
    void refresh()
    {
        stale_ = true;
        update();
    }

protected:
    void resizeEvent(QResizeEvent* /*event*/) override
    {
        stale_ = true;
        if (onResize)
            onResize();
    }

    void paintEvent(QPaintEvent* /*event*/) override
    {
        const AnimationPaintTimer timer;
        if (stale_)
            rebuild();
        QPainter p(this);
        back_.draw(p, *this, 0, [this](QPainter& layer) { paintGrid(layer); });
        if (animationOn())
            mist_.paint(p, 0.9, 0.3, QPointF(0.0, drift_));
        if (spectrum_.hasSignal())
            paintSpectrum(p);
        front_.draw(p, *this, frontKey(), [this](QPainter& layer) { paintCurves(layer); });
        if (animationOn())
            paintMotion(p);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        if (const auto section = sectionAt(event->position())) {
            dragging_ = true;
            if (onSelect)
                onSelect(*section);
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!dragging_) {
            setCursor(sectionAt(event->position()) ? Qt::PointingHandCursor : Qt::ArrowCursor);
            return;
        }
        if (!onChange)
            return;
        const EqSectionParameters ids = eqSectionParameters(selected_);
        onChange(ids.frequency, frequencyAt(event->position().x()));
        if (ids.gain)
            onChange(*ids.gain, dbAt(event->position().y()));
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton)
            dragging_ = false;
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        const auto section = sectionAt(event->position());
        if (!section || !onChange)
            return;
        const EqSectionParameters ids = eqSectionParameters(*section);
        if (ids.on)
            onChange(*ids.on, isOn(*section) ? 0.0 : 1.0); // a cut: switch it
        else if (ids.gain)
            onChange(*ids.gain, 0.0); // a shelf or a bell: back to flat
    }

    void wheelEvent(QWheelEvent* event) override
    {
        const EqSectionParameters ids = eqSectionParameters(sectionAt(event->position()).value_or(selected_));
        if (!ids.q || !onChange || event->angleDelta().y() == 0) {
            event->ignore();
            return;
        }
        const double notches = event->angleDelta().y() / 120.0;
        onChange(*ids.q, response_.value(*ids.q) * std::pow(kQStep, notches));
    }

private:
    /// The grid and its labels: follow only the size.
    void paintGrid(QPainter& p) const
    {
        const Theme::Colors& c = Theme::colors();
        p.fillRect(rect(), c.base);
        p.setRenderHint(QPainter::Antialiasing);
        QFont small = font();
        small.setPixelSize(10);
        p.setFont(small);
        for (double hz : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0}) {
            const double x = std::round(xOf(hz)) + 0.5;
            p.setPen(QPen(c.panel, 1));
            p.drawLine(QPointF(x, 0), QPointF(x, height()));
            p.setPen(c.textDim);
            p.drawText(QPointF(x + 4, height() - 6),
                hz >= 1000.0 ? QStringLiteral("%1k").arg(hz / 1000.0) : QString::number(hz));
        }
        for (double db : {18.0, 12.0, 6.0, -6.0, -12.0, -18.0}) {
            const double y = std::round(yOf(db)) + 0.5;
            p.setPen(QPen(c.panel, 1));
            p.drawLine(QPointF(0, y), QPointF(width(), y));
            p.setPen(c.textDim);
            const QString text = QStringLiteral("%1%2").arg(db > 0 ? QStringLiteral("+") : QString()).arg(db);
            p.drawText(QRectF(width() - 48, y - 14, 42, 12), Qt::AlignRight | Qt::AlignVCenter, text);
        }
        const double zero = std::round(yOf(0.0)) + 0.5;
        p.setPen(QPen(c.border, 1));
        p.drawLine(QPointF(0, zero), QPointF(width(), zero));
    }

    /// The spectrum's outline into spectrumPoints_, closed along the bottom
    /// by its last two points (reused: no allocation once sized).
    void makeSpectrum()
    {
        spectrumPoints_.clear();
        for (int x = 0; x <= width() + kSpectrumStep; x += kSpectrumStep) {
            const double from = frequencyAt(x - kSpectrumStep / 2.0);
            const double to = frequencyAt(x + kSpectrumStep / 2.0);
            const double tilt = kSpectrumTiltDb * std::log2(std::sqrt(from * to) / 1000.0);
            spectrumPoints_.emplace_back(static_cast<double>(x), levelY(spectrum_.levelDb(from, to) + tilt));
        }
        spectrumPoints_.emplace_back(
            static_cast<double>(width() + kSpectrumStep), static_cast<double>(height()));
        spectrumPoints_.emplace_back(0.0, static_cast<double>(height()));
    }

    /// What the equalizer puts out, behind the curves: filled without
    /// antialiasing, edged with a 1 px line (cheap every frame).
    void paintSpectrum(QPainter& p)
    {
        if (!animationOn() || spectrumPoints_.size() < 3)
            makeSpectrum(); // animating: advance() made it this frame
        const Theme::Colors& c = Theme::colors();
        if (!animationOn()) {
            // Animating, the mist carries the spectrum's body.
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(Qt::NoPen);
            p.setBrush(motion_paint::faded(c.text, 18.0 / 255.0));
            p.drawPolygon(spectrumPoints_.data(), static_cast<int>(spectrumPoints_.size()));
        }
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(motion_paint::faded(c.text, (70.0 + 80.0 * level_) / 255.0), 1));
        p.drawPolyline(spectrumPoints_.data(), static_cast<int>(spectrumPoints_.size()) - 2);
    }

    /// Each section's curve, the whole equalizer's and the points: follow
    /// the values and the selection.
    void paintCurves(QPainter& p) const
    {
        const Theme::Colors& c = Theme::colors();
        p.setRenderHint(QPainter::Antialiasing);
        for (EqSection section : kSections) {
            if (!shapes(section))
                continue;
            QColor fill = sectionColor(section);
            QColor line = fill;
            fill.setAlpha(section == selected_ ? 76 : 36);
            line.setAlpha(140);
            p.setPen(QPen(line, 1));
            p.setBrush(fill);
            p.drawPath(fills_[indexOf(section)]);
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(c.accent, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(total_);

        QFont bold = font();
        bold.setBold(true);
        bold.setPixelSize(11);
        p.setFont(bold);
        for (EqSection section : kSections) {
            const QPointF at = node(section);
            const QColor colour = sectionColor(section);
            if (section == selected_) {
                QColor ring = colour;
                ring.setAlpha(115);
                p.setPen(QPen(ring, 2));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(at, kNodeRadius + 6, kNodeRadius + 6);
            }
            const bool on = isOn(section);
            p.setPen(QPen(colour, 2));
            p.setBrush(on ? colour : c.base);
            p.drawEllipse(at, kNodeRadius, kNodeRadius);
            p.setPen(on ? c.base : colour);
            p.drawText(QRectF(at.x() - kNodeRadius, at.y() - kNodeRadius, 2 * kNodeRadius, 2 * kNodeRadius),
                Qt::AlignCenter, QString::number(indexOf(section) + 1));
        }
    }

    /// The glows: the boosted stretches of the summed curve, the points.
    void paintMotion(QPainter& p) const
    {
        if (level_ > 0.01 && !boosted_.isEmpty())
            motion_paint::glowStroke(p, boosted_, Theme::colors().accent,
                level_ * std::clamp(0.35 + maxBoostDb_ / 12.0, 0.0, 1.0), 9.0, 4.0);
        for (EqSection section : kSections) {
            const double energy = energy_[indexOf(section)].value();
            if (energy > 0.01)
                motion_paint::glowDot(
                    p, node(section), kNodeRadius + 6.0 + 10.0 * energy, sectionColor(section), 0.8 * energy);
        }
    }

    /// The key of the curves' layer: every value and the selection.
    [[nodiscard]] std::uint64_t frontKey() const
    {
        std::uint64_t key = motion_paint::keyWith(0, static_cast<double>(indexOf(selected_)));
        for (std::uint32_t id = 0; id < Eq::kParameterCount; ++id)
            key = motion_paint::keyWith(key, response_.value(id));
        return key;
    }

    [[nodiscard]] double xOf(double hz) const
    {
        return width() * std::log(hz / kMinHz) / std::log(kMaxHz / kMinHz);
    }
    [[nodiscard]] double frequencyAt(double x) const
    {
        return kMinHz * std::pow(kMaxHz / kMinHz, std::clamp(x / std::max(1, width()), 0.0, 1.0));
    }
    [[nodiscard]] double yOf(double db) const
    {
        return height() / 2.0 - std::clamp(db, -kRangeDb, kRangeDb) * (height() / 2.0) / kRangeDb;
    }
    [[nodiscard]] double dbAt(double y) const { return (height() / 2.0 - y) * kRangeDb / (height() / 2.0); }
    /// Where a level of the spectrum (dB, full scale: 0) is drawn.
    [[nodiscard]] double levelY(double db) const
    {
        const double part = (std::clamp(db, kSpectrumBottomDb, kSpectrumTopDb) - kSpectrumBottomDb)
            / (kSpectrumTopDb - kSpectrumBottomDb);
        return height() * (1.0 - part);
    }

    /// A cut that is off has a hollow point.
    [[nodiscard]] bool isOn(EqSection section) const
    {
        const EqSectionParameters ids = eqSectionParameters(section);
        return !ids.on || response_.value(*ids.on) >= 0.5;
    }
    /// Whether the section changes the sound, so its curve is worth drawing.
    [[nodiscard]] bool shapes(EqSection section) const
    {
        const EqSectionParameters ids = eqSectionParameters(section);
        return ids.on ? isOn(section) : response_.value(*ids.gain) != 0.0;
    }
    [[nodiscard]] QPointF node(EqSection section) const
    {
        const EqSectionParameters ids = eqSectionParameters(section);
        return {xOf(response_.value(ids.frequency)), yOf(ids.gain ? response_.value(*ids.gain) : 0.0)};
    }
    [[nodiscard]] std::optional<EqSection> sectionAt(QPointF point) const
    {
        std::optional<EqSection> found;
        double nearest = kGrabRadius;
        for (EqSection section : kSections) {
            const double distance = QLineF(point, node(section)).length();
            // Points on top of each other: the selected one wins.
            if (distance < nearest
                || (distance <= nearest && section == selected_ && distance < kGrabRadius)) {
                nearest = distance;
                found = section;
            }
        }
        return found;
    }

    void rebuild()
    {
        constexpr int kStep = 3; // pixels between points of a curve
        const double zero = yOf(0.0);
        total_ = QPainterPath();
        boosted_ = QPainterPath();
        maxBoostDb_ = 0.0;
        bool boosting = false;
        for (QPainterPath& fill : fills_)
            fill = QPainterPath();
        for (int x = 0; x <= width() + kStep; x += kStep) {
            const double hz = frequencyAt(x);
            const double totalDb = response_.totalDb(hz);
            const QPointF total(x, yOf(totalDb));
            if (x == 0)
                total_.moveTo(total);
            else
                total_.lineTo(total);
            // Where it boosts, for the glow.
            if (totalDb > kBoostDb && x <= width()) {
                if (boosting)
                    boosted_.lineTo(total);
                else
                    boosted_.moveTo(total);
                boosting = true;
                maxBoostDb_ = std::max(maxBoostDb_, totalDb);
            } else {
                boosting = false;
            }
            for (EqSection section : kSections) {
                QPainterPath& fill = fills_[indexOf(section)];
                if (x == 0)
                    fill.moveTo(0, zero);
                fill.lineTo(x, yOf(response_.sectionDb(section, hz)));
            }
        }
        for (QPainterPath& fill : fills_) {
            fill.lineTo(width() + kStep, zero);
            fill.closeSubpath();
        }
        stale_ = false;
    }

    const EqResponse& response_;
    const SpectrumAnalyzer& spectrum_;
    EqSection selected_ = EqSection::LowMid;
    bool dragging_ = false;
    bool stale_ = true;
    QPainterPath total_;
    std::array<QPainterPath, kEqSections> fills_;
    QPainterPath boosted_; ///< the stretches of total_ that boost
    double maxBoostDb_ = 0.0;

    motion_paint::LayerCache back_; ///< the grid
    motion_paint::LayerCache front_; ///< the curves and the points
    std::vector<QPointF> spectrumPoints_;
    motion_paint::TrailBuffer mist_{0.34};
    double drift_ = 0.0; ///< px the mist has risen
    double level_ = 0.0; ///< the output's level, faded with the sound
    std::array<motion::EnvelopeFollower, kEqSections> energy_{};
};

bool EqEditorWindow::edits(const vloom::PluginInstance& instance)
{
    return instance.processor().info().id == Eq::spec().id;
}

EqEditorWindow::EqEditorWindow(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate, QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::Tool)
    , instance_(std::move(instance))
    , response_(sampleRate)
    , analyzer_(sampleRate)
    , scope_(SpectrumAnalyzer::kFrames)
{
    setObjectName(QStringLiteral("eqEditor"));
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(title);

    const vloom::AudioProcessor& processor = instance_->processor();
    for (const vloom::dsp::ParameterSpec& parameter : Eq::parameters())
        response_.set(parameter.id, processor.parameterValue(parameter.id).value_or(parameter.defaultValue));

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    auto* header = new QFrame(this);
    header->setObjectName(QStringLiteral("pluginEditorHeader"));
    auto* headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(12, 7, 12, 7);
    headerRow->setSpacing(10);
    power_ = new QToolButton(header);
    power_->setIcon(Theme::icon(QStringLiteral("power")));
    power_->setCheckable(true);
    power_->setChecked(true);
    power_->setFocusPolicy(Qt::NoFocus);
    power_->setToolTip(tr("Effect on (click to bypass)"));
    power_->setAccessibleName(tr("Effect on"));
    connect(power_, &QToolButton::clicked, this, [this](bool on) { emit bypassToggled(!on); });
    headerRow->addWidget(power_);
    auto* heading = new QLabel(QString::fromUtf8(Eq::spec().name), header);
    heading->setObjectName(QStringLiteral("pluginEditorTitle"));
    headerRow->addWidget(heading);
    headerRow->addStretch(1);

    // Presets: the app's (the values that differ from the defaults) and the user's.
    std::vector<PresetBar::Factory> factory;
    for (const FactoryPreset& each : factoryPresets()) {
        PresetBar::Factory preset{.name = tr(each.name), .values = PresetBar::Values(Eq::kParameterCount)};
        for (const vloom::dsp::ParameterSpec& parameter : Eq::parameters())
            preset.values[parameter.id] = parameter.defaultValue;
        for (const auto& [id, value] : each.values)
            preset.values[id] = value;
        factory.push_back(std::move(preset));
    }
    presets_ = new PresetBar(QStringLiteral("vloom.eq"), std::move(factory),
        PresetBar::Source{
            .read =
                [this] {
                    const Values now = values();
                    return PresetBar::Values(now.begin(), now.end());
                },
            .apply =
                [this](const PresetBar::Values& all) {
                    Values next = values();
                    std::copy_n(all.begin(), std::min(all.size(), next.size()), next.begin());
                    apply(next);
                },
            .clamp =
                [](std::uint32_t id, double value) {
                    return std::clamp(value, eqParameter(id).minValue, eqParameter(id).maxValue);
                },
        },
        header);
    // A and B name presets by position: keep them pointing at the same ones.
    connect(presets_, &PresetBar::removed, this, [this](int gone) {
        for (Snapshot& snapshot : slots_) {
            if (snapshot.preset == gone)
                snapshot.preset = -1;
            else if (snapshot.preset > gone)
                --snapshot.preset;
        }
    });
    headerRow->addWidget(presets_);
    headerRow->addStretch(1);

    // A and B: two settings to switch between and compare.
    auto* slotGroup = new QFrame(header);
    slotGroup->setObjectName(QStringLiteral("slotGroup"));
    auto* slotRow = new QHBoxLayout(slotGroup);
    slotRow->setContentsMargins(2, 2, 2, 2);
    slotRow->setSpacing(2);
    for (int slot = 0; slot < 2; ++slot) {
        auto* button = new QToolButton(slotGroup);
        button->setText(slot == 0 ? QStringLiteral("A") : QStringLiteral("B"));
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolTip(
            slot == 0 ? tr("Setting A: switch to B to compare") : tr("Setting B: switch to A to compare"));
        button->setAccessibleName(slot == 0 ? tr("Setting A") : tr("Setting B"));
        connect(button, &QToolButton::clicked, this, [this, slot] { chooseSlot(slot); });
        slotRow->addWidget(button);
        slotButtons_[static_cast<std::size_t>(slot)] = button;
    }
    headerRow->addWidget(slotGroup);
    column->addWidget(header);

    graph_ = new EqGraph(response_, analyzer_, this);
    graph_->setObjectName(QStringLiteral("eqGraph"));
    graph_->onSelect = [this](EqSection section) { select(section); };
    graph_->onChange = [this](std::uint32_t id, double value) { change(id, value); };
    graph_->onResize = [this] { placePanel(); };
    column->addWidget(graph_, 1);
    motion_ = new EditorMotion(instance_, sampleRate, this, false); // the spectrum switches the scope
    motion_->add(graph_);

    auto* hint = new QLabel(
        tr("Drag a point: frequency and gain.  Wheel: Q.  Double-click: flat, or switch a cut."), this);
    hint->setObjectName(QStringLiteral("pluginEditorHint"));
    column->addWidget(hint);

    // The selected section's controls, over the bottom of the graph.
    auto* panel = new QFrame(graph_);
    panel->setObjectName(QStringLiteral("eqBandPanel"));
    panel_ = panel;
    // Another section's knobs change the size the panel needs; the request
    // arrives once they are laid out.
    panel->installEventFilter(this);
    auto* row = new QHBoxLayout(panel);
    row->setContentsMargins(14, 10, 12, 10);
    row->setSpacing(14);

    auto* about = new QVBoxLayout;
    about->setSpacing(6);
    auto* titleRow = new QHBoxLayout;
    titleRow->setSpacing(6);
    auto* badge = new Badge(panel);
    badge->setFixedSize(badge->sizeHint());
    badge_ = badge;
    titleRow->addWidget(badge);
    name_ = new QLabel(panel);
    name_->setObjectName(QStringLiteral("eqBandName"));
    name_->setMinimumWidth(78); // the longest name: the panel keeps its width
    titleRow->addWidget(name_, 1);
    about->addLayout(titleRow);
    shape_ = new QLabel(panel);
    shape_->setObjectName(QStringLiteral("eqBandShape"));
    about->addWidget(shape_);
    on_ = new ToggleSwitch(panel);
    on_->setAccessibleName(tr("Section on"));
    on_->setToolTip(tr("Switch this cut on or off"));
    connect(on_, &ToggleSwitch::clicked, this, [this](bool on) {
        if (const auto id = eqSectionParameters(selected_).on)
            change(*id, on ? 1.0 : 0.0);
    });
    about->addWidget(on_, 0, Qt::AlignLeft);
    about->addStretch(1);
    row->addLayout(about);

    knobs_ = new QWidget(panel);
    auto* knobRow = new QHBoxLayout(knobs_);
    knobRow->setContentsMargins(0, 0, 0, 0);
    knobRow->setSpacing(4);
    row->addWidget(knobs_);

    auto* steps = new QVBoxLayout;
    steps->setSpacing(6);
    const auto step = [this, panel, steps](const QString& icon, const QString& what, int by) {
        auto* button = new QToolButton(panel);
        button->setIcon(Theme::icon(icon));
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolTip(what);
        button->setAccessibleName(what);
        connect(button, &QToolButton::clicked, this, [this, by] {
            const int count = static_cast<int>(kEqSections);
            select(kSections[static_cast<std::size_t>((static_cast<int>(selected_) + by + count) % count)]);
        });
        steps->addWidget(button);
    };
    step(QStringLiteral("chevron-left"), tr("Previous section"), -1);
    step(QStringLiteral("chevron-right"), tr("Next section"), 1);
    row->addLayout(steps);

    rebuildPanel();
    graph_->setSelected(selected_);

    slots_.fill(Snapshot{.values = values(), .preset = presets_->current(), .modified = false});
    slotButtons_[0]->setChecked(true);

    spectrumTimer_ = new QTimer(this);
    spectrumTimer_->setInterval(kSpectrumMs);
    connect(spectrumTimer_, &QTimer::timeout, this, &EqEditorWindow::updateSpectrum);
    resize(900, 520);
}

EqEditorWindow::~EqEditorWindow() { instance_->setScopeEnabled(false); }

void EqEditorWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // The plugin copies its output for the spectrum only while it is looked at.
    instance_->setScopeEnabled(true);
    spectrumTimer_->start();
}

void EqEditorWindow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    spectrumTimer_->stop();
    instance_->setScopeEnabled(false);
    analyzer_.clear();
    lastScope_.clear();
}

void EqEditorWindow::updateSpectrum()
{
    const std::size_t count = instance_->readScope(scope_);
    const std::span<const float> newest(scope_.data(), count);
    // The same frames as last time: no audio is running. The picture then
    // falls away instead of freezing on the last sound.
    const bool stale
        = count == lastScope_.size() && std::equal(newest.begin(), newest.end(), lastScope_.begin());
    if (stale && !analyzer_.hasSignal())
        return; // already empty: nothing to draw again
    analyzer_.analyse(stale ? std::span<const float>() : newest);
    lastScope_.assign(newest.begin(), newest.end());
    if (!motion_->isAnimating())
        graph_->update(); // otherwise the next frame draws it
}

EqEditorWindow::Values EqEditorWindow::values() const
{
    Values all{};
    for (std::uint32_t id = 0; id < all.size(); ++id)
        all[id] = response_.value(id);
    return all;
}

void EqEditorWindow::apply(const Values& all)
{
    for (std::uint32_t id = 0; id < all.size(); ++id)
        change(id, all[id]);
}

void EqEditorWindow::chooseSlot(int slot)
{
    if (slot != slot_) {
        slots_[static_cast<std::size_t>(slot_)]
            = Snapshot{.values = values(), .preset = presets_->current(), .modified = presets_->isModified()};
        slot_ = slot;
        const Snapshot heard = slots_[static_cast<std::size_t>(slot)];
        apply(heard.values);
        presets_->restore(heard.preset, heard.modified);
    }
    for (int i = 0; i < 2; ++i)
        slotButtons_[static_cast<std::size_t>(i)]->setChecked(i == slot_);
}

void EqEditorWindow::setBypassed(bool bypassed)
{
    power_->setChecked(!bypassed);
    power_->setToolTip(
        bypassed ? tr("Effect bypassed (click to turn on)") : tr("Effect on (click to bypass)"));
}

void EqEditorWindow::select(EqSection section)
{
    if (section == selected_)
        return;
    selected_ = section;
    graph_->setSelected(section);
    rebuildPanel();
}

void EqEditorWindow::change(std::uint32_t id, double value)
{
    const double before = response_.value(id);
    response_.set(id, value);
    const double now = response_.value(id);
    if (now == before)
        return;
    // The built-in takes the value at once and never refuses.
    (void)instance_->setParameter(id, now);
    graph_->refresh();
    syncPanel();
    presets_->markModified();
    emit edited();
}

void EqEditorWindow::rebuildPanel()
{
    // The knobs are made for the section: its ranges and its defaults.
    qDeleteAll(knobs_->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly));
    frequency_ = gain_ = q_ = nullptr;
    frequencyText_ = gainText_ = qText_ = nullptr;

    const EqSectionParameters ids = eqSectionParameters(selected_);
    const QColor colour = sectionColor(selected_);
    const auto add = [this, colour](Knob* knob, QLabel*& reading) {
        auto* cell = new QWidget(knobs_);
        auto* stack = new QVBoxLayout(cell);
        stack->setContentsMargins(0, 0, 0, 0);
        stack->setSpacing(2);
        knob->setParent(cell);
        knob->setDialSize(kKnobSize);
        knob->setArcColor(colour);
        stack->addWidget(knob, 0, Qt::AlignHCenter);
        reading = new QLabel(cell);
        reading->setObjectName(QStringLiteral("parameterValue"));
        reading->setAlignment(Qt::AlignHCenter);
        reading->setMinimumWidth(76); // the longest reading: no jitter while turning
        stack->addWidget(reading);
        knobs_->layout()->addWidget(cell);
        cell->show(); // made after the window was shown: not shown with it
    };
    // Frequency and Q turn in octaves: the knob goes 0 .. 1 and the value follows.
    const auto octaves = [this](std::uint32_t id, const QString& caption) {
        const vloom::dsp::ParameterSpec& parameter = eqParameter(id);
        auto* knob = new Knob(caption, 0.0, 1.0, toPosition(parameter, parameter.defaultValue));
        knob->setValueText([&parameter](double position) {
            return parameterValueText(parameter.unit, fromPosition(parameter, position));
        });
        connect(knob, &Knob::valueChanged, this,
            [this, id, &parameter](double position) { change(id, fromPosition(parameter, position)); });
        return knob;
    };

    frequency_ = octaves(ids.frequency, tr("Frequency"));
    add(frequency_, frequencyText_);
    if (ids.gain) {
        const vloom::dsp::ParameterSpec& parameter = eqParameter(*ids.gain);
        gain_ = new Knob(tr("Gain"), parameter.minValue, parameter.maxValue, parameter.defaultValue);
        gain_->setValueText([&parameter](double value) { return parameterValueText(parameter.unit, value); });
        connect(
            gain_, &Knob::valueChanged, this, [this, id = *ids.gain](double value) { change(id, value); });
        add(gain_, gainText_);
    }
    if (ids.q) {
        q_ = octaves(*ids.q, tr("Q"));
        add(q_, qText_);
    }

    static_cast<Badge*>(badge_)->show(selected_);
    name_->setText(sectionName(selected_));
    shape_->setText(sectionShape(selected_));
    on_->setVisible(ids.on.has_value());
    syncPanel();
    placePanel();
}

bool EqEditorWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == panel_ && event->type() == QEvent::LayoutRequest)
        placePanel();
    return QWidget::eventFilter(watched, event);
}

void EqEditorWindow::syncPanel()
{
    const EqSectionParameters ids = eqSectionParameters(selected_);
    const auto show = [this](std::uint32_t id, Knob* knob, QLabel* reading, bool octaves) {
        const vloom::dsp::ParameterSpec& parameter = eqParameter(id);
        const double value = response_.value(id);
        knob->setValue(octaves ? toPosition(parameter, value) : value);
        reading->setText(parameterValueText(parameter.unit, value));
    };
    show(ids.frequency, frequency_, frequencyText_, true);
    if (ids.gain)
        show(*ids.gain, gain_, gainText_, false);
    if (ids.q)
        show(*ids.q, q_, qText_, true);
    if (ids.on)
        on_->setChecked(response_.value(*ids.on) >= 0.5);
}

void EqEditorWindow::placePanel()
{
    if (panel_ == nullptr)
        return;
    const QSize size = panel_->sizeHint();
    panel_->setGeometry((graph_->width() - size.width()) / 2, graph_->height() - size.height() - kPanelMargin,
        size.width(), size.height());
}

} // namespace vloomdaw
