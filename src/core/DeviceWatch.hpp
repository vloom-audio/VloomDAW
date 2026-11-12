// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <cstdint>
#include <functional>

namespace vloomdaw {

/// Decides what to do when the output device goes away or comes back.
///
/// Policy: when the device in use is lost (unplugged), play on through the
/// system default right away; while on that fallback (or with no output at
/// all), look for the user's chosen device every kPollMs and switch back as
/// soon as it is plugged in again. The user's choice itself never changes.
///
/// Qt-free and clock-free: the caller passes the time and does the switching.
class DeviceWatch {
public:
    static constexpr std::int64_t kPollMs = 2000;

    enum class Action {
        None,
        OpenDefault, ///< the device was lost (or nothing is open): use the system default
        OpenPreferred, ///< the user's device is back: switch to it
    };

    /// The user's choice: a specific device, or the system default.
    void setPreferredIsDefault(bool isDefault) noexcept { preferredIsDefault_ = isDefault; }

    /// A device was opened; `preferred` = it is the user's choice (or the
    /// user chose the default), false = a fallback. `nowMs` starts the poll.
    void opened(bool preferred, std::int64_t nowMs) noexcept;
    /// Opening failed: nothing plays. Polling retries.
    void openFailed(std::int64_t nowMs) noexcept;

    /// Call regularly. `running`: the stream is alive. `preferredPresent` is
    /// asked (at most every kPollMs) whether the user's device is listed.
    [[nodiscard]] Action tick(
        bool running, std::int64_t nowMs, const std::function<bool()>& preferredPresent);

    [[nodiscard]] bool onFallback() const noexcept { return state_ == State::Fallback; }

private:
    enum class State { Closed, Preferred, Fallback };

    State state_ = State::Closed;
    bool preferredIsDefault_ = true;
    std::int64_t lastPollMs_ = 0;
};

} // namespace vloomdaw
