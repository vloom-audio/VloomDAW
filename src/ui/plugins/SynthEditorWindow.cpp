// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/SynthEditorWindow.hpp"

#include "ui/Theme.hpp"
#include "ui/plugins/ParameterText.hpp"
#include "ui/plugins/PresetBar.hpp"
#include "ui/plugins/builtin/ParameterControl.hpp"
#include "ui/plugins/builtin/Readout.hpp"
#include "ui/widgets/Knob/Knob.hpp"
#include "ui/widgets/ToggleSwitch/ToggleSwitch.hpp"

#include <vloom/dsp/Wavetable.hpp>
#include <vloom/engine/plugin/PluginInstance.hpp>

#include <QButtonGroup>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStackedWidget>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string_view>
#include <utility>
#include <vector>

namespace vloomdaw {

namespace {

using Synth = vloom::dsp::Synth;
using Wavetable = vloom::dsp::Wavetable;

constexpr int kPictureHeight = 96;
constexpr std::size_t kHueA = 1; ///< Theme::patternColor: oscillator A
constexpr std::size_t kHueB = 6; ///< and B
constexpr int kWindowWidth = 1040;
constexpr int kDial = 30; ///< the knobs of the cards
constexpr int kLowestKey = 36; ///< the keyboard: C2 ..
constexpr int kOctaves = 5; ///< .. to C7

const vloom::dsp::ParameterSpec& spec(std::uint32_t id) { return Synth::parameters()[id]; }

/// How a value reads next to its knob.
QString valueText(std::uint32_t id, double value)
{
    const vloom::dsp::ParameterSpec& parameter = spec(id);
    const std::string_view unit = parameter.unit;
    if (unit == "%")
        return QStringLiteral("%1 %").arg(std::lround(value));
    if (unit == "ms")
        return value < 1000.0 ? QStringLiteral("%1 ms").arg(std::lround(value))
                              : QStringLiteral("%1 s").arg(value / 1000.0, 0, 'f', 2);
    if (unit == "ct" || unit == "st")
        return QStringLiteral("%1%2 %3")
            .arg(value > 0.0 ? QStringLiteral("+") : QString())
            .arg(std::lround(value))
            .arg(QString::fromUtf8(parameter.unit));
    if (parameter.stepped)
        return QString::number(std::lround(value));
    return parameterValueText(unit, value);
}

/// A sound that comes with the app: the values that differ from the defaults.
struct FactoryPreset {
    const char* name;
    std::vector<std::pair<std::uint32_t, double>> values;
};

const std::vector<FactoryPreset>& factoryPresets()
{
    using S = Synth;
    static const std::vector<FactoryPreset> kPresets{
        {QT_TRANSLATE_NOOP("vloomdaw::SynthEditorWindow", "Default"), {}},
        {QT_TRANSLATE_NOOP("vloomdaw::SynthEditorWindow", "Warm Pad"),
            {{S::kAUnison, 5}, {S::kADetune, 30}, {S::kBOn, 1}, {S::kBTable, 1}, {S::kBPosition, 40},
                {S::kBLevel, 60}, {S::kCutoff, 2500}, {S::kResonance, 15}, {S::kEnv1Attack, 600},
                {S::kEnv1Decay, 1500}, {S::kEnv1Release, 1200}, {S::kLfo1Rate, 0.3}, {S::kLfo1Target, 2},
                {S::kLfo1Amount, 25}}},
        {QT_TRANSLATE_NOOP("vloomdaw::SynthEditorWindow", "Pluck"),
            {{S::kCutoff, 600}, {S::kResonance, 25}, {S::kEnv1Decay, 350}, {S::kEnv1Sustain, 0},
                {S::kEnv1Release, 250}, {S::kEnv2Decay, 250}, {S::kEnv2Target, 1}, {S::kEnv2Amount, 70}}},
        {QT_TRANSLATE_NOOP("vloomdaw::SynthEditorWindow", "Sub Bass"),
            {{S::kAPosition, 0}, {S::kAOctave, -1}, {S::kSubLevel, 70}, {S::kCutoff, 400},
                {S::kEnv1Sustain, 100}, {S::kMono, 1}, {S::kLegato, 1}, {S::kGlide, 60}}},
        {QT_TRANSLATE_NOOP("vloomdaw::SynthEditorWindow", "Unison Lead"),
            {{S::kAUnison, 7}, {S::kADetune, 35}, {S::kBOn, 1}, {S::kBUnison, 7}, {S::kBDetune, 45},
                {S::kBFine, 8}, {S::kALevel, 60}, {S::kBLevel, 60}, {S::kCutoff, 9000},
                {S::kEnv1Release, 350}}},
        {QT_TRANSLATE_NOOP("vloomdaw::SynthEditorWindow", "Wobble Bass"),
            {{S::kATable, 1}, {S::kAOctave, -1}, {S::kSubLevel, 50}, {S::kCutoff, 500}, {S::kResonance, 40},
                {S::kDrive, 30}, {S::kLfo1Rate, 4}, {S::kLfo1Target, 1}, {S::kLfo1Amount, 60},
                {S::kMono, 1}}},
        {QT_TRANSLATE_NOOP("vloomdaw::SynthEditorWindow", "Formant Voices"),
            {{S::kATable, 2}, {S::kAPosition, 30}, {S::kAUnison, 3}, {S::kCutoff, 6000},
                {S::kEnv1Attack, 250}, {S::kEnv1Release, 800}, {S::kLfo2Rate, 0.2}, {S::kLfo2Target, 2},
                {S::kLfo2Amount, 40}}},
    };
    return kPresets;
}

/// A picture of part of the synth: draws itself from the current values.
class Picture : public QWidget {
public:
    using Value = std::function<double(std::uint32_t)>;

    Picture(Value value, QWidget* parent)
        : QWidget(parent)
        , value_(std::move(value))
    {
        setFixedHeight(kPictureHeight);
        setMinimumWidth(120);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const Theme::Colors& c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(c.base);
        p.drawRoundedRect(rect(), 8, 8);
        draw(p, c);
    }
    virtual void draw(QPainter& p, const Theme::Colors& c) = 0;
    [[nodiscard]] double value(std::uint32_t id) const { return value_(id); }

private:
    Value value_;
};

/// An oscillator's table, its frames one behind the other, and the cycle it reads now.
class TablePicture final : public Picture {
public:
    TablePicture(Value value, std::uint32_t onId, std::size_t hue, QWidget* parent)
        : Picture(std::move(value), parent)
        , onId_(onId)
        , hue_(hue)
    {
    }

protected:
    void draw(QPainter& p, const Theme::Colors& c) override
    {
        constexpr int kShown = 12; // of the table's frames
        constexpr int kPoints = 72;
        const auto shape = static_cast<Wavetable::Shape>(std::lround(value(onId_ + 1)));
        const Wavetable& table = Wavetable::builtin(shape);
        const auto position = static_cast<float>(value(onId_ + 2) / 100.0);
        const double slantX = width() * 0.22; // the last frame sits this far right and up
        const double slantY = height() * 0.42;
        const double cycleWidth = width() - slantX - 20.0;
        const double amplitude = height() * 0.19;
        const auto cycle = [&](float at) {
            QPainterPath path;
            const double x0 = 10.0 + slantX * at;
            const double y0 = height() - 12.0 - amplitude - slantY * at;
            for (int i = 0; i <= kPoints; ++i) {
                const float phase = std::min(static_cast<float>(i) / kPoints, 0.9999f);
                const QPointF point(
                    x0 + cycleWidth * i / kPoints, y0 - amplitude * table.sample(at, phase, 0));
                if (i == 0)
                    path.moveTo(point);
                else
                    path.lineTo(point);
            }
            return path;
        };
        QColor back = c.text;
        back.setAlpha(60);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(back, 1));
        for (int frame = kShown - 1; frame >= 0; --frame)
            p.drawPath(cycle(static_cast<float>(frame) / (kShown - 1)));
        QColor now = Theme::patternColor(hue_);
        if (value(onId_) < 0.5)
            now = c.textDim; // switched off
        p.setPen(QPen(now, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(cycle(std::clamp(position, 0.0f, 1.0f)));
    }

private:
    std::uint32_t onId_;
    std::size_t hue_;
};

/// The filter's response; its point is dragged for cutoff and resonance.
class FilterPicture final : public Picture {
public:
    std::function<void(std::uint32_t, double)> onChange;

    FilterPicture(Value value, QWidget* parent)
        : Picture(std::move(value), parent)
    {
        setCursor(Qt::CrossCursor);
    }

protected:
    void draw(QPainter& p, const Theme::Colors& c) override
    {
        const bool filtering = value(Synth::kFilterOn) >= 0.5;
        const QColor colour = filtering ? c.accent : c.textDim;
        QPainterPath curve;
        for (int x = 0; x <= width(); x += 2) {
            const QPointF point(x, yOf(responseDb(hzAt(x))));
            if (x == 0)
                curve.moveTo(point);
            else
                curve.lineTo(point);
        }
        p.setPen(QPen(c.border, 1));
        p.drawLine(QPointF(0, yOf(0.0)), QPointF(width(), yOf(0.0)));
        QPainterPath area = curve;
        area.lineTo(width(), height());
        area.lineTo(0, height());
        area.closeSubpath();
        QColor fill = colour;
        fill.setAlpha(40);
        p.setClipPath(clip());
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawPath(area);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(colour, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(curve);
        const double cutoff = value(Synth::kCutoff);
        p.setPen(QPen(colour, 2));
        p.setBrush(c.base);
        p.drawEllipse(
            QPointF(xOf(cutoff), std::clamp(yOf(responseDb(cutoff)), 6.0, height() - 6.0)), 5.5, 5.5);
    }

    void mousePressEvent(QMouseEvent* event) override { drag(event); }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if ((event->buttons() & Qt::LeftButton) != 0)
            drag(event);
    }

private:
    static constexpr double kTopDb = 24.0;
    static constexpr double kBottomDb = -36.0;

    [[nodiscard]] QPainterPath clip() const
    {
        QPainterPath path;
        path.addRoundedRect(rect(), 8, 8);
        return path;
    }
    [[nodiscard]] double xOf(double hz) const { return width() * std::log(hz / 20.0) / std::log(1000.0); }
    [[nodiscard]] double hzAt(double x) const
    {
        return 20.0 * std::pow(1000.0, std::clamp(x / std::max(1, width()), 0.0, 1.0));
    }
    [[nodiscard]] double yOf(double db) const
    {
        return height() * (kTopDb - std::clamp(db, kBottomDb - 12.0, kTopDb)) / (kTopDb - kBottomDb);
    }
    /// The damping the synth gives its filter for the resonance set (Q 0.5 .. 17).
    [[nodiscard]] double damping() const { return 2.0 * std::pow(0.03, value(Synth::kResonance) / 100.0); }
    /// The gain of the filter at `hz`, in dB: a 12 dB per octave state-variable filter.
    [[nodiscard]] double responseDb(double hz) const
    {
        const double ratio = hz / value(Synth::kCutoff);
        const double k = damping();
        const double bottom = std::hypot(1.0 - ratio * ratio, k * ratio);
        double top = 1.0; // low-pass
        const auto type = static_cast<Synth::FilterType>(std::lround(value(Synth::kFilterType)));
        if (type == Synth::FilterType::BandPass)
            top = ratio;
        else if (type == Synth::FilterType::HighPass)
            top = ratio * ratio;
        const double mix = value(Synth::kFilterMix) / 100.0;
        // Mixed with the unfiltered sound (in phase near enough for a picture).
        return 20.0 * std::log10(std::max(1.0e-6, (1.0 - mix) + mix * top / std::max(bottom, 1.0e-9)));
    }
    void drag(const QMouseEvent* event)
    {
        if (!onChange)
            return;
        onChange(Synth::kCutoff, hzAt(event->position().x()));
        // Up: more resonance. The top of the picture is all of it.
        onChange(Synth::kResonance,
            100.0 * std::clamp(1.0 - event->position().y() / std::max(1, height()), 0.0, 1.0));
    }
};

/// An envelope: attack, hold, decay, the sustain level, release.
class EnvelopePicture final : public Picture {
public:
    EnvelopePicture(Value value, std::uint32_t attackId, QWidget* parent)
        : Picture(std::move(value), parent)
        , attackId_(attackId)
    {
    }

protected:
    void draw(QPainter& p, const Theme::Colors& c) override
    {
        // Each time gets room by its cube root, so a short one still shows.
        const auto room
            = [this](std::uint32_t id) { return 0.04 + 0.2 * std::cbrt(value(id) / spec(id).maxValue); };
        const double left = 10.0;
        const double span = width() - 20.0;
        const double top = 12.0;
        const double bottom = height() - 12.0;
        const double sustain = bottom - (bottom - top) * value(attackId_ + 3) / 100.0;
        double x = left;
        QPainterPath path;
        path.moveTo(x, bottom);
        x += span * room(attackId_);
        path.lineTo(x, top);
        x += span * room(attackId_ + 1) * (value(attackId_ + 1) > 0.0 ? 1.0 : 0.0);
        path.lineTo(x, top);
        const double decayEnd = x + span * room(attackId_ + 2);
        path.quadTo(x + (decayEnd - x) * 0.25, sustain, decayEnd, sustain);
        const double releaseStart = left + span - span * room(attackId_ + 4);
        path.lineTo(std::max(releaseStart, decayEnd), sustain);
        path.quadTo(
            std::max(releaseStart, decayEnd) + (left + span - std::max(releaseStart, decayEnd)) * 0.25,
            bottom, left + span, bottom);
        QColor fill = c.accent;
        fill.setAlpha(40);
        QPainterPath area = path;
        area.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawPath(area);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(c.accent, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
    }

private:
    std::uint32_t attackId_;
};

/// Two cycles of an LFO's shape, as deep as its amount.
class LfoPicture final : public Picture {
public:
    LfoPicture(Value value, std::uint32_t rateId, QWidget* parent)
        : Picture(std::move(value), parent)
        , rateId_(rateId)
    {
    }

protected:
    void draw(QPainter& p, const Theme::Colors& c) override
    {
        const auto shape = static_cast<Synth::LfoShape>(std::lround(value(rateId_ + 1)));
        const bool moves = std::lround(value(rateId_ + 2)) != 0 && value(rateId_ + 3) != 0.0;
        const double middle = height() / 2.0;
        const double depth = (height() / 2.0 - 12.0) * std::max(0.15, std::abs(value(rateId_ + 3)) / 100.0);
        // The Random shape: a level per cycle, the same ones at every redraw.
        static constexpr double kLevels[] = {0.7, -0.4, 0.2, -0.9};
        const auto at = [shape](double phase, int cycle) {
            switch (shape) {
            case Synth::LfoShape::Sine:
                return std::sin(2.0 * std::numbers::pi * phase);
            case Synth::LfoShape::Triangle:
                return phase < 0.25 ? 4.0 * phase : phase < 0.75 ? 2.0 - 4.0 * phase : 4.0 * phase - 4.0;
            case Synth::LfoShape::Saw:
                return 1.0 - 2.0 * phase;
            case Synth::LfoShape::Square:
                return phase < 0.5 ? 1.0 : -1.0;
            case Synth::LfoShape::Random:
                break;
            }
            return kLevels[cycle % 4];
        };
        QPainterPath path;
        for (int x = 0; x <= width() - 20; ++x) {
            const double cycles = 2.0 * x / std::max(1, width() - 20);
            const int cycle = std::min(static_cast<int>(cycles), 1);
            const QPointF point(10.0 + x, middle - depth * at(std::min(cycles - cycle, 0.9999), cycle));
            if (x == 0)
                path.moveTo(point);
            else
                path.lineTo(point);
        }
        p.setPen(QPen(c.border, 1));
        p.drawLine(QPointF(10, middle), QPointF(width() - 10, middle));
        p.setPen(QPen(moves ? c.accent : c.textDim, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }

private:
    std::uint32_t rateId_;
};

/// The numbered dot that names an oscillator.
class LetterBadge final : public QWidget {
public:
    LetterBadge(const QString& letter, std::size_t hue, QWidget* parent)
        : QWidget(parent)
        , letter_(letter)
        , hue_(hue)
    {
        setFixedSize(18, 18);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::patternColor(hue_));
        p.drawEllipse(QRectF(rect()).adjusted(1, 1, -1, -1));
        QFont f = font();
        f.setBold(true);
        f.setPixelSize(11);
        p.setFont(f);
        p.setPen(Theme::colors().base);
        p.drawText(rect(), Qt::AlignCenter, letter_);
    }

private:
    QString letter_;
    std::size_t hue_;
};

/// What the sub oscillator or the noise puts out: a sine, or a scribble.
class SourcePicture final : public Picture {
public:
    SourcePicture(Value value, bool noise, std::uint32_t levelId, QWidget* parent)
        : Picture(std::move(value), parent)
        , noise_(noise)
        , levelId_(levelId)
    {
        setFixedHeight(44);
        setMinimumWidth(60);
    }

protected:
    void draw(QPainter& p, const Theme::Colors& c) override
    {
        QPainterPath path;
        const double middle = height() / 2.0;
        for (int x = 0; x <= width() - 12; ++x) {
            const double t = static_cast<double>(x) / std::max(1, width() - 12);
            // The noise: the same scribble at every redraw.
            const double y = noise_ ? std::sin(x * 1.9) * std::sin(x * 0.73 + 1.0) * std::cos(x * 3.1)
                                    : std::sin(2.0 * std::numbers::pi * t);
            const QPointF point(6.0 + x, middle - (height() / 2.0 - 8.0) * y);
            if (x == 0)
                path.moveTo(point);
            else
                path.lineTo(point);
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(
            value(levelId_) > 0.0 ? c.text : c.textDim, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
    }

private:
    bool noise_;
    std::uint32_t levelId_;
};

/// A keyboard to play the synth by hand: press a key (lower down is harder),
/// drag to slide to others.
class Keyboard final : public QWidget {
public:
    std::function<void(int key, float velocity)> onPress;
    std::function<void()> onRelease;

    explicit Keyboard(QWidget* parent)
        : QWidget(parent)
    {
        setFixedHeight(76);
        setCursor(Qt::PointingHandCursor);
    }

    /// Shows `key` as held (-1: none).
    void setHeld(int key)
    {
        held_ = key;
        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const Theme::Colors& c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        for (int white = 0; white < kWhites; ++white) {
            p.setBrush(keyOfWhite(white) == held_ ? c.accent : c.text);
            p.drawRoundedRect(QRectF(white * whiteWidth() + 0.5, 0, whiteWidth() - 1.0, height()), 3, 3);
        }
        for (int white = 0; white < kWhites - 1; ++white) {
            if (!hasBlackAfter(white))
                continue;
            p.setBrush(keyOfWhite(white) + 1 == held_ ? c.accent : c.base);
            p.drawRoundedRect(blackRect(white), 2, 2);
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton)
            play(event->position());
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if ((event->buttons() & Qt::LeftButton) != 0 && keyAt(event->position()) != held_)
            play(event->position()); // slid onto another key
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && onRelease)
            onRelease();
    }

private:
    static constexpr int kWhites = kOctaves * 7 + 1; // the last C too
    static constexpr int kSteps[7] = {0, 2, 4, 5, 7, 9, 11}; // C D E F G A B

    [[nodiscard]] double whiteWidth() const { return static_cast<double>(width()) / kWhites; }
    [[nodiscard]] static int keyOfWhite(int white)
    {
        return kLowestKey + 12 * (white / 7) + kSteps[white % 7];
    }
    [[nodiscard]] static bool hasBlackAfter(int white) { return white % 7 != 2 && white % 7 != 6; }
    [[nodiscard]] QRectF blackRect(int white) const
    {
        return {(white + 1) * whiteWidth() - whiteWidth() * 0.3, 0, whiteWidth() * 0.6, height() * 0.6};
    }
    [[nodiscard]] int keyAt(QPointF point) const
    {
        const int white = std::clamp(static_cast<int>(point.x() / whiteWidth()), 0, kWhites - 1);
        // A black key sits over the edge of two white ones.
        for (int beside : {white - 1, white})
            if (beside >= 0 && beside < kWhites - 1 && hasBlackAfter(beside)
                && blackRect(beside).contains(point))
                return keyOfWhite(beside) + 1;
        return keyOfWhite(white);
    }
    void play(QPointF point)
    {
        if (onPress)
            onPress(keyAt(point),
                static_cast<float>(std::clamp(0.3 + 0.7 * point.y() / std::max(1, height()), 0.3, 1.0)));
    }

    int held_ = -1;
};

/// A card: a frame with a heading row; returns the column to fill.
struct Card {
    QFrame* frame = nullptr;
    QHBoxLayout* heading = nullptr;
    QVBoxLayout* body = nullptr;
};

/// The design's dot before a sound source's name (the sub, the noise, the filter).
class SourceDot final : public QWidget {
public:
    explicit SourceDot(QWidget* parent)
        : QWidget(parent)
    {
        setFixedSize(8, 8);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::colors().accent);
        p.drawEllipse(QRectF(rect()));
    }
};

/// A control's spec for the shell's Readout, from a synth parameter.
ControlSpec readoutSpec(std::uint32_t id)
{
    const vloom::dsp::ParameterSpec& parameter = Synth::parameters()[id];
    ControlSpec spec;
    spec.minValue = parameter.minValue;
    spec.maxValue = parameter.maxValue;
    spec.defaultValue = parameter.defaultValue;
    spec.stepped = parameter.stepped;
    spec.unit = parameter.unit;
    return spec;
}

Card makeCard(const QString& title, QWidget* parent, bool dot = false)
{
    Card card;
    card.frame = new QFrame(parent);
    card.frame->setObjectName(QStringLiteral("synthCard"));
    card.body = new QVBoxLayout(card.frame);
    card.body->setContentsMargins(10, 8, 10, 10);
    card.body->setSpacing(8);
    card.heading = new QHBoxLayout;
    card.heading->setSpacing(dot ? 6 : 8);
    if (dot)
        card.heading->addWidget(new SourceDot(card.frame));
    auto* name = new QLabel(title, card.frame);
    name->setObjectName(QStringLiteral("synthCardTitle"));
    card.heading->addWidget(name);
    card.heading->addStretch(1);
    card.body->addLayout(card.heading);
    return card;
}

QStringList targetNames()
{
    return {SynthEditorWindow::tr("Off"), SynthEditorWindow::tr("Cutoff"),
        SynthEditorWindow::tr("A Position"), SynthEditorWindow::tr("B Position"),
        SynthEditorWindow::tr("Pitch"), SynthEditorWindow::tr("Level")};
}

} // namespace

bool SynthEditorWindow::edits(const vloom::PluginInstance& instance)
{
    return instance.processor().info().id == Synth::spec().id;
}

SynthEditorWindow::SynthEditorWindow(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::Tool)
    , instance_(std::move(instance))
{
    setObjectName(QStringLiteral("synthEditor"));
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(title);

    const vloom::AudioProcessor& processor = instance_->processor();
    for (const vloom::dsp::ParameterSpec& parameter : Synth::parameters())
        values_[parameter.id]
            = std::clamp(processor.parameterValue(parameter.id).value_or(parameter.defaultValue),
                parameter.minValue, parameter.maxValue);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    auto* pages = new QStackedWidget(this);

    auto* header = new QFrame(this);
    header->setObjectName(QStringLiteral("pluginEditorHeader"));
    auto* headerRow = new QHBoxLayout(header);
    header->setFixedHeight(40);
    headerRow->setContentsMargins(12, 0, 12, 0);
    headerRow->setSpacing(10);
    // The two sides share the room equally: the presets sit in the middle of the window.
    auto* left = new QWidget(header);
    auto* leftRow = new QHBoxLayout(left);
    leftRow->setContentsMargins(0, 0, 0, 0);
    leftRow->setSpacing(10);
    auto* right = new QWidget(header);
    auto* rightRow = new QHBoxLayout(right);
    rightRow->setContentsMargins(0, 0, 0, 0);
    power_ = new QToolButton(header);
    power_->setIcon(Theme::icon(QStringLiteral("power")));
    power_->setCheckable(true);
    power_->setFocusPolicy(Qt::NoFocus);
    power_->setAccessibleName(tr("Instrument on"));
    connect(power_, &QToolButton::clicked, this, [this](bool on) {
        setBypassed(!on);
        emit bypassToggled(!on);
    });
    leftRow->addWidget(power_);
    setBypassed(false);
    auto* heading = new QLabel(QString::fromUtf8(Synth::spec().name), header);
    heading->setObjectName(QStringLiteral("pluginEditorTitle"));
    leftRow->addWidget(heading);

    // The pages. Effects and Global have nothing yet: shown, not to be had.
    auto* tabs = new QFrame(header);
    tabs->setObjectName(QStringLiteral("slotGroup"));
    auto* tabRow = new QHBoxLayout(tabs);
    tabRow->setContentsMargins(2, 2, 2, 2);
    tabRow->setSpacing(2);
    auto* group = new QButtonGroup(tabs);
    const auto tab = [&](const QString& name, int page) {
        auto* button = new QToolButton(tabs);
        button->setText(name);
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        if (page < 0) {
            button->setEnabled(false);
            button->setToolTip(tr("Not in this version"));
        } else {
            connect(button, &QToolButton::clicked, pages, [pages, page] { pages->setCurrentIndex(page); });
        }
        group->addButton(button);
        tabRow->addWidget(button);
        return button;
    };
    tab(tr("Oscillators"), 0)->setChecked(true);
    tab(tr("Effects"), -1);
    tab(tr("Matrix"), 1);
    tab(tr("Global"), -1);
    leftRow->addWidget(tabs);
    leftRow->addStretch(1);
    headerRow->addWidget(left, 1);

    // Presets: the app's (the values that differ from the defaults) and the user's.
    std::vector<PresetBar::Factory> factory;
    for (const FactoryPreset& each : factoryPresets()) {
        PresetBar::Factory preset{.name = tr(each.name), .values = PresetBar::Values(Synth::kParameterCount)};
        for (const vloom::dsp::ParameterSpec& parameter : Synth::parameters())
            preset.values[parameter.id] = parameter.defaultValue;
        for (const auto& [id, value] : each.values)
            preset.values[id] = value;
        factory.push_back(std::move(preset));
    }
    presets_ = new PresetBar(QStringLiteral("vloom.synth"), std::move(factory),
        PresetBar::Source{
            .read = [this] { return PresetBar::Values(values_.begin(), values_.end()); },
            .apply =
                [this](const PresetBar::Values& all) {
                    releaseKey(); // the held note belongs to the sound before
                    for (std::uint32_t id = 0; id < all.size() && id < values_.size(); ++id)
                        change(id, all[id]);
                },
            .clamp = [](std::uint32_t id,
                         double value) { return std::clamp(value, spec(id).minValue, spec(id).maxValue); },
        },
        header);
    headerRow->addWidget(presets_);
    rightRow->addStretch(1);
    // The volume, as the design reads it ("Volume −6.0 dB"): drag, scroll or
    // double-click it as a knob.
    auto* volume = new Readout(tr("Volume"), header);
    volume->setSpec(readoutSpec(Synth::kVolume));
    volume->setWidestText(QStringLiteral("-60.0 dB"));
    volume->setToolTip(tr("Volume: drag up or down, scroll, or double-click for the default"));
    connect(volume, &ParameterControl::valueEdited, this,
        [this](double value) { change(Synth::kVolume, value); });
    syncs_.emplace_back([this, volume] { volume->setValue(value(Synth::kVolume)); });
    rightRow->addWidget(volume, 0, Qt::AlignVCenter);
    headerRow->addWidget(right, 1);
    column->addWidget(header);

    pages->addWidget(mainPage());
    pages->addWidget(matrixPage());
    column->addWidget(pages, 1);

    syncControls();
    // The window holds the focus when it opens, so no control shows a focus
    // mark until Tab moves to it (as BuiltinEditorWindow).
    setFocusPolicy(Qt::StrongFocus);
    setFocus(Qt::OtherFocusReason);
    setFixedWidth(kWindowWidth);
    setFixedHeight(sizeHint().height());
}

SynthEditorWindow::~SynthEditorWindow() { releaseKey(); }

void SynthEditorWindow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    releaseKey(); // no note left hanging behind a closed window
}

void SynthEditorWindow::releaseKey()
{
    if (heldKey_ < 0)
        return;
    (void)instance_->stopNote(heldKey_);
    heldKey_ = -1;
}

void SynthEditorWindow::change(std::uint32_t id, double value)
{
    const vloom::dsp::ParameterSpec& parameter = spec(id);
    value = std::clamp(value, parameter.minValue, parameter.maxValue);
    if (parameter.stepped)
        value = std::round(value);
    if (!std::isfinite(value) || value == values_[id])
        return;
    values_[id] = value;
    // The built-in takes the value at once and never refuses.
    (void)instance_->setParameter(id, value);
    syncControls(); // a value can have a control on each page
    if (presets_ != nullptr)
        presets_->markModified();
    emit edited();
}

void SynthEditorWindow::setBypassed(bool bypassed)
{
    if (bypassed)
        releaseKey();
    power_->setChecked(!bypassed);
    power_->setToolTip(
        bypassed ? tr("Instrument off (click to turn on)") : tr("Instrument on (click to turn off)"));
}

void SynthEditorWindow::syncControls()
{
    for (const auto& sync : syncs_)
        sync();
    for (QWidget* picture : pictures_)
        picture->update();
}

QWidget* SynthEditorWindow::knob(
    std::uint32_t id, const QString& caption, Scale scale, int dial, int cellWidth, const QColor& arc)
{
    const vloom::dsp::ParameterSpec& parameter = spec(id);
    // Octaves and Cubic: the knob turns 0 .. 1 and the value follows.
    const auto toPosition = [&parameter, scale](double value) {
        switch (scale) {
        case Scale::Octaves:
            return std::log(value / parameter.minValue) / std::log(parameter.maxValue / parameter.minValue);
        case Scale::Cubic:
            return std::cbrt((value - parameter.minValue) / (parameter.maxValue - parameter.minValue));
        case Scale::Linear:
            break;
        }
        return value;
    };
    const auto toValue = [&parameter, scale](double position) {
        double value = position;
        if (scale == Scale::Octaves)
            value = parameter.minValue * std::pow(parameter.maxValue / parameter.minValue, position);
        else if (scale == Scale::Cubic)
            value = parameter.minValue
                + (parameter.maxValue - parameter.minValue) * position * position * position;
        value = std::clamp(value, parameter.minValue, parameter.maxValue);
        return parameter.stepped ? std::round(value) : value;
    };

    auto* cell = new QWidget(this);
    cell->setFixedWidth(cellWidth);
    auto* stack = new QVBoxLayout(cell);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setSpacing(1);
    auto* dialKnob = scale == Scale::Linear
        ? new Knob(caption, parameter.minValue, parameter.maxValue, parameter.defaultValue, cell)
        : new Knob(caption, 0.0, 1.0, toPosition(parameter.defaultValue), cell);
    dialKnob->setDialSize(dial);
    if (arc.isValid())
        dialKnob->setArcColor(arc);
    dialKnob->setValueText([id, toValue](double position) { return valueText(id, toValue(position)); });
    stack->addWidget(dialKnob, 0, Qt::AlignHCenter);
    auto* reading = new QLabel(cell);
    reading->setObjectName(QStringLiteral("synthValue"));
    reading->setAlignment(Qt::AlignHCenter);
    stack->addWidget(reading);

    connect(dialKnob, &Knob::valueChanged, this,
        [this, id, toValue](double position) { change(id, toValue(position)); });
    syncs_.emplace_back([this, id, dialKnob, reading, toPosition] {
        dialKnob->setValue(toPosition(value(id)));
        reading->setText(valueText(id, value(id)));
    });
    return cell;
}

QWidget* SynthEditorWindow::toggle(std::uint32_t id, const QString& what)
{
    auto* button = new ToggleSwitch(this);
    button->setToolTip(what);
    button->setAccessibleName(what);
    connect(button, &ToggleSwitch::clicked, this, [this, id](bool on) { change(id, on ? 1.0 : 0.0); });
    syncs_.emplace_back([this, id, button] { button->setChecked(value(id) >= 0.5); });
    return button;
}

QWidget* SynthEditorWindow::flag(
    std::uint32_t id, const QString& text, const QString& what, const QColor& colour)
{
    auto* button = new QToolButton(this);
    button->setObjectName(QStringLiteral("synthFlag"));
    // On: filled in the source's colour, as the design's A, B and S.
    if (colour.isValid())
        button->setStyleSheet(
            QStringLiteral("QToolButton#synthFlag:checked { background: %1; border-color: %1; color: %2; }")
                .arg(colour.name(), Theme::colors().base.name()));
    button->setText(text);
    button->setCheckable(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolTip(what);
    button->setAccessibleName(what);
    connect(button, &QToolButton::clicked, this, [this, id](bool on) { change(id, on ? 1.0 : 0.0); });
    syncs_.emplace_back([this, id, button] { button->setChecked(value(id) >= 0.5); });
    return button;
}

QWidget* SynthEditorWindow::choice(std::uint32_t id, const QStringList& names, const QString& what)
{
    auto* box = new QComboBox(this);
    box->setFocusPolicy(Qt::NoFocus);
    box->addItems(names);
    box->setToolTip(what);
    box->setAccessibleName(what);
    connect(box, &QComboBox::activated, this, [this, id](int index) { change(id, index); });
    syncs_.emplace_back([this, id, box] { box->setCurrentIndex(static_cast<int>(std::lround(value(id)))); });
    return box;
}

QWidget* SynthEditorWindow::tabbed(const QStringList& names, const std::function<QWidget*(int)>& page)
{
    auto* whole = new QWidget(this);
    auto* column = new QVBoxLayout(whole);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(6);
    auto* row = new QHBoxLayout;
    row->setSpacing(2);
    auto* pages = new QStackedWidget(whole);
    auto* group = new QButtonGroup(whole);
    for (int i = 0; i < names.size(); ++i) {
        auto* button = new QToolButton(whole);
        button->setObjectName(QStringLiteral("synthTab"));
        button->setText(names[i]);
        button->setCheckable(true);
        button->setChecked(i == 0);
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QToolButton::clicked, pages, [pages, i] { pages->setCurrentIndex(i); });
        group->addButton(button);
        row->addWidget(button);
        pages->addWidget(page(i));
    }
    row->addStretch(1);
    column->addLayout(row);
    column->addWidget(pages);
    return whole;
}

QWidget* SynthEditorWindow::mainPage()
{
    auto* page = new QWidget(this);
    auto* body = new QVBoxLayout(page);
    body->setContentsMargins(12, 12, 12, 12);
    body->setSpacing(8);

    auto* sound = new QHBoxLayout;
    sound->setSpacing(8);
    sound->addWidget(
        sourceCard(tr("Sub"), false, Synth::kSubOctave, Synth::kSubLevel, tr("Octave"), tr("Level")));
    sound->addWidget(
        sourceCard(tr("Noise"), true, Synth::kNoiseColor, Synth::kNoiseLevel, tr("Color"), tr("Level")));
    sound->addWidget(oscillatorCard(QStringLiteral("A"), Synth::kAOn, kHueA));
    sound->addWidget(oscillatorCard(QStringLiteral("B"), Synth::kBOn, kHueB));
    sound->addWidget(filterCard(), 1);
    body->addLayout(sound);

    auto* movers = new QHBoxLayout;
    movers->setSpacing(8);
    movers->addWidget(macroCard());
    const auto tabCard = [this](QWidget* content, int width) {
        auto* card = new QFrame(this);
        card->setObjectName(QStringLiteral("synthCard"));
        card->setFixedWidth(width);
        auto* inside = new QVBoxLayout(card);
        inside->setContentsMargins(10, 8, 10, 10);
        inside->addWidget(content);
        return card;
    };
    movers->addWidget(tabCard(
        tabbed({tr("Env 1"), tr("Env 2"), tr("Env 3")}, [this](int i) { return envelopePage(i); }), 300));
    movers->addWidget(tabCard(
        tabbed({tr("LFO 1"), tr("LFO 2"), tr("LFO 3"), tr("LFO 4")}, [this](int i) { return lfoPage(i); }),
        370));
    movers->addWidget(voicingCard(), 1);
    body->addLayout(movers);

    auto* keys = new Keyboard(page);
    keys->onPress = [this, keys](int key, float velocity) {
        releaseKey();
        if (instance_->playNote(key, velocity))
            heldKey_ = key;
        keys->setHeld(heldKey_);
    };
    keys->onRelease = [this, keys] {
        releaseKey();
        keys->setHeld(-1);
    };
    keys->setToolTip(tr("Play the synth: press a key (lower down is harder), drag to slide"));
    body->addWidget(keys);
    return page;
}

QWidget* SynthEditorWindow::matrixPage()
{
    auto* page = new QWidget(this);
    auto* body = new QVBoxLayout(page);
    body->setContentsMargins(12, 12, 12, 12);
    body->setSpacing(8);
    const Card card = makeCard(tr("What moves what"), page);
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(6);
    // Two blocks side by side: source, target, amount.
    struct Source {
        QString name;
        std::uint32_t targetId; ///< its amount is the next id
    };
    std::vector<Source> sources;
    for (std::uint32_t e = 0; e < Synth::kModEnvelopes; ++e)
        sources.push_back({tr("Envelope %1").arg(e + 2),
            Synth::kEnv2Target + e * (Synth::kEnv3Target - Synth::kEnv2Target)});
    for (std::uint32_t l = 0; l < Synth::kLfos; ++l)
        sources.push_back(
            {tr("LFO %1").arg(l + 1), Synth::kLfo1Target + l * (Synth::kLfo2Target - Synth::kLfo1Target)});
    for (std::uint32_t m = 0; m < Synth::kMacros; ++m)
        sources.push_back({tr("Macro %1").arg(m + 1),
            Synth::kMacro1Target + m * (Synth::kMacro2Target - Synth::kMacro1Target)});
    const int perBlock = static_cast<int>(sources.size() + 1) / 2;
    for (int block = 0; block < 2; ++block) {
        for (int column = 0; column < 3; ++column) {
            auto* title = new QLabel(column == 0 ? tr("Source") : column == 1 ? tr("Moves") : tr("By"), page);
            title->setObjectName(QStringLiteral("synthHint"));
            grid->addWidget(title, 0, block * 4 + column);
        }
    }
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const int block = static_cast<int>(i) / perBlock;
        const int row = 1 + static_cast<int>(i) % perBlock;
        grid->addWidget(new QLabel(sources[i].name, page), row, block * 4);
        grid->addWidget(choice(sources[i].targetId, targetNames(), tr("What %1 moves").arg(sources[i].name)),
            row, block * 4 + 1);
        grid->addWidget(
            knob(sources[i].targetId + 1, tr("Amount"), Scale::Linear, 26, 64), row, block * 4 + 2);
    }
    grid->setColumnMinimumWidth(3, 40);
    grid->setColumnStretch(7, 1);
    card.body->addLayout(grid);
    auto* track = new QHBoxLayout;
    track->setSpacing(14);
    track->addWidget(new QLabel(tr("Keyboard"), page));
    auto* moves = new QLabel(tr("moves the cutoff by"), page);
    moves->setObjectName(QStringLiteral("synthHint"));
    track->addWidget(moves);
    track->addWidget(knob(Synth::kKeyTrack, tr("Key track"), Scale::Linear, 26, 64));
    track->addStretch(1);
    card.body->addLayout(track);
    body->addWidget(card.frame);
    body->addStretch(1);
    return page;
}

QWidget* SynthEditorWindow::sourceCard(const QString& title, bool noise, std::uint32_t first,
    std::uint32_t second, const QString& firstCaption, const QString& secondCaption)
{
    const Card card = makeCard(title, this, true);
    card.frame->setFixedWidth(92);
    auto* picture
        = new SourcePicture([this](std::uint32_t id) { return value(id); }, noise, second, card.frame);
    pictures_.push_back(picture);
    card.body->addWidget(picture);
    card.body->addWidget(knob(first, firstCaption, Scale::Linear, kDial, 70), 0, Qt::AlignHCenter);
    card.body->addWidget(knob(second, secondCaption, Scale::Linear, kDial, 70), 0, Qt::AlignHCenter);
    card.body->addStretch(1);
    return card.frame;
}

QWidget* SynthEditorWindow::oscillatorCard(const QString& letter, std::uint32_t onId, std::size_t hue)
{
    auto* frame = new QFrame(this);
    frame->setObjectName(QStringLiteral("synthCard"));
    frame->setFixedWidth(300);
    auto* body = new QVBoxLayout(frame);
    body->setContentsMargins(10, 8, 10, 10);
    body->setSpacing(6);

    auto* heading = new QHBoxLayout;
    heading->setSpacing(6);
    heading->addWidget(new LetterBadge(letter, hue, frame));
    QStringList tables;
    for (int shape = 0; shape < Wavetable::kShapes; ++shape)
        tables.append(QString::fromUtf8(Wavetable::name(static_cast<Wavetable::Shape>(shape))));
    heading->addWidget(choice(onId + 1, tables, tr("Wavetable of oscillator %1").arg(letter)), 1);
    auto* frameText = new QLabel(frame);
    frameText->setObjectName(QStringLiteral("synthHint"));
    syncs_.emplace_back([this, onId, frameText] {
        const long frameNumber = std::lround(value(onId + 2) / 100.0 * (Wavetable::kFrames - 1)) + 1;
        frameText->setText(tr("Frame %1 / %2").arg(frameNumber).arg(Wavetable::kFrames));
    });
    heading->addWidget(frameText);
    heading->addWidget(toggle(onId, tr("Oscillator %1 on").arg(letter)));
    body->addLayout(heading);

    auto* picture = new TablePicture([this](std::uint32_t id) { return value(id); }, onId, hue, frame);
    picture->setFixedHeight(112);
    pictures_.push_back(picture);
    body->addWidget(picture);

    auto* pitch = new QHBoxLayout;
    pitch->setSpacing(4);
    pitch->setSpacing(12);
    // Readouts, as the design's "Octave 0  Semi 0  Fine +7 ct": drag, scroll or double-click.
    const auto field = [this, frame, pitch](const QString& name, std::uint32_t id, const QString& what) {
        auto* reading = new Readout(name, frame);
        reading->setSpec(readoutSpec(id));
        reading->setFormatter([id](double v) { return valueText(id, v); });
        reading->setWidestText(QStringLiteral("+100 ct"));
        reading->setToolTip(what);
        connect(reading, &ParameterControl::valueEdited, this, [this, id](double v) { change(id, v); });
        syncs_.emplace_back([this, id, reading] { reading->setValue(value(id)); });
        pitch->addWidget(reading);
    };
    field(tr("Octave"), onId + 4, tr("Octaves up or down"));
    field(tr("Semi"), onId + 5, tr("Semitones up or down"));
    field(tr("Fine"), onId + 6, tr("Cents up or down"));
    pitch->addStretch(1);
    body->addLayout(pitch);

    auto* knobs = new QHBoxLayout;
    knobs->setSpacing(0);
    constexpr int kCell = 46;
    const QColor colour = Theme::patternColor(hue); // the design draws an oscillator's arcs in its colour
    knobs->addWidget(knob(onId + 2, tr("Position"), Scale::Linear, kDial, kCell, colour));
    knobs->addWidget(knob(onId + 3, tr("Warp"), Scale::Linear, kDial, kCell, colour));
    knobs->addWidget(knob(onId + 7, tr("Unison"), Scale::Linear, kDial, kCell, colour));
    knobs->addWidget(knob(onId + 8, tr("Detune"), Scale::Linear, kDial, kCell, colour));
    knobs->addWidget(knob(onId + 9, tr("Pan"), Scale::Linear, kDial, kCell, colour));
    knobs->addWidget(knob(onId + 10, tr("Level"), Scale::Linear, kDial, kCell, colour));
    body->addLayout(knobs);
    return frame;
}

QWidget* SynthEditorWindow::filterCard()
{
    const Card card = makeCard(tr("Filter"), this, true);
    card.body->setSpacing(6);
    card.heading->addWidget(
        choice(Synth::kFilterType, {tr("Low 12"), tr("Band 12"), tr("High 12")}, tr("Filter type")));
    card.heading->addWidget(toggle(Synth::kFilterOn, tr("Filter on")));

    auto* picture = new FilterPicture([this](std::uint32_t id) { return value(id); }, card.frame);
    picture->setFixedHeight(112);
    picture->onChange = [this](std::uint32_t id, double value) { change(id, value); };
    picture->setToolTip(tr("Drag: cutoff (sideways) and resonance (up)"));
    pictures_.push_back(picture);
    card.body->addWidget(picture);

    auto* routes = new QHBoxLayout;
    routes->setSpacing(4);
    auto* input = new QLabel(tr("Input"), card.frame);
    input->setObjectName(QStringLiteral("synthHint"));
    routes->addWidget(input);
    routes->addStretch(1);
    routes->addWidget(flag(Synth::kRouteA, QStringLiteral("A"), tr("Oscillator A goes through the filter"),
        Theme::patternColor(kHueA)));
    routes->addWidget(flag(Synth::kRouteB, QStringLiteral("B"), tr("Oscillator B goes through the filter"),
        Theme::patternColor(kHueB)));
    routes->addWidget(flag(Synth::kRouteSub, QStringLiteral("S"),
        tr("The sub oscillator goes through the filter"), Theme::colors().text));
    routes->addWidget(flag(Synth::kRouteNoise, QStringLiteral("N"), tr("The noise goes through the filter"),
        Theme::colors().text));
    card.body->addLayout(routes);

    auto* knobs = new QHBoxLayout;
    knobs->setSpacing(0);
    constexpr int kCell = 44;
    knobs->addWidget(knob(Synth::kCutoff, tr("Cutoff"), Scale::Octaves, kDial, kCell));
    knobs->addWidget(knob(Synth::kResonance, tr("Reso"), Scale::Linear, kDial, kCell));
    knobs->addWidget(knob(Synth::kDrive, tr("Drive"), Scale::Linear, kDial, kCell));
    knobs->addWidget(knob(Synth::kFilterMix, tr("Mix"), Scale::Linear, kDial, kCell));
    card.body->addLayout(knobs);
    return card.frame;
}

QWidget* SynthEditorWindow::macroCard()
{
    const Card card = makeCard(tr("Macros"), this);
    card.frame->setFixedWidth(150);
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(10);
    for (std::uint32_t m = 0; m < Synth::kMacros; ++m)
        grid->addWidget(knob(Synth::kMacro1 + m * (Synth::kMacro2 - Synth::kMacro1),
                            tr("Macro %1").arg(m + 1), Scale::Linear, kDial, 60),
            static_cast<int>(m / 2), static_cast<int>(m % 2));
    card.body->addLayout(grid);
    card.body->addStretch(1);
    return card.frame;
}

QWidget* SynthEditorWindow::envelopePage(int index)
{
    const std::uint32_t attackId = index == 0 ? Synth::kEnv1Attack
                                              : Synth::kEnv2Attack
            + static_cast<std::uint32_t>(index - 1) * (Synth::kEnv3Attack - Synth::kEnv2Attack);
    auto* page = new QWidget(this);
    auto* column = new QVBoxLayout(page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(6);
    auto* to = new QHBoxLayout;
    to->setSpacing(6);
    auto* label = new QLabel(index == 0 ? tr("to the level") : tr("to"), page);
    label->setObjectName(QStringLiteral("synthHint"));
    to->addWidget(label);
    if (index > 0)
        to->addWidget(choice(attackId + 5, targetNames(), tr("What this envelope moves")));
    to->addStretch(1);
    column->addLayout(to);
    auto* picture = new EnvelopePicture([this](std::uint32_t id) { return value(id); }, attackId, page);
    picture->setFixedHeight(84);
    pictures_.push_back(picture);
    column->addWidget(picture);
    auto* knobs = new QHBoxLayout;
    knobs->setSpacing(0);
    const int cell = index == 0 ? 54 : 46;
    knobs->addWidget(knob(attackId, tr("Attack"), Scale::Cubic, kDial, cell));
    knobs->addWidget(knob(attackId + 1, tr("Hold"), Scale::Cubic, kDial, cell));
    knobs->addWidget(knob(attackId + 2, tr("Decay"), Scale::Cubic, kDial, cell));
    knobs->addWidget(knob(attackId + 3, tr("Sustain"), Scale::Linear, kDial, cell));
    knobs->addWidget(knob(attackId + 4, tr("Release"), Scale::Cubic, kDial, cell));
    if (index > 0)
        knobs->addWidget(knob(attackId + 6, tr("Amount"), Scale::Linear, kDial, cell));
    column->addLayout(knobs);
    return page;
}

QWidget* SynthEditorWindow::lfoPage(int index)
{
    const std::uint32_t rateId
        = Synth::kLfo1Rate + static_cast<std::uint32_t>(index) * (Synth::kLfo2Rate - Synth::kLfo1Rate);
    auto* page = new QWidget(this);
    auto* column = new QVBoxLayout(page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(6);
    auto* to = new QHBoxLayout;
    to->setSpacing(6);
    auto* label = new QLabel(tr("to"), page);
    label->setObjectName(QStringLiteral("synthHint"));
    to->addWidget(label);
    to->addWidget(choice(rateId + 2, targetNames(), tr("What this LFO moves")));
    to->addStretch(1);
    to->addWidget(
        choice(rateId + 1, {tr("Sine"), tr("Triangle"), tr("Saw"), tr("Square"), tr("Random")}, tr("Shape")));
    column->addLayout(to);
    auto* picture = new LfoPicture([this](std::uint32_t id) { return value(id); }, rateId, page);
    picture->setFixedHeight(84);
    pictures_.push_back(picture);
    column->addWidget(picture);
    auto* row = new QHBoxLayout;
    row->setSpacing(0);
    constexpr int kCell = 48;
    row->addWidget(knob(rateId, tr("Rate"), Scale::Octaves, kDial, kCell));
    row->addWidget(knob(rateId + 5, tr("Rise"), Scale::Cubic, kDial, kCell));
    row->addWidget(knob(rateId + 6, tr("Delay"), Scale::Cubic, kDial, kCell));
    row->addWidget(knob(rateId + 7, tr("Smooth"), Scale::Linear, kDial, kCell));
    row->addWidget(knob(rateId + 3, tr("Amount"), Scale::Linear, kDial, kCell));
    row->addSpacing(8);
    row->addWidget(toggle(rateId + 4, tr("Start again with each note")));
    auto* again = new QLabel(tr("Retrigger"), page);
    again->setObjectName(QStringLiteral("synthHint"));
    row->addSpacing(6);
    row->addWidget(again);
    row->addStretch(1);
    column->addLayout(row);
    return page;
}

QWidget* SynthEditorWindow::voicingCard()
{
    const Card card = makeCard(tr("Voicing"), this);
    const auto line = [this, &card](std::uint32_t id, const QString& text, const QString& what) {
        auto* row = new QHBoxLayout;
        row->setSpacing(6);
        row->addWidget(toggle(id, what));
        auto* label = new QLabel(text, card.frame);
        label->setObjectName(QStringLiteral("synthHint"));
        row->addWidget(label);
        row->addStretch(1);
        card.body->addLayout(row);
    };
    line(Synth::kMono, tr("Mono"), tr("One note at a time"));
    line(Synth::kLegato, tr("Legato"),
        tr("Mono: a note played over another does not start the envelopes again"));
    card.body->addStretch(1);
    auto* knobs = new QHBoxLayout;
    knobs->setSpacing(0);
    knobs->addWidget(knob(Synth::kVoices, tr("Voices"), Scale::Linear, kDial, 54));
    knobs->addWidget(knob(Synth::kGlide, tr("Glide"), Scale::Cubic, kDial, 54));
    card.body->addLayout(knobs);
    return card.frame;
}

} // namespace vloomdaw
