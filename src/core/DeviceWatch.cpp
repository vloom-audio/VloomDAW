// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/DeviceWatch.hpp"

namespace vloomdaw {

void DeviceWatch::opened(bool preferred, std::int64_t nowMs) noexcept
{
    state_ = preferred || preferredIsDefault_ ? State::Preferred : State::Fallback;
    lastPollMs_ = nowMs;
}

void DeviceWatch::openFailed(std::int64_t nowMs) noexcept
{
    state_ = State::Closed;
    lastPollMs_ = nowMs;
}

DeviceWatch::Action DeviceWatch::tick(
    bool running, std::int64_t nowMs, const std::function<bool()>& preferredPresent)
{
    // Lost while playing: fall back at once, no waiting for the poll.
    if (state_ != State::Closed && !running) {
        state_ = State::Closed;
        lastPollMs_ = nowMs;
        return Action::OpenDefault;
    }
    if (state_ == State::Preferred || nowMs - lastPollMs_ < kPollMs)
        return Action::None;
    lastPollMs_ = nowMs;

    // On the fallback, or with nothing open: is the user's device back?
    if (!preferredIsDefault_ && preferredPresent && preferredPresent())
        return Action::OpenPreferred;
    return state_ == State::Closed ? Action::OpenDefault : Action::None;
}

} // namespace vloomdaw
