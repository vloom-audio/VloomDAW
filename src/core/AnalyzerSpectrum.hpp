// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <vloom/dsp/Fft.hpp>

#include <complex>
#include <cstddef>
#include <span>
#include <vector>

namespace vloomdaw {

/// The spectrum the built-in analyzer's editor draws (vloom.analyzer, ADR
/// 0005 of vloom-dsp: the processor passes audio through and the host's UI
/// does the FFT from the plugin's audio tap).
///
/// - An FFT of fftSize() frames (dsp::Fft, Hann window): a full-scale sine
///   reads 0 dB at its frequency.
/// - Averaged per bin over averagingSeconds() (dsp::Analyzer::
///   averagingSeconds() of Speed): a one-pole average of the power, which
///   covers 63 % of a change in that time, whatever the redraw rate.
/// - Tilted by tiltDbPerOctave() (dsp::Analyzer::tiltDb(), 0 dB at 1 kHz)
///   when read.
/// - A peak-hold line: the highest average each bin reached since the hold
///   was last cleared.
/// - Frozen: analyse() leaves everything as it is.
///
/// Not for the audio thread (it allocates when the size changes).
class AnalyzerSpectrum {
public:
    static constexpr double kFloorDb = -120.0; ///< silence, and the least any level reads
    static constexpr int kDefaultFftSize = 4096;

    explicit AnalyzerSpectrum(double sampleRate);

    /// A power of two, 2 .. dsp::Fft::kMaxSize; anything else is ignored. A
    /// new size starts again from silence.
    void setFftSize(int frames);
    [[nodiscard]] int fftSize() const noexcept { return static_cast<int>(window_.size()); }
    void setAveragingSeconds(double seconds) noexcept;
    [[nodiscard]] double averagingSeconds() const noexcept { return averagingSeconds_; }
    void setTiltDbPerOctave(double dbPerOctave) noexcept;
    [[nodiscard]] double tiltDbPerOctave() const noexcept { return tilt_; }
    /// Off clears the held line.
    void setPeakHold(bool on) noexcept;
    [[nodiscard]] bool peakHold() const noexcept { return peakHold_; }
    void setFrozen(bool frozen) noexcept { frozen_ = frozen; }
    [[nodiscard]] bool frozen() const noexcept { return frozen_; }

    /// Analyses the newest fftSize() frames of `signal` (shorter: as if
    /// silence came before it; empty: silence) and moves the averages
    /// `elapsedSeconds` on (the time since the last call). Nothing while
    /// frozen.
    void analyse(std::span<const float> signal, double elapsedSeconds);
    /// Every level and the held line back to silence.
    void clear();
    /// The held line back to the levels now.
    void clearPeaks();

    /// The highest averaged level between `fromHz` and `toHz` in dB, tilted
    /// at their middle (geometric); between two analysed frequencies it is
    /// read off the line joining them.
    [[nodiscard]] double levelDb(double fromHz, double toHz) const;
    /// The same for the held line; kFloorDb while Peak Hold is off.
    [[nodiscard]] double peakDb(double fromHz, double toHz) const;
    /// False once every average has fallen to the floor: nothing to draw.
    [[nodiscard]] bool hasSignal() const noexcept { return audible_; }

private:
    [[nodiscard]] double read(const std::vector<double>& levels, double fromHz, double toHz) const;

    double sampleRate_;
    double averagingSeconds_ = 0.25;
    double tilt_ = 0.0;
    bool peakHold_ = true;
    bool frozen_ = false;
    bool audible_ = false;
    vloom::dsp::Fft fft_;
    std::vector<double> window_; ///< Hann, fftSize() long
    double windowSum_ = 0.0;
    std::vector<std::complex<double>> buffer_;
    std::vector<double> power_; ///< averaged power per bin, 0 .. fftSize() / 2
    std::vector<double> levels_; ///< power_ in dB
    std::vector<double> peaks_; ///< the held line, dB
};

} // namespace vloomdaw
