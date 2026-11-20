// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/GateEditor.hpp"

#include "core/DynamicsMeter.hpp"
#include "core/ParameterScale.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"
#include "ui/plugins/builtin/LevelHistoryView.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <vloom/dsp/effects/Gate.hpp>

#include <QEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <utility>

namespace vloomdaw {

namespace {

using Gate = vloom::dsp::Gate;

constexpr double kTopDb = 0.0;
constexpr double kBottomDb = Gate::kMinRangeDb;
/// Turned down less than this, the gate reads as open.
constexpr double kOpenBelowDb = 1.0;

QString dbText(double db) { return QString::fromStdString(formatParameterValue("dB", db)); }

/// LevelHistoryView's padding above the top level and below the bottom one.
constexpr double kViewPadding = 20.0;
/// The hold bar at its longest, px.
constexpr double kHoldBarWidth = 160.0;

} // namespace

/// Over the level display (animated only), the gate breathing: open, the
/// threshold glows with the sound; closed, a fog settles over the levels
/// under it. The threshold flashes as the input crosses it, and the hold
/// time runs out as a bar under it once the input falls below.
class GateBreath : public QWidget, public Animated {
public:
    explicit GateBreath(LevelHistoryView* view)
        : QWidget(view)
        , view_(view)
        , fog_(0.25)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        view->installEventFilter(this);
        setGeometry(view->rect());
    }

    void setThreshold(double db)
    {
        thresholdDb_ = db;
        update();
    }
    void setHold(double ms) { holdSeconds_ = std::max(0.0, ms / 1000.0); }

    bool advance(const motion::AudioFeatures& audio, double dt) override
    {
        const double live = audio.liveliness();
        const double reduction = audio.reductionDb();
        const bool open = audio.running() && reduction < kOpenBelowDb;
        open_ = motion::approach(open_, open ? 1.0 : 0.0, dt, open ? 0.03 : 0.15);
        closed_ = (1.0 - open_) * live;
        glow_ = open_ * audio.level() * live;
        time_ += dt;

        // The input (the output with what the gate took off added back).
        const double input = audio.peakDb() + reduction;
        const bool above = std::isfinite(input) && input > thresholdDb_;
        if (above && !above_) {
            view_->pulseMarker(0.5 + 0.5 * normalisedLevelAbove(input));
            holdLeft_ = 0.0;
        } else if (!above && above_ && holdSeconds_ > 0.0) {
            holdLeft_ = holdSeconds_; // the gate stays open this long
        }
        above_ = above;
        holdLeft_ = std::max(0.0, holdLeft_ - dt);

        // The fog: a haze inked under the threshold while closed, rising
        // slowly and fading.
        const QRectF under(0.0, thresholdY(), width(), std::max(1.0, height() - thresholdY()));
        if (fog_.area() != under)
            fog_.setArea(under);
        fog_.advance(motion::decayFactor(dt, 0.6, 20.0), QTransform::fromTranslate(0.0, -10.0 * dt), 0.6);
        if (closed_ > 0.01) {
            const QColor haze = motion_paint::faded(editor_style::muted(), 0.03 * closed_);
            fog_.ink([&](QPainter& p) { p.fillRect(under, haze); });
        }
        update();
        return closed_ > 0.005 || glow_ > 0.005 || holdLeft_ > 0.0 || fog_.isVisible();
    }

    void rest() override
    {
        fog_.clear();
        open_ = 0.0;
        closed_ = 0.0;
        glow_ = 0.0;
        holdLeft_ = 0.0;
        above_ = false;
        update();
    }

    void animationSwitched(bool /*on*/) override { rest(); }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == parentWidget() && event->type() == QEvent::Resize)
            setGeometry(parentWidget()->rect());
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent* /*event*/) override
    {
        if (!animationOn())
            return;
        const AnimationPaintTimer timer;
        QPainter p(this);
        const QColor accent = editor_style::accent();
        const double y = std::round(thresholdY()) + 0.5;
        if (closed_ > 0.01) {
            // Closed: what is under the threshold dims and fogs over.
            p.fillRect(QRectF(0.0, y, width(), height() - y),
                motion_paint::faded(editor_style::background(), 0.5 * closed_));
        }
        fog_.paint(p, 1.0, 0.9, QPointF(14.0 * time_, 5.0 * time_));
        if (glow_ > 0.01) {
            // Open: the threshold the sound stands above glows with it.
            motion_paint::glowLine(p, QLineF(0.0, y, width(), y), accent, 0.35 * glow_, 10.0);
        }
        if (holdLeft_ > 0.0 && holdSeconds_ > 0.0) {
            // The hold running out: a bar under the line that shrinks to the right.
            const double share = holdLeft_ / holdSeconds_;
            const double length = kHoldBarWidth * share;
            const QRectF bar(width() - length, y + 5.0, length, 3.0);
            p.fillRect(bar, motion_paint::faded(accent, 0.25 + 0.5 * share));
            motion_paint::glowLine(
                p, QLineF(bar.left(), bar.center().y(), bar.right(), bar.center().y()), accent, share, 5.0);
        }
    }

private:
    [[nodiscard]] double thresholdY() const
    {
        const double perDb = std::max(1.0, height() - 2.0 * kViewPadding) / (kTopDb - kBottomDb);
        return kViewPadding + (kTopDb - std::clamp(thresholdDb_, kBottomDb, kTopDb)) * perDb;
    }

    /// How far above the threshold `db` is, 0 .. 1 (1 at 12 dB or more).
    [[nodiscard]] double normalisedLevelAbove(double db) const
    {
        return std::clamp((db - thresholdDb_) / 12.0, 0.0, 1.0);
    }

    LevelHistoryView* view_;
    motion_paint::TrailBuffer fog_;
    double thresholdDb_ = -40.0;
    double holdSeconds_ = 0.0;
    double open_ = 0.0; ///< 0 closed .. 1 open, eased
    double closed_ = 0.0; ///< how much the closed look shows (fades with the sound)
    double glow_ = 0.0;
    double holdLeft_ = 0.0; ///< s
    double time_ = 0.0;
    bool above_ = false;
};

GateEditor::GateEditor(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate, QWidget* parent)
    : BuiltinEditorWindow(std::move(instance), title, sampleRate,
          {
              {tr("Drum Tight"),
                  {{Gate::kThreshold, -30.0}, {Gate::kAttack, 0.2}, {Gate::kHold, 30.0},
                      {Gate::kRelease, 80.0}}},
              {tr("Vocal Clean"),
                  {{Gate::kThreshold, -45.0}, {Gate::kHysteresis, 6.0}, {Gate::kRange, -20.0},
                      {Gate::kAttack, 2.0}, {Gate::kHold, 80.0}, {Gate::kRelease, 200.0}}},
              {tr("Gentle Expander"),
                  {{Gate::kThreshold, -40.0}, {Gate::kRange, -10.0}, {Gate::kAttack, 5.0},
                      {Gate::kRelease, 300.0}}},
              {tr("Kick Only"),
                  {{Gate::kThreshold, -24.0}, {Gate::kAttack, 0.1}, {Gate::kHold, 20.0},
                      {Gate::kRelease, 60.0}, {Gate::kSidechainFilter, 1.0},
                      {Gate::kSidechainFrequency, 60.0}}},
          },
          parent)
{
    levels_ = new LevelHistoryView(LevelHistoryView::Look{
        .inputArea = true, .outputArea = true, .outputLine = true, .outputAccent = true, .reduction = false});
    levels_->setRange(kTopDb, kBottomDb, {-12.0, -24.0, -36.0, -48.0, -60.0, -72.0});
    levels_->setAccessibleName(tr("Levels: the input, what the gate lets through, and the threshold"));
    levels_->onMarkerDragged = [this](double db) { change(Gate::kThreshold, db); };
    levels_->onMarkerReset = [this] {
        if (const vloom::ParameterInfo* threshold = parameter(Gate::kThreshold))
            change(Gate::kThreshold, threshold->defaultValue);
    };
    setDisplay(levels_);
    breath_ = new GateBreath(levels_);
    bind(Gate::kThreshold, [this](double db) {
        levels_->setMarker(db, tr("Threshold %1").arg(dbText(db)));
        breath_->setThreshold(db);
    });
    bind(Gate::kHold, [this](double ms) { breath_->setHold(ms); });
    animate(levels_);
    animate(breath_);

    panel()->add(knob(Gate::kThreshold));
    ArcKnob* range = knob(Gate::kRange);
    // At its bottom the gate closes to silence.
    range->setFormatter(
        [](double db) { return db <= Gate::kMinRangeDb ? QStringLiteral("−inf dB") : dbText(db); });
    panel()->add(range);
    panel()->add(knob(Gate::kAttack));
    panel()->add(knob(Gate::kHold));
    panel()->add(knob(Gate::kRelease));
    panel()->add(knob(Gate::kHysteresis));

    auto* lowCut = toggle(Gate::kSidechainFilter, tr("Detector low cut"), PillToggle::Layout::Inline);
    lowCut->setToolTip(tr("Filter the low end out of what opens the gate"));
    footer()->addLeft(lowCut);
    auto* lowCutFrequency = readout(Gate::kSidechainFrequency);
    lowCutFrequency->setLabel({}); // reads after its switch
    lowCutFrequency->setAccessibleName(tr("Detector low cut frequency"));
    lowCutFrequency->setWidestText(QStringLiteral("2.00 kHz"));
    footer()->addLeft(lowCutFrequency);
    auto* listen = toggle(Gate::kSidechainListen, tr("Listen"), PillToggle::Layout::Inline);
    listen->setToolTip(tr("Hear what opens the gate in place of the output"));
    footer()->addLeft(listen);

    state_ = readout(tr("Gate"));
    state_->setWidestText(tr("Closed"));
    state_->setText(QStringLiteral("–"));
    state_->setToolTip(tr("Open while the gate turns the sound down by less than 1 dB"));
    footer()->addRight(state_);

    startMetering();
}

void GateEditor::meter(const Meter& reading)
{
    double input = kNoLevel;
    if (reading.running && value(Gate::kSidechainListen) < 0.5)
        input
            = isBypassed() ? reading.outputDb : inputFromOutputDb(reading.outputDb, reading.reductionDb, 0.0);
    levels_->push(input, reading.outputDb, kNoLevel);
    if (!std::isfinite(reading.reductionDb))
        state_->setText(QStringLiteral("–"));
    else
        state_->setText(reading.reductionDb < kOpenBelowDb ? tr("Open") : tr("Closed"));
}

} // namespace vloomdaw
