// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <string>

namespace vloomdaw {

/// The maths of the built-in tuner's editor (vloom.tuner): which note its
/// readouts name, and a needle that moves smoothly.

/// A note as a tuner names it.
struct TunerNote {
    std::string name; ///< "A", "C#"; empty: none
    int octave = 0; ///< scientific pitch: MIDI 69 is A4, 60 is C4
    int midi = -1; ///< the MIDI note; -1: none
};

/// How far either side of a note still reads as in tune, in cents (the
/// meter's green zone).
inline constexpr double kInTuneCents = 5.0;

/// The note the tuner's `note` readout (a fractional MIDI note) and `cents`
/// readout (off the nearest note) point at: the note `cents` is measured
/// from, so the two always agree, also half-way between two notes. None
/// for a value that is not finite or outside MIDI 0 .. 127.
[[nodiscard]] TunerNote tunerNote(double midiNote, double cents);

/// True if `cents` off is within kInTuneCents.
[[nodiscard]] bool inTune(double cents) noexcept;

/// The needle of the cents meter: it follows the readout with a one-pole
/// smoothing (63 % of the way in `seconds`), so 30 readings a second move it
/// gently rather than in jumps. A new note, or a pitch after none, puts it
/// where the reading is at once: it never sweeps across from another note.
class NeedleSmoother {
public:
    explicit NeedleSmoother(double seconds = 0.08) noexcept;

    /// A reading of `cents` off note `midi`, `elapsedSeconds` after the last
    /// one; returns where the needle is now (cents, -50 .. 50).
    double step(double cents, int midi, double elapsedSeconds) noexcept;
    /// No pitch: the next step() starts afresh.
    void reset() noexcept;
    [[nodiscard]] double value() const noexcept { return value_; }
    [[nodiscard]] bool active() const noexcept { return midi_ >= 0; }

private:
    double seconds_;
    double value_ = 0.0;
    int midi_ = -1; ///< the note it is on; -1: none
};

} // namespace vloomdaw
