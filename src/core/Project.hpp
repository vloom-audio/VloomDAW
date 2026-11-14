// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/PluginSlot.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vloomdaw {

// Musical time in ticks, the same resolution as vloom-engine (960 PPQ).
inline constexpr int kTicksPerBeat = 960;
inline constexpr int kBeatsPerBar = 4; ///< 4/4 until time signatures arrive
inline constexpr int kTicksPerBar = kTicksPerBeat * kBeatsPerBar;
inline constexpr int kStepsPerBar = 16; ///< channel rack steps are 1/16 notes
inline constexpr int kTicksPerStep = kTicksPerBar / kStepsPerBar;
inline constexpr int kMinNoteLength = kTicksPerStep / 4; ///< 1/64
inline constexpr int kLowestKey = 0; ///< MIDI note numbers
inline constexpr int kHighestKey = 127;

/// A note in the piano roll; positions in ticks from the pattern start.
struct Note {
    int key = 60; ///< MIDI note number, 60 = C5 (middle C)
    int start = 0;
    int length = kTicksPerStep;

    [[nodiscard]] int end() const { return start + length; }
    friend bool operator==(const Note&, const Note&) = default;
};

/// What one channel plays in one pattern: step sequencer and piano roll.
struct ChannelPattern {
    std::vector<bool> steps = std::vector<bool>(kStepsPerBar, false);
    std::vector<Note> notes;
};

struct Pattern {
    std::string name;
    int steps = kStepsPerBar; ///< step pads per channel; every ChannelPattern has this many
    std::vector<ChannelPattern> channels; ///< index = channel index
};

/// An instrument line in the channel rack.
struct Channel {
    std::string name;
    bool muted = false;
    std::string samplePath; ///< UTF-8 path of the sample it plays; empty = none (silent)
    double volume = 0.8; ///< fader position 0..1 (faderToGain(); 0.8 is unity, as on the mixer)
    double pan = 0.0; ///< -1 left .. +1 right
    int insert = 0; ///< mixer insert it plays into; 0 = the master
    /// 0xRRGGBB chosen by the user (rack, piano roll); none: the theme's colours.
    std::optional<std::uint32_t> color{};
    /// A hosted instrument plugin; when loaded it plays instead of the sample.
    std::optional<PluginSlot> instrument{};
};

/// A playlist track: a lane for clips.
///
/// Tracks form an outline: a track one level deeper than the track above it
/// is that track's sub-track. A track's sub-tracks (and theirs) follow it
/// directly; the first one after them at its own depth or less ends the group.
struct PlaylistTrack {
    static constexpr int kDefaultHeight = 36;
    static constexpr int kMinHeight = 24;
    static constexpr int kMaxHeight = 200;
    static constexpr int kMaxDepth = 4; ///< sub-tracks nest at most this deep

    std::string name;
    bool named = false; ///< the user named it; otherwise "Track N", following its number
    bool muted = false; ///< its clips, and its sub-tracks', do not play
    bool solo = false; ///< while any track is solo, only solo tracks (and their sub-tracks) play
    int height = kDefaultHeight; ///< lane height in pixels at 100% scale
    int depth = 0; ///< 0: top level; 1: a sub-track of the nearest track above at depth 0; ...
    bool collapsed = false; ///< its sub-tracks are hidden (they still play)
    std::optional<std::uint32_t> color{}; ///< 0xRRGGBB chosen by the user; none: no colour
};

/// A pattern placed in the playlist. It is as long as the pattern unless
/// given a length (trimmed or stretched in the playlist: a longer clip
/// repeats the pattern); `offset` is where in the pattern it starts (cut).
struct Clip {
    std::size_t pattern = 0;
    int track = 0; ///< 0-based playlist track
    int start = 0; ///< ticks from the song start
    bool muted = false; ///< stays silent (the playlist's mute tool)
    int offset = 0; ///< ticks into the pattern where the clip starts
    int length = 0; ///< ticks; 0: the pattern's length

    friend bool operator==(const Clip&, const Clip&) = default;
};

/// A channel's sample placed in the playlist (FL Studio's audio clip): plays
/// once from `start` at its own speed (its length in ticks depends on the
/// tempo), through the channel: its volume, pan, mute and mixer insert.
///
/// It can show part of the sample (cut or trimmed): from `offset` ticks into
/// it (at the current tempo), for `length` ticks.
struct AudioClip {
    std::size_t channel = 0; ///< the channel whose sample plays
    int track = 0; ///< 0-based playlist track
    int start = 0; ///< ticks from the song start
    bool muted = false; ///< stays silent (the playlist's mute tool)
    int offset = 0; ///< ticks into the sample where the clip starts
    int length = 0; ///< ticks; 0: to the sample's end
    /// Fitted to the song's tempo: the sample, recorded at `sourceBpm`,
    /// follows the song's tempo, so its bars line up with the song's.
    /// Stretch keeps its pitch (time-stretched); Resample plays it faster or
    /// slower, its pitch following. Offset and length then count in the
    /// sample's own beats, whatever the song's tempo.
    enum class Fit : std::uint8_t { Off, Stretch, Resample };
    Fit fit = Fit::Off;
    double sourceBpm = 0.0; ///< the sample's own tempo (detected or set); 0: unknown, so not fitted

    /// Whether the clip follows the song's tempo (it has a tempo to fit from).
    [[nodiscard]] bool fitted() const { return fit != Fit::Off && sourceBpm > 0.0; }

    friend bool operator==(const AudioClip&, const AudioClip&) = default;
};

/// The song: channels, patterns (steps and notes per channel) and the
/// playlist, as in FL Studio. Settings only: audio/ChannelRackAudio turns
/// the channel rack into sound.
///
/// Indices are 0-based. Every method that takes an index ignores (or
/// returns empty for) one out of range, and every position, key and length
/// is clamped to a valid value, so the UI cannot corrupt the model.
class Project {
public:
    /// Playlist tracks of a new project, and the most it can have.
    static constexpr int kDefaultPlaylistTracks = 1;
    static constexpr int kMaxPlaylistTracks = 500;
    /// Longest track name, in bytes (UTF-8).
    static constexpr std::size_t kMaxTrackNameLength = 64;
    /// The tempi an audio clip can be fitted from (AudioClip::sourceBpm).
    static constexpr double kMinSourceBpm = 20.0;
    static constexpr double kMaxSourceBpm = 999.0;
    /// Most patterns the UI lets a project have (the selector is sized for "Pattern 999").
    static constexpr std::size_t kMaxPatterns = 999;
    /// Most step pads a pattern can have: 16 bars.
    static constexpr int kMaxSteps = 16 * kStepsPerBar;

    /// A new song: four channels (Kick, Clap, Hat, Snare) and one pattern.
    Project();

    [[nodiscard]] const std::vector<Channel>& channels() const { return channels_; }
    [[nodiscard]] const std::vector<Pattern>& patterns() const { return patterns_; }
    [[nodiscard]] const std::vector<Clip>& clips() const { return clips_; }
    [[nodiscard]] const std::vector<PlaylistTrack>& tracks() const { return tracks_; }
    [[nodiscard]] const std::vector<AudioClip>& audioClips() const { return audioClips_; }

    // ---- channels
    /// Adds a channel to every pattern; returns its index.
    std::size_t addChannel(const std::string& name);
    /// The first channel playing the sample at `path`, if any.
    [[nodiscard]] std::optional<std::size_t> channelWithSample(const std::string& path) const;
    void setChannelMuted(std::size_t channel, bool muted);
    /// The channel's colour, 0xRRGGBB (higher bits dropped); nullopt: none.
    void setChannelColor(std::size_t channel, std::optional<std::uint32_t> color);
    void setChannelName(std::size_t channel, const std::string& name);
    /// The sample the channel plays (UTF-8 path; empty = none).
    void setChannelSample(std::size_t channel, const std::string& path);
    /// The instrument plugin the channel plays; nullopt: its sample again.
    void setChannelInstrument(std::size_t channel, std::optional<PluginSlot> instrument);
    /// Fader position 0..1 (clamped; NaN ignored).
    void setChannelVolume(std::size_t channel, double volume);
    /// -1 .. +1 (clamped; NaN ignored).
    void setChannelPan(std::size_t channel, double pan);
    /// Mixer insert, 0 = master (negative counts as 0).
    void setChannelInsert(std::size_t channel, int insert);
    /// Mixer insert `insert` (>= 1) was deleted: its channels play into the
    /// master and channels on later inserts follow them down one. Returns
    /// how many channels went to the master.
    std::size_t removeInsert(int insert);
    /// Mixer insert `from` moved to `to` (both >= 1): channels follow their insert.
    void moveInsert(int from, int to);

    // ---- patterns
    /// Appends an empty pattern named "Pattern N" (the lowest N not in use);
    /// returns its index.
    std::size_t addPattern();
    /// Inserts an empty pattern at `at` (clamped to the end); clips of later
    /// patterns follow. Returns its index.
    std::size_t insertPattern(std::size_t at);
    /// Copies a pattern (steps and notes) right after it, named "<name> #2"
    /// (the lowest free number); returns the copy's index, or nullopt.
    std::optional<std::size_t> clonePattern(std::size_t pattern);
    /// Removes a pattern (never the last one) and its clips; later patterns
    /// move down one index and keep their names.
    void removePattern(std::size_t pattern);
    /// Moves a pattern to index `to` (clamped); the playlist's clips follow
    /// their pattern. Returns its new index.
    std::size_t movePattern(std::size_t from, std::size_t to);
    /// Renames a pattern; an empty or blank name is ignored.
    void setPatternName(std::size_t pattern, const std::string& name);
    /// No step on and no note in any channel.
    [[nodiscard]] bool isPatternEmpty(std::size_t pattern) const;
    /// The first empty pattern at or after `from`, if any.
    [[nodiscard]] std::optional<std::size_t> findEmptyPattern(std::size_t from = 0) const;
    /// Moves every note of the pattern by `semitones`; notes stay within the
    /// key range (the whole set shifts less rather than squash notes together).
    void transposePattern(std::size_t pattern, int semitones);
    /// One new pattern per channel that has steps or notes in `pattern`,
    /// named "<pattern> - <channel>", inserted after it in channel order.
    /// The original stays. Returns the new indices.
    std::vector<std::size_t> splitPatternByChannel(std::size_t pattern);
    /// Step pads per channel in the pattern (kStepsPerBar for a new one).
    [[nodiscard]] int stepCount(std::size_t pattern) const;
    /// Sets the pattern's step pads (clamped to 1..kMaxSteps). Shrinking
    /// drops the steps past the new end; growing adds steps that are off.
    void setStepCount(std::size_t pattern, int steps);
    /// Whether any step from `from` on is on, in any channel: what
    /// setStepCount(pattern, from) would drop.
    [[nodiscard]] bool hasStepsFrom(std::size_t pattern, int from) const;
    /// Length in ticks: whole bars covering the step pads and every note, at
    /// least one bar.
    [[nodiscard]] int patternLength(std::size_t pattern) const;

    /// Steps in the pattern's length (16 per bar).
    [[nodiscard]] int patternSteps(std::size_t pattern) const
    {
        return patternLength(pattern) / kTicksPerStep;
    }

    // ---- step sequencer
    [[nodiscard]] bool step(std::size_t pattern, std::size_t channel, int step) const;
    void setStep(std::size_t pattern, std::size_t channel, int step, bool on);
    void toggleStep(std::size_t pattern, std::size_t channel, int step);

    // ---- piano roll
    [[nodiscard]] const std::vector<Note>* notes(std::size_t pattern, std::size_t channel) const;
    /// Adds a note (clamped); returns its index.
    std::optional<std::size_t> addNote(std::size_t pattern, std::size_t channel, Note note);
    /// Replaces note `index` (clamped), e.g. after a move or resize.
    void setNote(std::size_t pattern, std::size_t channel, std::size_t index, Note note);
    void removeNote(std::size_t pattern, std::size_t channel, std::size_t index);

    // ---- playlist
    /// Appends a track named "Track N"; its index, or nullopt at kMaxPlaylistTracks.
    std::optional<std::size_t> addTrack();
    /// Removes a track (never the last one) with its clips (pattern and
    /// audio); clips on later tracks move up one, and unnamed tracks after it
    /// take their new number.
    void removeTrack(std::size_t track);
    /// Names a track (trimmed, at most kMaxTrackNameLength bytes); a blank
    /// name gives back "Track N".
    void setTrackName(std::size_t track, const std::string& name);
    /// Moves a track to position `to` (clamped); its clips go with it, and
    /// unnamed tracks take their new number. Returns the new position.
    std::size_t moveTrack(std::size_t from, std::size_t to);
    /// Mutes or unmutes a track: its clips (pattern and audio) stay silent.
    void setTrackMuted(std::size_t track, bool muted);
    /// Solos a track: while any track is solo, only solo tracks play.
    void setTrackSolo(std::size_t track, bool solo);
    /// Whether `track`'s clips play: neither it nor a track it belongs to is
    /// muted, and when any track is solo, it or a track it belongs to is.
    /// False for a track that does not exist.
    [[nodiscard]] bool trackAudible(std::size_t track) const;
    /// The track's colour, 0xRRGGBB (higher bits dropped); nullopt: none.
    void setTrackColor(std::size_t track, std::optional<std::uint32_t> color);
    /// Sets a track's lane height (clamped to PlaylistTrack::kMinHeight..kMaxHeight).
    void setTrackHeight(std::size_t track, int height);
    /// Hides or shows a track's sub-tracks in the playlist.
    void setTrackCollapsed(std::size_t track, bool collapsed);
    /// Makes `track` (with its sub-tracks) a sub-track of the track above it,
    /// or of that track's group. Returns whether it could (not the first
    /// track, not deeper than PlaylistTrack::kMaxDepth).
    bool indentTrack(std::size_t track);
    /// Moves `track` (with its sub-tracks) one level out of its group;
    /// tracks after it in the same group become its sub-tracks. Returns
    /// whether it could (it was a sub-track).
    bool outdentTrack(std::size_t track);
    [[nodiscard]] bool canIndentTrack(std::size_t track) const;
    /// The track `track` is a sub-track of; nullopt at the top level.
    [[nodiscard]] std::optional<std::size_t> parentTrack(std::size_t track) const;
    /// Whether the track right after `track` is its sub-track.
    [[nodiscard]] bool hasSubTracks(std::size_t track) const;
    /// Swaps `track` and its sub-tracks with the neighbouring track of the
    /// same group above (up) or below (down), with its sub-tracks; clips go
    /// with their tracks. Returns `track`'s new index, or nullopt when there
    /// is no such neighbour.
    std::optional<std::size_t> moveTrackGroup(std::size_t track, bool down);
    [[nodiscard]] bool canMoveTrackGroup(std::size_t track, bool down) const
    {
        return groupSwap(track, down).has_value();
    }
    /// Index one past `track`'s last sub-track (track + 1 without any).
    [[nodiscard]] std::size_t subTreeEnd(std::size_t track) const;
    /// Whether `track` shows in the playlist: no track it belongs to is collapsed.
    [[nodiscard]] bool trackVisible(std::size_t track) const;
    /// Clips placed on `track`, pattern and audio.
    [[nodiscard]] std::size_t clipsOnTrack(std::size_t track) const;
    /// Places `pattern` on `track` at `start` (clamped); returns its index.
    std::optional<std::size_t> addClip(Clip clip);
    void setClip(std::size_t index, Clip clip);
    void removeClip(std::size_t index);
    [[nodiscard]] int clipLength(const Clip& clip) const
    {
        return clip.length > 0 ? clip.length : patternLength(clip.pattern);
    }
    /// Places a channel's sample on the playlist (track and start clamped);
    /// returns its index, or nullopt for a channel that does not exist.
    std::optional<std::size_t> addAudioClip(AudioClip clip);
    void setAudioClip(std::size_t index, AudioClip clip);
    void removeAudioClip(std::size_t index);

    /// Clamps a note to the valid key range, start >= 0 and length >= kMinNoteLength.
    [[nodiscard]] static Note clamped(Note note);

private:
    [[nodiscard]] ChannelPattern* channelPattern(std::size_t pattern, std::size_t channel);
    [[nodiscard]] const ChannelPattern* channelPattern(std::size_t pattern, std::size_t channel) const;
    [[nodiscard]] Clip clamped(Clip clip) const;
    [[nodiscard]] AudioClip clamped(AudioClip clip) const;
    [[nodiscard]] bool patternNameUsed(const std::string& name) const;
    [[nodiscard]] Pattern emptyPattern(std::string name) const;
    /// Puts `pattern` at index `at`; clips of later patterns follow.
    std::size_t insertPatternAt(std::size_t at, Pattern pattern);
    /// The neighbouring groups moveTrackGroup() swaps: [first, middle) and
    /// [middle, last); nullopt when there is nothing to swap with.
    struct GroupSwap {
        std::size_t first = 0;
        std::size_t middle = 0;
        std::size_t last = 0;
    };
    [[nodiscard]] std::optional<GroupSwap> groupSwap(std::size_t track, bool down) const;
    /// Keeps the track outline valid after tracks were removed or moved.
    void normalizeTrackDepths();
    /// Puts the tracks in `order` (old indices, each once); clips follow and
    /// unnamed tracks take their new number.
    void reorderTracks(const std::vector<std::size_t>& order);

    std::vector<Channel> channels_;
    std::vector<Pattern> patterns_;
    std::vector<Clip> clips_;
    std::vector<PlaylistTrack> tracks_;
    std::vector<AudioClip> audioClips_;
};

} // namespace vloomdaw
