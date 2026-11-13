// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

namespace vloom {
class SampleBuffer;
}

namespace vloomdaw {

/// What detectTempo() heard.
struct TempoGuess {
    double bpm = 0.0; ///< 0: no steady beat found
    /// How clearly the beat stands out, 0..1 (below about 0.2, a guess).
    double confidence = 0.0;
};

/// Measures the tempo of `sample` (a loop or a song) from its audio alone.
///
/// The attacks (rises in loudness) are found, and the beat is the spacing
/// at which they repeat best, between 70 and 180 BPM, the range dance and
/// pop loops are written in (a loop at 200 is heard as 100). When the
/// sample is a whole number of bars long at about that tempo, as a loop
/// usually is, the tempo is made exact so the bars fit it to the sample;
/// otherwise a tempo within a third of a BPM of a whole number is rounded
/// to it (loops are nearly always made at a whole tempo).
///
/// Slow for long files (a song takes well under a second): call it off the
/// UI thread.
[[nodiscard]] TempoGuess detectTempo(const vloom::SampleBuffer& sample);

} // namespace vloomdaw
