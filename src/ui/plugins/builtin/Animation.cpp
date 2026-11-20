// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/Animation.hpp"

#include <QCoreApplication>
#include <QPointer>

#include <algorithm>

#if defined(Q_OS_WIN)
#include "ui/WinWindow.hpp"
#elif defined(Q_OS_MACOS)
#include "ui/MacWindow.hpp"
#endif

namespace vloomdaw {

namespace {

bool systemReducedMotion()
{
    // For renders and measurements on a machine with animation effects off.
    if (qEnvironmentVariableIsSet("VLOOMDAW_IGNORE_REDUCED_MOTION"))
        return false;
#if defined(Q_OS_WIN)
    return windowsPrefersReducedMotion();
#elif defined(Q_OS_MACOS)
    return macPrefersReducedMotion();
#else
    return false; // Qt has no reduced-motion hint yet; the user's setting decides
#endif
}

} // namespace

// --- AnimationSettings ---------------------------------------------------------

AnimationSettings::AnimationSettings()
    : reduced_(systemReducedMotion())
{
}

AnimationSettings& AnimationSettings::instance()
{
    static QPointer<AnimationSettings> settings;
    if (settings.isNull()) {
        settings = new AnimationSettings;
        settings->setParent(QCoreApplication::instance()); // goes with the application
    }
    return *settings;
}

void AnimationSettings::setUserEnabled(bool on)
{
    if (on == user_)
        return;
    const bool was = enabled();
    user_ = on;
    if (enabled() != was)
        emit changed(enabled());
}

void AnimationSettings::refreshSystem()
{
    const bool reduced = systemReducedMotion();
    if (reduced == reduced_)
        return;
    const bool was = enabled();
    reduced_ = reduced;
    if (enabled() != was)
        emit changed(enabled());
}

// --- FrameClock ----------------------------------------------------------------

FrameClock::FrameClock()
{
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(kIntervalMs);
    connect(&timer_, &QTimer::timeout, this, &FrameClock::tick);
}

namespace {

QPointer<FrameClock>& theClock()
{
    static QPointer<FrameClock> clock;
    return clock;
}

} // namespace

FrameClock& FrameClock::instance()
{
    QPointer<FrameClock>& clock = theClock();
    if (clock.isNull()) {
        clock = new FrameClock;
        clock->setParent(QCoreApplication::instance());
    }
    return *clock;
}

void FrameClock::leave(Client* client)
{
    if (FrameClock* clock = theClock().data())
        clock->remove(client);
}

void FrameClock::add(Client* client)
{
    if (client == nullptr || contains(client))
        return;
    clients_.push_back(client);
    if (!timer_.isActive()) {
        elapsed_.start();
        timer_.start();
    }
}

void FrameClock::remove(Client* client)
{
    std::erase(clients_, client);
    std::replace(calling_.begin(), calling_.end(), client, static_cast<Client*>(nullptr));
    if (clients_.empty())
        timer_.stop(); // nothing wants frames: no CPU
}

bool FrameClock::contains(const Client* client) const
{
    return std::find(clients_.begin(), clients_.end(), client) != clients_.end();
}

void FrameClock::tick()
{
    const double dt = std::clamp(static_cast<double>(elapsed_.nsecsElapsed()) * 1e-9, 0.0, kMaxFrameSeconds);
    elapsed_.start();
    QElapsedTimer work;
    work.start();
    calling_.assign(clients_.begin(), clients_.end()); // no allocation once grown
    for (Client* client : calling_)
        if (client != nullptr) // removed by an earlier one this tick
            client->frame(dt);
    ++stats_.ticks;
    stats_.tickSeconds += static_cast<double>(work.nsecsElapsed()) * 1e-9;
}

} // namespace vloomdaw
