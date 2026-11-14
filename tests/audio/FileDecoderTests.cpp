// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/FileDecoder.hpp"

#include <vloom/engine/render/WavWriter.hpp>
#include <vloom/engine/sample/WavReader.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
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
        path = std::filesystem::temp_directory_path() / ("vloomdaw_decode_" + std::to_string(stamp));
        std::filesystem::create_directories(path);
    }
    ~Folder()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    [[nodiscard]] std::string file(const char* name) const { return (path / name).string(); }
};

/// A stereo sine at `rate`, `seconds` long, written as a float WAV.
std::string sineWav(const Folder& folder, double rate, double seconds)
{
    const auto frames = static_cast<int>(rate * seconds);
    std::vector<float> samples;
    for (int i = 0; i < frames; ++i) {
        const auto v = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 441.0 * i / rate));
        samples.push_back(v);
        samples.push_back(v);
    }
    const std::string path = folder.file("sine.wav");
    REQUIRE(vloom::writeWavFloat32(path, samples, 2, static_cast<int>(rate)));
    return path;
}

vloom::SampleBuffer read(const std::string& path)
{
    auto result = vloom::readWavFile(path);
    REQUIRE(result.buffer.has_value());
    return std::move(*result.buffer);
}

float peak(const vloom::SampleBuffer& b, int channel)
{
    float p = 0.0f;
    for (std::int64_t i = 0; i < b.frames(); ++i)
        p = std::max(p, std::abs(b.channel(channel)[i]));
    return p;
}

} // namespace

TEST_CASE("An MP3 file decodes to a float WAV at the rate asked for", "[decode]")
{
    const Folder folder;
    const std::string target = folder.file("out.wav");
    std::string error;
    REQUIRE(decodeToWav(VLOOMDAW_TEST_DATA "/sine-441hz-half-second.mp3", target, 48000, &error, {}));
    CHECK(error.empty());
    const auto decoded = read(target);
    CHECK(decoded.sampleRate() == 48000.0);
    REQUIRE(decoded.channels() == 2);
    // Half a second, give or take the encoder's padding.
    CHECK(decoded.frames() == Approx(24000).margin(2400));
    CHECK(peak(decoded, 0) == Approx(0.5).margin(0.05));
    CHECK(peak(decoded, 1) == Approx(0.5).margin(0.05));
}

TEST_CASE("A WAV file goes through FFmpeg too, resampled or at its own rate", "[decode]")
{
    const Folder folder;
    const std::string source = sineWav(folder, 44100.0, 1.0);

    const std::string resampled = folder.file("48k.wav");
    REQUIRE(decodeToWav(source, resampled, 48000, nullptr, {}));
    const auto at48 = read(resampled);
    CHECK(at48.sampleRate() == 48000.0);
    CHECK(at48.frames() == Approx(48000).margin(64));
    CHECK(peak(at48, 0) == Approx(0.5).margin(0.01));

    const std::string own = folder.file("own.wav");
    REQUIRE(decodeToWav(source, own, 0, nullptr, {}));
    const auto at44 = read(own);
    CHECK(at44.sampleRate() == 44100.0);
    CHECK(at44.frames() == 44100);
}

TEST_CASE("Files that are not audio, or too long, are refused and write nothing", "[decode]")
{
    const Folder folder;
    const std::string target = folder.file("out.wav");
    std::string error;

    CHECK_FALSE(decodeToWav(folder.file("missing.mp3"), target, 48000, &error, {}));
    CHECK_FALSE(error.empty());

    const std::string text = folder.file("notes.mp3");
    std::ofstream(text) << "not audio at all\n";
    error.clear();
    CHECK_FALSE(decodeToWav(text, target, 48000, &error, {}));
    CHECK_FALSE(error.empty());

    // A list of other files (FFmpeg's concat format) named like an MP3: only
    // audio containers are opened, so it never gets to read what it lists.
    const std::string list = folder.file("playlist.mp3");
    std::ofstream(list) << "ffconcat version 1.0\nfile 'https://example.com/a.mp3'\n";
    error.clear();
    CHECK_FALSE(decodeToWav(list, target, 48000, &error, {}));

    // Longer than allowed: refused while decoding.
    DecodeLimits limits;
    limits.maxSeconds = 0.25;
    error.clear();
    CHECK_FALSE(decodeToWav(sineWav(folder, 44100.0, 1.0), target, 48000, &error, limits));
    CHECK_FALSE(error.empty());

    CHECK_FALSE(std::filesystem::exists(target));
}
