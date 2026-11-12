// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/FrameStats.hpp"

#include <algorithm>
#include <cmath>

namespace vloomdaw {

namespace {
/// More samples than any real second can hold; caps memory if the caller
/// never asks for a snapshot.
constexpr std::size_t kMaxSamples = 4096;
} // namespace

void FrameStats::addFrame(double now, double paintMs)
{
    if (std::isnan(now) || std::isnan(paintMs))
        return;
    frames_.push_back({now, std::max(0.0, paintMs)});
    if (frames_.size() > kMaxSamples)
        frames_.pop_front();
}

void FrameStats::addTick(double now, double workMs)
{
    if (std::isnan(now))
        return;
    ticks_.push_back({now, std::isnan(workMs) ? 0.0 : std::max(0.0, workMs)});
    if (ticks_.size() > kMaxSamples)
        ticks_.pop_front();
}

void FrameStats::prune(double now)
{
    const double oldest = now - kWindowSeconds;
    while (!frames_.empty() && frames_.front().time <= oldest)
        frames_.pop_front();
    // Keep one tick before the window, so the first interval in it counts.
    while (ticks_.size() > 1 && ticks_[1].time <= oldest)
        ticks_.pop_front();
}

FrameStats::Snapshot FrameStats::snapshot(double now)
{
    prune(now);
    Snapshot s;
    s.fps = static_cast<double>(frames_.size()) / kWindowSeconds;
    if (!frames_.empty()) {
        double total = 0.0;
        for (const Frame& f : frames_) {
            total += f.paintMs;
            s.paintMaxMs = std::max(s.paintMaxMs, f.paintMs);
        }
        s.paintAvgMs = total / static_cast<double>(frames_.size());
    }
    if (ticks_.size() >= 2) {
        double total = 0.0;
        for (std::size_t i = 1; i < ticks_.size(); ++i) {
            const double ms = (ticks_[i].time - ticks_[i - 1].time) * 1000.0;
            total += ms;
            s.tickMaxMs = std::max(s.tickMaxMs, ms);
        }
        s.tickAvgMs = total / static_cast<double>(ticks_.size() - 1);
    }
    if (!ticks_.empty()) {
        double work = 0.0;
        for (const Tick& t : ticks_)
            work += t.workMs;
        s.workAvgMs = work / static_cast<double>(ticks_.size());
    }
    if (!frames_.empty()) {
        const double perFrame = s.paintAvgMs + s.workAvgMs;
        s.headroomFps = perFrame > 0.0 ? 1000.0 / perFrame : 0.0;
    }
    return s;
}

} // namespace vloomdaw
