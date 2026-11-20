// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace vloomdaw {

/// The pictures of the built-in sampler's and drum sampler's editors: a
/// sample's waveform, the envelope over it and the time along it.

/// The lowest and highest value of a slice of a sample (all channels).
struct PeakColumn {
    float low = 0.0f;
    float high = 0.0f;

    friend bool operator==(const PeakColumn&, const PeakColumn&) = default;
};

/// `columns` slices of a sample `frames` long in `channels` (each `frames`
/// values), the lowest and highest value of each, across channels; fewer
/// when the sample is shorter than `columns` frames, none for no frames.
[[nodiscard]] std::vector<PeakColumn> samplePeaks(
    std::span<const float* const> channels, std::int64_t frames, std::size_t columns);

/// The lowest and highest of `peaks` from `from` to `to`, fractions (0 .. 1)
/// of the whole sample: one column of a picture of it. At least the column
/// at `from`; {0, 0} without peaks.
[[nodiscard]] PeakColumn peakBetween(std::span<const PeakColumn> peaks, double from, double to) noexcept;

/// vloom-dsp's envelope (dsp::EnvelopeShape) `seconds` after a note starts:
/// up in a straight line over `attack` (at least 0.5 ms), then down towards
/// `sustain` (0 .. 1), reaching -43 dB of the way there after `decay` (at
/// least 2 ms). Before the note is let go.
[[nodiscard]] double envelopeLevel(double seconds, double attack, double decay, double sustain) noexcept;

/// The envelope `seconds` after the note was let go at `level`: down to
/// -43 dB of it over `release` (at least 2 ms).
[[nodiscard]] double releaseLevel(double level, double seconds, double release) noexcept;

/// How much faster than its own a sample plays `semitones` and `cents` up
/// (slower for down).
[[nodiscard]] double playbackSpeed(double semitones, double cents) noexcept;

/// The distance between the labels of a time line `seconds` long so that
/// at most `maxTicks` fit: 1, 2 or 5 times a power of ten, in seconds.
[[nodiscard]] double tickStep(double seconds, int maxTicks) noexcept;

/// A label of that time line: "0.5 s", "2.0 s", "250 ms", "12 s".
[[nodiscard]] std::string tickLabel(double seconds, double step);

/// A pan in percent (-100 left .. 100 right) as the designs read it: "C",
/// "20 L", "100 R".
[[nodiscard]] std::string panText(double percent);

} // namespace vloomdaw
