// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/EditorMotion.hpp"

#include <vloom/engine/plugin/PluginInstance.hpp>

#include <QEvent>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace vloomdaw {

namespace {

constexpr int kListenMs = 33; ///< between two readings while the clock idles
constexpr std::size_t kTailFrames = 64; ///< compared between readings to tell whether audio runs
/// As BuiltinEditorWindow: silent this long after settling, the clock idles
/// even if a display still says it moves.
constexpr double kSettleGrace = 1.0;
/// A frame with no new audio within this long of one with some still runs.
constexpr double kAudioHoldSeconds = 0.1;

} // namespace

EditorMotion::EditorMotion(
    std::shared_ptr<vloom::PluginInstance> instance, double sampleRate, QWidget* window, bool manageScope)
    : QObject(window)
    , instance_(std::move(instance))
    , sampleRate_(sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 48000.0)
    , window_(window)
    , manageScope_(manageScope)
    , scope_(vloom::PluginInstance::kScopeFrames)
{
    listener_ = new QTimer(this);
    listener_->setInterval(kListenMs);
    connect(listener_, &QTimer::timeout, this, &EditorMotion::listen);
    connect(&AnimationSettings::instance(), &AnimationSettings::changed, this, &EditorMotion::settingChanged);
    window_->installEventFilter(this);
}

EditorMotion::~EditorMotion()
{
    FrameClock::leave(this);
    if (manageScope_)
        instance_->setScopeEnabled(false);
}

void EditorMotion::add(Animated* display)
{
    if (display == nullptr || std::find(displays_.begin(), displays_.end(), display) != displays_.end())
        return;
    displays_.push_back(display);
    display->setAnimationOn(AnimationSettings::instance().enabled());
    awake_ = true;
    updateClock();
}

bool EditorMotion::isAnimating() const { return FrameClock::instance().contains(this); }

bool EditorMotion::wanted() const
{
    return !displays_.empty() && AnimationSettings::instance().enabled() && window_->isVisible()
        && !window_->isMinimized();
}

bool EditorMotion::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == window_) {
        switch (event->type()) {
        case QEvent::Show:
            AnimationSettings::instance().refreshSystem(); // the user may have changed it meanwhile
            awake_ = true;
            updateClock();
            break;
        case QEvent::Hide:
            updateClock();
            break;
        case QEvent::WindowStateChange:
            if (!window_->isMinimized())
                awake_ = true;
            updateClock();
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}

std::span<const float> EditorMotion::readNewest(double seconds, std::vector<float>& tail, bool& running)
{
    const auto frames = static_cast<std::size_t>(std::clamp(
        std::lround(seconds * sampleRate_), 1L, static_cast<long>(vloom::PluginInstance::kScopeFrames)));
    const std::size_t count
        = instance_->readScope(std::span<float>(scope_.data(), std::max(frames, kTailFrames)));
    const std::span<const float> newest(scope_.data(), count);
    // The same frames as last time: no audio runs (the scope keeps the last ones).
    const std::span<const float> end = newest.last(std::min(count, kTailFrames));
    running = count > 0 && !(end.size() == tail.size() && std::equal(end.begin(), end.end(), tail.begin()));
    tail.assign(end.begin(), end.end());
    return newest.last(std::min(count, frames));
}

void EditorMotion::updateClock()
{
    const bool want = wanted();
    if (manageScope_)
        instance_->setScopeEnabled(want);
    FrameClock& clock = FrameClock::instance();
    const bool joined = clock.contains(this);
    if (want && awake_ && !joined) {
        listener_->stop();
        // What the scope holds now is old sound: only what comes after it counts.
        bool running = false;
        frameTail_.clear();
        (void)readNewest(0.0, frameTail_, running);
        sinceAudio_ = kAudioHoldSeconds;
        clock.add(this);
    } else if (!(want && awake_) && joined) {
        clock.remove(this);
        features_.reset();
        for (Animated* each : displays_)
            each->rest();
    }
    if (want && !awake_ && !listener_->isActive()) {
        listenTail_.clear();
        bool running = false;
        (void)readNewest(0.0, listenTail_, running);
        sinceListen_.start();
        listener_->start();
    } else if (!want) {
        listener_->stop();
    }
}

void EditorMotion::listen()
{
    bool running = false;
    (void)readNewest(static_cast<double>(sinceListen_.restart()) / 1000.0, listenTail_, running);
    if (running && !awake_) {
        awake_ = true;
        updateClock();
    }
}

void EditorMotion::frame(double dt)
{
    bool running = false;
    const std::span<const float> newest = readNewest(std::max(dt, 0.001), frameTail_, running);
    sinceAudio_ = running ? 0.0 : sinceAudio_ + dt;
    running = running || sinceAudio_ < kAudioHoldSeconds;
    motion::AudioInput input;
    input.running = running;
    if (running)
        input.mono = newest;
    features_.update(input, dt);

    bool moving = false;
    for (Animated* each : displays_)
        moving = each->advance(features_, dt) || moving;
    if (features_.settled()
        && (!moving || features_.silentSeconds() >= motion::AudioFeatures::kSettleSeconds + kSettleGrace)) {
        awake_ = false;
        updateClock();
    }
}

void EditorMotion::settingChanged(bool enabled)
{
    for (Animated* each : displays_)
        each->setAnimationOn(enabled);
    awake_ = enabled;
    updateClock();
}

} // namespace vloomdaw
