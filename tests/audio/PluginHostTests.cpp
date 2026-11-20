// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/PluginHost.hpp"
#include "core/PluginMetadata.hpp"
#include "core/ProjectFile.hpp"

#include <vloom/engine/render/WavWriter.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace vloomdaw;

TEST_CASE("A plugin that cannot be loaded gives no handle and says why", "[plugins]")
{
    PluginHost host;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto missing
        = std::filesystem::temp_directory_path() / ("vloomdaw_missing_" + std::to_string(stamp) + ".clap");
    for (const PluginFormat format : {PluginFormat::Clap, PluginFormat::Vst3, PluginFormat::Vst2}) {
        const PluginHost::Loaded loaded = host.load(format, missing, {}, "Missing", 48000.0);
        CHECK(loaded.handle == 0);
        CHECK_FALSE(loaded.error.empty());
    }

    // A file that is not a plugin at all.
    const auto bogus
        = std::filesystem::temp_directory_path() / ("vloomdaw_bogus_" + std::to_string(stamp) + ".clap");
    std::ofstream(bogus) << "not a plugin";
    const PluginHost::Loaded loaded = host.load(PluginFormat::Clap, bogus, {}, "Bogus", 48000.0);
    CHECK(loaded.handle == 0);
    CHECK_FALSE(loaded.error.empty());
    std::error_code ec;
    std::filesystem::remove(bogus, ec);

    CHECK(host.size() == 0);
    CHECK(host.instance(1) == nullptr);
    CHECK(host.resolver()(1) == nullptr);
    host.retainOnly({}); // nothing to drop
    host.idle();
}

TEST_CASE("The plugin to load is found by id, then by name, then as a file's only plugin", "[plugins]")
{
    const auto plugin = [](std::string id, std::string name) {
        vloom::PluginInfo info;
        info.id = std::move(id);
        info.name = std::move(name);
        return info;
    };
    const std::vector<vloom::PluginInfo> several{
        plugin("com.a.synth", "Synth"), plugin("com.a.verb", "Verb")};

    CHECK(pickPlugin(several, "com.a.verb", "Synth").plugin == &several[1]); // the id wins
    const PluginPick gone = pickPlugin(several, "com.a.delay", "Delay");
    CHECK(gone.plugin == nullptr);
    CHECK_FALSE(gone.error.empty());

    CHECK(pickPlugin(several, {}, "Verb").plugin == &several[1]);
    // Several and no name matches (an unprobed scan names a plugin after
    // its file): no guess.
    const PluginPick unknown = pickPlugin(several, {}, "Bundle");
    CHECK(unknown.plugin == nullptr);
    CHECK(unknown.error.find("2 plugins") != std::string::npos);

    const std::vector<vloom::PluginInfo> one{plugin("com.a.synth", "Synth")};
    CHECK(pickPlugin(one, {}, "synth-file").plugin == &one[0]);
    CHECK(pickPlugin({}, {}, "Synth").plugin == nullptr);
}

TEST_CASE("The built-in synth is listed as an instrument and plays notes", "[plugins]")
{
    const std::vector<PluginInfo> builtins = builtinPlugins();
    const auto synth = std::find_if(
        builtins.begin(), builtins.end(), [](const PluginInfo& info) { return info.path == "vloom.synth"; });
    REQUIRE(synth != builtins.end());
    CHECK(synth->name == "Synth");
    CHECK(synth->category == "Instrument"); // what the lists and the picker go by
    CHECK(synth->format == PluginFormat::Builtin);

    PluginHost host;
    const PluginHost::Loaded loaded = host.load(PluginFormat::Builtin, {}, "vloom.synth", "Synth", 48000.0);
    REQUIRE(loaded.handle != 0);
    CHECK(loaded.info.kind == vloom::PluginInfo::Kind::Instrument);
    const auto instance = host.instance(loaded.handle);
    REQUIRE(instance != nullptr);
    CHECK_FALSE(instance->processor().hasEditor()); // the app draws its editor

    // A note in, sound out (rendered here: nothing reaches an audio device).
    std::vector<float> left(512, 0.0f);
    std::vector<float> right(512, 0.0f);
    const std::array<vloom::NoteEvent, 1> note{vloom::NoteEvent{0, vloom::NoteEvent::Type::On, 60, 1.0f}};
    instance->process(left.data(), right.data(), 512, vloom::TransportInfo{}, note);
    CHECK(*std::max_element(left.begin(), left.end()) > 0.01f);
}

TEST_CASE("The built-in effects are listed and load without a file", "[plugins]")
{
    const std::vector<PluginInfo> builtins = builtinPlugins();
    const auto eq = std::find_if(
        builtins.begin(), builtins.end(), [](const PluginInfo& info) { return info.path == "vloom.eq"; });
    REQUIRE(eq != builtins.end());
    CHECK(eq->name == "EQ");
    CHECK(eq->format == PluginFormat::Builtin);
    CHECK(eq->category == "EQ");
    CHECK(eq->probe == PluginInfo::Probe::Ok);
    for (const PluginInfo& info : builtins)
        CHECK(info.format == PluginFormat::Builtin);

    PluginHost host;
    const PluginHost::Loaded loaded = host.load(PluginFormat::Builtin, {}, "vloom.eq", "EQ", 48000.0);
    REQUIRE(loaded.handle != 0);
    CHECK(loaded.info.id == "vloom.eq");
    CHECK(loaded.info.kind == vloom::PluginInfo::Kind::Effect);
    const auto instance = host.instance(loaded.handle);
    REQUIRE(instance != nullptr);
    CHECK_FALSE(instance->processor().hasEditor());
    REQUIRE_FALSE(instance->processor().parameters().empty());

    // Its settings travel as a state, like a plugin's.
    const vloom::ParameterInfo gain = instance->processor().parameters()[3]; // the low shelf's gain
    CHECK(instance->setParameter(gain.id, 6.0));
    CHECK(instance->processor().parameterValue(gain.id) == 6.0);
    const std::vector<std::uint8_t> state = instance->processor().saveState();
    const PluginHost::Loaded again
        = host.loadWithState(PluginFormat::Builtin, {}, "vloom.eq", "EQ", 48000.0, state);
    REQUIRE(again.handle != 0);
    CHECK(again.error.empty());
    CHECK(host.instance(again.handle)->processor().parameterValue(gain.id) == 6.0);

    const PluginHost::Loaded unknown
        = host.load(PluginFormat::Builtin, {}, "vloom.nothing", "Nothing", 48000.0);
    CHECK(unknown.handle == 0);
    CHECK_FALSE(unknown.error.empty());
}

#if defined(VLOOMDAW_FAKE_VST2_PATH)
TEST_CASE("PluginHost loads a VST2 plugin and preserves state through saveState", "[plugins]")
{
    PluginHost host;
    const std::filesystem::path vst2Path{VLOOMDAW_FAKE_VST2_PATH};
    const PluginHost::Loaded loaded = host.load(PluginFormat::Vst2, vst2Path, {}, "Fake VST2 Synth", 48000.0);
    REQUIRE(loaded.handle != 0);
    CHECK(loaded.info.name == "Fake VST2 Synth");
    CHECK(loaded.info.kind == vloom::PluginInfo::Kind::Instrument);
    const auto inst = host.instance(loaded.handle);
    REQUIRE(inst != nullptr);
    CHECK_FALSE(inst->processor().hasFailed());
    const std::vector<std::uint8_t> state = host.saveState(loaded.handle);
    CHECK_FALSE(state.empty());
    CHECK(host.idle().empty());
}
#endif

namespace {

/// A temporary folder, removed with what it holds at the end of the test.
struct TempFolder {
    std::filesystem::path path;
    TempFolder()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("vloomdaw_builtins_" + std::to_string(stamp));
        std::filesystem::create_directories(path);
    }
    ~TempFolder()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;
};

/// A short mono WAV file of a steady tone.
void writeTone(const std::filesystem::path& path, int rate)
{
    std::vector<float> samples(static_cast<std::size_t>(rate / 10));
    for (std::size_t i = 0; i < samples.size(); ++i)
        samples[i] = 0.5f * static_cast<float>(std::sin(0.06 * static_cast<double>(i)));
    REQUIRE(vloom::writeWavFloat32(pathToUtf8(path), samples, 1, rate));
}

const PluginInfo& listed(const std::vector<PluginInfo>& list, std::string_view id)
{
    const auto it = std::find_if(
        list.begin(), list.end(), [id](const PluginInfo& info) { return pathToUtf8(info.path) == id; });
    REQUIRE(it != list.end());
    return *it;
}

} // namespace

TEST_CASE("All built-ins are listed with the app's categories", "[plugins]")
{
    const std::vector<PluginInfo> builtins = builtinPlugins();
    CHECK(builtins.size() == 20);
    CHECK(listed(builtins, "vloom.compressor").category == "Dynamics");
    CHECK(listed(builtins, "vloom.limiter").category == "Dynamics");
    CHECK(listed(builtins, "vloom.deesser").category == "Dynamics");
    CHECK(detectPluginSubCategory(listed(builtins, "vloom.deesser")) == "Dynamics"); // not by its name
    CHECK(listed(builtins, "vloom.eq").category == "EQ");
    CHECK(listed(builtins, "vloom.filter").category == "Filter");
    CHECK(listed(builtins, "vloom.delay").category == "Delay");
    CHECK(listed(builtins, "vloom.reverb").category == "Reverb");
    CHECK(listed(builtins, "vloom.chorus").category == "Modulation");
    CHECK(listed(builtins, "vloom.saturator").category == "Distortion");
    CHECK(detectPluginSubCategory(listed(builtins, "vloom.bitcrusher")) == "Amp / Drive");
    CHECK(listed(builtins, "vloom.utility").category == "Utility");
    CHECK(listed(builtins, "vloom.gain").category == "Utility");

    // Instruments go on channels, each with its type.
    CHECK(pluginMainGroup(listed(builtins, "vloom.synth")) == "Instrument");
    CHECK(effectiveInstrumentType(listed(builtins, "vloom.synth")) == "Synth");
    CHECK(effectiveInstrumentType(listed(builtins, "vloom.sampler")) == "Sampler");
    CHECK(effectiveInstrumentType(listed(builtins, "vloom.drums")) == "Drums");
    CHECK(detectPluginSubCategory(listed(builtins, "vloom.drums")) == "Drums / Perc");
    int instruments = 0;
    for (const PluginInfo& info : builtins)
        instruments += pluginMainGroup(info) == "Instrument" ? 1 : 0;
    CHECK(instruments == 3);

    // Every one loads, as the kind it is listed as.
    PluginHost host;
    for (const PluginInfo& info : builtins) {
        INFO(pathToUtf8(info.path));
        const PluginHost::Loaded loaded
            = host.load(PluginFormat::Builtin, {}, pathToUtf8(info.path), info.name, 48000.0);
        REQUIRE(loaded.handle != 0);
        CHECK((loaded.info.kind == vloom::PluginInfo::Kind::Instrument)
            == (pluginMainGroup(info) == "Instrument"));
    }
}

TEST_CASE("The built-in compressor and gate take a sidechain send", "[plugins]")
{
    PluginHost host;
    for (const char* id : {"vloom.compressor", "vloom.gate"}) {
        const PluginHost::Loaded loaded = host.load(PluginFormat::Builtin, {}, id, id, 48000.0);
        REQUIRE(loaded.handle != 0);
        CHECK(host.instance(loaded.handle)->processor().hasSidechain()); // what the send menu asks
    }
    const PluginHost::Loaded eq = host.load(PluginFormat::Builtin, {}, "vloom.eq", "EQ", 48000.0);
    CHECK_FALSE(host.instance(eq.handle)->processor().hasSidechain());
}

TEST_CASE("A project keeps a built-in sampler's samples and reports missing ones", "[plugins][project-file]")
{
    const TempFolder folder;
    const auto kick = folder.path / "kick.wav";
    const auto hat = folder.path / "hat.wav";
    writeTone(kick, 44100);
    writeTone(hat, 48000);

    PluginHost host;
    const PluginHost::Loaded drums
        = host.load(PluginFormat::Builtin, {}, "vloom.drums", "Drum Sampler", 48000.0);
    REQUIRE(drums.handle != 0);
    vloom::AudioProcessor& processor = host.instance(drums.handle)->processor();
    REQUIRE(processor.sampleSlotCount() == 16);
    REQUIRE(processor.loadSample(0, pathToUtf8(kick)));
    REQUIRE(processor.loadSample(5, pathToUtf8(hat)));
    const std::uint32_t tune = [&] {
        for (const vloom::ParameterInfo& p : processor.parameters())
            if (p.name == "Pad 6 Tune")
                return p.id;
        FAIL("no Pad 6 Tune");
        return 0u;
    }();
    REQUIRE(host.instance(drums.handle)->setParameter(tune, -3.0));
    CHECK(sampleProblems(processor).empty());

    // Saved in a project and read back, as the app does.
    ProjectData data;
    data.mixer = MixerModel(1);
    const std::size_t channel = data.project.addChannel("Drums");
    PluginSlot slot;
    slot.format = PluginFormat::Builtin;
    slot.id = "vloom.drums";
    slot.name = "Drum Sampler";
    slot.instance = drums.handle;
    data.project.setChannelInstrument(channel, slot);
    data.states.instruments.resize(data.project.channels().size());
    data.states.instruments[channel] = host.saveState(drums.handle);
    const std::string text = ProjectFile::write(data, folder.path);
    const auto read = ProjectFile::read(text, folder.path);
    REQUIRE(read.data);
    const auto& opened = read.data->project.channels()[channel].instrument;
    REQUIRE(opened);
    const auto& state = read.data->states.instruments[channel];

    const PluginHost::Loaded restored
        = host.loadWithState(opened->format, {}, opened->id, opened->name, 48000.0, state);
    REQUIRE(restored.handle != 0);
    CHECK(restored.error.empty());
    const vloom::AudioProcessor& again = host.instance(restored.handle)->processor();
    CHECK(again.sampleSlot(0).path == pathToUtf8(kick));
    CHECK(again.sampleSlot(0).loaded());
    CHECK(again.sampleSlot(5).path == pathToUtf8(hat));
    CHECK(again.sampleSlot(5).loaded());
    CHECK(again.parameterValue(tune) == -3.0);
    CHECK(sampleProblems(again).empty());

    // A sample gone missing: the instrument still loads with its settings,
    // and the project lists what is missing.
    std::filesystem::remove(hat);
    const PluginHost::Loaded missing
        = host.loadWithState(opened->format, {}, opened->id, opened->name, 48000.0, state);
    REQUIRE(missing.handle != 0);
    CHECK(missing.error.empty());
    const vloom::AudioProcessor& gone = host.instance(missing.handle)->processor();
    CHECK(gone.sampleSlot(0).loaded());
    CHECK_FALSE(gone.sampleSlot(5).loaded());
    CHECK(gone.parameterValue(tune) == -3.0);
    const std::vector<std::string> problems = sampleProblems(gone);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].rfind("Pad 6: ", 0) == 0);
    // Saved again, the path is not lost.
    CHECK(host.saveState(missing.handle) == state);
}

TEST_CASE("Built-in samples are read through the host's resolver", "[plugins]")
{
    const TempFolder folder;
    const auto wav = folder.path / "decoded.wav";
    writeTone(wav, 48000);
    PluginHost host;
    std::vector<std::string> asked;
    host.setSampleFileResolver([&](const std::string& path, std::string*) {
        asked.push_back(path);
        return pathToUtf8(wav);
    });
    const PluginHost::Loaded sampler
        = host.load(PluginFormat::Builtin, {}, "vloom.sampler", "Sampler", 48000.0);
    REQUIRE(sampler.handle != 0);
    vloom::AudioProcessor& processor = host.instance(sampler.handle)->processor();
    REQUIRE(processor.loadSample(0, "C:/music/loop.mp3"));
    CHECK(asked == std::vector<std::string>{"C:/music/loop.mp3"});
    CHECK(processor.sampleSlot(0).path == "C:/music/loop.mp3"); // the project keeps the original
    CHECK(processor.sampleSlot(0).loaded());

    // A state loads through it too.
    const PluginHost::Loaded restored = host.loadWithState(
        PluginFormat::Builtin, {}, "vloom.sampler", "Sampler", 48000.0, processor.saveState());
    CHECK(host.instance(restored.handle)->processor().sampleSlot(0).loaded());
    CHECK(asked.size() == 2);
}
