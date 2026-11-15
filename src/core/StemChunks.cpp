// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/StemChunks.hpp"

#include <algorithm>

namespace vloomdaw {

std::vector<StemChunk> stemChunks(
    std::int64_t frames, int wanted, std::int64_t overlap, std::int64_t minFrames)
{
    std::vector<StemChunk> chunks;
    if (frames <= 0)
        return chunks;
    overlap = std::max<std::int64_t>(overlap, 0);
    // Twice the overlap: the fades at a chunk's two ends must not meet.
    const std::int64_t shortest = std::max<std::int64_t>({minFrames, 2 * overlap, 1});
    const std::int64_t count = std::clamp<std::int64_t>(frames / shortest, 1, std::max(wanted, 1));
    chunks.reserve(static_cast<std::size_t>(count));
    for (std::int64_t i = 0; i < count; ++i) {
        StemChunk chunk;
        chunk.begin = frames * i / count;
        chunk.end = frames * (i + 1) / count;
        chunk.from = i == 0 ? 0 : chunk.begin - overlap;
        chunk.to = i == count - 1 ? frames : chunk.end + overlap;
        chunks.push_back(chunk);
    }
    return chunks;
}

float stemChunkWeight(const StemChunk& chunk, std::int64_t frame, std::int64_t frames, std::int64_t overlap)
{
    if (frame < chunk.from || frame >= chunk.to)
        return 0.0f;
    if (overlap <= 0)
        return frame >= chunk.begin && frame < chunk.end ? 1.0f : 0.0f;
    // The fade is centred on the seam and takes the middle of each frame, so
    // the neighbour's fade the other way adds up to exactly 1 with it.
    const auto across = [overlap](std::int64_t fromSeam) {
        return (static_cast<float>(fromSeam + overlap) + 0.5f) / static_cast<float>(2 * overlap);
    };
    float weight = 1.0f;
    if (chunk.begin > 0 && frame < chunk.begin + overlap)
        weight = across(frame - chunk.begin);
    if (chunk.end < frames && frame >= chunk.end - overlap)
        weight = std::min(weight, 1.0f - across(frame - chunk.end));
    return weight;
}

} // namespace vloomdaw
