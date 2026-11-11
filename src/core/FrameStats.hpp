// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <deque>

namespace vloomdaw {

/// Rolling UI performance figures over the last second, for the debug
/// overlay: frames (window repaints) and how long they took, and the
/// spacing of the UI refresh timer (late ticks mean a busy UI thread).
///
/// Times are in seconds on any monotonic clock; durations in milliseconds.
class FrameStats {
public:
    static constexpr double kWindowSeconds = 1.0;

    struct Snapshot {
        double fps = 0.0; ///< frames in the last second
        double paintAvgMs = 0.0;
        double paintMaxMs = 0.0;
        double tickAvgMs = 0.0; ///< spacing of refresh ticks
        double tickMaxMs = 0.0;
        double workAvgMs = 0.0; ///< UI work per refresh tick (reading the engine, updating widgets)
        /// Rough frame rate the UI could reach if nothing else limited it:
        /// 1000 / (average paint + average tick work). 0 when unknown.
        double headroomFps = 0.0;
    };

    /// A frame finished at `now` after `paintMs` of painting.
    void addFrame(double now, double paintMs);
    /// The refresh timer fired at `now`; handling it took `workMs`.
    void addTick(double now, double workMs = 0.0);
    /// Figures over (now - kWindowSeconds, now]; also forgets older samples.
    [[nodiscard]] Snapshot snapshot(double now);

private:
    struct Frame {
        double time;
        double paintMs;
    };
    void prune(double now);

    std::deque<Frame> frames_;
    struct Tick {
        double time;
        double workMs;
    };
    std::deque<Tick> ticks_;
};

} // namespace vloomdaw
