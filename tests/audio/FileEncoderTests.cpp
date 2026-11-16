// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/FileDecoder.hpp"
#include "audio/FileEncoder.hpp"

#include <vloom/engine/render/SongExport.hpp>
#include <vloom/engine/render/WavWriter.hpp>
#include <vloom/engine/sample/SampleBuffer.hpp>
#include <vloom/engine/sample/WavReader.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
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
        path = std::filesystem::temp_directory_path() / ("vloomdaw_encode_" + std::to_string(stamp));
        std::filesystem::create_directories(path);
    }
    ~Folder()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    [[nodiscard]] std::string file(const std::string& name) const { return (path / name).string(); }
};

/// A sine of 441 Hz at half scale, `seconds` long, as a float WAV like the engine's export.
std::string sineWav(const Folder& folder, int rate, double seconds, int channels = 2)
{
    const auto frames = static_cast<int>(rate * seconds);
    std::vector<float> samples;
    for (int i = 0; i < frames; ++i) {
        const auto v = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 441.0 * i / rate));
        for (int c = 0; c < channels; ++c)
            samples.push_back(v);
    }
    const std::string path = folder.file("in.wav");
    REQUIRE(vloom::writeWavFloat32(path, samples, channels, rate));
    return path;
}

struct Decoded {
    std::int64_t frames = 0;
    int channels = 0;
    double rate = 0.0;
    double rms = 0.0; ///< of the middle half, away from the codec's edges
};

Decoded decode(const Folder& folder, const std::string& file)
{
    std::string error;
    const std::string wav = folder.file("back.wav");
    INFO(error);
    REQUIRE(decodeToWav(file, wav, 0, &error));
    const vloom::WavReadResult read = vloom::readWavFile(wav);
    REQUIRE(read.buffer);
    Decoded out{read.buffer->frames(), read.buffer->channels(), read.buffer->sampleRate()};
    double sum = 0.0;
    const std::int64_t from = out.frames / 4;
    const std::int64_t to = out.frames * 3 / 4;
    for (std::int64_t i = from; i < to; ++i)
        sum += static_cast<double>(read.buffer->channel(0)[i]) * read.buffer->channel(0)[i];
    out.rms = std::sqrt(sum / static_cast<double>(std::max<std::int64_t>(to - from, 1)));
    return out;
}

const double kSineRms = 0.5 / std::numbers::sqrt2;

} // namespace

TEST_CASE("The formats on offer have an extension, and WAV can always be written", "[encode]")
{
    CHECK(audioFileExtension(AudioFileFormat::Wav) == "wav");
    CHECK(audioFileExtension(AudioFileFormat::Flac) == "flac");
    CHECK(audioFileExtension(AudioFileFormat::Mp3) == "mp3");
    CHECK(audioFileExtension(AudioFileFormat::Ogg) == "ogg");
    CHECK(audioFileExtension(AudioFileFormat::M4a) == "m4a");
    CHECK(audioEncoderAvailable(AudioFileFormat::Wav));
    CHECK(audioBitrates(AudioFileFormat::Wav).empty());
    CHECK(audioBitrates(AudioFileFormat::Flac).empty());
    CHECK(audioBitrates(AudioFileFormat::Mp3).back() == 320);
}

TEST_CASE("Every format this build can write comes back as the same sound", "[encode]")
{
    for (const AudioFileFormat format :
        {AudioFileFormat::Flac, AudioFileFormat::Mp3, AudioFileFormat::Ogg, AudioFileFormat::M4a}) {
        const std::string extension(audioFileExtension(format));
        if (!audioEncoderAvailable(format)) {
            WARN("no encoder for " << extension << " in this FFmpeg: not tested");
            continue;
        }
        INFO(extension);
        const Folder folder;
        const std::string wav = sineWav(folder, 44100, 2.0);
        const std::string target = folder.file("out." + extension);
        std::string error;
        EncodeOptions options;
        options.format = format;
        options.bitrateKbps = 192;
        int calls = 0;
        REQUIRE(encodeWavFile(wav, target, options, &error, [&calls](double done) {
            ++calls;
            return done >= 0.0 && done <= 1.0;
        }));
        CHECK(error.empty());
        CHECK(calls > 0);
        CHECK_FALSE(std::filesystem::exists(target + ".part"));
        const Decoded back = decode(folder, target);
        CHECK(back.channels == 2);
        CHECK(back.rate == 44100.0);
        // Lossy codecs pad the ends by a frame or two.
        CHECK(back.frames == Approx(88200).margin(format == AudioFileFormat::Flac ? 0 : 4096));
        CHECK(back.rms == Approx(kSineRms).epsilon(format == AudioFileFormat::Flac ? 0.0005 : 0.03));
        // Smaller than the 32-bit float it came from.
        CHECK(std::filesystem::file_size(target) < std::filesystem::file_size(wav));
    }
}

TEST_CASE("FLAC is written at 16 or 24 bits, mono too", "[encode]")
{
    if (!audioEncoderAvailable(AudioFileFormat::Flac)) {
        WARN("no FLAC encoder in this FFmpeg: not tested");
        return;
    }
    const Folder folder;
    const std::string wav = sineWav(folder, 48000, 1.0, 1);
    EncodeOptions options;
    options.format = AudioFileFormat::Flac;
    options.bitDepth = 24;
    REQUIRE(encodeWavFile(wav, folder.file("deep.flac"), options, nullptr));
    options.bitDepth = 16;
    REQUIRE(encodeWavFile(wav, folder.file("cd.flac"), options, nullptr));
    CHECK(std::filesystem::file_size(folder.file("cd.flac"))
        < std::filesystem::file_size(folder.file("deep.flac")));
    const Decoded back = decode(folder, folder.file("cd.flac"));
    CHECK(back.channels == 1);
    CHECK(back.rate == 48000.0);
    CHECK(back.frames == 48000);
    CHECK(back.rms == Approx(kSineRms).epsilon(0.001));
}

TEST_CASE("A rate a format cannot take is resampled", "[encode]")
{
    if (!audioEncoderAvailable(AudioFileFormat::Mp3)) {
        WARN("no MP3 encoder in this FFmpeg: not tested");
        return;
    }
    const Folder folder;
    const std::string wav = sineWav(folder, 96000, 1.0);
    EncodeOptions options;
    options.format = AudioFileFormat::Mp3;
    REQUIRE(encodeWavFile(wav, folder.file("out.mp3"), options, nullptr));
    const Decoded back = decode(folder, folder.file("out.mp3"));
    CHECK(back.rate == 48000.0);
    CHECK(back.frames == Approx(48000).margin(4096));
    CHECK(back.rms == Approx(kSineRms).epsilon(0.03));
}

TEST_CASE("A cancelled or failed encode leaves no file", "[encode]")
{
    if (!audioEncoderAvailable(AudioFileFormat::Flac)) {
        WARN("no FLAC encoder in this FFmpeg: not tested");
        return;
    }
    const Folder folder;
    const std::string wav = sineWav(folder, 44100, 2.0);
    EncodeOptions options;
    options.format = AudioFileFormat::Flac;
    std::string error = "unset";
    CHECK_FALSE(encodeWavFile(wav, folder.file("out.flac"), options, &error, [](double) { return false; }));
    CHECK(error.empty()); // cancelled: no reason to show
    CHECK_FALSE(std::filesystem::exists(folder.file("out.flac")));
    CHECK_FALSE(std::filesystem::exists(folder.file("out.flac.part")));

    CHECK_FALSE(encodeWavFile(folder.file("missing.wav"), folder.file("out.flac"), options, &error));
    CHECK_FALSE(error.empty());
    options.format = AudioFileFormat::Wav; // the engine writes WAV, not this
    CHECK_FALSE(encodeWavFile(wav, folder.file("out.wav"), options, &error));
    CHECK_FALSE(std::filesystem::exists(folder.file("out.wav")));
    // A folder that is not there.
    options.format = AudioFileFormat::Flac;
    CHECK_FALSE(encodeWavFile(wav, folder.file("nowhere/out.flac"), options, &error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("A song the engine renders can be encoded as it is", "[encode]")
{
    if (!audioEncoderAvailable(AudioFileFormat::Flac)) {
        WARN("no FLAC encoder in this FFmpeg: not tested");
        return;
    }
    // One channel playing a short sample on the first step of a one-bar pattern.
    constexpr double kRate = 48000.0;
    auto sample = std::make_shared<vloom::SampleBuffer>(1, 4800, kRate);
    for (int i = 0; i < 4800; ++i)
        sample->channel(0)[i] = 0.5f;
    vloom::SequencerState state;
    vloom::ChannelState channel;
    channel.sample = sample;
    channel.voices = std::make_shared<vloom::ChannelVoices>();
    channel.steps.assign(16, 0);
    channel.steps[0] = 1;
    state.channels.push_back(std::move(channel));

    const Folder folder;
    for (const int channels : {2, 1}) {
        INFO(channels);
        vloom::SongExportOptions options;
        options.format = vloom::WavFormat::Float32; // what the app renders before encoding
        options.channels = channels;
        options.maxTailSeconds = 0.0;
        const vloom::SongExportResult rendered = vloom::exportSong(
            folder.file("render.wav"), *vloom::copyWithOwnVoices(state), nullptr, kRate, options);
        REQUIRE(rendered.ok);
        EncodeOptions encode;
        encode.format = AudioFileFormat::Flac;
        std::string error;
        INFO(error);
        REQUIRE(encodeWavFile(folder.file("render.wav"), folder.file("song.flac"), encode, &error));
        const Decoded back = decode(folder, folder.file("song.flac"));
        CHECK(back.channels == channels);
        CHECK(back.rate == kRate);
        CHECK(back.frames == static_cast<std::int64_t>(rendered.frames));
        CHECK(back.frames == 96000); // one bar at 120 BPM
    }
}

namespace {

std::string bytesOf(const std::string& file)
{
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

AudioMetadata someTags()
{
    AudioMetadata tags;
    tags.title = "Night Drive \xE0\xB8\x81\xE0\xB8\xA5\xE0\xB8\xAD\xE0\xB8\x87"; // with Thai letters
    tags.artist = "Vloom Tester";
    tags.album = "Loom Tapes";
    tags.year = "2026";
    tags.genre = "Electronic";
    tags.comment = "Made with VloomDAW";
    return tags;
}

} // namespace

TEST_CASE("Tags are added to a WAV file without touching its audio", "[encode][tags]")
{
    const Folder folder;
    const std::string wav = sineWav(folder, 44100, 0.5);
    const vloom::WavReadResult before = vloom::readWavFile(wav);
    REQUIRE(before.buffer);
    const std::string plain = bytesOf(wav);

    std::string error;
    REQUIRE(writeWavMetadata(wav, AudioMetadata{}, &error)); // nothing to write
    CHECK(bytesOf(wav) == plain);

    const AudioMetadata tags = someTags();
    REQUIRE(writeWavMetadata(wav, tags, &error));
    const std::string tagged = bytesOf(wav);
    CHECK(tagged.size() > plain.size());
    CHECK(tagged.size() % 2 == 0);
    CHECK(tagged.compare(8, plain.size() - 8, plain, 8, plain.size() - 8) == 0); // the audio as it was
    // The RIFF size takes the new chunk in.
    std::uint32_t riff = 0;
    for (int i = 0; i < 4; ++i)
        riff |= static_cast<std::uint32_t>(static_cast<unsigned char>(tagged[4 + i])) << (8 * i);
    CHECK(riff == tagged.size() - 8);
    for (const std::string& text : {std::string("LISTr"), std::string("INAM"), tags.title,
             std::string("IART"), tags.artist, std::string("IPRD"), tags.album, std::string("ICRD"),
             std::string("IGNR"), std::string("ICMT"), tags.comment})
        CHECK(tagged.find(text.substr(0, 4)) != std::string::npos);
    CHECK(tagged.find(tags.title) != std::string::npos);

    // Both readers still give the same sound.
    const vloom::WavReadResult after = vloom::readWavFile(wav);
    REQUIRE(after.buffer);
    CHECK(after.buffer->frames() == before.buffer->frames());
    CHECK(after.buffer->channel(1)[1000] == before.buffer->channel(1)[1000]);
    const Decoded back = decode(folder, wav);
    CHECK(back.frames == before.buffer->frames());
    CHECK(back.rms == Approx(kSineRms).epsilon(0.001));
}

TEST_CASE("A file that is not a WAV file gets no tags", "[encode][tags]")
{
    const Folder folder;
    const std::string file = folder.file("notes.txt");
    std::ofstream(file) << "just some text, long enough to have a header";
    const std::string before = bytesOf(file);
    std::string error;
    CHECK_FALSE(writeWavMetadata(file, someTags(), &error));
    CHECK_FALSE(error.empty());
    CHECK(bytesOf(file) == before);
    CHECK_FALSE(writeWavMetadata(folder.file("missing.wav"), someTags(), &error));
}

TEST_CASE("Tags are cleaned before they are written", "[encode][tags]")
{
    const Folder folder;
    const std::string wav = sineWav(folder, 44100, 0.1);
    AudioMetadata tags;
    tags.title = "  A\tTitle\nwith breaks  ";
    tags.artist = std::string(2000, 'x');
    tags.album = "   ";
    REQUIRE(writeWavMetadata(wav, tags, nullptr));
    const std::string tagged = bytesOf(wav);
    CHECK(tagged.find("ATitlewith breaks") != std::string::npos);
    CHECK(tagged.find(std::string(AudioMetadata::kMaxField, 'x')) != std::string::npos);
    CHECK(tagged.find(std::string(AudioMetadata::kMaxField + 1, 'x')) == std::string::npos);
    CHECK(tagged.find("IPRD") == std::string::npos); // only spaces: left out
    REQUIRE(vloom::readWavFile(wav).buffer);
}

TEST_CASE("Every encoded format carries the tags and still plays", "[encode][tags]")
{
    const AudioMetadata tags = someTags();
    for (const AudioFileFormat format :
        {AudioFileFormat::Flac, AudioFileFormat::Mp3, AudioFileFormat::Ogg, AudioFileFormat::M4a}) {
        const std::string extension(audioFileExtension(format));
        if (!audioEncoderAvailable(format)) {
            WARN("no encoder for " << extension << " in this FFmpeg: not tested");
            continue;
        }
        INFO(extension);
        const Folder folder;
        const std::string wav = sineWav(folder, 44100, 1.0);
        EncodeOptions options;
        options.format = format;
        options.metadata = tags;
        const std::string target = folder.file("out." + extension);
        std::string error;
        REQUIRE(encodeWavFile(wav, target, options, &error));
        const std::string bytes = bytesOf(target);
        CHECK(bytes.find(tags.title) != std::string::npos);
        CHECK(bytes.find(tags.artist) != std::string::npos);
        CHECK(bytes.find(tags.album) != std::string::npos);
        CHECK(bytes.find(tags.genre) != std::string::npos);
        CHECK(bytes.find(tags.comment) != std::string::npos);
        CHECK(bytes.find("2026") != std::string::npos);
        const Decoded back = decode(folder, target);
        CHECK(back.frames == Approx(44100).margin(format == AudioFileFormat::Flac ? 0 : 4096));
        CHECK(back.rms == Approx(kSineRms).epsilon(format == AudioFileFormat::Flac ? 0.0005 : 0.03));
    }
}
