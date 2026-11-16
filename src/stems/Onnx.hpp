// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace vloomdaw::stems {

/// What separateOnnx() is told to do.
struct OnnxJob {
    /// One model: every stem comes from it. Several: stem i comes from model i.
    std::vector<std::filesystem::path> models;
    /// Try the graphics card first (DirectML: any DirectX 12 card); the
    /// processor is used when there is none, when it fails, or when what it
    /// gives does not add up to the song.
    bool allowGpu = true;
    /// Leaves DirectML's graph fusion on. Only to test the check of the
    /// card's results: fused, these models are known to come out wrong on
    /// some drivers, which the check must catch.
    bool unsafeFusion = false;
    /// How much of each piece the model is given is shared with the next, in
    /// percent (25 .. 75; 25 is what Demucs and UVR use). More: smoother
    /// joins, and as much slower (half: one and a half times, three quarters:
    /// three times).
    int overlapPercent = 25;
    /// Not zero: the model is an MDX-Net one (a spectrogram in, the
    /// spectrogram of one stem out), made for a transform window of this
    /// many samples. It gives two stems: the model's, and the rest of the song.
    int mdxFft = 0;
    /// MDX-Net: what the model's stem is multiplied by (UVR's "compensate").
    float mdxGain = 1.0f;
    std::function<void(double done)> progress; ///< 0 .. 1
    /// Told what the work runs on: "gpu" or "cpu", and why when it changes.
    std::function<void(const std::string& device)> device;
};

/// Separates the stereo audio `left` / `right` (44.1 kHz) into stems with
/// ONNX models of the Demucs family, of either kind: those that take the
/// waveform and its spectrogram and give the two branches back (the Fourier
/// transform stays outside the model: core/StemSpectrogram; these are the
/// ones graphics cards run well), and those that take the waveform and give
/// the stems (the transform inside; it does not fit a 4 GB card, so the app
/// runs them on the processor); or with an MDX-Net model (OnnxJob::mdxFft;
/// core/MdxSpectrogram). Returns the stems as
/// [stem][2 * frame + channel]. Throws std::runtime_error with a reason.
std::vector<std::vector<float>> separateOnnx(
    const std::vector<float>& left, const std::vector<float>& right, const OnnxJob& job);

} // namespace vloomdaw::stems
