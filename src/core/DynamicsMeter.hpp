// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace vloomdaw {

/// A level in a history that has none: no audio was running then.
inline constexpr double kNoLevel = std::numeric_limits<double>::quiet_NaN();

/// The peak level of `samples` in dB full scale; -infinity for silence or
/// none.
[[nodiscard]] double peakDb(std::span<const float> samples) noexcept;

/// The last `capacity` levels of a scrolling display, oldest first. A
/// level may be kNoLevel (a gap) or -infinity (silence). Sized once; push()
/// never allocates.
class LevelHistory {
public:
    explicit LevelHistory(std::size_t capacity);

    /// Adds the newest level; the oldest falls off when full.
    void push(double level) noexcept;
    /// Empties it.
    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return levels_.size(); }
    /// The level `index` from the oldest (index < size()).
    [[nodiscard]] double at(std::size_t index) const noexcept;
    /// The newest level; kNoLevel when empty.
    [[nodiscard]] double newest() const noexcept;

private:
    std::vector<double> levels_;
    std::size_t next_ = 0; ///< where the next push() goes
    std::size_t size_ = 0;
};

/// What the built-in compressor ("vloom.compressor") adds after its curve,
/// in dB: the makeup, plus with auto makeup half the reduction the curve
/// makes at 0 dBFS (as dsp::Compressor does).
[[nodiscard]] double compressorMakeupDb(
    double thresholdDb, double ratio, double kneeDb, double makeupDb, bool autoMakeup) noexcept;

/// The compressor's output level for a steady input of `inputDb`: its static
/// curve plus compressorMakeupDb(). What a transfer curve draws.
[[nodiscard]] double compressorOutputDb(double inputDb, double thresholdDb, double ratio, double kneeDb,
    double makeupDb, bool autoMakeup) noexcept;

/// The level that went into a dynamics processor, worked out from what came
/// out (`outputDb`, measured), the reduction it reports (`reductionDb`, >= 0)
/// and a fixed gain it adds (`addedGainDb`: makeup, input gain). Only true
/// for a processor that puts out nothing of its dry input (no mix). kNoLevel
/// when the output is not a level (silent or missing): the input is then
/// unknown.
[[nodiscard]] double inputFromOutputDb(double outputDb, double reductionDb, double addedGainDb) noexcept;

} // namespace vloomdaw
