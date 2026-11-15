// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace vloomdaw {

/// The spectrum an editor draws behind its curve: the level of a signal per
/// frequency, rising at once and falling slowly so it is readable.
///
/// Give it the newest frames of the signal at each redraw (analyse()); ask
/// for the level of each column of pixels (levelDb()). A full-scale sine
/// reads 0 dB. Not for the audio thread.
class SpectrumAnalyzer {
public:
    static constexpr std::size_t kFrames = 4096; ///< analysed per call
    static constexpr double kFloorDb = -120.0; ///< silence, and the least any level reads
    static constexpr double kFallDb = 1.5; ///< how far a level drops per analyse() once the sound is gone

    explicit SpectrumAnalyzer(double sampleRate);

    /// Analyses the newest kFrames of `signal` (shorter: as if silence came
    /// before it) and moves the levels: up to the new ones at once, down by
    /// kFallDb.
    void analyse(std::span<const float> signal);
    /// Every level back to silence.
    void clear();

    /// The highest level between `fromHz` and `toHz`, in dB; between two
    /// analysed frequencies it is read off the line joining them.
    [[nodiscard]] double levelDb(double fromHz, double toHz) const;
    /// False once every level has fallen to the floor: nothing to draw.
    [[nodiscard]] bool hasSignal() const noexcept { return audible_; }

private:
    double sampleRate_;
    std::vector<float> window_; ///< Hann, kFrames long
    double windowSum_ = 0.0;
    std::vector<float> real_;
    std::vector<float> imaginary_;
    std::vector<double> levels_; ///< dB per bin, 0 .. kFrames / 2
    bool audible_ = false;
};

} // namespace vloomdaw
