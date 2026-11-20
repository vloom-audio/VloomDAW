// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/TunerReading.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace vloomdaw {

TunerNote tunerNote(double midiNote, double cents)
{
    static constexpr std::array<const char*, 12> kNames{
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (!std::isfinite(midiNote) || !std::isfinite(cents))
        return {};
    const double nearest = std::round(midiNote - cents / 100.0);
    if (nearest < 0.0 || nearest > 127.0)
        return {};
    const int midi = static_cast<int>(nearest);
    return TunerNote{kNames[static_cast<std::size_t>(midi % 12)], midi / 12 - 1, midi};
}

bool inTune(double cents) noexcept { return std::isfinite(cents) && std::abs(cents) <= kInTuneCents; }

NeedleSmoother::NeedleSmoother(double seconds) noexcept
    : seconds_(std::isfinite(seconds) && seconds > 0.0 ? seconds : 0.0)
{
}

double NeedleSmoother::step(double cents, int midi, double elapsedSeconds) noexcept
{
    if (!std::isfinite(cents) || midi < 0) {
        reset();
        return value_;
    }
    const double target = std::clamp(cents, -50.0, 50.0);
    if (midi != midi_ || seconds_ <= 0.0) {
        midi_ = midi;
        value_ = target;
        return value_;
    }
    const double elapsed = std::isfinite(elapsedSeconds) ? std::max(elapsedSeconds, 0.0) : 0.0;
    value_ += (1.0 - std::exp(-elapsed / seconds_)) * (target - value_);
    return value_;
}

void NeedleSmoother::reset() noexcept
{
    midi_ = -1;
    value_ = 0.0;
}

} // namespace vloomdaw
