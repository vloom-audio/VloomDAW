// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/StemAudio.hpp"

#include <vloom/engine/render/WavWriter.hpp>
#include <vloom/engine/sample/WavReader.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace vloomdaw;
using Catch::Approx;

namespace {

/// A temporary folder, removed afterwards.
struct Folder {
    std::filesystem::path path;
    Folder()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("vloomdaw_stems_" + std::to_string(stamp));
        std::filesystem::create_directories(path);
    }
    ~Folder()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    [[nodiscard]] std::string file(const char* name) const { return (path / name).string(); }
};

std::vector<float> readRaw(const std::filesystem::path& path)
{
    std::vector<float> samples(std::filesystem::file_size(path) / sizeof(float));
    std::ifstream in(path, std::ios::binary);
    in.read(reinterpret_cast<char*>(samples.data()),
        static_cast<std::streamsize>(samples.size() * sizeof(float)));
    return samples;
}

void writeRaw(const std::filesystem::path& path, const std::vector<float>& samples)
{
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(samples.data()),
        static_cast<std::streamsize>(samples.size() * sizeof(float)));
}

} // namespace

TEST_CASE("The separator's input is stereo at 44.1 kHz, whatever the file", "[stems]")
{
    const Folder folder;
    SECTION("stereo at the same rate comes through sample for sample")
    {
        std::vector<float> samples;
        for (int i = 0; i < 4410; ++i) {
            samples.push_back(static_cast<float>(i) / 4410.0f);
            samples.push_back(-static_cast<float>(i) / 8820.0f);
        }
        REQUIRE(vloom::writeWavFloat32(folder.file("in.wav"), samples, 2, kStemSampleRate));
        std::string error;
        CHECK(
            writeStemInput(folder.file("in.wav"), folder.path / "in.f32", folder.file("scratch.wav"), &error)
            == 4410);
        CHECK(error.empty());
        CHECK(readRaw(folder.path / "in.f32") == samples);
        CHECK_FALSE(std::filesystem::exists(folder.path / "scratch.wav"));
    }
    SECTION("mono goes on both sides, another rate is resampled")
    {
        std::vector<float> samples(48000, 0.25f); // one second
        REQUIRE(vloom::writeWavFloat32(folder.file("mono.wav"), samples, 1, 48000));
        std::string error;
        const std::int64_t frames = writeStemInput(
            folder.file("mono.wav"), folder.path / "in.f32", folder.file("scratch.wav"), &error);
        CHECK(frames == Approx(44100).margin(64));
        const std::vector<float> raw = readRaw(folder.path / "in.f32");
        REQUIRE(raw.size() == static_cast<std::size_t>(frames) * 2);
        CHECK(raw[2 * 22050] == Approx(0.25f).margin(1e-3));
        CHECK(raw[2 * 22050 + 1] == raw[2 * 22050]);
    }
    SECTION("a file that is not audio is refused with a reason")
    {
        std::ofstream(folder.path / "bad.wav") << "not audio";
        std::string error;
        CHECK(
            writeStemInput(folder.file("bad.wav"), folder.path / "in.f32", folder.file("scratch.wav"), &error)
            == 0);
        CHECK_FALSE(error.empty());
    }
}

TEST_CASE("A stem becomes a WAV file only when it is as long as the song", "[stems]")
{
    const Folder folder;
    const std::vector<float> stem = {0.5f, -0.5f, 0.25f, -0.25f, 0.0f, 1.0f};
    writeRaw(folder.path / "0.f32", stem);
    std::string error;
    REQUIRE(writeStemWav(folder.path / "0.f32", folder.file("vocals.wav"), 3, &error));
    const vloom::WavReadResult read = vloom::readWavFile(folder.file("vocals.wav"));
    REQUIRE(read.buffer);
    CHECK(read.buffer->channels() == 2);
    CHECK(read.buffer->frames() == 3);
    CHECK(read.buffer->sampleRate() == kStemSampleRate);
    CHECK(read.buffer->channel(0)[2] == 0.0f);
    CHECK(read.buffer->channel(1)[2] == 1.0f);

    CHECK_FALSE(writeStemWav(folder.path / "0.f32", folder.file("short.wav"), 4, &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(std::filesystem::exists(folder.path / "short.wav"));
    CHECK_FALSE(writeStemWav(folder.path / "0.f32", folder.file("none.wav"), 0, &error));
    CHECK_FALSE(writeStemWav(folder.path / "missing.f32", folder.file("none.wav"), 3, &error));
}

TEST_CASE("A stem with samples that cannot be played is made safe", "[stems]")
{
    const Folder folder;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    writeRaw(folder.path / "0.f32", {nan, inf, 100.0f, -100.0f});
    REQUIRE(writeStemWav(folder.path / "0.f32", folder.file("out.wav"), 2, nullptr));
    const vloom::WavReadResult read = vloom::readWavFile(folder.file("out.wav"));
    REQUIRE(read.buffer);
    CHECK(read.buffer->channel(0)[0] == 0.0f);
    CHECK(read.buffer->channel(1)[0] == 0.0f);
    CHECK(read.buffer->channel(0)[1] == 8.0f);
    CHECK(read.buffer->channel(1)[1] == -8.0f);
}
