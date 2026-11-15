// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/StemAudio.hpp"

#include "audio/FileDecoder.hpp"

#include <vloom/engine/render/WavWriter.hpp>
#include <vloom/engine/sample/SampleBuffer.hpp>
#include <vloom/engine/sample/WavReader.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <system_error>
#include <vector>

namespace vloomdaw {

namespace {

bool fail(std::string* error, const std::string& reason)
{
    if (error != nullptr)
        *error = reason;
    return false;
}

} // namespace

std::int64_t writeStemInput(const std::string& source, const std::filesystem::path& target,
    const std::string& scratch, std::string* error)
{
    // FFmpeg decodes and resamples in one go; the engine's reader then gives
    // the samples (it checks the file it reads, as for any sample).
    if (!decodeToWav(source, scratch, kStemSampleRate, error))
        return 0;
    vloom::WavReadResult decoded = vloom::readWavFile(scratch);
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(std::u8string(scratch.begin(), scratch.end())), ec);
    if (!decoded.buffer) {
        fail(error, decoded.error);
        return 0;
    }
    const vloom::SampleBuffer& buffer = *decoded.buffer;
    const std::int64_t frames = buffer.frames();
    if (buffer.empty() || frames <= 0) {
        fail(error, "it has no audio");
        return 0;
    }
    const float* left = buffer.channel(0);
    const float* right = buffer.channels() > 1 ? buffer.channel(1) : left;
    std::vector<float> samples(static_cast<std::size_t>(frames) * 2);
    for (std::int64_t i = 0; i < frames; ++i) {
        samples[static_cast<std::size_t>(i) * 2] = left[i];
        samples[static_cast<std::size_t>(i) * 2 + 1] = right[i];
    }
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(samples.data()),
        static_cast<std::streamsize>(samples.size() * sizeof(float)));
    out.flush();
    if (!out) {
        fail(error, "the copy for the separator cannot be written");
        return 0;
    }
    return frames;
}

bool writeStemWav(
    const std::filesystem::path& raw, const std::string& wav, std::int64_t frames, std::string* error)
{
    std::error_code ec;
    const std::uintmax_t bytes = std::filesystem::file_size(raw, ec);
    constexpr std::uintmax_t kFrameBytes = 2 * sizeof(float);
    if (ec || frames <= 0 || bytes != static_cast<std::uintmax_t>(frames) * kFrameBytes)
        return fail(error, "the separator gave a stem of the wrong length");
    std::vector<float> samples(static_cast<std::size_t>(frames) * 2);
    std::ifstream in(raw, std::ios::binary);
    in.read(reinterpret_cast<char*>(samples.data()), static_cast<std::streamsize>(bytes));
    if (!in || in.gcount() != static_cast<std::streamsize>(bytes))
        return fail(error, "the stem cannot be read");
    // Whatever the separator wrote, the file is safe to play: no NaN, and
    // nothing far past full scale.
    for (float& sample : samples)
        sample = std::isfinite(sample) ? std::clamp(sample, -8.0f, 8.0f) : 0.0f;
    std::string writeError;
    if (!vloom::writeWavFloat32(wav, samples, 2, kStemSampleRate, &writeError))
        return fail(error, "the stem cannot be written (" + writeError + ")");
    return true;
}

} // namespace vloomdaw
