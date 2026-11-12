// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/ChannelRackAudio.hpp"

#include <vloom/engine/render/OfflineRenderer.hpp>
#include <vloom/engine/render/WavWriter.hpp>

#include <catch2/catch_test_macros.hpp>

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
