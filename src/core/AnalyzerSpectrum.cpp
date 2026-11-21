// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/AnalyzerSpectrum.hpp"

#include <vloom/dsp/effects/Analyzer.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace vloomdaw {

namespace {

/// kFloorDb (-120 dB) as a power. Below it a bin is silent; it is then set
/// to 0 so the average never runs into denormals.
constexpr double kFloorPower = 1e-12;
static_assert(AnalyzerSpectrum::kFloorDb == -120.0);

} // namespace

AnalyzerSpectrum::AnalyzerSpectrum(double sampleRate)
    : sampleRate_(std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 48000.0)
{
    setFftSize(kDefaultFftSize);
}

void AnalyzerSpectrum::setFftSize(int frames)
{
    if (frames < 2 || !vloom::dsp::Fft::isPowerOfTwo(static_cast<std::size_t>(frames))
        || static_cast<std::size_t>(frames) > vloom::dsp::Fft::kMaxSize || frames == fftSize())
        return;
    const auto n = static_cast<std::size_t>(frames);
    fft_.prepare(n);
    window_.assign(n, 0.0);
    windowSum_ = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        window_[i]
            = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n));
        windowSum_ += window_[i];
    }
    buffer_.assign(n, {});
    const std::size_t bins = n / 2 + 1;
    power_.assign(bins, 0.0);
    levels_.assign(bins, kFloorDb);
    peaks_.assign(bins, kFloorDb);
    audible_ = false;
}

void AnalyzerSpectrum::setAveragingSeconds(double seconds) noexcept
{
    if (std::isfinite(seconds) && seconds >= 0.0)
        averagingSeconds_ = seconds;
}

void AnalyzerSpectrum::setTiltDbPerOctave(double dbPerOctave) noexcept
{
    if (std::isfinite(dbPerOctave))
        tilt_ = dbPerOctave;
}

void AnalyzerSpectrum::setPeakHold(bool on) noexcept
{
    peakHold_ = on;
    if (!on)
        std::fill(peaks_.begin(), peaks_.end(), kFloorDb);
}

void AnalyzerSpectrum::analyse(std::span<const float> signal, double elapsedSeconds)
{
    if (frozen_ || window_.empty())
        return;
    const std::size_t n = window_.size();
    if (signal.size() > n)
        signal = signal.last(n);
    const std::size_t lead = n - signal.size(); // silence before a short signal
    std::fill(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(lead), std::complex<double>{});
    for (std::size_t i = 0; i < signal.size(); ++i) {
        const double sample = std::isfinite(signal[i]) ? static_cast<double>(signal[i]) : 0.0;
        buffer_[lead + i] = {sample * window_[lead + i], 0.0};
    }
    fft_.forward(buffer_);

    // The one-pole average: 63 % of the way in averagingSeconds_.
    const double elapsed = std::isfinite(elapsedSeconds) ? std::clamp(elapsedSeconds, 0.0, 60.0) : 0.0;
    const double share = averagingSeconds_ > 0.0 ? 1.0 - std::exp(-elapsed / averagingSeconds_) : 1.0;
    const double scale = 2.0 / windowSum_; // a full-scale sine: 0 dB
    audible_ = false;
    for (std::size_t bin = 0; bin < power_.size(); ++bin) {
        const double magnitude = std::abs(buffer_[bin]) * scale;
        double& power = power_[bin];
        power += share * (magnitude * magnitude - power);
        if (!(power > kFloorPower))
            power = 0.0;
        levels_[bin] = power > 0.0 ? std::max(kFloorDb, 10.0 * std::log10(power)) : kFloorDb;
        if (peakHold_)
            peaks_[bin] = std::max(peaks_[bin], levels_[bin]);
        audible_ = audible_ || levels_[bin] > kFloorDb;
    }
}

void AnalyzerSpectrum::clear()
{
    std::fill(power_.begin(), power_.end(), 0.0);
    std::fill(levels_.begin(), levels_.end(), kFloorDb);
    std::fill(peaks_.begin(), peaks_.end(), kFloorDb);
    audible_ = false;
}

void AnalyzerSpectrum::clearPeaks() { peaks_ = levels_; }

double AnalyzerSpectrum::levelDb(double fromHz, double toHz) const { return read(levels_, fromHz, toHz); }

double AnalyzerSpectrum::peakDb(double fromHz, double toHz) const
{
    return peakHold_ ? read(peaks_, fromHz, toHz) : kFloorDb;
}

double AnalyzerSpectrum::read(const std::vector<double>& levels, double fromHz, double toHz) const
{
    if (levels.empty() || !std::isfinite(fromHz) || !std::isfinite(toHz))
        return kFloorDb;
    if (fromHz > toHz)
        std::swap(fromHz, toHz);
    const double binHz = sampleRate_ / static_cast<double>(window_.size());
    const std::size_t lastBin = levels.size() - 1;
    const double last = static_cast<double>(lastBin);
    const double from = std::clamp(fromHz / binHz, 0.0, last);
    const double to = std::clamp(toHz / binHz, 0.0, last);
    // The line between the bins either side of each end, and every bin between.
    const auto at = [&levels, lastBin](double position) {
        const auto below = static_cast<std::size_t>(position);
        const std::size_t above = std::min(below + 1, lastBin);
        const double part = position - static_cast<double>(below);
        return levels[below] + (levels[above] - levels[below]) * part;
    };
    double level = std::max(at(from), at(to));
    for (auto bin = static_cast<std::size_t>(std::ceil(from)); static_cast<double>(bin) <= to; ++bin)
        level = std::max(level, levels[bin]);
    if (!(level > kFloorDb))
        return kFloorDb;
    const double middle = fromHz > 0.0 ? std::sqrt(fromHz * toHz) : toHz;
    return level + vloom::dsp::Analyzer::tiltDb(middle, tilt_);
}

} // namespace vloomdaw
