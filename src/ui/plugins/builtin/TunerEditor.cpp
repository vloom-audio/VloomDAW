// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/TunerEditor.hpp"

#include "core/ParameterScale.hpp"
#include "ui/plugins/builtin/AnimationPaint.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"
#include "ui/plugins/builtin/PillToggle.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <vloom/dsp/effects/Tuner.hpp>
#include <vloom/engine/plugin/PluginInstance.hpp>

#include <QFontMetricsF>
#include <QPainter>
#include <QPixmap>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace vloomdaw {

namespace {

namespace style = editor_style;
using Tuner = vloom::dsp::Tuner;

constexpr int kPollMs = 33; ///< about 30 readings a second
constexpr double kMeterWidth = 600.0; ///< -50 .. +50 cents
constexpr double kNoReadingDb = -100.0; ///< a level at or below: no input
const QString kNone = QStringLiteral("—");
/// The level readout unchanged this long: no audio runs through the tuner.
constexpr double kStaleSeconds = 0.25;

QString dbText(double db) { return QString::fromStdString(formatParameterValue("dB", db)); }

/// The index of readout `id` in `list`, or -1.
int indexOf(const std::vector<vloom::ReadoutInfo>& list, const char* id)
{
    for (std::size_t i = 0; i < list.size(); ++i)
        if (list[i].id == id)
            return static_cast<int>(i);
    return -1;
}

} // namespace

/// The tuner's picture: the note, its frequency, the cents meter.
///
/// Animated (docs/builtin-editors.md, "Animation"), everything moves with
/// the tuner's own readings: the needle eases between them and leaves a
/// short glowing trail, the in-tune zone blooms while the pitch holds within
/// kInTuneCents, the note glows with the input level and pulses when the
/// detected note changes. No pitch: it all fades out and stands still.
class TunerDisplay : public QWidget, public Animated {
public:
    using QWidget::QWidget;

    /// A pitch: `note` with `hz`, `cents` off it (the readout) and the
    /// needle at `needle` cents.
    void showPitch(const TunerNote& note, double hz, double cents, double needle)
    {
        if (note.midi != note_.midi) {
            // Another note: the needle jumps rather than sweeps, and the note pulses.
            shownNeedle_ = needle;
            if (animationOn())
                notePulse_ = 1.0;
        }
        note_ = note;
        hz_ = hz;
        cents_ = cents;
        needle_ = needle;
        if (!ticking_)
            update(); // else the next frame repaints it
    }
    /// No pitch: "—".
    void showNone()
    {
        if (note_.midi < 0)
            return;
        note_ = TunerNote{};
        update();
    }
    /// What the tuner hears (dB full scale): the note's glow follows it.
    void setLevel(double db) { levelDb_ = db; }

    bool advance(const motion::AudioFeatures& /*audio*/, double dt) override
    {
        ticking_ = true;
        const bool pitch = note_.midi >= 0;
        const double level = motion::normalisedLevel(levelDb_);
        if (pitch)
            shownNeedle_ = motion::approach(shownNeedle_, needle_, dt, kNeedleEaseSeconds);

        // The note glows with the level while a pitch is found.
        const bool live = pitch && levelDb_ > kNoReadingDb;
        glow_ = motion::approach(glow_, live ? 0.3 + 0.7 * level : 0.0, dt, live ? 0.08 : 0.3);
        notePulse_ *= std::exp(-dt / kPulseSeconds);
        // The zone blooms once the pitch has held in tune a moment.
        holding_ = live && inTune(cents_) ? holding_ + dt : 0.0;
        const double bloomTarget = holding_ >= kHoldSeconds ? 0.45 + 0.55 * level : 0.0;
        bloom_ = motion::approach(bloom_, bloomTarget, dt, bloomTarget > bloom_ ? 0.15 : 0.25);

        // The trail: the needle inked each frame, fading in a fraction of a second.
        const QRectF strip = trailArea();
        trail_.setArea(strip);
        trail_.advance(motion::decayFactor(dt, kTrailSeconds, 40.0), QTransform(), 0.6);
        if (live) {
            const double x = needleX(shownNeedle_);
            QColor ink = needleColour();
            ink.setAlphaF(static_cast<float>(0.16 + 0.2 * glow_));
            trail_.ink([&](QPainter& p) {
                p.fillRect(QRectF(x - 2.0, strip.top() + 4.0, 4.0, strip.height() - 8.0), ink);
            });
        }

        const bool moving
            = live || glow_ > kRest || bloom_ > kRest || notePulse_ > kRest || trail_.isVisible();
        if (!moving) {
            glow_ = bloom_ = notePulse_ = 0.0;
        }
        // The note band holds the text too: repainted with the frames.
        update(noteBand());
        update(meterBand());
        update(centsBand());
        return moving;
    }

    void rest() override
    {
        ticking_ = false;
        glow_ = bloom_ = notePulse_ = holding_ = 0.0;
        shownNeedle_ = needle_;
        trail_.clear();
        update();
    }

    void animationSwitched(bool /*on*/) override { rest(); }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        if (!animationOn()) {
            paintStatic();
            return;
        }
        const AnimationPaintTimer timer;
        QPainter p(this);
        // The background and the meter's scale (with the zone at rest) never move.
        scale_.draw(p, *this, 0, [this](QPainter& layer) {
            layer.setRenderHint(QPainter::Antialiasing);
            layer.fillRect(rect(), style::background());
            paintScale(layer, meterArea(), false);
        });
        const bool pitch = note_.midi >= 0;
        const QColor colour = needleColour();

        // The note's glow, behind it: the level, and a pulse at a new note.
        const double noteGlow = std::min(1.0, 0.22 * glow_ + 0.4 * notePulse_);
        if (noteGlow > kRest)
            motion_paint::glowDot(p, QPointF(width() / 2.0, 66.0), 84.0, colour, noteGlow);

        // The in-tune zone's bloom.
        if (bloom_ > kRest) {
            const QRectF zone = zoneRect();
            motion_paint::glowDot(p, zone.center(), 52.0, style::level(), 0.5 * bloom_);
            motion_paint::glowLine(p, QLineF(zone.left(), lineY(), zone.right(), lineY()), style::level(),
                0.7 * bloom_, zone.height() / 2.0);
        }

        trail_.paint(p, 0.9);
        // The text changes with the readings (30 a second at most), not each frame.
        std::uint64_t key = motion_paint::keyWith(0, static_cast<double>(note_.midi));
        key = motion_paint::keyWith(key, std::round(hz_ * 10.0));
        key = motion_paint::keyWith(key, static_cast<double>(std::lround(cents_)));
        key = motion_paint::keyWith(key, inTune(cents_) ? 1.0 : 0.0);
        text_.draw(p, *this, key, [this, pitch](QPainter& layer) {
            layer.setRenderHint(QPainter::Antialiasing);
            paintText(layer, pitch);
        });

        if (pitch) {
            // The needle: a plain bar (no wide strokes), a glow and a dot sprite.
            const QRectF area = meterArea();
            const double x = needleX(shownNeedle_);
            motion_paint::glowLine(
                p, QLineF(x, area.top() + 6.0, x, area.top() + 62.0), colour, 0.35 + 0.4 * glow_, 7.0);
            p.fillRect(QRectF(x - 1.5, area.top() + 6.0, 3.0, 56.0), colour);
            motion_paint::glowDot(p, QPointF(x, lineY()), 16.0, colour, 0.3 + 0.5 * glow_);
            p.drawPixmap(QPointF(x - kDotRadius - 1.0, lineY() - kDotRadius - 1.0), dot(colour));
        }
    }

private:
    static constexpr double kNeedleEaseSeconds = 0.03; ///< between two readings (30 a second)
    static constexpr double kPulseSeconds = 0.25; ///< a new note's pulse fades in about this
    static constexpr double kHoldSeconds = 0.25; ///< in tune this long: the zone blooms
    static constexpr double kTrailSeconds = 0.35; ///< the trail fades 40 dB in this
    static constexpr double kDotRadius = 7.0;
    static constexpr double kRest = 0.003; ///< less is still

    [[nodiscard]] QRectF meterArea() const
    {
        return {width() / 2.0 - kMeterWidth / 2.0, 140.0, kMeterWidth, 84.0};
    }
    [[nodiscard]] double lineY() const { return meterArea().top() + 34.0; }
    [[nodiscard]] double needleX(double cents) const
    {
        const QRectF area = meterArea();
        return area.center().x() + std::clamp(cents, -50.0, 50.0) * area.width() / 100.0;
    }
    [[nodiscard]] QRectF zoneRect() const
    {
        const QRectF area = meterArea();
        const double perCent = area.width() / 100.0;
        return {area.center().x() - kInTuneCents * perCent, area.top() + 14.0, 2.0 * kInTuneCents * perCent,
            40.0};
    }
    [[nodiscard]] QRectF trailArea() const
    {
        const QRectF area = meterArea();
        return {area.left() - 12.0, area.top(), area.width() + 24.0, 68.0};
    }
    /// What advance() repaints: the note and its glow, the meter and its glows.
    [[nodiscard]] QRect noteBand() const { return {0, 0, width(), 140}; }
    [[nodiscard]] QRect meterBand() const
    {
        const QRectF area = meterArea();
        return QRectF(area.left() - 60.0, area.top() - 20.0, area.width() + 120.0, 104.0).toAlignedRect();
    }
    [[nodiscard]] QRect centsBand() const { return {0, 226, width(), 26}; }
    [[nodiscard]] QColor needleColour() const { return inTune(cents_) ? style::level() : style::accent(); }

    /// The needle's dot, drawn once for its colour.
    const QPixmap& dot(const QColor& colour)
    {
        const qreal ratio = devicePixelRatioF();
        if (dotColour_ != colour || dotRatio_ != ratio || dot_.isNull()) {
            const double side = 2.0 * kDotRadius + 2.0;
            dot_ = QPixmap(QSizeF(side * ratio, side * ratio).toSize());
            dot_.setDevicePixelRatio(ratio);
            dot_.fill(Qt::transparent);
            QPainter p(&dot_);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(colour);
            p.drawEllipse(QPointF(side / 2.0, side / 2.0), kDotRadius, kDotRadius);
            dotColour_ = colour;
            dotRatio_ = ratio;
        }
        return dot_;
    }

    /// The picture with the animation off (and as the design has it).
    void paintStatic()
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), style::background());
        const bool pitch = note_.midi >= 0;
        paintText(p, pitch);
        paintScale(p, meterArea(), pitch && inTune(cents_));
        if (!pitch)
            return;
        const double x = needleX(needle_);
        const QRectF area = meterArea();
        const QColor colour = needleColour();
        p.setPen(QPen(colour, 3.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(x, area.top() + 6.0), QPointF(x, area.top() + 62.0));
        p.setPen(Qt::NoPen);
        p.setBrush(colour);
        p.drawEllipse(QPointF(x, lineY()), kDotRadius, kDotRadius);
    }

    /// The note with its octave, the frequency and the cents off.
    void paintText(QPainter& p, bool pitch) const
    {
        const double centre = width() / 2.0;
        // The note, 96 px, with its octave beside it.
        // No pitch: a dash, lighter, so it does not read as a bar.
        const QFont noteFont = pitch ? style::font(font(), 96, QFont::Bold) : style::font(font(), 48);
        const QFont octaveFont = style::font(font(), 32, QFont::DemiBold);
        const QString name = pitch ? QString::fromStdString(note_.name) : kNone;
        const QString octave = pitch ? QString::number(note_.octave) : QString();
        const double nameWidth = QFontMetricsF(noteFont).horizontalAdvance(name);
        const double octaveWidth
            = octave.isEmpty() ? 0.0 : 4.0 + QFontMetricsF(octaveFont).horizontalAdvance(octave);
        const double baseline = 100.0;
        const double left = centre - (nameWidth + octaveWidth) / 2.0;
        p.setFont(noteFont);
        p.setPen(pitch ? style::text() : style::muted());
        p.drawText(QPointF(left, baseline), name);
        if (!octave.isEmpty()) {
            p.setFont(octaveFont);
            p.setPen(style::muted());
            p.drawText(QPointF(left + nameWidth + 4.0, baseline), octave);
        }

        // The frequency.
        p.setFont(style::font(font(), 13, QFont::Normal, true));
        p.setPen(style::muted());
        const QString hz = pitch ? QStringLiteral("%1 Hz").arg(hz_, 0, 'f', 1) : QString();
        p.drawText(QRectF(0.0, 112.0, width(), 20.0), Qt::AlignCenter, hz);

        // The cents off, and which way.
        QString amount = kNone;
        QString way;
        if (pitch) {
            const long rounded = std::lround(cents_);
            const QString unit = std::abs(rounded) == 1 ? tr("cent") : tr("cents");
            amount = rounded == 0 ? QStringLiteral("0 %1").arg(unit)
                : rounded > 0     ? QStringLiteral("+%1 %2").arg(rounded).arg(unit)
                                  : QStringLiteral("−%1 %2").arg(-rounded).arg(unit);
            way = inTune(cents_) ? tr("in tune") : rounded > 0 ? tr("sharp") : tr("flat");
        }
        const QFont amountFont = style::font(font(), 14, QFont::DemiBold, true);
        const QFont wayFont = style::font(font(), 14);
        const double amountWidth = QFontMetricsF(amountFont).horizontalAdvance(amount);
        const double wayWidth = way.isEmpty() ? 0.0 : 5.0 + QFontMetricsF(wayFont).horizontalAdvance(way);
        const double textLeft = centre - (amountWidth + wayWidth) / 2.0;
        p.setFont(amountFont);
        p.setPen(pitch ? style::text() : style::muted());
        p.drawText(QPointF(textLeft, 244.0), amount);
        if (!way.isEmpty()) {
            p.setFont(wayFont);
            p.setPen(inTune(cents_) ? style::level() : style::muted());
            p.drawText(QPointF(textLeft + amountWidth + 5.0, 244.0), way);
        }
    }

    /// The ±50 cent scale in `area` (the design's 640 x 84 box, less its
    /// margins): the in-tune zone (brighter if `tuned`), the line, the ticks
    /// and their labels.
    void paintScale(QPainter& p, const QRectF& area, bool tuned) const
    {
        const double perCent = area.width() / 100.0;
        const double zero = area.center().x();
        const double y = area.top() + 34.0;

        QColor zone = style::level();
        zone.setAlphaF(tuned ? 0.32f : 0.18f);
        p.setPen(Qt::NoPen);
        p.setBrush(zone);
        p.drawRoundedRect(zoneRect(), 4.0, 4.0);

        p.setPen(QPen(style::line(), 2.0));
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        p.setPen(QPen(style::muted(), 1.0));
        for (int tick = 0; tick <= 10; ++tick) {
            const double x = area.left() + static_cast<double>(tick) * 10.0 * perCent;
            const bool major = tick % 5 == 0;
            p.drawLine(QPointF(x, area.top() + (major ? 18.0 : 26.0)),
                QPointF(x, area.top() + (major ? 50.0 : 42.0)));
        }
        p.setFont(style::font(font(), 11));
        for (const auto& [cents, label] : {std::pair{-50.0, QStringLiteral("−50")},
                 std::pair{0.0, QStringLiteral("0")}, std::pair{50.0, QStringLiteral("+50")}}) {
            const double x = zero + cents * perCent;
            p.drawText(QRectF(x - 24.0, area.top() + 64.0, 48.0, 16.0), Qt::AlignCenter, label);
        }
    }

    TunerNote note_;
    double hz_ = 0.0;
    double cents_ = 0.0;
    double needle_ = 0.0;
    double levelDb_ = kNoReadingDb;
    bool ticking_ = false; ///< frames come (and repaint): showPitch() need not

    // The motion (animated only).
    double shownNeedle_ = 0.0; ///< the needle drawn: eased towards needle_
    double glow_ = 0.0; ///< the note's glow, 0 .. 1
    double notePulse_ = 0.0; ///< 1 at a new note, fading
    double holding_ = 0.0; ///< s in tune
    double bloom_ = 0.0; ///< the in-tune zone's bloom, 0 .. 1
    motion_paint::TrailBuffer trail_{0.5};
    motion_paint::LayerCache scale_;
    motion_paint::LayerCache text_;
    QPixmap dot_;
    QColor dotColour_;
    qreal dotRatio_ = 0.0;
};

TunerEditor::TunerEditor(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate, QWidget* parent)
    : BuiltinEditorWindow(std::move(instance), title, sampleRate,
          {
              {tr("Baroque (415 Hz)"), {{Tuner::kReference, 415.0}}},
              {tr("Orchestra (442 Hz)"), {{Tuner::kReference, 442.0}}},
              {tr("Through (no mute)"), {{Tuner::kMute, 0.0}}},
          },
          parent)
{
    resize(900, 440);
    display_ = new TunerDisplay(nullptr);
    display_->setAccessibleName(tr("The note, its frequency and how many cents it is off"));
    setDisplay(display_);
    animate(display_);

    const std::vector<vloom::ReadoutInfo> readouts = this->instance().processor().readouts();
    frequencyIndex_ = indexOf(readouts, "frequency_hz");
    noteIndex_ = indexOf(readouts, "note");
    centsIndex_ = indexOf(readouts, "cents");
    pitchIndex_ = indexOf(readouts, "has_pitch");
    levelIndex_ = indexOf(readouts, "level_db");

    auto* reference = knob(Tuner::kReference, tr("Reference"), ParameterScale::Linear);
    reference->setFormatter(
        [](double hz) { return QStringLiteral("%1 Hz").arg(hz, 0, 'f', std::fmod(hz, 1.0) == 0.0 ? 0 : 1); });
    reference->setToolTip(tr("The frequency of A4"));
    panel()->add(reference);
    panel()->addSpacing(8);
    auto* mute = toggle(Tuner::kMute, tr("Mute output"));
    mute->setToolTip(tr("Silence the track while tuning"));
    panel()->add(mute);

    auto* input = readout(Tuner::kInput, tr("Input"));
    input->setWidestText(QStringLiteral("Left + right"));
    input->setToolTip(tr("Which side the tuner listens to (click to change)"));
    footer()->addLeft(input);
    level_ = readout(tr("Level"));
    level_->setWidestText(dbText(-60.0));
    level_->setText(QStringLiteral("–"));
    footer()->addRight(level_);

    timer_ = new QTimer(this);
    timer_->setInterval(kPollMs);
    connect(timer_, &QTimer::timeout, this, &TunerEditor::poll);
}

void TunerEditor::showEvent(QShowEvent* event)
{
    BuiltinEditorWindow::showEvent(event);
    sincePoll_.start();
    timer_->start();
    poll();
}

void TunerEditor::hideEvent(QHideEvent* event)
{
    BuiltinEditorWindow::hideEvent(event);
    timer_->stop();
}

std::optional<double> TunerEditor::inputPeakDb() const
{
    // What the tuner hears, so a muted tuner (the default) still animates.
    if (isBypassed() || levelIndex_ < 0 || unchanged_ >= kStaleSeconds)
        return std::nullopt;
    return static_cast<double>(readoutAt(levelIndex_));
}

float TunerEditor::readoutAt(int index) const
{
    return index >= 0 ? instance().processor().readoutValue(index) : 0.0f;
}

void TunerEditor::poll()
{
    const double elapsed = sincePoll_.isValid() ? static_cast<double>(sincePoll_.restart()) / 1000.0 : 0.0;
    // Bypassed, the tuner does not run: what it read last is stale.
    const bool live = !isBypassed();
    const double level
        = live && levelIndex_ >= 0 ? static_cast<double>(readoutAt(levelIndex_)) : kNoReadingDb;
    level_->setText(level > kNoReadingDb ? dbText(level) : QStringLiteral("–"));
    // The readouts keep their last values when no audio runs (the transport
    // stopped): a level that has not moved at all is not heard any more.
    const auto reading = static_cast<float>(level);
    unchanged_ = reading == lastLevel_ ? unchanged_ + elapsed : 0.0;
    lastLevel_ = reading;
    display_->setLevel(unchanged_ >= kStaleSeconds ? kNoReadingDb : level);

    const bool pitch = live && pitchIndex_ >= 0 && readoutAt(pitchIndex_) >= 0.5f;
    const double cents = static_cast<double>(readoutAt(centsIndex_));
    const TunerNote note = pitch ? tunerNote(static_cast<double>(readoutAt(noteIndex_)), cents) : TunerNote{};
    if (note.midi < 0) {
        needle_.reset();
        display_->showNone();
        return;
    }
    const double needle = needle_.step(cents, note.midi, elapsed);
    display_->showPitch(note, static_cast<double>(readoutAt(frequencyIndex_)), cents, needle);
}

} // namespace vloomdaw
