// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/StemChunks.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using vloomdaw::StemChunk;
using vloomdaw::stemChunks;
using vloomdaw::stemChunkWeight;

TEST_CASE("A song is split into equal chunks that reach into their neighbours", "[stems]")
{
    const auto chunks = stemChunks(3000, 3, 100, 500);
    REQUIRE(chunks.size() == 3);
    CHECK(chunks[0] == StemChunk{0, 1000, 0, 1100});
    CHECK(chunks[1] == StemChunk{1000, 2000, 900, 2100});
    CHECK(chunks[2] == StemChunk{2000, 3000, 1900, 3000});
}

TEST_CASE("A short song gets fewer chunks, and at least one", "[stems]")
{
    CHECK(stemChunks(1200, 8, 100, 500).size() == 2); // 500 frames each at least
    CHECK(stemChunks(300, 8, 100, 500) == std::vector<StemChunk>{{0, 300, 0, 300}});
    CHECK(stemChunks(1000, 8, 400, 100).size() == 1); // twice the overlap at least
    CHECK(stemChunks(1000, 0, 100, 100).size() == 1);
    CHECK(stemChunks(0, 4, 100, 100).empty());
    CHECK(stemChunks(-5, 4, 100, 100).empty());
}

TEST_CASE("The chunks' weights add up to one at every frame", "[stems]")
{
    for (const int wanted : {1, 2, 3, 7}) {
        const std::int64_t frames = 10007; // does not divide evenly
        const std::int64_t overlap = 64;
        const auto chunks = stemChunks(frames, wanted, overlap, 200);
        for (std::int64_t frame = 0; frame < frames; ++frame) {
            float sum = 0.0f;
            for (const StemChunk& chunk : chunks) {
                const float weight = stemChunkWeight(chunk, frame, frames, overlap);
                CHECK(weight >= 0.0f);
                CHECK(weight <= 1.0f);
                sum += weight;
            }
            REQUIRE(sum == Approx(1.0f).margin(1e-5));
        }
    }
}

TEST_CASE("A chunk fades across the seam and is silent outside what it separated", "[stems]")
{
    const auto chunks = stemChunks(3000, 3, 100, 500);
    const StemChunk& middle = chunks[1];
    CHECK(stemChunkWeight(middle, 899, 3000, 100) == 0.0f);
    CHECK(stemChunkWeight(middle, 900, 3000, 100) == Approx(0.0025f));
    CHECK(stemChunkWeight(middle, 1000, 3000, 100) == Approx(0.5025f));
    CHECK(stemChunkWeight(middle, 1500, 3000, 100) == 1.0f);
    CHECK(stemChunkWeight(middle, 2099, 3000, 100) == Approx(0.0025f));
    CHECK(stemChunkWeight(middle, 2100, 3000, 100) == 0.0f);
    // The song's own ends are not faded.
    CHECK(stemChunkWeight(chunks[0], 0, 3000, 100) == 1.0f);
    CHECK(stemChunkWeight(chunks[2], 2999, 3000, 100) == 1.0f);
}
