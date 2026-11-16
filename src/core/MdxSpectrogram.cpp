// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/MdxSpectrogram.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace vloomdaw {

namespace {

/// a * b, spelled out: std::complex's own product checks for NaN on every call.
std::complex<float> times(std::complex<float> a, std::complex<float> b)
{
    return {a.real() * b.real() - a.imag() * b.imag(), a.real() * b.imag() + a.imag() * b.real()};
}

} // namespace

AnySizeFft::AnySizeFft(int size)
    : size_(size)
    , padded_(1)
{
    if (size < 1 || size > (1 << 20))
        throw std::invalid_argument("a Fourier transform of that size is not supported");
    while (padded_ < 2 * size - 1)
        padded_ *= 2;
    const double pi = std::numbers::pi;
    twiddles_.resize(static_cast<std::size_t>(padded_ / 2));
    for (int k = 0; k < padded_ / 2; ++k)
        twiddles_[static_cast<std::size_t>(k)] = {static_cast<float>(std::cos(2.0 * pi * k / padded_)),
            static_cast<float>(-std::sin(2.0 * pi * k / padded_))};
    int bits = 0;
    while ((1 << bits) < padded_)
        ++bits;
    reversed_.resize(static_cast<std::size_t>(padded_));
    for (int i = 0; i < padded_; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            r |= ((i >> b) & 1) << (bits - 1 - b);
        reversed_[static_cast<std::size_t>(i)] = r;
    }
    // n^2 taken modulo 2 * size: the angle is the same and stays exact.
    chirp_.resize(static_cast<std::size_t>(size));
    for (int n = 0; n < size; ++n) {
        const auto square = (static_cast<long long>(n) * n) % (2LL * size);
        const double angle = pi * static_cast<double>(square) / size;
        chirp_[static_cast<std::size_t>(n)]
            = {static_cast<float>(std::cos(angle)), static_cast<float>(-std::sin(angle))};
    }
    filter_.assign(static_cast<std::size_t>(padded_), {0.0f, 0.0f});
    filter_[0] = std::conj(chirp_[0]);
    for (int n = 1; n < size; ++n) {
        filter_[static_cast<std::size_t>(n)] = std::conj(chirp_[static_cast<std::size_t>(n)]);
        filter_[static_cast<std::size_t>(padded_ - n)] = std::conj(chirp_[static_cast<std::size_t>(n)]);
    }
    radix2(filter_.data(), false);
}

void AnySizeFft::radix2(std::complex<float>* data, bool invert) const
{
    for (int i = 0; i < padded_; ++i)
        if (const int j = reversed_[static_cast<std::size_t>(i)]; j > i)
            std::swap(data[i], data[j]);
    for (int half = 1; half < padded_; half *= 2) {
        const int step = padded_ / (2 * half);
        for (int start = 0; start < padded_; start += 2 * half)
            for (int k = 0; k < half; ++k) {
                std::complex<float> w = twiddles_[static_cast<std::size_t>(k * step)];
                if (invert)
                    w = std::conj(w);
                const std::complex<float> a = data[start + k];
                const std::complex<float> t = times(data[start + k + half], w);
                data[start + k] = a + t;
                data[start + k + half] = a - t;
            }
    }
}

void AnySizeFft::transform(
    std::complex<float>* data, bool invert, std::vector<std::complex<float>>& scratch) const
{
    // The inverse is the forward transform of the conjugate, conjugated.
    scratch.assign(static_cast<std::size_t>(padded_), {0.0f, 0.0f});
    for (int n = 0; n < size_; ++n) {
        const std::complex<float> value = invert ? std::conj(data[n]) : data[n];
        scratch[static_cast<std::size_t>(n)] = times(value, chirp_[static_cast<std::size_t>(n)]);
    }
    radix2(scratch.data(), false);
    for (int i = 0; i < padded_; ++i)
        scratch[static_cast<std::size_t>(i)]
            = times(scratch[static_cast<std::size_t>(i)], filter_[static_cast<std::size_t>(i)]);
    radix2(scratch.data(), true);
    const float scale = 1.0f / static_cast<float>(padded_);
    for (int k = 0; k < size_; ++k) {
        const std::complex<float> value
            = times(scratch[static_cast<std::size_t>(k)], chirp_[static_cast<std::size_t>(k)]) * scale;
        data[k] = invert ? std::conj(value) : value;
    }
}

MdxSpectrogram::MdxSpectrogram(int fftSize, int hop, int bins, int frames)
    : fft_(fftSize)
    , hop_(hop)
    , bins_(bins)
    , frames_(frames)
{
    // The chunk must be longer than half a window, or it cannot be reflected at its ends.
    if (fftSize % 2 != 0 || hop < 1 || hop > fftSize || bins < 1 || bins > fftSize / 2 + 1 || frames < 2
        || static_cast<long long>(hop) * (frames - 1) <= fftSize / 2)
        throw std::invalid_argument("a spectrogram of that shape is not supported");
    const double pi = std::numbers::pi;
    window_.resize(static_cast<std::size_t>(fftSize));
    for (int n = 0; n < fftSize; ++n)
        window_[static_cast<std::size_t>(n)]
            = static_cast<float>(0.5 * (1.0 - std::cos(2.0 * pi * n / fftSize)));
    // Frame f covers samples [f * hop - fftSize / 2, f * hop + fftSize / 2) of the chunk.
    envelope_.assign(static_cast<std::size_t>(chunk()), 0.0f);
    for (int f = 0; f < frames; ++f)
        for (int n = 0; n < fftSize; ++n) {
            const int at = f * hop + n - fftSize / 2;
            if (at >= 0 && at < chunk())
                envelope_[static_cast<std::size_t>(at)]
                    += window_[static_cast<std::size_t>(n)] * window_[static_cast<std::size_t>(n)];
        }
}

void MdxSpectrogram::forward(const float* wave, float* planes, int first, int end) const
{
    const int size = fft_.size();
    const int length = chunk();
    std::vector<std::complex<float>> frame(static_cast<std::size_t>(size));
    std::vector<std::complex<float>> scratch;
    float* real = planes;
    float* imaginary = planes + plane();
    for (int f = first; f < end; ++f) {
        const int start = f * hop_ - size / 2;
        for (int n = 0; n < size; ++n) {
            int at = start + n;
            if (at < 0)
                at = -at;
            else if (at >= length)
                at = 2 * (length - 1) - at;
            frame[static_cast<std::size_t>(n)] = {wave[at] * window_[static_cast<std::size_t>(n)], 0.0f};
        }
        fft_.transform(frame.data(), false, scratch);
        for (int bin = 0; bin < bins_; ++bin) {
            const std::size_t at = static_cast<std::size_t>(bin) * static_cast<std::size_t>(frames_)
                + static_cast<std::size_t>(f);
            real[at] = frame[static_cast<std::size_t>(bin)].real();
            imaginary[at] = frame[static_cast<std::size_t>(bin)].imag();
        }
    }
}

void MdxSpectrogram::inverseFrames(const float* planes, float* pieces, int first, int end) const
{
    const int size = fft_.size();
    std::vector<std::complex<float>> frame(static_cast<std::size_t>(size));
    std::vector<std::complex<float>> scratch;
    const float* real = planes;
    const float* imaginary = planes + plane();
    const float scale = 1.0f / static_cast<float>(size);
    for (int f = first; f < end; ++f) {
        // The whole spectrum from its lower half: what was not kept is zero, the rest mirrors.
        std::fill(frame.begin(), frame.end(), std::complex<float>{0.0f, 0.0f});
        for (int bin = 0; bin < bins_; ++bin) {
            const std::size_t at = static_cast<std::size_t>(bin) * static_cast<std::size_t>(frames_)
                + static_cast<std::size_t>(f);
            const bool alone = bin == 0 || bin == size / 2; // their own mirror: real
            frame[static_cast<std::size_t>(bin)] = {real[at], alone ? 0.0f : imaginary[at]};
            if (!alone)
                frame[static_cast<std::size_t>(size - bin)] = {real[at], -imaginary[at]};
        }
        fft_.transform(frame.data(), true, scratch);
        float* piece = pieces + static_cast<std::size_t>(f) * static_cast<std::size_t>(size);
        for (int n = 0; n < size; ++n)
            piece[n]
                = frame[static_cast<std::size_t>(n)].real() * scale * window_[static_cast<std::size_t>(n)];
    }
}

void MdxSpectrogram::overlapAdd(const float* pieces, float* wave) const
{
    const int size = fft_.size();
    const int length = chunk();
    std::fill_n(wave, length, 0.0f);
    for (int f = 0; f < frames_; ++f) {
        const float* piece = pieces + static_cast<std::size_t>(f) * static_cast<std::size_t>(size);
        const int start = f * hop_ - size / 2;
        const int from = std::max(0, -start);
        const int to = std::min(size, length - start);
        for (int n = from; n < to; ++n)
            wave[start + n] += piece[n];
    }
    for (int i = 0; i < length; ++i) {
        const float sum = envelope_[static_cast<std::size_t>(i)];
        wave[i] = sum > 1e-8f ? wave[i] / sum : 0.0f;
    }
}

void MdxSpectrogram::inverse(const float* planes, float* wave) const
{
    std::vector<float> pieces(static_cast<std::size_t>(frames_) * static_cast<std::size_t>(fft_.size()));
    inverseFrames(planes, pieces.data(), 0, frames_);
    overlapAdd(pieces.data(), wave);
}

} // namespace vloomdaw
