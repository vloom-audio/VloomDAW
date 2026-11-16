// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/StemSpectrogram.hpp"

#include <cmath>
#include <numbers>
#include <utility>

namespace vloomdaw {

namespace {

constexpr int kSize = StemSpectrogram::kFftSize;
constexpr int kHop = StemSpectrogram::kHop;
constexpr int kLength = StemSpectrogram::kSegmentFrames;
constexpr int kFrames = StemSpectrogram::kFrames;
// Demucs pads the segment by 3/2 hops at each end (and at the right up to a
// whole number of hops), then the transform centres its frames: half a
// window more. The frames kept are the third to the third from last.
constexpr int kPad = kHop / 2 * 3;
constexpr int kPadded = kFrames * kHop + 2 * kPad;
constexpr int kCentre = kSize / 2;
constexpr int kDroppedFrames = 2;
constexpr float kScale = 64.0f; // sqrt(kSize)
// Four overlapping squared Hann windows at a quarter-window hop add up to this, everywhere.
constexpr float kWindowSum = 1.5f;

/// Sample `index` of the padded segment (`index` may be outside it: the
/// transform reflects that, too), read from the `wave` it was padded from.
float padded(const float* wave, int index)
{
    if (index < 0)
        index = -index;
    else if (index >= kPadded)
        index = 2 * (kPadded - 1) - index;
    index -= kPad;
    if (index < 0)
        index = -index;
    else if (index >= kLength)
        index = 2 * (kLength - 1) - index;
    return wave[index];
}

} // namespace

StemSpectrogram::StemSpectrogram()
    : window_(kSize)
    , twiddles_(kSize / 2)
    , reversed_(kSize)
{
    const double pi = std::numbers::pi;
    for (int n = 0; n < kSize; ++n)
        window_[static_cast<std::size_t>(n)]
            = static_cast<float>(0.5 * (1.0 - std::cos(2.0 * pi * n / kSize)));
    for (int k = 0; k < kSize / 2; ++k)
        twiddles_[static_cast<std::size_t>(k)] = {static_cast<float>(std::cos(2.0 * pi * k / kSize)),
            static_cast<float>(-std::sin(2.0 * pi * k / kSize))};
    int bits = 0;
    while ((1 << bits) < kSize)
        ++bits;
    for (int i = 0; i < kSize; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            r |= ((i >> b) & 1) << (bits - 1 - b);
        reversed_[static_cast<std::size_t>(i)] = r;
    }
}

void StemSpectrogram::transform(std::complex<float>* data, bool invert) const
{
    for (int i = 0; i < kSize; ++i)
        if (const int j = reversed_[static_cast<std::size_t>(i)]; j > i)
            std::swap(data[i], data[j]);
    for (int half = 1; half < kSize; half *= 2) {
        const int step = kSize / (2 * half);
        for (int start = 0; start < kSize; start += 2 * half)
            for (int k = 0; k < half; ++k) {
                std::complex<float> w = twiddles_[static_cast<std::size_t>(k * step)];
                if (invert)
                    w = std::conj(w);
                const std::complex<float> a = data[start + k];
                const std::complex<float> b = data[start + k + half];
                // Spelled out: std::complex's own product checks for NaN on every call.
                const std::complex<float> t{
                    b.real() * w.real() - b.imag() * w.imag(), b.real() * w.imag() + b.imag() * w.real()};
                data[start + k] = a + t;
                data[start + k + half] = a - t;
            }
    }
}

void StemSpectrogram::forward(const float* wave, float* planes) const
{
    std::vector<std::complex<float>> frame(kSize);
    float* real = planes;
    float* imaginary = planes + kPlane;
    for (int f = 0; f < kFrames; ++f) {
        const int start = (f + kDroppedFrames) * kHop - kCentre; // in the padded segment
        for (int n = 0; n < kSize; ++n)
            frame[static_cast<std::size_t>(n)]
                = {padded(wave, start + n) * window_[static_cast<std::size_t>(n)], 0.0f};
        transform(frame.data(), false);
        for (int bin = 0; bin < kBins; ++bin) {
            const std::size_t at = static_cast<std::size_t>(bin) * kFrames + static_cast<std::size_t>(f);
            real[at] = frame[static_cast<std::size_t>(bin)].real() / kScale;
            imaginary[at] = frame[static_cast<std::size_t>(bin)].imag() / kScale;
        }
    }
}

void StemSpectrogram::inverse(const float* planes, float* wave) const
{
    // Overlap-add of the frames, in the coordinates of the centred, padded
    // segment; `wave` is the part of it that the segment itself takes.
    constexpr int kFirst = kCentre + kPad;
    std::vector<float> sum(static_cast<std::size_t>(kPadded) + kSize, 0.0f);
    std::vector<std::complex<float>> frame(kSize);
    const float* real = planes;
    const float* imaginary = planes + kPlane;
    for (int f = 0; f < kFrames; ++f) {
        // The whole spectrum from its lower half: the top bin is zero, the rest mirrors.
        frame[0] = {real[static_cast<std::size_t>(f)], 0.0f};
        frame[kSize / 2] = {0.0f, 0.0f};
        for (int bin = 1; bin < kBins; ++bin) {
            const std::size_t at = static_cast<std::size_t>(bin) * kFrames + static_cast<std::size_t>(f);
            frame[static_cast<std::size_t>(bin)] = {real[at], imaginary[at]};
            frame[static_cast<std::size_t>(kSize - bin)] = {real[at], -imaginary[at]};
        }
        transform(frame.data(), true);
        const std::size_t start = static_cast<std::size_t>(f + kDroppedFrames) * kHop;
        for (int n = 0; n < kSize; ++n)
            sum[start + static_cast<std::size_t>(n)]
                += frame[static_cast<std::size_t>(n)].real() * window_[static_cast<std::size_t>(n)];
    }
    // Undo the forward scale (the inverse transform above is unscaled) and the windows' sum.
    const float gain = kScale / (static_cast<float>(kSize) * kWindowSum);
    for (int i = 0; i < kLength; ++i)
        wave[i] = sum[static_cast<std::size_t>(kFirst + i)] * gain;
}

} // namespace vloomdaw
