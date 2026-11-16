// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace vloomdaw {

/// The spectrogram the Demucs models work on, for models exported without it
/// (ONNX models keep the Fourier transform outside the network, where it is
/// cheap on the processor and does not get in a graphics card's way).
///
/// It follows Demucs to the sample (HTDemucs._spec and _ispec in
/// facebookresearch/demucs): a segment of kSegmentFrames samples, a periodic
/// Hann window of 4096 at a hop of 1024, the signal reflected at its ends,
/// values scaled by 1 / sqrt(4096); of the 2049 bins the highest is dropped,
/// and of the frames two at each end, leaving kBins x kFrames.
///
/// A spectrogram is two planes of kBins * kFrames floats, the real parts and
/// then the imaginary parts, each [bin * kFrames + frame]: the layout of a
/// model's "complex as channels" input and output.
class StemSpectrogram {
public:
    static constexpr int kFftSize = 4096;
    static constexpr int kHop = 1024;
    static constexpr int kSegmentFrames = 343980; ///< 7.8 s at 44.1 kHz: what the models take at a time
    static constexpr int kBins = 2048;
    static constexpr int kFrames = 336;
    static constexpr std::size_t kPlane = static_cast<std::size_t>(kBins) * kFrames;

    StemSpectrogram();

    /// The spectrogram of `wave` (kSegmentFrames samples) into `planes` (2 * kPlane floats).
    void forward(const float* wave, float* planes) const;
    /// The inverse: `planes` back to `wave` (kSegmentFrames samples). As in
    /// Demucs, the first and last 1536 samples come out incomplete (their
    /// outermost frames were dropped); the model's other branch and the
    /// overlap between segments make up for it.
    void inverse(const float* planes, float* wave) const;

private:
    /// In-place FFT of kFftSize values; `invert`: the inverse, unscaled.
    void transform(std::complex<float>* data, bool invert) const;

    std::vector<float> window_;
    std::vector<std::complex<float>> twiddles_; ///< exp(-2 pi i k / kFftSize), k < kFftSize / 2
    std::vector<int> reversed_; ///< bit-reversed indices
};

} // namespace vloomdaw
