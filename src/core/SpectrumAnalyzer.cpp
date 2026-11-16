// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/SpectrumAnalyzer.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace vloomdaw {

namespace {

constexpr std::size_t kBins = SpectrumAnalyzer::kFrames / 2 + 1;

/// In-place radix-2 FFT of `real` + i `imaginary` (a power of two long).
void transform(std::vector<float>& real, std::vector<float>& imaginary)
{
    const std::size_t n = real.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) { // bit-reversed order
        std::size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imaginary[i], imaginary[j]);
        }
    }
    for (std::size_t length = 2; length <= n; length <<= 1) {
        const double angle = -2.0 * std::numbers::pi / static_cast<double>(length);
        const double stepReal = std::cos(angle);
        const double stepImaginary = std::sin(angle);
        for (std::size_t start = 0; start < n; start += length) {
            double wReal = 1.0;
            double wImaginary = 0.0;
            for (std::size_t k = 0; k < length / 2; ++k) {
                const std::size_t a = start + k;
                const std::size_t b = a + length / 2;
                const double tReal = real[b] * wReal - imaginary[b] * wImaginary;
                const double tImaginary = real[b] * wImaginary + imaginary[b] * wReal;
                real[b] = static_cast<float>(real[a] - tReal);
                imaginary[b] = static_cast<float>(imaginary[a] - tImaginary);
                real[a] = static_cast<float>(real[a] + tReal);
                imaginary[a] = static_cast<float>(imaginary[a] + tImaginary);
                const double nextReal = wReal * stepReal - wImaginary * stepImaginary;
                wImaginary = wReal * stepImaginary + wImaginary * stepReal;
                wReal = nextReal;
            }
        }
    }
}

} // namespace

SpectrumAnalyzer::SpectrumAnalyzer(double sampleRate)
    : sampleRate_(std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 48000.0)
    , window_(kFrames)
    , real_(kFrames)
    , imaginary_(kFrames)
    , levels_(kBins, kFloorDb)
{
    for (std::size_t i = 0; i < kFrames; ++i) {
        window_[i] = static_cast<float>(0.5
            - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(kFrames)));
        windowSum_ += window_[i];
    }
}

void SpectrumAnalyzer::analyse(std::span<const float> signal)
{
    if (signal.size() > kFrames)
        signal = signal.last(kFrames);
    const std::size_t lead = kFrames - signal.size(); // silence before a short signal
    std::fill(real_.begin(), real_.begin() + static_cast<std::ptrdiff_t>(lead), 0.0f);
    for (std::size_t i = 0; i < signal.size(); ++i) {
        const float sample = std::isfinite(signal[i]) ? signal[i] : 0.0f;
        real_[lead + i] = sample * window_[lead + i];
    }
    std::fill(imaginary_.begin(), imaginary_.end(), 0.0f);
    transform(real_, imaginary_);

    audible_ = false;
    const double scale = 2.0 / windowSum_; // a full-scale sine: 0 dB
    for (std::size_t bin = 0; bin < kBins; ++bin) {
        const double magnitude = std::hypot(real_[bin], imaginary_[bin]) * scale;
        const double now = magnitude > 0.0 ? std::max(kFloorDb, 20.0 * std::log10(magnitude)) : kFloorDb;
        levels_[bin] = std::max({now, levels_[bin] - kFallDb, kFloorDb});
        audible_ = audible_ || levels_[bin] > kFloorDb;
    }
}

void SpectrumAnalyzer::clear()
{
    std::fill(levels_.begin(), levels_.end(), kFloorDb);
    audible_ = false;
}

double SpectrumAnalyzer::levelDb(double fromHz, double toHz) const
{
    if (!std::isfinite(fromHz) || !std::isfinite(toHz))
        return kFloorDb;
    if (fromHz > toHz)
        std::swap(fromHz, toHz);
    const double binHz = sampleRate_ / static_cast<double>(kFrames);
    const double last = static_cast<double>(kBins - 1);
    const double from = std::clamp(fromHz / binHz, 0.0, last);
    const double to = std::clamp(toHz / binHz, 0.0, last);
    // The line between the bins either side of each end, and every bin between.
    const auto at = [this](double position) {
        const auto below = static_cast<std::size_t>(position);
        const std::size_t above = std::min(below + 1, kBins - 1);
        const double part = position - static_cast<double>(below);
        return levels_[below] + (levels_[above] - levels_[below]) * part;
    };
    double level = std::max(at(from), at(to));
    for (auto bin = static_cast<std::size_t>(std::ceil(from)); static_cast<double>(bin) <= to; ++bin)
        level = std::max(level, levels_[bin]);
    return level;
}

} // namespace vloomdaw
