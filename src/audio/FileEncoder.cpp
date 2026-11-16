// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/FileEncoder.hpp"

#include <vloom/engine/sample/SampleBuffer.hpp>
#include <vloom/engine/sample/WavReader.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace vloomdaw {

namespace {

/// What FFmpeg needs to write a format: the encoder and the container.
struct Target {
    const char* encoder;
    const char* muxer;
    AVSampleFormat sampleFormat; ///< what the encoder is fed (FLAC: by bit depth)
    int maxRate; ///< the highest sample rate it takes
};

Target targetOf(AudioFileFormat format)
{
    switch (format) {
    case AudioFileFormat::Flac:
        return {"flac", "flac", AV_SAMPLE_FMT_S32, 384000};
    case AudioFileFormat::Mp3:
        return {"libmp3lame", "mp3", AV_SAMPLE_FMT_FLTP, 48000};
    case AudioFileFormat::Ogg:
        return {"libvorbis", "ogg", AV_SAMPLE_FMT_FLTP, 192000};
    case AudioFileFormat::M4a:
        return {"aac", "ipod", AV_SAMPLE_FMT_FLTP, 96000};
    case AudioFileFormat::Wav:
        break;
    }
    return {nullptr, nullptr, AV_SAMPLE_FMT_NONE, 0};
}

struct OutputDeleter {
    void operator()(AVFormatContext* p) const noexcept
    {
        if (p != nullptr && p->pb != nullptr)
            avio_closep(&p->pb);
        avformat_free_context(p);
    }
};
struct CodecDeleter {
    void operator()(AVCodecContext* p) const noexcept { avcodec_free_context(&p); }
};
struct ResamplerDeleter {
    void operator()(SwrContext* p) const noexcept { swr_free(&p); }
};
struct PacketDeleter {
    void operator()(AVPacket* p) const noexcept { av_packet_free(&p); }
};
struct FrameDeleter {
    void operator()(AVFrame* p) const noexcept { av_frame_free(&p); }
};

std::string describe(int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof(text));
    return text;
}

bool fail(std::string* error, std::string why)
{
    if (error != nullptr)
        *error = std::move(why);
    return false;
}

std::filesystem::path pathOf(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

/// A tag as it is written: no control characters, no spaces at its ends, at
/// most AudioMetadata::kMaxField bytes (cut between characters).
std::string tagText(const std::string& text)
{
    std::string out;
    for (const char c : text)
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7f)
            out.push_back(c);
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    out.erase(0, out.find_first_not_of(' '));
    if (out.size() > AudioMetadata::kMaxField) {
        std::size_t end = AudioMetadata::kMaxField;
        while (end > 0 && (static_cast<unsigned char>(out[end]) & 0xC0) == 0x80)
            --end; // not in the middle of a UTF-8 character
        out.resize(end);
    }
    return out;
}

/// The fields with something in them, under the given names.
struct TagNames {
    const char* title;
    const char* artist;
    const char* album;
    const char* year;
    const char* genre;
    const char* comment;
};

std::vector<std::pair<const char*, std::string>> tags(const AudioMetadata& metadata, const TagNames& names)
{
    std::vector<std::pair<const char*, std::string>> out;
    const auto add = [&out](const char* name, const std::string& value) {
        if (std::string text = tagText(value); !text.empty())
            out.emplace_back(name, std::move(text));
    };
    add(names.title, metadata.title);
    add(names.artist, metadata.artist);
    add(names.album, metadata.album);
    add(names.year, metadata.year);
    add(names.genre, metadata.genre);
    add(names.comment, metadata.comment);
    return out;
}

void put32(std::vector<char>& out, std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<char>((value >> shift) & 0xFF));
}

} // namespace

std::string_view audioFileExtension(AudioFileFormat format)
{
    switch (format) {
    case AudioFileFormat::Wav:
        return "wav";
    case AudioFileFormat::Flac:
        return "flac";
    case AudioFileFormat::Mp3:
        return "mp3";
    case AudioFileFormat::Ogg:
        return "ogg";
    case AudioFileFormat::M4a:
        return "m4a";
    }
    return "wav";
}

bool audioEncoderAvailable(AudioFileFormat format)
{
    if (format == AudioFileFormat::Wav)
        return true;
    const Target target = targetOf(format);
    return avcodec_find_encoder_by_name(target.encoder) != nullptr
        && av_guess_format(target.muxer, nullptr, nullptr) != nullptr;
}

const std::vector<int>& audioBitrates(AudioFileFormat format)
{
    static const std::vector<int> kNone;
    static const std::vector<int> kLossy = {96, 128, 160, 192, 256, 320};
    switch (format) {
    case AudioFileFormat::Mp3:
    case AudioFileFormat::Ogg:
    case AudioFileFormat::M4a:
        return kLossy;
    case AudioFileFormat::Wav:
    case AudioFileFormat::Flac:
        break;
    }
    return kNone;
}

bool writeWavMetadata(const std::string& path, const AudioMetadata& metadata, std::string* error)
{
    // RIFF's own names for the fields (the INFO list).
    const auto fields = tags(metadata, {"INAM", "IART", "IPRD", "ICRD", "IGNR", "ICMT"});
    if (fields.empty())
        return true;
    std::vector<char> list = {'I', 'N', 'F', 'O'};
    for (const auto& [name, text] : fields) {
        list.insert(list.end(), name, name + 4);
        put32(list, static_cast<std::uint32_t>(text.size() + 1)); // with its closing zero
        list.insert(list.end(), text.begin(), text.end());
        list.push_back('\0');
        if (list.size() % 2 != 0)
            list.push_back('\0'); // chunks start on even bytes
    }

    const std::filesystem::path file = pathOf(path);
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(file, ec);
    const std::uintmax_t pad = size % 2;
    const std::uintmax_t grown = size + pad + 8 + list.size();
    if (ec || size < 12 || grown - 8 > 0xFFFFFFFFULL)
        return fail(error, "the file is not a WAV file the tags fit in");
    std::fstream io(file, std::ios::binary | std::ios::in | std::ios::out);
    char head[12] = {};
    io.read(head, sizeof(head));
    if (!io || std::string_view(head, 4) != "RIFF" || std::string_view(head + 8, 4) != "WAVE")
        return fail(error, "the file is not a WAV file the tags fit in");
    // The chunk goes after the audio, then the RIFF size at the front takes it in.
    std::vector<char> chunk(pad, '\0');
    chunk.insert(chunk.end(), {'L', 'I', 'S', 'T'});
    put32(chunk, static_cast<std::uint32_t>(list.size()));
    chunk.insert(chunk.end(), list.begin(), list.end());
    io.seekp(0, std::ios::end);
    io.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    std::vector<char> riffSize;
    put32(riffSize, static_cast<std::uint32_t>(grown - 8));
    io.seekp(4);
    io.write(riffSize.data(), 4);
    io.flush();
    if (!io) {
        // Back to the audio alone: the old size in the header still describes it.
        io.close();
        std::filesystem::resize_file(file, size, ec);
        return fail(error, "the tags cannot be written");
    }
    return true;
}

bool encodeWavFile(const std::string& wav, const std::string& target, const EncodeOptions& options,
    std::string* error, const EncodeProgress& progress)
{
    static std::once_flag quiet;
    std::call_once(quiet, [] { av_log_set_level(AV_LOG_QUIET); });

    const Target kind = targetOf(options.format);
    if (kind.encoder == nullptr)
        return fail(error, "this format is not written here");
    const AVCodec* codec = avcodec_find_encoder_by_name(kind.encoder);
    if (codec == nullptr)
        return fail(error, "this build of the app cannot write that format");

    // The export as the engine wrote it; its reader checks the file.
    vloom::WavReadLimits limits;
    limits.maxFileBytes = std::uint64_t{4} << 30;
    limits.maxDecodedBytes = std::uint64_t{4} << 30;
    limits.maxFrames = std::int64_t{192000} * 60 * 120;
    vloom::WavReadResult read = vloom::readWavFile(wav, limits);
    if (!read.buffer)
        return fail(error, "the rendered audio cannot be read (" + read.error + ")");
    const vloom::SampleBuffer& audio = *read.buffer;
    const int channels = audio.channels();
    const int inRate = static_cast<int>(audio.sampleRate());
    const std::int64_t frames = audio.frames();
    if (frames <= 0 || channels < 1 || channels > 2 || inRate <= 0)
        return fail(error, "the rendered audio is empty");
    const int outRate = inRate > kind.maxRate ? 48000 : inRate;
    const bool sixteen = options.format == AudioFileFormat::Flac && options.bitDepth <= 16;
    const AVSampleFormat sampleFormat = sixteen ? AV_SAMPLE_FMT_S16 : kind.sampleFormat;

    AVChannelLayout layout{};
    av_channel_layout_default(&layout, channels);

    const std::unique_ptr<AVCodecContext, CodecDeleter> encoder(avcodec_alloc_context3(codec));
    if (encoder == nullptr)
        return fail(error, "not enough memory");
    encoder->sample_fmt = sampleFormat;
    encoder->sample_rate = outRate;
    encoder->time_base = AVRational{1, outRate};
    if (av_channel_layout_copy(&encoder->ch_layout, &layout) < 0)
        return fail(error, "not enough memory");
    if (options.format == AudioFileFormat::Flac) {
        encoder->bits_per_raw_sample = sixteen ? 16 : 24;
    } else {
        const std::vector<int>& rates = audioBitrates(options.format);
        encoder->bit_rate
            = static_cast<std::int64_t>(std::clamp(options.bitrateKbps, rates.front(), rates.back())) * 1000;
    }

    // Written next to the target and renamed when whole, so a failed or
    // cancelled export never leaves half a file under the name asked for.
    const std::string part = target + ".part";
    AVFormatContext* rawOutput = nullptr;
    if (const int code = avformat_alloc_output_context2(&rawOutput, nullptr, kind.muxer, part.c_str());
        code < 0 || rawOutput == nullptr)
        return fail(error, "this build of the app cannot write that format");
    std::unique_ptr<AVFormatContext, OutputDeleter> output(rawOutput);
    const auto discard = [&] {
        output.reset(); // closes the file
        std::error_code ec;
        std::filesystem::remove(pathOf(part), ec);
    };
    if ((output->oformat->flags & AVFMT_GLOBALHEADER) != 0)
        encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (const int code = avcodec_open2(encoder.get(), codec, nullptr); code < 0)
        return fail(error, "the encoder cannot start (" + describe(code) + ")");

    AVStream* stream = avformat_new_stream(output.get(), nullptr);
    if (stream == nullptr || avcodec_parameters_from_context(stream->codecpar, encoder.get()) < 0)
        return fail(error, "not enough memory");
    stream->time_base = encoder->time_base;
    // FFmpeg's names; each muxer writes them its own way (ID3, Vorbis
    // comments, MP4 atoms). Ogg keeps them with the stream.
    for (const auto& [name, text] :
        tags(options.metadata, {"title", "artist", "album", "date", "genre", "comment"})) {
        av_dict_set(&output->metadata, name, text.c_str(), 0);
        if (options.format == AudioFileFormat::Ogg)
            av_dict_set(&stream->metadata, name, text.c_str(), 0);
    }
    if (const int code = avio_open(&output->pb, part.c_str(), AVIO_FLAG_WRITE); code < 0)
        return fail(error, "the file cannot be written (" + describe(code) + ")");
    if (const int code = avformat_write_header(output.get(), nullptr); code < 0) {
        discard();
        return fail(error, "the file cannot be written (" + describe(code) + ")");
    }

    // The engine's planar float to what the encoder takes.
    SwrContext* rawResampler = nullptr;
    const int swrCode = swr_alloc_set_opts2(
        &rawResampler, &layout, sampleFormat, outRate, &layout, AV_SAMPLE_FMT_FLTP, inRate, 0, nullptr);
    const std::unique_ptr<SwrContext, ResamplerDeleter> resampler(rawResampler);
    if (swrCode >= 0 && resampler != nullptr && sixteen && options.dither)
        av_opt_set_int(resampler.get(), "dither_method", SWR_DITHER_TRIANGULAR, 0);
    if (swrCode < 0 || resampler == nullptr || swr_init(resampler.get()) < 0) {
        discard();
        return fail(error, "the audio cannot be converted");
    }

    const std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
    const std::unique_ptr<AVFrame, FrameDeleter> frame(av_frame_alloc());
    if (packet == nullptr || frame == nullptr) {
        discard();
        return fail(error, "not enough memory");
    }
    // Encoders with a fixed frame size take exactly that much, but for the last frame.
    const int frameSize = encoder->frame_size > 0 ? encoder->frame_size : 4096;
    std::string failure;
    std::int64_t written = 0; // samples sent to the encoder, for the time stamps

    // Writes every packet the encoder has ready.
    const auto drain = [&] {
        for (;;) {
            const int code = avcodec_receive_packet(encoder.get(), packet.get());
            if (code == AVERROR(EAGAIN) || code == AVERROR_EOF)
                return true;
            if (code < 0) {
                failure = "encoding failed (" + describe(code) + ")";
                return false;
            }
            av_packet_rescale_ts(packet.get(), encoder->time_base, stream->time_base);
            packet->stream_index = stream->index;
            const int wrote = av_interleaved_write_frame(output.get(), packet.get());
            av_packet_unref(packet.get());
            if (wrote < 0) {
                failure = "the file cannot be written (" + describe(wrote) + ")";
                return false;
            }
        }
    };
    // Sends up to `count` converted samples waiting in the resampler to the encoder.
    const auto sendFrame = [&](int count) {
        frame->nb_samples = count;
        frame->format = sampleFormat;
        frame->sample_rate = outRate;
        if (av_channel_layout_copy(&frame->ch_layout, &layout) < 0
            || av_frame_get_buffer(frame.get(), 0) < 0) {
            failure = "not enough memory";
            return false;
        }
        const int made = swr_convert(resampler.get(), frame->extended_data, count, nullptr, 0);
        if (made < 0) {
            failure = "the audio cannot be converted";
            return false;
        }
        bool ok = true;
        if (made > 0) {
            frame->nb_samples = made;
            frame->pts = written;
            written += made;
            const int code = avcodec_send_frame(encoder.get(), frame.get());
            if (code < 0)
                failure = "encoding failed (" + describe(code) + ")";
            ok = code >= 0 && drain();
        }
        av_frame_unref(frame.get());
        return ok;
    };

    bool ok = true;
    bool cancelled = false;
    constexpr std::int64_t kBlock = 16384;
    for (std::int64_t at = 0; at < frames && ok; at += kBlock) {
        const int count = static_cast<int>(std::min(kBlock, frames - at));
        const std::uint8_t* in[2] = {reinterpret_cast<const std::uint8_t*>(audio.channel(0) + at),
            reinterpret_cast<const std::uint8_t*>(audio.channel(channels > 1 ? 1 : 0) + at)};
        if (swr_convert(resampler.get(), nullptr, 0, in, count) < 0) {
            failure = "the audio cannot be converted";
            ok = false;
        }
        while (ok && swr_get_out_samples(resampler.get(), 0) >= frameSize)
            ok = sendFrame(frameSize);
        if (ok && progress && !progress(static_cast<double>(at + count) / static_cast<double>(frames))) {
            cancelled = true;
            ok = false;
        }
    }
    // What is left in the resampler: whole frames, then a last short one.
    for (int left = 0; ok && (left = swr_get_out_samples(resampler.get(), 0)) > 0;) {
        const std::int64_t before = written;
        ok = sendFrame(std::min(left, frameSize));
        if (written == before)
            break; // nothing more comes out
    }
    if (ok) {
        const int code = avcodec_send_frame(encoder.get(), nullptr); // the end
        if (code < 0 && code != AVERROR_EOF)
            failure = "encoding failed (" + describe(code) + ")";
        ok = (code >= 0 || code == AVERROR_EOF) && drain();
    }
    if (ok) {
        if (const int code = av_write_trailer(output.get()); code < 0) {
            failure = "the file cannot be written (" + describe(code) + ")";
            ok = false;
        }
    }
    av_channel_layout_uninit(&layout);
    if (!ok) {
        discard();
        return fail(error, cancelled ? std::string() : failure);
    }
    output.reset(); // closes the file
    std::error_code ec;
    std::filesystem::rename(pathOf(part), pathOf(target), ec);
    if (ec) {
        std::filesystem::remove(pathOf(part), ec);
        return fail(error, "the file cannot be written under that name");
    }
    return true;
}

} // namespace vloomdaw
