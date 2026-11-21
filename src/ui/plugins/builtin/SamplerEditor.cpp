// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/SamplerEditor.hpp"

#include "core/SampleView.hpp"
#include "core/TimeEffects.hpp"
#include "ui/DroppedFiles.hpp"
#include "ui/plugins/SampleFiles.hpp"
#include "ui/plugins/builtin/Animation.hpp"
#include "ui/plugins/builtin/AnimationPaint.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"
#include "ui/plugins/builtin/ParameterControl.hpp"
#include "ui/plugins/builtin/Readout.hpp"
#include "ui/plugins/builtin/SampleParts.hpp"

#include <vloom/dsp/instruments/Sampler.hpp>
#include <vloom/engine/plugin/PluginInstance.hpp>

#include <QAbstractButton>
#include <QButtonGroup>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLinearGradient>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace vloomdaw {

namespace {

namespace style = editor_style;
using Sampler = vloom::dsp::Sampler;

constexpr int kSlot = 0; ///< the sampler's one sample slot
constexpr int kWatchMs = 300; ///< how often the slot is looked at for a sample loaded elsewhere
constexpr double kSide = 40.0; ///< the sample's start and end, from the sides of the display
constexpr double kTop = 20.0;
constexpr double kRoomBelow = 148.0; ///< the time line and the panel, under the waveform
constexpr double kEnvelopeTop = 24.0; ///< the envelope at full level, under the top
constexpr double kGrab = 6.0; ///< how near a marker a press takes it
constexpr double kTickSpacing = 90.0; ///< px between time labels, at least
constexpr std::size_t kHeads = 4; ///< notes whose sweep shows at once
constexpr double kSweepTail = 70.0; ///< px of lit sample behind a sweep
constexpr double kFadeOut = 0.3; ///< s a sweep takes to fade once the next note starts

/// How parameter `info` reads at `value` (a key: "C3"); empty for none.
QString choiceName(const vloom::ParameterInfo* info, double value)
{
    return info != nullptr ? ControlSpec::of(*info).text(value) : QString();
}

} // namespace

/// The sample: its waveform, the markers to drag, the envelope and the time.
///
/// Animated: each note (an onset in what it plays) sends a glowing sweep
/// along the sample from Start at the speed it plays (its tune), round the
/// loop while Loop Mode is on, with a spark riding the envelope over it;
/// the envelope glows with the level. The engine does not report where its
/// voices are, so the sweep is worked out from the onset and the settings.
class SamplerDisplay final : public QWidget, public Animated {
public:
    using Value = std::function<double(std::uint32_t)>;

    std::function<void(std::uint32_t, double)> onChange; ///< a marker was dragged
    std::function<void(std::uint32_t)> onReset; ///< a marker was double-clicked
    std::function<void()> onLoad; ///< the empty display was clicked
    std::function<void(const QString&)> onDrop; ///< an audio file was dropped

    explicit SamplerDisplay(Value value)
        : value_(std::move(value))
    {
        setAcceptDrops(true);
        setMouseTracking(true);
        setMinimumSize(480, 240);
        setAccessibleName(tr("Sample: the waveform with the start and loop markers and the envelope"));
    }

    void setSample(const vloom::SampleSlotInfo& info,
        std::shared_ptr<const SampleWaveforms::Waveform> waveform, bool loading)
    {
        const QString path = QString::fromStdString(info.path);
        const QString error = QString::fromStdString(info.error);
        if (path == path_ && error == error_ && info.frames == frames_ && waveform == waveform_
            && loading == loading_)
            return;
        path_ = path;
        error_ = error;
        frames_ = info.frames;
        seconds_ = info.loaded() && info.sampleRate > 0.0 ? static_cast<double>(info.frames) / info.sampleRate
                                                          : 0.0;
        waveform_ = std::move(waveform);
        loading_ = loading;
        setCursor(info.loaded() ? Qt::ArrowCursor : Qt::PointingHandCursor);
        update();
    }

    bool advance(const motion::AudioFeatures& audio, double dt) override
    {
        const double live = audio.liveliness();
        glow_ = audio.level() * live;
        swell_ = audio.envelope() * live;
        const QRectF area = waveArea();
        if (frames_ > 0 && seconds_ > 0.0) {
            const double speed = playbackSpeed(value_(Sampler::kTranspose), value_(Sampler::kFineTune));
            const double pixelsPerSecond = area.width() / seconds_ * speed;
            const bool looping = value_(Sampler::kLoopMode) >= 0.5;
            const double loopStart = xOf(value_(Sampler::kLoopStart));
            const double loopEnd = xOf(value_(Sampler::kLoopEnd));
            heads_.advance(dt, [&](auto& head, double step) {
                head.state.x += pixelsPerSecond * step;
                if (looping && loopEnd - loopStart > 1.0 && head.state.x >= loopEnd)
                    head.state.x = loopStart + std::fmod(head.state.x - loopStart, loopEnd - loopStart);
                if (head.state.x > area.right() || live < 0.02)
                    head.life = head.age; // played out, or the sound stopped
            });
            if (audio.onset() && live > 0.0) {
                // A new note: the sweeps of the notes before fade out.
                heads_.advance(
                    0.0, [](auto& head, double) { head.life = std::min(head.life, head.age + kFadeOut); });
                heads_.spawn(
                    Head{.x = xOf(value_(Sampler::kStart)), .strength = audio.onsetStrength()}, 60.0);
            }
        } else {
            heads_.clear();
        }
        update(QRectF(0.0, 0.0, width(), area.bottom() + 2.0).toAlignedRect());
        return glow_ > 0.002 || swell_ > 0.002 || heads_.alive() > 0;
    }

    void rest() override
    {
        glow_ = 0.0;
        swell_ = 0.0;
        heads_.clear();
        update();
    }

    void animationSwitched(bool /*on*/) override { rest(); }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const AnimationPaintTimer timer;
        QPainter p(this);
        std::uint64_t key = motion_paint::keyWith(0, static_cast<double>(frames_));
        for (const std::uint32_t id : {Sampler::kStart, Sampler::kLoopMode, Sampler::kLoopStart,
                 Sampler::kLoopEnd, Sampler::kAttack, Sampler::kDecay, Sampler::kSustain, Sampler::kRelease,
                 Sampler::kTranspose, Sampler::kFineTune})
            key = motion_paint::keyWith(key, value_(id));
        key = motion_paint::keyWith(key, static_cast<double>(qHash(path_) ^ qHash(error_)));
        key = motion_paint::keyWith(
            key, static_cast<double>(reinterpret_cast<std::uintptr_t>(waveform_.get())));
        key = motion_paint::keyWith(key, (loading_ ? 1.0 : 0.0) + (dropping_ ? 2.0 : 0.0));
        back_.draw(p, *this, key, [this](QPainter& layer) { paintStatic(layer); });
        if (animationOn() && frames_ > 0)
            paintMotion(p);
    }

    void paintMotion(QPainter& p)
    {
        const QRectF area = waveArea();
        const QColor accent = style::accent();
        if (swell_ > 0.002 && !envelope_.isEmpty())
            motion_paint::glowStroke(p, envelope_, accent, 0.6 * swell_, 6.0, 7.0);
        if (glow_ <= 0.002)
            return;
        p.setRenderHint(QPainter::Antialiasing, false);
        heads_.forEach([&](const auto& head) {
            const double x = head.state.x;
            const double strength = glow_ * (0.5 + 0.5 * head.state.strength)
                * std::min(1.0, (head.life - head.age) / kFadeOut);
            // The stretch it just played, lit and fading behind it.
            QLinearGradient tail(QPointF(x - kSweepTail, 0.0), QPointF(x, 0.0));
            tail.setColorAt(0.0, motion_paint::faded(accent, 0.0));
            tail.setColorAt(1.0, motion_paint::faded(accent, 0.22 * strength));
            p.fillRect(
                QRectF(QPointF(std::max(area.left(), x - kSweepTail), area.top()), QPointF(x, area.bottom())),
                tail);
            motion_paint::glowLine(p, QLineF(x, area.top(), x, area.bottom()), accent, strength, 7.0);
            motion_paint::glowDot(p, QPointF(x, envelopeY(x)), 9.0 + 8.0 * strength, accent, strength);
            motion_paint::glowDot(p, QPointF(x, envelopeY(x)), 3.0, style::text(), strength);
        });
    }

    void paintStatic(QPainter& p)
    {
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), style::background());
        const QRectF area = waveArea();
        p.fillRect(QRectF(0, area.center().y() - 0.5, width(), 1), style::line());

        const bool loaded = frames_ > 0;
        if (loaded) {
            const bool looping = value_(Sampler::kLoopMode) >= 0.5;
            const double loopStart = xOf(value_(Sampler::kLoopStart));
            const double loopEnd = xOf(value_(Sampler::kLoopEnd));
            if (looping) {
                QColor fill = style::accent();
                fill.setAlphaF(0.12f);
                p.fillRect(QRectF(QPointF(loopStart, area.top()), QPointF(loopEnd, area.bottom())), fill);
            }
            if (waveform_ != nullptr) {
                QColor wave = style::text();
                wave.setAlphaF(0.6f);
                paintWaveform(p, area, waveform_->peaks, false, wave, 3.0, 1.5);
            }
            // Before Start does not play: dimmed.
            const double start = xOf(value_(Sampler::kStart));
            QColor shade = style::background();
            shade.setAlphaF(0.55f);
            p.fillRect(QRectF(QPointF(0, area.top()), QPointF(start, area.bottom())), shade);
            paintEnvelope(p, area, start);
            paintTicks(p, area);

            // The loop: dimmer and dashed while it is off.
            QColor loopColour = style::accent();
            if (!looping)
                loopColour.setAlphaF(0.45f);
            QPen loopPen(loopColour, 2.0);
            if (!looping)
                loopPen.setStyle(Qt::DashLine);
            p.setPen(loopPen);
            p.drawLine(QPointF(loopStart, area.top()), QPointF(loopStart, area.bottom()));
            p.drawLine(QPointF(loopEnd, area.top()), QPointF(loopEnd, area.bottom()));
            paintTag(p, QPointF(loopStart, area.top()), tr("Loop"), loopColour, false);

            p.setPen(QPen(style::text(), 2.0));
            p.drawLine(QPointF(start, area.top()), QPointF(start, area.bottom()));
            paintTag(p, QPointF(start, area.top()), tr("Start"), style::text(), false);
            const double end = area.right();
            p.setPen(QPen(style::text(), 2.0));
            p.drawLine(QPointF(end, area.top()), QPointF(end, area.bottom()));
            paintTag(p, QPointF(end, area.top()), tr("End"), style::text(), true);
        }

        // What is (not) there, in the middle.
        QString message;
        if (!loaded && path_.isEmpty())
            message = tr("Drop a sample here, or click to choose one");
        else if (!loaded)
            message = tr("Missing: %1\n%2").arg(QFileInfo(path_).fileName(), error_);
        else if (waveform_ == nullptr)
            message = loading_ ? tr("Reading the waveform…") : tr("No waveform for this file");
        if (!message.isEmpty()) {
            p.setFont(style::font(font(), 12));
            p.setPen(style::muted());
            p.drawText(area.toRect(), Qt::AlignCenter, message);
        }
        if (dropping_) {
            p.setPen(QPen(style::accent(), 2.0));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(rect()).adjusted(1, 1, -1, -1));
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        if (frames_ <= 0) {
            if (onLoad)
                onLoad();
            return;
        }
        dragging_ = markerAt(event->position());
        if (dragging_)
            drag(event->position().x());
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (dragging_) {
            drag(event->position().x());
            return;
        }
        if (frames_ > 0)
            setCursor(markerAt(event->position()) ? Qt::SizeHorCursor : Qt::ArrowCursor);
    }

    void mouseReleaseEvent(QMouseEvent* /*event*/) override { dragging_.reset(); }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (frames_ <= 0)
            return;
        if (const auto id = markerAt(event->position()); id && onReset)
            onReset(*id);
    }

    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (droppedAudio(event->mimeData()).isEmpty())
            return;
        event->acceptProposedAction();
        dropping_ = true;
        update();
    }
    void dragLeaveEvent(QDragLeaveEvent* /*event*/) override
    {
        dropping_ = false;
        update();
    }
    void dropEvent(QDropEvent* event) override
    {
        dropping_ = false;
        update();
        const QString file = droppedAudio(event->mimeData());
        if (file.isEmpty())
            return;
        event->acceptProposedAction();
        if (onDrop)
            onDrop(file);
    }

private:
    [[nodiscard]] QRectF waveArea() const
    {
        const double bottom = std::max(kTop + 80.0, height() - kRoomBelow);
        return {kSide, kTop, std::max(1.0, width() - 2.0 * kSide), bottom - kTop};
    }
    /// Where `percent` of the sample is.
    [[nodiscard]] double xOf(double percent) const
    {
        const QRectF area = waveArea();
        return area.left() + area.width() * std::clamp(percent, 0.0, 100.0) / 100.0;
    }

    /// The marker under `point`: the nearest within reach.
    [[nodiscard]] std::optional<std::uint32_t> markerAt(QPointF point) const
    {
        const QRectF area = waveArea();
        if (point.y() < area.top() - 4.0 || point.y() > area.bottom() + 4.0)
            return std::nullopt;
        std::optional<std::uint32_t> nearest;
        double best = kGrab;
        for (const std::uint32_t id : {Sampler::kStart, Sampler::kLoopStart, Sampler::kLoopEnd}) {
            double distance = std::abs(point.x() - xOf(value_(id)));
            // The tags over the lines take a press too.
            if (id != Sampler::kLoopEnd && point.y() <= area.top() + 18.0 && point.x() >= xOf(value_(id))
                && point.x() <= xOf(value_(id)) + 44.0)
                distance = 0.0;
            if (distance <= best) {
                best = distance;
                nearest = id;
            }
        }
        return nearest;
    }

    void drag(double x)
    {
        if (!dragging_ || !onChange)
            return;
        const QRectF area = waveArea();
        double percent = std::clamp((x - area.left()) / area.width() * 100.0, 0.0, 100.0);
        // On a frame of the sample, as the dsp plays it.
        if (frames_ > 0 && frames_ < std::numeric_limits<int>::max()) {
            const auto frames = static_cast<int>(frames_);
            percent
                = Sampler::percentForFrame(static_cast<int>(std::lround(percent / 100.0 * frames)), frames);
        }
        // The loop keeps its start before its end.
        if (*dragging_ == Sampler::kLoopStart)
            percent = std::min(percent, value_(Sampler::kLoopEnd));
        else if (*dragging_ == Sampler::kLoopEnd)
            percent = std::max(percent, value_(Sampler::kLoopStart));
        onChange(*dragging_, percent);
    }

    /// Where the amp envelope (paintEnvelope()) is at `x`.
    [[nodiscard]] double envelopeY(double x) const
    {
        const QRectF area = waveArea();
        const double startX = xOf(value_(Sampler::kStart));
        if (seconds_ <= 0.0 || x < startX)
            return area.bottom();
        const double speed = playbackSpeed(value_(Sampler::kTranspose), value_(Sampler::kFineTune));
        const double attack = value_(Sampler::kAttack) / 1000.0;
        const double decay = value_(Sampler::kDecay) / 1000.0;
        const double sustain = value_(Sampler::kSustain) / 100.0;
        const double release = value_(Sampler::kRelease) / 1000.0;
        // Seconds of playing from Start to the end of the sample.
        const double playing = seconds_ * (1.0 - value_(Sampler::kStart) / 100.0) / speed;
        const double letGo = std::max(0.0, playing - release);
        const double t = (x - startX) / area.width() * seconds_ / speed;
        const double level = t < letGo
            ? envelopeLevel(t, attack, decay, sustain)
            : releaseLevel(envelopeLevel(letGo, attack, decay, sustain), t - letGo, release);
        const double top = area.top() + kEnvelopeTop;
        return area.bottom() - level * (area.bottom() - top);
    }

    /// The amp envelope from Start, at the root key; its release ends where the sample does.
    void paintEnvelope(QPainter& p, const QRectF& area, double startX)
    {
        envelope_ = QPainterPath();
        if (seconds_ <= 0.0)
            return;
        QPainterPath path(QPointF(startX, area.bottom()));
        for (double x = startX + 1.0; x <= area.right() + 0.5; x += 1.0)
            path.lineTo(x, envelopeY(x));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(style::accent(), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
        envelope_ = path;
    }

    /// The sample's own seconds under it.
    void paintTicks(QPainter& p, const QRectF& area)
    {
        if (seconds_ <= 0.0)
            return;
        const double step = tickStep(seconds_, std::max(1, static_cast<int>(area.width() / kTickSpacing)));
        p.setFont(style::font(font(), 10, QFont::Normal, true));
        for (int i = 1;; ++i) {
            const double t = step * i;
            if (t > seconds_ * (1.0 + 1e-9))
                break;
            const double x = area.left() + area.width() * t / seconds_;
            p.setPen(QPen(style::muted(), 1.0));
            p.drawLine(QPointF(x, area.bottom()), QPointF(x, area.bottom() + 8.0));
            const QString label = QString::fromStdString(tickLabel(t, step));
            p.drawText(QRectF(x - 40.0, area.bottom() + 12.0, 80.0, 14.0), Qt::AlignCenter, label);
        }
    }

    /// A marker's tag at the top of its line: "Start", "Loop", "End" (`left`: on the line's left).
    void paintTag(QPainter& p, QPointF at, const QString& text, const QColor& colour, bool left)
    {
        const QFont font = style::font(this->font(), 10, QFont::Bold);
        const double w = QFontMetrics(font).horizontalAdvance(text) + 14.0;
        const QRectF tag(left ? at.x() - w : at.x(), at.y(), w, 18.0);
        p.setPen(Qt::NoPen);
        p.setBrush(colour);
        p.drawRoundedRect(tag, 3, 3);
        p.setFont(font);
        p.setPen(style::background());
        p.drawText(tag, Qt::AlignCenter, text);
    }

    Value value_;
    QString path_;
    QString error_;
    std::int64_t frames_ = 0;
    double seconds_ = 0.0;
    std::shared_ptr<const SampleWaveforms::Waveform> waveform_;
    bool loading_ = false;
    std::optional<std::uint32_t> dragging_;
    bool dropping_ = false;

    // The motion (animationOn()).
    struct Head {
        double x = 0.0; ///< where it plays now, px
        double strength = 0.0; ///< the onset's
    };
    motion_paint::LayerCache back_;
    QPainterPath envelope_; ///< as drawn (paintEnvelope())
    motion::ParticlePool<Head, kHeads> heads_;
    double glow_ = 0.0;
    double swell_ = 0.0;
};

namespace {

/// A page's name to the left of the panel ("Amp", "Filter", "Play").
class PageTab final : public QAbstractButton {
public:
    explicit PageTab(const QString& text)
    {
        setText(text);
        setCheckable(true);
        setFocusPolicy(Qt::TabFocus);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(52, 22);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (isChecked()) {
            p.setPen(Qt::NoPen);
            p.setBrush(style::line());
            p.drawRoundedRect(QRectF(rect()), 5, 5);
        }
        p.setFont(style::font(font(), 11, isChecked() ? QFont::Bold : QFont::Normal));
        p.setPen(isChecked() ? style::text() : style::muted());
        p.drawText(rect(), Qt::AlignCenter, text());
        if (hasFocus()) {
            p.setPen(QPen(style::accent(), 1.0));
            p.drawLine(QPointF(8, height() - 1.5), QPointF(width() - 8, height() - 1.5));
        }
    }
};

/// An upright rule between two groups in a page, as ControlPanel::addDivider().
class Divider final : public QWidget {
public:
    Divider() { setFixedSize(9, 56); }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter p(this);
        p.fillRect(QRect(width() / 2, 0, 1, height()), style::strong());
    }
};

} // namespace

SamplerEditor::SamplerEditor(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate, QWidget* parent)
    : BuiltinEditorWindow(std::move(instance), title, sampleRate,
          {
              {tr("Soft Pad"),
                  {{Sampler::kLoopMode, 1.0}, {Sampler::kLoopCrossfade, 60.0}, {Sampler::kAttack, 450.0},
                      {Sampler::kDecay, 800.0}, {Sampler::kSustain, 80.0}, {Sampler::kRelease, 1500.0},
                      {Sampler::kFilterType, 1.0}, {Sampler::kCutoff, 5000.0}, {Sampler::kResonance, 10.0}}},
              {tr("Pluck"),
                  {{Sampler::kAttack, 1.0}, {Sampler::kDecay, 450.0}, {Sampler::kSustain, 0.0},
                      {Sampler::kRelease, 200.0}, {Sampler::kFilterType, 1.0}, {Sampler::kCutoff, 900.0},
                      {Sampler::kResonance, 25.0}, {Sampler::kFilterEnvAmount, 55.0},
                      {Sampler::kFilterDecay, 300.0}}},
              {tr("Mono Glide"),
                  {{Sampler::kLoopMode, 1.0}, {Sampler::kLoopCrossfade, 30.0}, {Sampler::kMono, 1.0},
                      {Sampler::kLegato, 1.0}, {Sampler::kGlide, 90.0}, {Sampler::kRelease, 250.0}}},
              {tr("Lo-fi Keys"),
                  {{Sampler::kDecay, 1200.0}, {Sampler::kSustain, 40.0}, {Sampler::kRelease, 400.0},
                      {Sampler::kFilterType, 1.0}, {Sampler::kCutoff, 2800.0}, {Sampler::kKeyTrack, 50.0},
                      {Sampler::kVelocity, 70.0}}},
          },
          parent)
{
    resize(900, 520); // the design's size

    waveforms_ = new SampleWaveforms(1, this);
    connect(waveforms_, &SampleWaveforms::changed, this, [this] { refreshSlot(); });

    display_ = new SamplerDisplay([this](std::uint32_t id) { return value(id); });
    display_->onChange = [this](std::uint32_t id, double v) { change(id, v); };
    display_->onReset = [this](std::uint32_t id) {
        if (const vloom::ParameterInfo* info = parameter(id))
            change(id, info->defaultValue);
    };
    display_->onLoad = [this] { chooseFile(); };
    display_->onDrop = [this](const QString& path) { load(path); };
    setDisplay(display_);
    animate(display_);
    // Everything the picture shows.
    for (const std::uint32_t id :
        {Sampler::kStart, Sampler::kLoopMode, Sampler::kLoopStart, Sampler::kLoopEnd, Sampler::kAttack,
            Sampler::kDecay, Sampler::kSustain, Sampler::kRelease, Sampler::kTranspose, Sampler::kFineTune})
        bind(id, [this](double) { display_->update(); });

    buildPanel();

    // The footer, as the design's: the sample, the root key, the loop and the voices.
    sample_ = readout(tr("Sample"));
    sample_->setCursor(Qt::PointingHandCursor);
    sample_->installEventFilter(this);
    footer()->addLeft(sample_);
    auto* clear = new SmallButton(QStringLiteral("×"));
    clear->setFixedSize(20, 20);
    clear->setToolTip(tr("Remove the sample"));
    clear->setAccessibleName(tr("Remove the sample"));
    connect(clear, &QAbstractButton::clicked, this, [this] { clearSample(); });
    footer()->addLeft(clear);
    clear_ = clear;
    Readout* root = readout(Sampler::kRootKey, tr("Root note"));
    root->setToolTip(tr("The key that plays the sample at its own pitch"));
    footer()->addLeft(root);
    Readout* low = readout(Sampler::kKeyLow, tr("Keys"));
    low->setToolTip(tr("The lowest key that plays the sample"));
    footer()->addLeft(low);
    Readout* high = readout(Sampler::kKeyHigh);
    high->setLabel({}); // reads after the lowest: "Keys C-2 to G8"
    high->setToolTip(tr("The highest key that plays the sample"));
    high->setAccessibleName(tr("Highest key"));
    high->setFormatter(
        [this](double key) { return tr("to %1").arg(choiceName(parameter(Sampler::kKeyHigh), key)); });
    footer()->addLeft(high);
    Readout* loop = readout(Sampler::kLoopMode, tr("Loop"));
    loop->setWidestText(tr("Ping-pong"));
    footer()->addLeft(loop);
    footer()->addRight(readout(Sampler::kVoices, tr("Voices")));

    watch_ = new QTimer(this);
    watch_->setInterval(kWatchMs);
    connect(watch_, &QTimer::timeout, this, [this] { refreshSlot(); });
    refreshSlot();
}

void SamplerEditor::buildPanel()
{
    // The pages' names, left of the knobs.
    auto* names = new QWidget;
    auto* nameColumn = new QVBoxLayout(names);
    nameColumn->setContentsMargins(0, 0, 0, 0);
    nameColumn->setSpacing(2);
    auto* group = new QButtonGroup(names);
    const std::array<QString, 3> titles{tr("Amp"), tr("Filter"), tr("Play")};
    for (std::size_t i = 0; i < titles.size(); ++i) {
        auto* tab = new PageTab(titles[i]);
        tab->setAccessibleName(tr("%1 controls").arg(titles[i]));
        group->addButton(tab, static_cast<int>(i));
        nameColumn->addWidget(tab);
        tabs_[i] = tab;
    }
    connect(group, &QButtonGroup::idClicked, this, [this](int page) { showPage(page); });
    panel()->add(names);
    panel()->addDivider();

    const auto page = [this](std::initializer_list<std::vector<QWidget*>> groups) {
        auto* widget = new QWidget;
        auto* row = new QHBoxLayout(widget);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        bool first = true;
        for (const std::vector<QWidget*>& each : groups) {
            if (!first)
                row->addWidget(new Divider, 0, Qt::AlignVCenter);
            first = false;
            for (QWidget* control : each)
                row->addWidget(control, 0, Qt::AlignVCenter);
        }
        panel()->add(widget);
        return widget;
    };
    // Amp: the design's row.
    pages_[0] = page({
        {knob(Sampler::kTranspose, tr("Pitch")), knob(Sampler::kFineTune, tr("Fine"))},
        {knob(Sampler::kAttack, tr("Attack")), knob(Sampler::kDecay, tr("Decay")),
            knob(Sampler::kSustain, tr("Sustain")), knob(Sampler::kRelease, tr("Release"))},
        {knob(Sampler::kCutoff, tr("Cutoff")), knob(Sampler::kGain, tr("Gain"))},
    });
    ArcKnob* envelope = knob(Sampler::kFilterEnvAmount, tr("Env"));
    envelope->setToolTip(tr("How far the filter envelope moves the cutoff"));
    pages_[1] = page({
        {knob(Sampler::kFilterType, tr("Type")), knob(Sampler::kCutoff, tr("Cutoff")),
            knob(Sampler::kResonance, tr("Reso")), envelope, knob(Sampler::kKeyTrack, tr("Key track"))},
        {knob(Sampler::kFilterAttack, tr("Attack")), knob(Sampler::kFilterDecay, tr("Decay")),
            knob(Sampler::kFilterSustain, tr("Sustain")), knob(Sampler::kFilterRelease, tr("Release"))},
    });
    ArcKnob* crossfade = knob(Sampler::kLoopCrossfade, tr("X-fade"));
    crossfade->setToolTip(tr("Loop crossfade: a forward loop fades its end into the audio before its start"));
    ArcKnob* pan = knob(Sampler::kPan, tr("Pan"));
    pan->setFormatter([](double percent) { return QString::fromStdString(panText(percent)); });
    pages_[2] = page({
        {knob(Sampler::kStart, tr("Start")), knob(Sampler::kVelocity, tr("Velocity")), crossfade},
        {toggle(Sampler::kMono, tr("Mono")), toggle(Sampler::kLegato, tr("Legato")),
            knob(Sampler::kGlide, tr("Glide")), knob(Sampler::kVoices, tr("Voices"))},
        {pan},
    });
    showPage(0);
}

void SamplerEditor::showPage(int page)
{
    for (std::size_t i = 0; i < pages_.size(); ++i) {
        pages_[i]->setVisible(static_cast<int>(i) == page);
        static_cast<QAbstractButton*>(tabs_[i])->setChecked(static_cast<int>(i) == page);
    }
}

bool SamplerEditor::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == sample_ && event->type() == QEvent::MouseButtonRelease
        && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        chooseFile();
        return true;
    }
    return BuiltinEditorWindow::eventFilter(watched, event);
}

void SamplerEditor::showEvent(QShowEvent* event)
{
    BuiltinEditorWindow::showEvent(event);
    refreshSlot();
    watch_->start();
}

void SamplerEditor::hideEvent(QHideEvent* event)
{
    BuiltinEditorWindow::hideEvent(event);
    watch_->stop();
}

void SamplerEditor::chooseFile()
{
    const QString file
        = chooseSampleFile(this, QString::fromStdString(instance().processor().sampleSlot(kSlot).path));
    if (!file.isEmpty())
        load(file);
}

void SamplerEditor::load(const QString& path)
{
    const bool loaded = loadSampleFile(instance().processor(), kSlot, path, sample_);
    refreshSlot();
    if (loaded)
        emit edited();
}

void SamplerEditor::clearSample()
{
    if (!instance().processor().clearSample(kSlot))
        return;
    refreshSlot();
    emit edited();
}

void SamplerEditor::refreshSlot()
{
    const vloom::SampleSlotInfo info = instance().processor().sampleSlot(kSlot);
    waveforms_->update(kSlot, info);
    display_->setSample(info, waveforms_->waveform(kSlot), waveforms_->loading(kSlot));
    const QString path = QString::fromStdString(info.path);
    QString name = path.isEmpty() ? tr("None (click to load)") : QFileInfo(path).fileName();
    if (!path.isEmpty() && !info.error.empty())
        name = tr("Missing: %1").arg(name);
    sample_->setText(sample_->fontMetrics().elidedText(name, Qt::ElideMiddle, 260));
    QString tip = path.isEmpty() ? tr("Click to load a sample, or drop one on the waveform")
                                 : QDir::toNativeSeparators(path) + tr("\nClick to replace it");
    if (!info.error.empty())
        tip += QLatin1Char('\n') + QString::fromStdString(info.error);
    sample_->setToolTip(tip);
    clear_->setVisible(!path.isEmpty());
}

} // namespace vloomdaw
