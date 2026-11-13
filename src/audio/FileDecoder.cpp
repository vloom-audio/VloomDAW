// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/FileDecoder.hpp"

#include <vloom/engine/render/WavWriter.hpp>

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
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace vloomdaw {

namespace {

/// Containers FFmpeg may open (demuxer names) and codecs it may decode.
/// Everything else (playlists, concat lists, images, video, network
/// formats) is refused before FFmpeg looks further into the file.
constexpr const char* kFormats = "mp3,flac,ogg,wav,aiff,mov,mp4,m4a";
constexpr const char* kCodecs = "mp3,mp3float,flac,vorbis,opus,aac,alac,"
                                "pcm_u8,pcm_s8,pcm_s16le,pcm_s16be,pcm_s24le,pcm_s24be,pcm_s32le,pcm_s32be,"
                                "pcm_f32le,pcm_f32be,pcm_f64le,pcm_f64be";

struct FormatDeleter {
    void operator()(AVFormatContext* p) const noexcept { avformat_close_input(&p); }
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

/// Frees an AVDictionary when it goes out of scope.
struct Options {
    AVDictionary* dict = nullptr;
    Options() = default;
    Options(const Options&) = delete;
    Options& operator=(const Options&) = delete;
    ~Options() { av_dict_free(&dict); }
    void set(const char* key, const char* value) { av_dict_set(&dict, key, value, 0); }
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

} // namespace

bool decodeToWav(const std::string& path, const std::string& target, int sampleRate, std::string* error,
    const DecodeLimits& limits)
{
    // FFmpeg's own messages are not for the user; errors come back here.
    static std::once_flag quiet;
    std::call_once(quiet, [] { av_log_set_level(AV_LOG_QUIET); });

    // Open: local files only, the listed containers only.
    Options openOptions;
    openOptions.set("protocol_whitelist", "file");
    openOptions.set("format_whitelist", kFormats);
    openOptions.set("codec_whitelist", kCodecs);
    AVFormatContext* rawFormat = nullptr;
    if (const int code = avformat_open_input(&rawFormat, path.c_str(), nullptr, &openOptions.dict); code < 0)
        return fail(error, "it cannot be opened as audio (" + describe(code) + ")");
    const std::unique_ptr<AVFormatContext, FormatDeleter> format(rawFormat);
    if (const int code = avformat_find_stream_info(format.get(), nullptr); code < 0)
        return fail(error, "its audio cannot be read (" + describe(code) + ")");

    const AVCodec* codec = nullptr;
    const int stream = av_find_best_stream(format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (stream < 0 || codec == nullptr)
        return fail(error, "it has no audio the app can decode");
    // Only this stream's packets are read; the others are skipped unparsed.
    for (unsigned i = 0; i < format->nb_streams; ++i)
        format->streams[i]->discard = static_cast<int>(i) == stream ? AVDISCARD_DEFAULT : AVDISCARD_ALL;

    const std::unique_ptr<AVCodecContext, CodecDeleter> decoder(avcodec_alloc_context3(codec));
    if (decoder == nullptr)
        return fail(error, "not enough memory to decode it");
    if (avcodec_parameters_to_context(decoder.get(), format->streams[stream]->codecpar) < 0)
        return fail(error, "its audio cannot be read");
    Options codecOptions;
    codecOptions.set("codec_whitelist", kCodecs);
    if (const int code = avcodec_open2(decoder.get(), codec, &codecOptions.dict); code < 0)
        return fail(error, "its audio cannot be decoded (" + describe(code) + ")");

    AVChannelLayout inLayout{};
    if (decoder->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC || decoder->ch_layout.nb_channels <= 0)
        av_channel_layout_default(&inLayout, decoder->ch_layout.nb_channels);
    else if (av_channel_layout_copy(&inLayout, &decoder->ch_layout) < 0)
        return fail(error, "not enough memory to decode it");
    const int channels = inLayout.nb_channels;
    const int inRate = decoder->sample_rate;
    if (channels <= 0 || channels > limits.maxChannels || inRate <= 0) {
        av_channel_layout_uninit(&inLayout);
        return fail(error, "it has no usable audio (channels or rate)");
    }
    const int outRate = sampleRate > 0 ? sampleRate : inRate;

    // Interleaved 32-bit float at outRate, the same channels as the file.
    SwrContext* rawResampler = nullptr;
    const int swrCode = swr_alloc_set_opts2(&rawResampler, &inLayout, AV_SAMPLE_FMT_FLT, outRate, &inLayout,
        decoder->sample_fmt, inRate, 0, nullptr);
    av_channel_layout_uninit(&inLayout);
    const std::unique_ptr<SwrContext, ResamplerDeleter> resampler(rawResampler);
    if (swrCode < 0 || resampler == nullptr || swr_init(resampler.get()) < 0)
        return fail(error, "its audio cannot be converted");

    const std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
    const std::unique_ptr<AVFrame, FrameDeleter> frame(av_frame_alloc());
    if (packet == nullptr || frame == nullptr)
        return fail(error, "not enough memory to decode it");

    std::vector<float> samples;
    const auto maxSamples = static_cast<std::size_t>(std::min(
        limits.maxSeconds * outRate * channels, static_cast<double>(limits.maxBytes / sizeof(float))));
    std::string failure;

    // Converts what the resampler gives for `input` (null: flush) and appends it.
    const auto convert = [&](const AVFrame* input) {
        const int inCount = input != nullptr ? input->nb_samples : 0;
        const int room = swr_get_out_samples(resampler.get(), inCount);
        if (room < 0) {
            failure = "its audio cannot be converted";
            return false;
        }
        const std::size_t before = samples.size();
        if (before + static_cast<std::size_t>(room) * channels > maxSamples) {
            failure = "it is longer than the app takes (20 minutes, 1 GiB)";
            return false;
        }
        samples.resize(before + static_cast<std::size_t>(room) * channels);
        auto* out = reinterpret_cast<std::uint8_t*>(samples.data() + before);
        const int made = swr_convert(resampler.get(), &out, room,
            input != nullptr ? const_cast<const std::uint8_t**>(input->extended_data) : nullptr, inCount);
        if (made < 0) {
            samples.resize(before);
            failure = "its audio cannot be converted";
            return false;
        }
        samples.resize(before + static_cast<std::size_t>(made) * channels);
        return true;
    };
    // Takes every frame the decoder has ready.
    const auto drain = [&] {
        for (;;) {
            const int code = avcodec_receive_frame(decoder.get(), frame.get());
            if (code == AVERROR(EAGAIN) || code == AVERROR_EOF)
                return true;
            if (code < 0) {
                failure = "its audio is damaged (" + describe(code) + ")";
                return false;
            }
            // The format must stay the same all the way through.
            const bool same = frame->ch_layout.nb_channels == channels && frame->sample_rate == inRate
                && frame->format == decoder->sample_fmt;
            const bool ok = same && convert(frame.get());
            if (!same)
                failure = "its audio format changes part-way";
            av_frame_unref(frame.get());
            if (!ok)
                return false;
        }
    };

    bool ok = true;
    while (ok && av_read_frame(format.get(), packet.get()) >= 0) {
        if (packet->stream_index == stream) {
            // A damaged packet is skipped, as players do; the rest still plays.
            if (avcodec_send_packet(decoder.get(), packet.get()) >= 0)
                ok = drain();
        }
        av_packet_unref(packet.get());
    }
    if (ok) {
        avcodec_send_packet(decoder.get(), nullptr); // the end: whatever the decoder still holds
        ok = drain() && convert(nullptr); // and what the resampler still holds
    }
    if (!ok)
        return fail(error, failure);
    if (samples.empty())
        return fail(error, "it has no audio");

    std::string writeError;
    if (!vloom::writeWavFloat32(target, samples, channels, outRate, &writeError))
        return fail(error, "the decoded copy cannot be written (" + writeError + ")");
    return true;
}

} // namespace vloomdaw
