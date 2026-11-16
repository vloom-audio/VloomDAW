// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/PluginMetadata.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace vloomdaw;

TEST_CASE("Probe output is read; lines a plugin printed itself are ignored", "[plugins]")
{
    const auto out = parseProbeOutput("Copy protection v2 starting...\r\n"
                                      "@@vloom\tprobe\t1\r\n"
                                      "@@vloom\tplugin\tPro-R 2\tFabFilter\t2.1.0\tFx|Reverb\r\n"
                                      "debug: hello\n"
                                      "@@vloom\tplugin\tSerum 2\tXfer Records\t2.0\tInstrument|Synth\n");
    REQUIRE(out.ok);
    REQUIRE(out.plugins.size() == 2);
    CHECK(out.plugins[0].name == "Pro-R 2");
    CHECK(out.plugins[0].vendor == "FabFilter");
    CHECK(out.plugins[0].version == "2.1.0");
    CHECK(out.plugins[0].tags == "Fx|Reverb");
    CHECK(out.plugins[1].tags == "Instrument|Synth");
}

TEST_CASE("Probe errors and silence are failures", "[plugins]")
{
    const auto error = parseProbeOutput("@@vloom\terror\tcould not load the module\n");
    CHECK_FALSE(error.ok);
    CHECK(error.error == "could not load the module");

    const auto crashed = parseProbeOutput("@@vloom\tprobe\t1\n@@vloom\tplugin\tA\t\t\t\n"); // header seen: ok
    CHECK(crashed.ok);
    const auto nothing = parseProbeOutput("Segmentation fault\n");
    CHECK_FALSE(nothing.ok);
    CHECK_FALSE(nothing.error.empty());

    // Records before the header, and nameless ones, are dropped.
    const auto early
        = parseProbeOutput("@@vloom\tplugin\tA\tB\tC\tD\n@@vloom\tprobe\t1\n@@vloom\tplugin\t\tB\tC\tD\n");
    CHECK(early.ok);
    CHECK(early.plugins.empty());
}

TEST_CASE("VST3 sub-categories and CLAP features map to categories", "[plugins]")
{
    CHECK(pluginCategory("Fx|Reverb") == "Reverb");
    CHECK(pluginCategory("Fx|Dynamics") == "Dynamics");
    CHECK(pluginCategory("Fx|EQ") == "EQ");
    CHECK(pluginCategory("Instrument|Synth") == "Instrument");
    CHECK(pluginCategory("Instrument|Synth|Fx|Reverb") == "Instrument"); // instruments win
    CHECK(pluginCategory("Fx|Distortion") == "Distortion");
    CHECK(pluginCategory("Fx|Amp Simulator") == "Amp");
    CHECK(pluginCategory("Fx|Pitch Shift") == "Pitch");
    CHECK(pluginCategory("Fx|Tools") == "Utility");
    CHECK(pluginCategory("audio-effect|compressor|stereo") == "Dynamics");
    CHECK(pluginCategory("audio-effect|equalizer") == "EQ");
    CHECK(pluginCategory("instrument|synthesizer") == "Instrument");
    CHECK(pluginCategory("audio-effect|chorus") == "Modulation");
    CHECK(pluginCategory("Fx") == "Other");
    CHECK(pluginCategory("") == "Uncategorized");

    const auto& all = pluginCategories();
    CHECK(std::find(all.begin(), all.end(), "Other") != all.end());
    CHECK(all.back() == "Uncategorized");
}

TEST_CASE("Vendor names drop legal suffixes", "[plugins]")
{
    CHECK(normalizeVendor("iZotope, Inc.") == "iZotope");
    CHECK(normalizeVendor("  Xfer Records ") == "Xfer Records");
    CHECK(normalizeVendor("Native Instruments GmbH") == "Native Instruments");
    CHECK(normalizeVendor("Acme Audio Ltd.") == "Acme Audio");
    CHECK(normalizeVendor("FabFilter") == "FabFilter");
    CHECK(normalizeVendor("") == "");
}

TEST_CASE("A probe turns one module into its plugins", "[plugins]")
{
    PluginInfo file;
    file.name = "Bundle";
    file.path = "C:/VST3/Acme/Bundle.vst3";
    file.vendor = "Acme"; // install folder
    file.fileSize = 10;

    ProbeOutput probe;
    probe.ok = true;
    probe.plugins
        = {{"Room", "", "1.0", "Fx|Reverb"}, {"Lead", "Acme Audio, Inc.", "2.0", "Instrument|Synth"}};
    const auto plugins = applyProbe(file, probe);
    REQUIRE(plugins.size() == 2);
    CHECK(plugins[0].name == "Room");
    CHECK(plugins[0].vendor == "Acme"); // the factory gave none: the folder's
    CHECK(plugins[0].category == "Reverb");
    CHECK(plugins[0].probe == PluginInfo::Probe::Ok);
    CHECK(plugins[1].vendor == "Acme Audio");
    CHECK(plugins[1].category == "Instrument");
    CHECK(plugins[1].version == "2.0");
    CHECK(plugins[1].path == file.path);
    CHECK(plugins[1].fileSize == 10);
}

TEST_CASE("A failed or empty probe keeps the module, marked failed", "[plugins]")
{
    PluginInfo file;
    file.name = "Broken";
    ProbeOutput crashed;
    crashed.error = "the probe crashed";
    auto out = applyProbe(file, crashed);
    REQUIRE(out.size() == 1);
    CHECK(out[0].name == "Broken");
    CHECK(out[0].probe == PluginInfo::Probe::Failed);
    CHECK(out[0].probeError == "the probe crashed");
    CHECK(out[0].category == "Uncategorized");

    ProbeOutput empty;
    empty.ok = true;
    out = applyProbe(file, empty);
    REQUIRE(out.size() == 1);
    CHECK(out[0].probeError == "no plugins inside");
}

TEST_CASE("Unchanged modules are recognised", "[plugins]")
{
    PluginInfo a;
    a.path = "x.vst3";
    a.fileSize = 5;
    a.modified = 100;
    PluginInfo b = a;
    b.name = "renamed by a probe";
    CHECK(sameModule(a, b));
    b.modified = 101;
    CHECK_FALSE(sameModule(a, b));
    b = a;
    b.format = PluginFormat::Clap;
    CHECK_FALSE(sameModule(a, b));
}

TEST_CASE("Instruments are split by type, the most specific first", "[plugins]")
{
    CHECK(instrumentType("Instrument|Synth") == "Synth");
    CHECK(instrumentType("Instrument|Sampler") == "Sampler");
    CHECK(instrumentType("Instrument|Drum") == "Drums");
    CHECK(instrumentType("Instrument|Sampler|Drum") == "Drums"); // a drum sampler
    CHECK(instrumentType("Instrument|Piano") == "Keys");
    CHECK(instrumentType("Instrument|External") == "External");
    CHECK(instrumentType("Instrument") == "Other");
    CHECK(instrumentType("instrument|synthesizer|stereo") == "Synth");
    CHECK(instrumentType("instrument|drum-machine") == "Drums");
    CHECK(instrumentType("Fx|Reverb").empty()); // not an instrument
    CHECK(instrumentType("").empty());
    CHECK(instrumentTypes().front() == "Synth");
}

TEST_CASE("A category set by the user wins over the plugin's", "[plugins]")
{
    PluginInfo drums;
    drums.category = "Instrument";
    drums.tags = "Instrument|Synth"; // what Addictive Drums 2 reports
    CHECK(effectiveCategory(drums) == "Instrument");
    CHECK(effectiveInstrumentType(drums) == "Synth");

    drums.userCategory = "Instrument";
    drums.userInstrumentType = "Drums";
    CHECK(effectiveInstrumentType(drums) == "Drums");

    PluginInfo tool;
    tool.category = "Other";
    tool.tags = "Fx";
    tool.userCategory = "Dynamics";
    CHECK(effectiveCategory(tool) == "Dynamics");
    CHECK(effectiveInstrumentType(tool).empty());

    tool.userCategory = "Instrument"; // made an instrument without a type
    CHECK(effectiveInstrumentType(tool) == "Other");
}

TEST_CASE("User categories survive a new scan of the same plugins", "[plugins]")
{
    PluginInfo old;
    old.name = "Addictive Drums 2";
    old.path = "C:/VST3/XLN Audio/Addictive Drums 2.vst3";
    old.userCategory = "Instrument";
    old.userInstrumentType = "Drums";

    PluginInfo same = old;
    same.userCategory.clear();
    same.userInstrumentType.clear();
    PluginInfo other = same;
    other.name = "Something Else"; // another class in the same module
    PluginInfo clap = same;
    clap.format = PluginFormat::Clap;

    std::vector<PluginInfo> fresh{same, other, clap};
    carryUserCategories(fresh, {old});
    CHECK(fresh[0].userCategory == "Instrument");
    CHECK(fresh[0].userInstrumentType == "Drums");
    CHECK(fresh[1].userCategory.empty());
    CHECK(fresh[2].userCategory.empty());
}

TEST_CASE("Plugin favourites, enabled state and smart sub-categories work without emojis", "[plugins]")
{
    PluginInfo prev;
    prev.name = "Pro-Q 3";
    prev.path = "C:/VST3/FabFilter Pro-Q 3.vst3";
    prev.favorite = true;
    prev.enabled = false;

    PluginInfo rescanned = prev;
    rescanned.favorite = false;
    rescanned.enabled = true;
    std::vector<PluginInfo> fresh{rescanned};
    carryUserCategories(fresh, {prev});
    CHECK(fresh[0].favorite);
    CHECK_FALSE(fresh[0].enabled);

    PluginInfo gtr;
    gtr.name = "Ample Guitar SC";
    gtr.category = "Instrument";
    gtr.tags = "Instrument|Synth";
    CHECK(pluginMainGroup(gtr) == "Instrument");
    CHECK(detectPluginSubCategory(gtr) == "Guitar / Bass");

    PluginInfo rompler;
    rompler.name = "ZENOLOGY";
    rompler.category = "Instrument";
    rompler.tags = "Instrument|Synth";
    CHECK(detectPluginSubCategory(rompler) == "Sampler / ROMpler");

    PluginInfo eq;
    eq.name = "FabFilter Pro-Q 3";
    eq.category = "EQ";
    eq.tags = "Fx|EQ";
    CHECK(pluginMainGroup(eq) == "Effect");
    CHECK(detectPluginSubCategory(eq) == "EQ / Filter");

    PluginInfo vocal;
    vocal.name = "Auto-Tune Pro";
    vocal.category = "Other";
    vocal.tags = "Fx";
    CHECK(detectPluginSubCategory(vocal) == "Pitch / Vocal");

    // Ensure zero non-ASCII emoji bytes in sub-category list.
    for (const std::string& sub : pluginSubCategories()) {
        for (unsigned char ch : sub)
            CHECK(ch < 128);
    }
}

TEST_CASE("Plugin thumbnail IDs are deterministic, normalized and distinct per class/format", "[plugins]")
{
    PluginInfo a;
    a.format = PluginFormat::Vst3;
    a.path = "C:\\Program Files\\Common Files\\VST3\\Cakewalk\\CA-2A.vst3\\";
    a.name = "CA-2A";

    PluginInfo sameNormalized;
    sameNormalized.format = PluginFormat::Vst3;
    sameNormalized.path = "c:/program files/common files/vst3/cakewalk/ca-2a.vst3";
    sameNormalized.name = "ca-2a";

    const std::string idA = pluginThumbnailId(a);
    CHECK(idA.size() == 16);
    CHECK(idA == pluginThumbnailId(sameNormalized));

    PluginInfo otherClass = a;
    otherClass.name = "CA-2A Mono";
    CHECK(pluginThumbnailId(otherClass) != idA);

    PluginInfo clapVariant = a;
    clapVariant.format = PluginFormat::Clap;
    CHECK(pluginThumbnailId(clapVariant) != idA);

    // Built-in processors must not collide with VST3 paths of the same stem/id.
    CHECK(pluginThumbnailId(PluginFormat::Builtin, "vloom.eq", "EQ")
        != pluginThumbnailId(PluginFormat::Vst3, "vloom.eq", "EQ"));
    CHECK(isValidPluginThumbnailId(idA));
    CHECK_FALSE(isValidPluginThumbnailId(""));
    CHECK_FALSE(isValidPluginThumbnailId("../../victim0000"));
    CHECK_FALSE(isValidPluginThumbnailId("..\\..\\victim0000"));
    CHECK_FALSE(isValidPluginThumbnailId("0123456789abcdeg"));

#if defined(_WIN32)
    // Unpaired UTF-16 surrogates in a filesystem path must never throw out of pluginThumbnailId.
    const std::filesystem::path illFormed(std::wstring{static_cast<wchar_t>(0xD800), L'x'});
    std::string illId;
    REQUIRE_NOTHROW(illId = pluginThumbnailId(PluginFormat::Vst3, illFormed, "BrokenName"));
    CHECK(illId.size() == 16);
#endif
}
