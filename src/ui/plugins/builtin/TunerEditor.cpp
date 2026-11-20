// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/TunerEditor.hpp"

#include "core/ParameterScale.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"
#include "ui/plugins/builtin/PillToggle.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <vloom/dsp/effects/Tuner.hpp>
#include <vloom/engine/plugin/PluginInstance.hpp>

#include <QFontMetricsF>
#include <QPainter>
#include <QTimer>

#include <algorithm>
#include <cmath>
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
class TunerDisplay : public QWidget {
public:
    using QWidget::QWidget;

    /// A pitch: `note` with `hz`, `cents` off it (the readout) and the
    /// needle at `needle` cents.
    void showPitch(const TunerNote& note, double hz, double cents, double needle)
    {
        note_ = note;
        hz_ = hz;
        cents_ = cents;
        needle_ = needle;
        update();
    }
    /// No pitch: "—".
    void showNone()
    {
        if (note_.midi < 0)
            return;
        note_ = TunerNote{};
        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), style::background());
        const double centre = width() / 2.0;
        const bool pitch = note_.midi >= 0;

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

        paintMeter(p, QRectF(centre - kMeterWidth / 2.0, 140.0, kMeterWidth, 84.0), pitch);

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

private:
    /// The ±50 cent meter in `area` (the design's 640 x 84 box, less its margins).
    void paintMeter(QPainter& p, const QRectF& area, bool pitch) const
    {
        const double perCent = area.width() / 100.0;
        const double zero = area.center().x();
        const double lineY = area.top() + 34.0;

        // The in-tune zone.
        QColor zone = style::level();
        zone.setAlphaF(pitch && inTune(cents_) ? 0.32f : 0.18f);
        p.setPen(Qt::NoPen);
        p.setBrush(zone);
        p.drawRoundedRect(
            QRectF(zero - kInTuneCents * perCent, area.top() + 14.0, 2.0 * kInTuneCents * perCent, 40.0), 4.0,
            4.0);

        p.setPen(QPen(style::line(), 2.0));
        p.drawLine(QPointF(area.left(), lineY), QPointF(area.right(), lineY));
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

        if (!pitch)
            return;
        const double x = zero + std::clamp(needle_, -50.0, 50.0) * perCent;
        const QColor colour = inTune(cents_) ? style::level() : style::accent();
        p.setPen(QPen(colour, 3.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(x, area.top() + 6.0), QPointF(x, area.top() + 62.0));
        p.setPen(Qt::NoPen);
        p.setBrush(colour);
        p.drawEllipse(QPointF(x, lineY), 7.0, 7.0);
    }

    TunerNote note_;
    double hz_ = 0.0;
    double cents_ = 0.0;
    double needle_ = 0.0;
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
