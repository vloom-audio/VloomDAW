// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/ChannelRackAudio.hpp"

#include <vloom/engine/render/OfflineRenderer.hpp>
#include <vloom/engine/render/WavWriter.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace vloomdaw;

namespace {

constexpr double kRate = 48000.0;
constexpr std::size_t kStepFrames = 6000; // a 16th note at 120 BPM, 48 kHz

/// A folder with a one-frame impulse WAV, removed afterwards.
struct Samples {
    std::filesystem::path folder;
    std::string impulse;
    Samples()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        folder = std::filesystem::temp_directory_path() / ("vloomdaw_rack_" + std::to_string(stamp));
        std::filesystem::create_directories(folder);
        impulse = (folder / "hit.wav").string();
        const std::vector<float> one{1.0f};
        vloom::writeWavFloat32(impulse, one, 1, static_cast<int>(kRate));
    }
    ~Samples()
    {
        std::error_code ec;
        std::filesystem::remove_all(folder, ec);
    }
};

std::vector<std::size_t> onsets(const std::vector<float>& interleaved)
{
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < interleaved.size() / 2; ++i)
        if (std::abs(interleaved[i * 2]) > 1e-6f)
            out.push_back(i);
    return out;
}

std::vector<float> render(vloom::Engine& engine, double seconds)
{
    vloom::OfflineRenderOptions options;
    options.seconds = seconds;
    options.blockSize = 256;
    return vloom::renderInterleaved(engine, options);
}

void prepare(vloom::Engine& engine)
{
    engine.prepare(kRate, 256);
    engine.setTempo(120.0);
    engine.setMetronomeEnabled(false);
}

} // namespace

TEST_CASE("The channel rack plays the current pattern's steps with each channel's sample", "[audio]")
{
    const Samples samples;
    Project project;
    project.setChannelSample(0, samples.impulse);
    project.setStep(0, 0, 0, true);
    project.setStep(0, 0, 4, true);
    project.setStep(0, 1, 2, true); // channel 1 has no sample: silent

    vloom::Engine engine;
    prepare(engine);
    ChannelRackAudio rack;
    CHECK(rack.sync(engine, project, 0, kRate).empty());
    engine.play();

    const auto audio = render(engine, 2.6); // a bar is 2 s: the pattern loops once
    const std::vector<std::size_t> expected{0, 4 * kStepFrames, 16 * kStepFrames, 20 * kStepFrames};
    CHECK(onsets(audio) == expected);
}

TEST_CASE("Muting and step edits reach the engine on the next sync", "[audio]")
{
    const Samples samples;
    Project project;
    project.setChannelSample(0, samples.impulse);
    project.setStep(0, 0, 0, true);

    vloom::Engine engine;
    prepare(engine);
    ChannelRackAudio rack;
    rack.sync(engine, project, 0, kRate);
    engine.play();
    CHECK(onsets(render(engine, 0.5)) == std::vector<std::size_t>{0});

    project.setStep(0, 0, 8, true); // step 8 is at 1.0 s, 0.5 s from here
    rack.sync(engine, project, 0, kRate);
    CHECK(onsets(render(engine, 1.0)) == std::vector<std::size_t>{8 * kStepFrames - 24000});

    project.setChannelMuted(0, true);
    rack.sync(engine, project, 0, kRate);
    CHECK(onsets(render(engine, 2.0)).empty());
}

TEST_CASE("A sample that cannot be loaded is reported once", "[audio]")
{
    const Samples samples;
    Project project;
    const std::string missing = (samples.folder / "missing.wav").string();
    project.setChannelSample(2, missing);

    vloom::Engine engine;
    prepare(engine);
    ChannelRackAudio rack;
    const auto errors = rack.sync(engine, project, 0, kRate);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].channel == 2);
    CHECK(errors[0].path == missing);
    CHECK_FALSE(errors[0].message.empty());
    CHECK(rack.sync(engine, project, 0, kRate).empty()); // not again

    rack.reset();
    CHECK(rack.sync(engine, project, 0, kRate).size() == 1); // tried again after a reset
}

TEST_CASE("Nothing is published without a running device", "[audio]")
{
    const Samples samples;
    Project project;
    project.setChannelSample(0, samples.impulse);
    project.setStep(0, 0, 0, true);

    vloom::Engine engine;
    prepare(engine);
    ChannelRackAudio rack;
    rack.sync(engine, project, 0, 0.0); // no device
    engine.play();
    CHECK(onsets(render(engine, 0.2)).empty());
}

TEST_CASE("A channel's volume and insert reach the engine", "[audio]")
{
    const Samples samples;
    Project project;
    project.setChannelSample(0, samples.impulse);
    project.setStep(0, 0, 0, true);
    project.setChannelVolume(0, 0.8); // unity
    project.setChannelInsert(0, 2);

    vloom::Engine engine;
    prepare(engine);
    ChannelRackAudio rack;
    rack.sync(engine, project, 0, kRate);
    engine.play();
    const auto audio = render(engine, 0.05);
    CHECK(audio[0] > 0.7f); // the impulse, centred: about 0.707
    CHECK(engine.takeInsertPeak(2, 0) > 0.7f); // went through insert 2
    CHECK(engine.takeInsertPeak(1, 0) == 0.0f);
}

TEST_CASE("Song mode plays the playlist: pattern clips and audio clips, once", "[audio]")
{
    const Samples samples;
    Project project;
    project.setChannelSample(0, samples.impulse);
    project.setStep(0, 0, 0, true); // pattern 1: a hit on its first step
    project.addTrack();
    project.addClip(Clip{0, 0, kTicksPerBar}); // bar 2
    const auto clipChannel = project.addChannel("hit");
    project.setChannelSample(clipChannel, samples.impulse);
    project.addAudioClip(AudioClip{clipChannel, 1, 2 * kTicksPerBeat}); // beat 3

    vloom::Engine engine;
    prepare(engine);
    ChannelRackAudio rack;
    CHECK(rack.sync(engine, project, 0, kRate, true).empty());
    engine.play();
    const auto audio = render(engine, 5.0);
    // Beat 3 (1 s) for the audio clip, bar 2 (2 s) for the pattern; no loop.
    const std::vector<std::size_t> expected{8 * kStepFrames, 16 * kStepFrames};
    CHECK(onsets(audio) == expected);
}

TEST_CASE("Song mode leaves out clips on muted tracks and on others while one is solo", "[audio]")
{
    const Samples samples;
    Project project;
    project.setChannelSample(0, samples.impulse);
    project.setStep(0, 0, 0, true);
    project.addTrack();
    project.addClip(Clip{0, 0, 0}); // track 1, bar 1
    const auto clipChannel = project.addChannel("hit");
    project.setChannelSample(clipChannel, samples.impulse);
    project.addAudioClip(AudioClip{clipChannel, 1, 2 * kTicksPerBeat}); // track 2, beat 3

    const auto play = [&] {
        vloom::Engine engine;
        prepare(engine);
        ChannelRackAudio rack;
        rack.sync(engine, project, 0, kRate, true);
        engine.play();
        return onsets(render(engine, 1.5));
    };
    project.setTrackMuted(0, true);
    CHECK(play() == std::vector<std::size_t>{8 * kStepFrames});
    project.setTrackMuted(0, false);
    project.setTrackSolo(0, true);
    CHECK(play() == std::vector<std::size_t>{0});
    project.setTrackSolo(1, true);
    CHECK(play() == std::vector<std::size_t>{0, 8 * kStepFrames});
    project.setTrackSolo(0, false);
    project.setTrackSolo(1, false);

    // Track 2 as a sub-track of track 1: muting the group silences both.
    REQUIRE(project.indentTrack(1));
    project.setTrackMuted(0, true);
    CHECK(play().empty());
}

TEST_CASE("Muted clips stay silent in song mode", "[audio]")
{
    const Samples samples;
    Project project;
    project.setChannelSample(0, samples.impulse);
    project.setStep(0, 0, 0, true);
    Clip pattern{0, 0, 0};
    pattern.muted = true;
    project.addClip(pattern); // bar 1, muted
    project.addClip(Clip{0, 0, kTicksPerBar}); // bar 2
    AudioClip audio{0, 0, 2 * kTicksPerBeat};
    audio.muted = true;
    project.addAudioClip(audio); // beat 3, muted

    vloom::Engine engine;
    prepare(engine);
    ChannelRackAudio rack;
    rack.sync(engine, project, 0, kRate, true);
    engine.play();
    CHECK(onsets(render(engine, 2.5)) == std::vector<std::size_t>{16 * kStepFrames});
}

TEST_CASE("Cut and stretched pattern clips play the right part of their pattern", "[audio]")
{
    const Samples samples;
    Project project;
    project.setChannelSample(0, samples.impulse);
    project.setStep(0, 0, 0, true); // pattern 1 (one bar): steps 0 and 8
    project.setStep(0, 0, 8, true);

    // The second half of a cut clip: half a bar long, 8 steps into the pattern.
    Clip half{0, 0, 0};
    half.offset = 8 * kTicksPerStep;
    half.length = 8 * kTicksPerStep;
    project.addClip(half);
    // Stretched to two bars from bar 2: the pattern plays twice.
    Clip twice{0, 0, kTicksPerBar};
    twice.length = 2 * kTicksPerBar;
    project.addClip(twice);

    vloom::Engine engine;
    prepare(engine);
    ChannelRackAudio rack;
    rack.sync(engine, project, 0, kRate, true);
    engine.play();
    const std::vector<std::size_t> expected{
        0, 16 * kStepFrames, 24 * kStepFrames, 32 * kStepFrames, 40 * kStepFrames};
    CHECK(onsets(render(engine, 6.5)) == expected);
}

TEST_CASE("An audio clip fitted to the tempo follows it: resampled or stretched", "[audio][fit]")
{
    // A sample with clicks one second apart, "recorded" at 60 BPM: a beat each.
    const Samples samples;
    const std::string clicks = (samples.folder / "clicks.wav").string();
    std::vector<float> data(static_cast<std::size_t>(kRate * 2), 0.0f);
    data[0] = 1.0f;
    data[static_cast<std::size_t>(kRate)] = 1.0f;
    REQUIRE(vloom::writeWavFloat32(clicks, data, 1, static_cast<int>(kRate)));

    Project project;
    project.setChannelSample(0, clicks);
    AudioClip clip{0, 0, 0};
    clip.fit = AudioClip::Fit::Resample;
    clip.sourceBpm = 60.0;
    project.addAudioClip(clip);

    // At 120 BPM, resampled at twice the speed: the second click after half a second.
    {
        vloom::Engine engine;
        prepare(engine);
        ChannelRackAudio rack;
        rack.setTempo(120.0);
        rack.sync(engine, project, 0, kRate, true);
        engine.play();
        const auto hits = onsets(render(engine, 1.5));
        REQUIRE_FALSE(hits.empty());
        CHECK(hits.front() == 0);
        // The interpolated click spreads over a few frames around 24000.
        CHECK(std::any_of(hits.begin(), hits.end(), [](std::size_t f) { return f >= 23990 && f <= 24010; }));
        CHECK(std::none_of(hits.begin(), hits.end(), [](std::size_t f) { return f >= 40000; }));
    }

    // Stretch: the rack asks for the stretched copy and plays it as it is.
    {
        project.setAudioClip(0, [&] {
            AudioClip stretched = project.audioClips()[0];
            stretched.fit = AudioClip::Fit::Stretch;
            return stretched;
        }());
        vloom::Engine engine;
        prepare(engine);
        ChannelRackAudio rack;
        rack.setTempo(120.0);
        double askedRatio = 0.0;
        rack.setStretchProvider([&](const std::string&, double ratio) {
            askedRatio = ratio;
            auto copy = std::make_shared<vloom::SampleBuffer>(1, static_cast<std::int64_t>(kRate), kRate);
            copy->channel(0)[100] = 1.0f; // stands for the stretched audio
            return std::shared_ptr<const vloom::SampleBuffer>(std::move(copy));
        });
        rack.sync(engine, project, 0, kRate, true);
        engine.play();
        CHECK(askedRatio == Catch::Approx(0.5)); // half as long at twice the tempo
        CHECK(onsets(render(engine, 0.5)) == std::vector<std::size_t>{100});
    }
}

TEST_CASE("Pattern mode leaves the playlist alone", "[audio]")
{
    const Samples samples;
    Project project;
    project.setChannelSample(0, samples.impulse);
    project.addAudioClip(AudioClip{0, 0, kTicksPerBeat});
    vloom::Engine engine;
    prepare(engine);
    ChannelRackAudio rack;
    rack.sync(engine, project, 0, kRate, false);
    engine.play();
    CHECK(onsets(render(engine, 1.0)).empty());
}

TEST_CASE("An audio clip plays through its channel's volume and mute", "[audio]")
{
    const Samples samples;
    Project project;
    project.setChannelSample(0, samples.impulse);
    project.addAudioClip(AudioClip{0, 0, 0});
    vloom::Engine engine;
    prepare(engine);
    ChannelRackAudio rack;

    project.setChannelMuted(0, true);
    rack.sync(engine, project, 0, kRate, true);
    engine.play();
    CHECK(onsets(render(engine, 0.2)).empty());
}

TEST_CASE("The playlist can load a sample for its waveform", "[audio]")
{
    const Samples samples;
    Project project;
    vloom::Engine engine;
    prepare(engine);
    ChannelRackAudio rack;
    rack.sync(engine, project, 0, kRate, true);
    CHECK(rack.sample((samples.folder / "missing.wav").string()) == nullptr);
    const auto sample = rack.sample(samples.impulse);
    REQUIRE(sample != nullptr);
    CHECK(sample->frames() == 1);
}
