// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <cstdint>
#include <vector>

namespace vloomdaw {

/// One part of a song that the stem separator works on by itself, so several
/// parts can run at once. A part is separated together with `overlap` frames
/// of its neighbours on each side, and the results are cross-faded there, so
/// no seam is heard where two parts meet.
struct StemChunk {
    std::int64_t begin = 0; ///< its own frames: [begin, end)
    std::int64_t end = 0;
    std::int64_t from = 0; ///< what is separated: [from, to), its own frames and the overlaps
    std::int64_t to = 0;

    friend bool operator==(const StemChunk&, const StemChunk&) = default;
};

/// Splits `frames` frames into at most `wanted` equal parts in order, each at
/// least `minFrames` long and at least twice `overlap` (fewer parts when the
/// song is too short for that; one part for the whole of it at least). Empty
/// when `frames` is not positive.
[[nodiscard]] std::vector<StemChunk> stemChunks(
    std::int64_t frames, int wanted, std::int64_t overlap, std::int64_t minFrames);

/// How much of `chunk`'s result goes into frame `frame` of the song, 0 .. 1:
/// 1 inside the chunk, a linear fade across the `overlap` frames either side
/// of where it meets a neighbour, 0 outside [from, to). The weights of all
/// the chunks of a song add up to 1 at every frame.
[[nodiscard]] float stemChunkWeight(
    const StemChunk& chunk, std::int64_t frame, std::int64_t frames, std::int64_t overlap);

} // namespace vloomdaw
