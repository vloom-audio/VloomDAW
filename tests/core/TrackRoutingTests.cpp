// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/TrackRouting.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace vloomdaw;

TEST_CASE("Track palette gives distinct readable colours around the spectrum", "[routing]")
{
    std::set<std::uint32_t> seen;
    for (std::size_t i = 0; i < 20; ++i) {
        const std::uint32_t c = trackPaletteColor(i);
        CHECK(c != 0u);
        CHECK((c & 0xff000000u) == 0u);
        seen.insert(c);
    }
    CHECK(seen.size() == 20);
    CHECK(trackPaletteColor(20) == trackPaletteColor(0));
}

TEST_CASE("Channels link automatically to dedicated mixer inserts outputting to Master", "[routing]")
{
    Project project;
    MixerModel mixer(16);

    // Default project channels link to Inserts 1..4 with matching names and colours.
    linkAllChannelsToInserts(project, mixer);
    REQUIRE(project.channels().size() == 4);
    for (std::size_t c = 0; c < 4; ++c) {
        CHECK(project.channels()[c].insert == static_cast<int>(c + 1));
        CHECK(project.channels()[c].color.has_value());
        CHECK(mixer.track(c + 1).name == project.channels()[c].name);
        CHECK(mixer.track(c + 1).color == project.channels()[c].color);
    }

    // Adding 20 audio stems allocates Inserts 5..24, growing the mixer past 16 automatically.
    for (int i = 0; i < 20; ++i) {
        const std::string stemName = "Stem " + std::to_string(i + 1);
        const std::size_t ch = project.addChannel(stemName);
        project.setChannelColor(ch, trackPaletteColor(static_cast<std::size_t>(i)));
        const std::size_t ins = linkChannelToInsert(project, mixer, ch);
        CHECK(ins == static_cast<std::size_t>(5 + i));
        CHECK(project.channels()[ch].insert == 5 + i);
        CHECK(mixer.track(ins).name == stemName);
        CHECK(mixer.track(ins).color == trackPaletteColor(static_cast<std::size_t>(i)));
    }
    CHECK(mixer.insertCount() == 24);

    // Customised inserts (with effects or user names) are never overwritten by a new channel.
    MixerModel smallMixer(2);
    Project smallProject;
    smallMixer.setName(1, "Drum Bus");
    const std::size_t ch = smallProject.addChannel("Lead Vox");
    CHECK(linkChannelToInsert(smallProject, smallMixer, ch) == 2);
}

TEST_CASE("Changing a track's colour updates its dedicated stem channel and mixer insert", "[routing]")
{
    Project project;
    MixerModel mixer(4);
    const std::size_t ch = project.addChannel("Bass");
    project.setChannelColor(ch, 0xff0000u);
    const std::size_t ins = linkChannelToInsert(project, mixer, ch);
    REQUIRE(ins == 1);
    project.addAudioClip(AudioClip{ch, 0, 0});

    syncTrackColor(project, mixer, 0, 0x38bdf8u);
    CHECK(project.tracks()[0].color == 0x38bdf8u);
    CHECK(project.channels()[ch].color == 0x38bdf8u);
    CHECK(mixer.track(ins).color == 0x38bdf8u);

    // A channel shared across two tracks keeps its own colour when one track changes.
    project.addTrack();
    project.addAudioClip(AudioClip{ch, 1, 0});
    syncTrackColor(project, mixer, 1, 0x4ade80u);
    CHECK(project.tracks()[1].color == 0x4ade80u);
    CHECK(project.channels()[ch].color == 0x38bdf8u);
}

TEST_CASE("Playlist track headers link 1:1 to their stem channel and mixer insert", "[routing]")
{
    Project project;
    MixerModel mixer(4);

    // Empty track: no primary channel or insert yet; volume/pan persist on the track itself.
    CHECK_FALSE(trackPrimaryChannel(project, 0).has_value());
    CHECK(trackMixerInsert(project, mixer, 0) == -1);
    setTrackMixVolume(project, mixer, 0, 0.5);
    setTrackMixPan(project, mixer, 0, -0.4);
    CHECK(trackMixVolume(project, mixer, 0) == 0.5);
    CHECK(trackMixPan(project, mixer, 0) == -0.4);

    // Audio stem on Track 1 routed to dedicated Insert 1: volume & pan drive Mixer Insert 1 directly!
    const std::size_t stemCh = project.addChannel("Lead Guitar");
    const std::size_t ins = linkChannelToInsert(project, mixer, stemCh);
    REQUIRE(ins == 1);
    project.addAudioClip(AudioClip{stemCh, 0, 0});

    CHECK(trackPrimaryChannel(project, 0) == stemCh);
    CHECK(trackMixerInsert(project, mixer, 0) == 1);

    setTrackMixVolume(project, mixer, 0, 0.65);
    setTrackMixPan(project, mixer, 0, 0.35);
    CHECK(mixer.track(1).fader == 0.65);
    CHECK(mixer.track(1).pan == 0.35);
    CHECK(trackMixVolume(project, mixer, 0) == 0.65);
    CHECK(trackMixPan(project, mixer, 0) == 0.35);

    // Moving the fader on the Mixer also reflects back on the Playlist track header.
    mixer.setFader(1, 0.45);
    mixer.setPan(1, -0.75);
    CHECK(trackMixVolume(project, mixer, 0) == 0.45);
    CHECK(trackMixPan(project, mixer, 0) == -0.75);

    // Pattern clip on Track 2 finds its first active channel in the pattern.
    project.addTrack();
    project.setStep(0, 2, 0, true); // Hat active in Pattern 1
    project.setChannelInsert(2, 3);
    project.addClip(Clip{0, 1, 0});
    CHECK(trackPrimaryChannel(project, 1) == std::size_t{2});
    CHECK(trackMixerInsert(project, mixer, 1) == 3);

    // Shared insert (Drum Bus): Track 2 and Track 3 both send to Insert 3, so header Volume/Pan
    // trims the individual channel without pulling down the shared bus fader!
    project.addTrack();
    project.setChannelInsert(3, 3); // Snare also routed to Insert 3
    mixer.setFader(3, 0.8);
    const std::size_t p2 = project.addPattern();
    project.setStep(p2, 3, 4, true);
    project.addClip(Clip{p2, 2, 0});
    setTrackMixVolume(project, mixer, 2, 0.3);
    CHECK(project.channels()[3].volume == 0.3);
    CHECK(mixer.track(3).fader == 0.8);
    CHECK(trackMixVolume(project, mixer, 2) == 0.3);

    // Routing from the track inspector menu grows the mixer when needed and assigns the insert.
    CHECK(routeTrackToInsert(project, mixer, 0, 6) == 6);
    CHECK(mixer.insertCount() == 6);
    CHECK(project.channels()[stemCh].insert == 6);
    CHECK(trackMixerInsert(project, mixer, 0) == 6);
}

TEST_CASE("Empty mixer starts with Master only and resolves selected inserts back to tracks and channels",
    "[core][routing]")
{
    Project project;
    MixerModel mixer(0);
    REQUIRE(mixer.size() == 1);
    REQUIRE(mixer.insertCount() == 0);
    CHECK(!insertPrimaryTrack(project, mixer, 0).has_value());
    CHECK(!insertPrimaryChannel(project, 0).has_value());

    const std::size_t bassCh = project.addChannel("Bass");
    const std::size_t voxCh = project.addChannel("Vocals");
    REQUIRE(linkChannelToInsert(project, mixer, bassCh) == 1);
    REQUIRE(linkChannelToInsert(project, mixer, voxCh) == 2);
    REQUIRE(mixer.insertCount() == 2);

    project.addTrack(); // Track 2
    project.addAudioClip(AudioClip{bassCh, 0, 0});
    project.addAudioClip(AudioClip{voxCh, 1, 0});

    CHECK(insertPrimaryChannel(project, 1) == bassCh);
    CHECK(insertPrimaryChannel(project, 2) == voxCh);
    CHECK(insertPrimaryTrack(project, mixer, 1) == std::size_t{0});
    CHECK(insertPrimaryTrack(project, mixer, 2) == std::size_t{1});
    CHECK(!insertPrimaryTrack(project, mixer, 3).has_value());
}

TEST_CASE("Dropping stems onto pre-named tracks and mixer strips binds 1:1 without orphaned strips",
    "[core][routing]")
{
    Project project;
    MixerModel mixer(2);

    // Reproduce exact user bug: Track 1 & Strip 1 named "RHSG P", Track 2 renamed "3" while Strip 2 is "Track
    // 2".
    project.addTrack();
    project.setTrackName(0, "RHSG P");
    mixer.setName(1, "RHSG P");
    project.setTrackName(1, "3");
    mixer.setName(2, "Track 2");

    // Drop 3 stems starting at Track 1..3
    project.addTrack(); // Track 3 (unnamed)
    const std::size_t ch0
        = bindAudioToTrack(project, mixer, 0, "C:/stems/Ample_Metal_Hellrazer.wav", "Ample Metal Hellrazer");
    project.addAudioClip(AudioClip{ch0, 0, 0});
    const std::size_t ch1 = bindAudioToTrack(project, mixer, 1, "C:/stems/Bongo.wav", "Bongo");
    project.addAudioClip(AudioClip{ch1, 1, 0});
    const std::size_t ch2 = bindAudioToTrack(project, mixer, 2, "C:/stems/Brass_Band.wav", "Brass Band");
    project.addAudioClip(AudioClip{ch2, 2, 0});

    // Zero orphaned inserts: mixer has exactly 3 inserts, mapped 1:1 to Tracks 1..3!
    REQUIRE(mixer.insertCount() == 3);
    CHECK(project.channels()[ch0].insert == 1);
    CHECK(project.channels()[ch1].insert == 2);
    CHECK(project.channels()[ch2].insert == 3);

    // User's custom track names ("RHSG P", "3") stay on BOTH timeline and mixer; unnamed Track 3 gets stem
    // name.
    CHECK(project.tracks()[0].name == "RHSG P");
    CHECK(mixer.track(1).name == "RHSG P");
    CHECK(project.tracks()[1].name == "3");
    CHECK(mixer.track(2).name == "3");
    CHECK(project.tracks()[2].name == "Brass Band");
    CHECK(mixer.track(3).name == "Brass Band");
}

TEST_CASE("Timeline tracks and mixer strips synchronise Mute, Solo, Rename and Colour bidirectionally",
    "[core][routing]")
{
    Project project;
    MixerModel mixer(0);

    project.addTrack();
    const std::size_t ch0 = bindAudioToTrack(project, mixer, 0, "C:/stems/Kick.wav", "Kick");
    project.addAudioClip(AudioClip{ch0, 0, 0});
    const std::size_t ch1 = bindAudioToTrack(project, mixer, 1, "C:/stems/Snare.wav", "Snare");
    project.addAudioClip(AudioClip{ch1, 1, 0});
    REQUIRE(mixer.insertCount() == 2);

    // Mute from timeline -> mixer, and from mixer -> timeline
    syncTrackMute(project, mixer, 0, true);
    CHECK(project.tracks()[0].muted);
    CHECK(mixer.track(1).muted);
    CHECK_FALSE(trackEffectiveAudible(project, mixer, 0));

    syncInsertMute(project, mixer, 1, false);
    CHECK_FALSE(project.tracks()[0].muted);
    CHECK_FALSE(mixer.track(1).muted);
    CHECK(trackEffectiveAudible(project, mixer, 0));

    // Solo from timeline -> mixer, and from mixer -> timeline
    syncTrackSolo(project, mixer, 0, true);
    CHECK(project.tracks()[0].solo);
    CHECK(mixer.track(1).soloed);
    CHECK_FALSE(mixer.track(2).soloed);
    CHECK(trackEffectiveAudible(project, mixer, 0));
    CHECK_FALSE(trackEffectiveAudible(project, mixer, 1));

    syncInsertSolo(project, mixer, 1, false);
    CHECK_FALSE(project.tracks()[0].solo);
    CHECK_FALSE(mixer.anySoloed());
    CHECK(trackEffectiveAudible(project, mixer, 1));

    // Rename from either side updates both
    syncTrackName(project, mixer, 1, "Tight Snare");
    CHECK(project.tracks()[1].name == "Tight Snare");
    CHECK(mixer.track(2).name == "Tight Snare");

    syncInsertName(project, mixer, 1, "Punch Kick");
    CHECK(mixer.track(1).name == "Punch Kick");
    CHECK(project.tracks()[0].name == "Punch Kick");

    // Colour from mixer updates bound track & channel
    syncInsertColor(project, mixer, 2, 0x22d3eeu);
    CHECK(mixer.track(2).color == 0x22d3eeu);
    CHECK(project.tracks()[1].color == 0x22d3eeu);
    CHECK(project.channels()[ch1].color == 0x22d3eeu);
}

TEST_CASE("bindAudioToTrack checks per-channel pattern usage across all patterns instead of Pattern 1 only",
    "[core][routing]")
{
    // Case A: Channel 0 has steps in Pattern 1; Track 2 has a pre-created dedicated empty
    // placeholder Channel + Insert. Dropping audio onto Track 2 must reuse that placeholder
    // without creating orphaned channels or duplicate mixer strips.
    {
        Project project;
        MixerModel mixer(1); // Insert 1 for Kick
        project.setChannelInsert(0, 1);
        project.setStep(0, 0, 0, true); // Pattern 1 is NOT empty (Kick has step 0)

        const auto trk2 = project.addTrack();
        REQUIRE(trk2 == std::size_t{1});
        const std::size_t placeholderCh = project.addChannel("Track 2");
        const auto ins2 = mixer.addInsert();
        REQUIRE(ins2 == std::size_t{2});
        project.setChannelInsert(placeholderCh, static_cast<int>(*ins2));
        pairTrackWithInsert(project, mixer, *trk2, *ins2);

        const std::size_t boundCh = bindAudioToTrack(project, mixer, *trk2, "C:/stems/Vocal.wav", "Vocal");
        CHECK(boundCh == placeholderCh);
        CHECK(mixer.insertCount() == 2); // No duplicate Insert 3 created!
        CHECK(project.channels()[boundCh].insert == 2);
    }

    // Case B: Pattern 1 is empty, but Pattern 2 has notes on Channel 4 (routed to Insert 2).
    // Dropping audio onto Track 2 must NOT overwrite Channel 4!
    {
        Project project;
        MixerModel mixer(2);
        const auto trk2 = project.addTrack();
        REQUIRE(trk2 == std::size_t{1});
        const std::size_t midiCh = project.addChannel("Lead Keys");
        project.setChannelInsert(midiCh, 2);
        project.setTrackInsert(*trk2, 2);
        const std::size_t pat2 = project.addPattern(); // Pattern 2
        project.addNote(pat2, midiCh, Note{60, 0, 96}); // Pattern 1 is still empty!

        const std::size_t stemCh = bindAudioToTrack(project, mixer, *trk2, "C:/stems/Guitar.wav", "Guitar");
        CHECK(stemCh != midiCh);
        CHECK(project.channels()[midiCh].name == "Lead Keys");
        CHECK(project.channels()[midiCh].samplePath.empty());
    }
}
