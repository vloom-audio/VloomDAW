// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace vloomdaw {

/// A Fourier transform of any size (the MDX-Net models use windows of 5120,
/// 6144 and 7680 samples, none a power of two): Bluestein's algorithm, which
/// turns it into a convolution done with power-of-two transforms.
class AnySizeFft {
public:
    explicit AnySizeFft(int size);

    [[nodiscard]] int size() const { return size_; }
    /// In place, `size()` values; `invert`: the inverse, unscaled.
    /// `scratch` is working memory, resized as needed (one per thread).
    void transform(std::complex<float>* data, bool invert, std::vector<std::complex<float>>& scratch) const;

private:
    void radix2(std::complex<float>* data, bool invert) const;

    int size_;
    int padded_; ///< the power of two the convolution is done in
    std::vector<std::complex<float>> chirp_; ///< exp(-i pi n^2 / size)
    std::vector<std::complex<float>> filter_; ///< the transform of the conjugate chirp
    std::vector<std::complex<float>> twiddles_;
    std::vector<int> reversed_;
};

/// The spectrogram the MDX-Net models work on (as in UVR and the kuielab
/// code they come from): a chunk of `hop * (frames - 1)` samples, a periodic
/// Hann window of `fftSize` at `hop`, the chunk reflected at its ends, no
/// scaling; of the `fftSize / 2 + 1` bins the lowest `bins` are kept.
///
/// A spectrogram is two planes of `bins * frames` floats, the real parts and
/// then the imaginary parts, each [bin * frames + frame].
///
/// The work is offered a range of frames at a time so the caller can spread
/// it over threads; every method is safe to call from several at once.
class MdxSpectrogram {
public:
    MdxSpectrogram(int fftSize, int hop, int bins, int frames);

    [[nodiscard]] int fftSize() const { return fft_.size(); }
    [[nodiscard]] int bins() const { return bins_; }
    [[nodiscard]] int frames() const { return frames_; }
    [[nodiscard]] int chunk() const { return hop_ * (frames_ - 1); } ///< samples in, samples out
    [[nodiscard]] std::size_t plane() const
    {
        return static_cast<std::size_t>(bins_) * static_cast<std::size_t>(frames_);
    }

    /// Frames [first, end) of the spectrogram of `wave` (chunk() samples)
    /// into `planes` (2 * plane() floats).
    void forward(const float* wave, float* planes, int first, int end) const;
    /// Frames [first, end) of `planes` back to windowed waveforms, into
    /// `pieces` (frames() * fftSize() floats); the bins above `bins` are silent.
    void inverseFrames(const float* planes, float* pieces, int first, int end) const;
    /// The `pieces` added up into `wave` (chunk() samples).
    void overlapAdd(const float* pieces, float* wave) const;

    /// The whole of it at once.
    void forward(const float* wave, float* planes) const { forward(wave, planes, 0, frames_); }
    void inverse(const float* planes, float* wave) const;

private:
    AnySizeFft fft_;
    int hop_;
    int bins_;
    int frames_;
    std::vector<float> window_;
    std::vector<float> envelope_; ///< the windows' squares added up, per sample of the chunk
};

} // namespace vloomdaw
