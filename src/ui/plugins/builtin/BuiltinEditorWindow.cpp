// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/BuiltinEditorWindow.hpp"

#include "ui/plugins/PresetBar.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"
#include "ui/plugins/builtin/ParameterControl.hpp"
#include "ui/plugins/builtin/Readout.hpp"

#include <vloom/engine/plugin/PluginInstance.hpp>

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <span>
#include <string>

namespace vloomdaw {

namespace {

namespace style = editor_style;

constexpr int kMeterMs = 33; ///< between two meter readings
constexpr std::size_t kTailFrames = 64; ///< compared between readings to tell whether audio runs
/// Silent for this long after settling, the clock idles even if a display
/// still says it moves (a long reverb trail): no CPU when nothing plays.
constexpr double kSettleGrace = 1.0;
/// A frame with no new audio within this long of one with some is still
/// running: the engine's blocks can come further apart than the frames.
constexpr double kAudioHoldSeconds = 0.1;

/// Reads what `instance` put out in the last `seconds` into `buffer` (sized
/// kScopeFrames) and returns those frames. `running`: new audio came since
/// the last read (the end differs from `lastTail`, which it updates).
std::span<const float> readNewest(const vloom::PluginInstance& instance, double seconds, double sampleRate,
    std::vector<float>& buffer, std::vector<float>& lastTail, bool& running)
{
    const auto wanted = static_cast<std::size_t>(std::clamp(
        std::lround(seconds * sampleRate), 1L, static_cast<long>(vloom::PluginInstance::kScopeFrames)));
    const std::size_t count
        = instance.readScope(std::span<float>(buffer.data(), std::max(wanted, kTailFrames)));
    const std::span<const float> newest(buffer.data(), count);
    // The same frames as last time: no audio is running (the scope keeps
    // the last ones it got).
    const std::span<const float> tail = newest.last(std::min(count, kTailFrames));
    running = count > 0
        && !(tail.size() == lastTail.size() && std::equal(tail.begin(), tail.end(), lastTail.begin()));
    lastTail.assign(tail.begin(), tail.end());
    return newest.last(std::min(count, wanted));
}

/// The bypass button: the power symbol in the accent while the effect is
/// on, muted while it is bypassed.
class PowerButton : public QToolButton {
public:
    using QToolButton::QToolButton;

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QToolButton::paintEvent(event); // the frame, from the style sheet
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        // The designs' 24-unit icon at 13 px, centred.
        const double size = 13.0;
        p.translate((width() - size) / 2.0, (height() - size) / 2.0);
        p.scale(size / 24.0, size / 24.0);
        p.setPen(QPen(isChecked() ? style::accent() : style::muted(), 2.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(12, 3), QPointF(12, 12));
        // An arc of radius 8 about (12, 12.8), open at the top.
        p.drawArc(QRectF(4, 4.8, 16, 16), static_cast<int>(135.5 * 16), static_cast<int>(269 * 16));
    }
};

} // namespace

BuiltinEditorWindow::BuiltinEditorWindow(std::shared_ptr<vloom::PluginInstance> instance,
    const QString& title, double sampleRate, std::vector<Preset> presets, QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::Tool)
    , instance_(std::move(instance))
    , sampleRate_(sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 48000.0)
{
    setObjectName(QStringLiteral("builtinEditor"));
    setAttribute(Qt::WA_DeleteOnClose);
    // The window takes the focus when shown, so no control shows a focus
    // ring until Tab moves to it.
    setFocusPolicy(Qt::StrongFocus);
    setWindowTitle(title);

    const vloom::AudioProcessor& processor = instance_->processor();
    for (vloom::ParameterInfo& each : processor.parameters()) {
        const std::size_t id = each.id;
        Q_ASSERT(id < 4096); // built-ins number theirs from 0
        if (id >= 4096)
            continue;
        if (parameters_.size() <= id) {
            parameters_.resize(id + 1);
            values_.resize(id + 1, 0.0);
        }
        values_[id]
            = ControlSpec::of(each).clamp(processor.parameterValue(each.id).value_or(each.defaultValue));
        parameters_[id] = std::move(each);
    }

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    buildHeader(std::move(presets));

    body_ = new QWidget(this);
    auto* bodyLayout = new QVBoxLayout(body_);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    body_->installEventFilter(this);
    column->addWidget(body_, 1);

    panel_ = new ControlPanel(body_);
    panel_->installEventFilter(this); // its controls change the size it needs
    footer_ = new FooterBar(this);
    column->addWidget(footer_);

    slots_.fill(Snapshot{.values = values_, .preset = presets_->current(), .modified = false});
    slotButtons_[0]->setChecked(true);
    setMinimumSize(640, 400);
    resize(900, 480);
}

BuiltinEditorWindow::~BuiltinEditorWindow()
{
    FrameClock::leave(this);
    if (metering_)
        instance_->setScopeEnabled(false);
}

void BuiltinEditorWindow::buildHeader(std::vector<Preset> presets)
{
    auto* header = new QFrame(this);
    header->setObjectName(QStringLiteral("pluginEditorHeader"));
    header->setFixedHeight(style::kHeaderHeight);
    auto* row = new QHBoxLayout(header);
    row->setContentsMargins(12, 0, 12, 0);
    row->setSpacing(10);

    power_ = new PowerButton(header);
    power_->setObjectName(QStringLiteral("bypassButton"));
    power_->setFixedSize(26, 26);
    power_->setCheckable(true);
    power_->setChecked(true);
    power_->setFocusPolicy(Qt::NoFocus);
    connect(power_, &QToolButton::clicked, this, [this](bool on) { emit bypassToggled(!on); });
    row->addWidget(power_);
    setBypassed(false); // its tooltip

    const vloom::PluginInfo info = instance_->processor().info();
    auto* name = new QLabel(QString::fromStdString(info.name), header);
    name->setObjectName(QStringLiteral("builtinEditorTitle"));
    name->setFont(style::font(font(), 13, QFont::Bold));
    row->addWidget(name);
    row->addStretch(1);

    // Presets: "Default", the editor's (the values that differ from the
    // defaults) and the user's, in a file named after the plugin.
    std::vector<PresetBar::Factory> factory;
    const auto defaults = [this] {
        PresetBar::Values all(parameters_.size(), 0.0);
        for (std::size_t id = 0; id < parameters_.size(); ++id)
            all[id] = parameters_[id].defaultValue;
        return all;
    };
    factory.push_back(PresetBar::Factory{.name = tr("Default"), .values = defaults()});
    for (Preset& each : presets) {
        PresetBar::Factory preset{.name = std::move(each.name), .values = defaults()};
        for (const auto& [id, v] : each.values)
            if (id < preset.values.size())
                preset.values[id] = ControlSpec::of(parameters_[id]).clamp(v);
        factory.push_back(std::move(preset));
    }
    presets_ = new PresetBar(QString::fromStdString(info.id), std::move(factory),
        PresetBar::Source{
            .read = [this] { return PresetBar::Values(values_.begin(), values_.end()); },
            .apply =
                [this](const PresetBar::Values& all) {
                    std::vector<double> next = values_;
                    std::copy_n(all.begin(), std::min(all.size(), next.size()), next.begin());
                    applyAll(next);
                },
            .clamp =
                [this](std::uint32_t id, double v) {
                    const vloom::ParameterInfo* known = parameter(id);
                    return known != nullptr ? ControlSpec::of(*known).clamp(v) : v;
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
    row->addWidget(presets_);
    row->addStretch(1);

    // A and B: two settings to switch between and compare.
    auto* group = new QFrame(header);
    group->setObjectName(QStringLiteral("slotGroup"));
    group->setFixedHeight(26);
    auto* slotRow = new QHBoxLayout(group);
    slotRow->setContentsMargins(2, 2, 2, 2);
    slotRow->setSpacing(2);
    for (int slot = 0; slot < 2; ++slot) {
        auto* button = new QToolButton(group);
        button->setText(slot == 0 ? QStringLiteral("A") : QStringLiteral("B"));
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedSize(26, 20);
        button->setToolTip(
            slot == 0 ? tr("Setting A: switch to B to compare") : tr("Setting B: switch to A to compare"));
        button->setAccessibleName(slot == 0 ? tr("Setting A") : tr("Setting B"));
        connect(button, &QToolButton::clicked, this, [this, slot] { chooseSlot(slot); });
        slotRow->addWidget(button);
        slotButtons_[static_cast<std::size_t>(slot)] = button;
    }
    row->addWidget(group);
    layout()->addWidget(header);
}

const vloom::ParameterInfo* BuiltinEditorWindow::parameter(std::uint32_t id) const
{
    return id < parameters_.size() && parameters_[id].maxValue > parameters_[id].minValue ? &parameters_[id]
                                                                                          : nullptr;
}

double BuiltinEditorWindow::value(std::uint32_t id) const { return id < values_.size() ? values_[id] : 0.0; }

void BuiltinEditorWindow::setDisplay(QWidget* display)
{
    Q_ASSERT(display_ == nullptr);
    display_ = display;
    display->setParent(body_);
    body_->layout()->addWidget(display);
    panel_->raise();
}

ArcKnob* BuiltinEditorWindow::knob(
    std::uint32_t id, const QString& label, std::optional<ParameterScale> scale)
{
    const vloom::ParameterInfo* known = parameter(id);
    Q_ASSERT(known != nullptr);
    auto* control
        = new ArcKnob(label.isEmpty() && known != nullptr ? QString::fromStdString(known->name) : label);
    if (known == nullptr)
        return control;
    control->setSpec(ControlSpec::of(*known, scale));
    connect(control, &ParameterControl::valueEdited, this, [this, id](double v) { change(id, v); });
    bind(id, [control](double v) { control->setValue(v); });
    return control;
}

PillToggle* BuiltinEditorWindow::toggle(std::uint32_t id, const QString& label, PillToggle::Layout layout)
{
    const vloom::ParameterInfo* known = parameter(id);
    Q_ASSERT(known != nullptr);
    auto* control = new PillToggle(
        label.isEmpty() && known != nullptr ? QString::fromStdString(known->name) : label, layout);
    if (known == nullptr)
        return control;
    connect(control, &PillToggle::clicked, this, [this, id](bool on) { change(id, on ? 1.0 : 0.0); });
    bind(id, [control](double v) { control->setChecked(v >= 0.5); });
    return control;
}

Readout* BuiltinEditorWindow::readout(
    std::uint32_t id, const QString& label, std::optional<ParameterScale> scale)
{
    const vloom::ParameterInfo* known = parameter(id);
    Q_ASSERT(known != nullptr);
    auto* control
        = new Readout(label.isEmpty() && known != nullptr ? QString::fromStdString(known->name) : label);
    if (known == nullptr)
        return control;
    control->setSpec(ControlSpec::of(*known, scale));
    connect(control, &ParameterControl::valueEdited, this, [this, id](double v) { change(id, v); });
    bind(id, [control](double v) { control->setValue(v); });
    return control;
}

Readout* BuiltinEditorWindow::readout(const QString& label) { return new Readout(label); }

void BuiltinEditorWindow::bind(std::uint32_t id, std::function<void(double)> show)
{
    show(value(id));
    shows_.emplace_back(id, std::move(show));
}

void BuiltinEditorWindow::change(std::uint32_t id, double v)
{
    const vloom::ParameterInfo* known = parameter(id);
    if (known == nullptr)
        return;
    v = ControlSpec::of(*known).clamp(v);
    if (v == values_[id])
        return;
    values_[id] = v;
    // The built-in takes the value at once; one dropped by a full queue
    // is sent again with the next change, as in ParameterEditorWindow.
    (void)instance_->setParameter(id, v);
    for (const auto& [bound, show] : shows_)
        if (bound == id)
            show(v);
    presets_->markModified();
    parameterChanged(id, v);
    emit edited();
}

void BuiltinEditorWindow::parameterChanged(std::uint32_t /*id*/, double /*value*/) { }

void BuiltinEditorWindow::applyAll(const std::vector<double>& all)
{
    for (std::uint32_t id = 0; id < all.size(); ++id)
        change(id, all[id]);
}

void BuiltinEditorWindow::chooseSlot(int slot)
{
    if (slot != slot_) {
        slots_[static_cast<std::size_t>(slot_)]
            = Snapshot{.values = values_, .preset = presets_->current(), .modified = presets_->isModified()};
        slot_ = slot;
        const Snapshot heard = slots_[static_cast<std::size_t>(slot)];
        applyAll(heard.values);
        presets_->restore(heard.preset, heard.modified);
    }
    for (int i = 0; i < 2; ++i)
        slotButtons_[static_cast<std::size_t>(i)]->setChecked(i == slot_);
}

void BuiltinEditorWindow::setBypassed(bool bypassed)
{
    bypassed_ = bypassed;
    power_->setChecked(!bypassed);
    // An instrument (the sampler, the drum sampler) is switched off, as SynthEditorWindow says.
    if (instance_->processor().info().kind == vloom::PluginInfo::Kind::Instrument) {
        power_->setToolTip(
            bypassed ? tr("Instrument off (click to turn on)") : tr("Instrument on (click to turn off)"));
        power_->setAccessibleName(bypassed ? tr("Instrument off") : tr("Instrument on"));
    } else {
        power_->setToolTip(
            bypassed ? tr("Effect bypassed (click to turn on)") : tr("Effect on (click to bypass)"));
        power_->setAccessibleName(bypassed ? tr("Effect bypassed") : tr("Effect on"));
    }
    power_->update();
}

void BuiltinEditorWindow::setTempo(double bpm)
{
    if (!std::isfinite(bpm) || !(bpm > 0.0) || bpm == tempo_)
        return;
    tempo_ = bpm;
    tempoChanged(bpm);
}

void BuiltinEditorWindow::tempoChanged(double /*bpm*/) { }

void BuiltinEditorWindow::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        if (!isMinimized())
            awake_ = true;
        updateAnimation(); // none while minimised
    }
}

bool BuiltinEditorWindow::eventFilter(QObject* watched, QEvent* event)
{
    if ((watched == body_ && event->type() == QEvent::Resize)
        || (watched == panel_ && event->type() == QEvent::LayoutRequest))
        placePanel();
    return QWidget::eventFilter(watched, event);
}

void BuiltinEditorWindow::placePanel()
{
    const QSize size = panel_->sizeHint();
    panel_->setGeometry((body_->width() - size.width()) / 2,
        body_->height() - size.height() - style::kPanelMargin, size.width(), size.height());
    panel_->raise();
}

void BuiltinEditorWindow::startMetering()
{
    if (metering_)
        return;
    metering_ = true;
    scope_.resize(vloom::PluginInstance::kScopeFrames);
    meterTimer_ = new QTimer(this);
    meterTimer_->setInterval(kMeterMs);
    connect(meterTimer_, &QTimer::timeout, this, &BuiltinEditorWindow::readMeters);
    if (isVisible()) {
        instance_->setScopeEnabled(true);
        sinceReading_.start();
        meterTimer_->start();
    }
}

void BuiltinEditorWindow::meter(const Meter& /*reading*/) { }

void BuiltinEditorWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    placePanel();
    setFocus(Qt::OtherFocusReason);
    if (!animated_.empty()) {
        AnimationSettings::instance().refreshSystem(); // the user may have changed it meanwhile
        awake_ = true;
        updateAnimation();
    }
    if (!metering_)
        return;
    // The plugin copies its output for the meters only while they are looked at.
    instance_->setScopeEnabled(true);
    lastTail_.clear();
    sinceReading_.start();
    meterTimer_->start();
}

void BuiltinEditorWindow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    updateAnimation(); // no frames while hidden
    if (!metering_)
        return;
    meterTimer_->stop();
    instance_->setScopeEnabled(false);
    lastTail_.clear();
}

void BuiltinEditorWindow::readMeters()
{
    // What the plugin put out since the last reading.
    const double seconds = static_cast<double>(sinceReading_.restart()) / 1000.0;
    bool running = false;
    const std::span<const float> newest
        = readNewest(*instance_, seconds, sampleRate_, scope_, lastTail_, running);

    Meter reading;
    reading.running = running;
    if (running) {
        reading.outputDb = peakDb(newest);
        if (const auto reduction = instance_->processor().gainReductionDb())
            reading.reductionDb = bypassed_ ? 0.0 : std::max(0.0, static_cast<double>(*reduction));
    }
    meter(reading);
    // Sound again: the animated displays wake up.
    if (running && !awake_ && !animated_.empty()) {
        awake_ = true;
        updateAnimation();
    }
}

// --- Animation -------------------------------------------------------------------

bool BuiltinEditorWindow::isAnimating() const
{
    return !animated_.empty() && FrameClock::instance().contains(this);
}

void BuiltinEditorWindow::animate(Animated* display)
{
    Q_ASSERT(display != nullptr);
    if (display == nullptr || std::find(animated_.begin(), animated_.end(), display) != animated_.end())
        return;
    animated_.push_back(display);
    if (animated_.size() == 1) {
        frameScope_.resize(vloom::PluginInstance::kScopeFrames);
        connect(&AnimationSettings::instance(), &AnimationSettings::changed, this,
            &BuiltinEditorWindow::animationSettingChanged);
        if (qEnvironmentVariableIsSet("VLOOMDAW_ANIMATION_STATS"))
            showAnimationStats();
    }
    display->setAnimationOn(AnimationSettings::instance().enabled());
    startMetering();
    awake_ = true;
    updateAnimation();
}

void BuiltinEditorWindow::wakeAnimation()
{
    if (animated_.empty() || awake_)
        return;
    awake_ = true;
    updateAnimation();
}

void BuiltinEditorWindow::updateAnimation()
{
    if (animated_.empty())
        return;
    const bool want = awake_ && AnimationSettings::instance().enabled() && isVisible() && !isMinimized();
    FrameClock& clock = FrameClock::instance();
    const bool joined = clock.contains(this);
    if (want && !joined) {
        // What the scope holds now is old sound: only what comes after it counts.
        bool running = false;
        frameTail_.clear();
        (void)readNewest(*instance_, 0.0, sampleRate_, frameScope_, frameTail_, running);
        sinceAudio_ = kAudioHoldSeconds;
        clock.add(this);
    } else if (!want && joined) {
        clock.remove(this);
        features_.reset();
        for (Animated* each : animated_)
            each->rest();
    }
}

void BuiltinEditorWindow::frame(double dt)
{
    bool running = false;
    const std::span<const float> newest
        = readNewest(*instance_, std::max(dt, 0.001), sampleRate_, frameScope_, frameTail_, running);
    sinceAudio_ = running ? 0.0 : sinceAudio_ + dt;
    running = running || sinceAudio_ < kAudioHoldSeconds; // the newest frames are still the newest
    motion::AudioInput input;
    input.running = running;
    if (running) {
        input.mono = newest;
        if (const auto reduction = instance_->processor().gainReductionDb())
            input.reductionDb = bypassed_ ? 0.0 : std::max(0.0, static_cast<double>(*reduction));
    }
    features_.update(input, dt);

    bool moving = false;
    for (Animated* each : animated_)
        moving = each->advance(features_, dt) || moving;
    // Nothing plays and nothing moves (or it has been quiet long enough):
    // the clock may idle until the sound comes back.
    if (features_.settled()
        && (!moving || features_.silentSeconds() >= motion::AudioFeatures::kSettleSeconds + kSettleGrace)) {
        awake_ = false;
        updateAnimation();
    }
}

void BuiltinEditorWindow::animationSettingChanged(bool enabled)
{
    for (Animated* each : animated_)
        each->setAnimationOn(enabled);
    awake_ = enabled;
    updateAnimation();
}

void BuiltinEditorWindow::showAnimationStats()
{
    // A debug overlay: what the animated displays cost, once a second.
    statsLabel_ = new QLabel(body_);
    statsLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    statsLabel_->setStyleSheet(
        QStringLiteral("color: #9aa4b2; background: rgba(0,0,0,140); padding: 2px 6px;"));
    statsLabel_->move(8, 8);
    statsTimer_ = new QTimer(this);
    statsTimer_->setInterval(1000);
    connect(statsTimer_, &QTimer::timeout, this, [this, last = FrameClock::Stats{}]() mutable {
        const FrameClock::Stats now = FrameClock::instance().stats();
        const auto frames = static_cast<double>(now.ticks - last.ticks);
        const auto paints = static_cast<double>(now.paints - last.paints);
        const double paintMs = paints > 0.0 ? (now.paintSeconds - last.paintSeconds) * 1000.0 / paints : 0.0;
        const double tickMs = frames > 0.0 ? (now.tickSeconds - last.tickSeconds) * 1000.0 / frames : 0.0;
        // The share of one core over the last second: painting plus the frames' work.
        const double load
            = (now.paintSeconds - last.paintSeconds + now.tickSeconds - last.tickSeconds) * 100.0;
        statsLabel_->setText(QStringLiteral("%1 fps · paint %2 ms · frame %3 ms · %4 % CPU")
                .arg(frames, 0, 'f', 0)
                .arg(paintMs, 0, 'f', 2)
                .arg(tickMs, 0, 'f', 2)
                .arg(load, 0, 'f', 1));
        statsLabel_->adjustSize();
        statsLabel_->raise();
        last = now;
    });
    statsTimer_->start();
}

} // namespace vloomdaw
