// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <cstddef>
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
};

/// A playlist track: a lane for clips.
struct PlaylistTrack {
    std::string name;
    bool named = false; ///< the user named it; otherwise "Track N", following its number
};

/// A pattern placed in the playlist. Its length is the pattern's length.
struct Clip {
    std::size_t pattern = 0;
    int track = 0; ///< 0-based playlist track
    int start = 0; ///< ticks from the song start

    friend bool operator==(const Clip&, const Clip&) = default;
};

/// A channel's sample placed in the playlist (FL Studio's audio clip): plays
/// once from `start` at its own speed (its length in ticks depends on the
/// tempo), through the channel: its volume, pan, mute and mixer insert.
struct AudioClip {
    std::size_t channel = 0; ///< the channel whose sample plays
    int track = 0; ///< 0-based playlist track
    int start = 0; ///< ticks from the song start

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
    void setChannelName(std::size_t channel, const std::string& name);
    /// The sample the channel plays (UTF-8 path; empty = none).
    void setChannelSample(std::size_t channel, const std::string& path);
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
    /// Clips placed on `track`, pattern and audio.
    [[nodiscard]] std::size_t clipsOnTrack(std::size_t track) const;
    /// Places `pattern` on `track` at `start` (clamped); returns its index.
    std::optional<std::size_t> addClip(Clip clip);
    void setClip(std::size_t index, Clip clip);
    void removeClip(std::size_t index);
    [[nodiscard]] int clipLength(const Clip& clip) const { return patternLength(clip.pattern); }
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

    std::vector<Channel> channels_;
    std::vector<Pattern> patterns_;
    std::vector<Clip> clips_;
    std::vector<PlaylistTrack> tracks_;
    std::vector<AudioClip> audioClips_;
};

} // namespace vloomdaw
