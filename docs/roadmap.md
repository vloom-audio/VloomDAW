# Roadmap to a complete VloomDAW

What has to be built, in order, until every feature in the
[feature list](features.md) works end to end (no 🎨 or ⏳ left), plus the
basics a DAW is expected to have that the list does not show yet (undo,
recording, MIDI input, effects).

Each milestone lands as a series of small PRs, engine first, then the app with
the pinned engine bumped (see the `vloom-dev` skill, `full-stack-feature.md`).
"Done when" is the acceptance test for the milestone; the feature list and the
README roadmap are updated in the same PR that makes a feature work.

## Milestone status summary

| Milestone | Scope | Status |
|---|---|---|
| **M0** | Workspace shell, audio device configuration, transport, metronome, sample browser | ✅ Complete |
| **M1** | Channel rack & 16-step sequencer with real-time sample playback and mixer insert routing | ✅ Complete |
| **M1.5** | Per-channel panning, pattern selector, and sample browser waveform preview | ✅ Complete |
| **M2** | Piano roll playback, built-in wavetable synthesizer, master pitch, and undo/redo history | ✅ Complete |
| **M3** | Playlist arrangement playback, Song/Pattern mode, Loop Bar, and multi-format offline export (WAV, FLAC, MP3, Ogg, M4A) | ✅ Complete |
| **M4** | `.vloom` project save/load, audio & multi-track MIDI import, CLAP hosting, mixer insert FX, and Start Hub | ✅ Complete |
| **M5** | VST 3 & VST 2 hosting (with 32-bit Windows bridge), built-in EQ/FX, Bus/FX/Aux channels, Pre/Post & Sidechain sends, PDC, and stem separation | ✅ Complete |
| **M6** | Hardware audio input & monitoring, Audio I/O Setup channel routing matrix, latency-compensated Audio & MIDI recording, MIDI FX chains, multi-output instruments, and Piano Roll MIDI CC/Pitch Bend lanes | ✅ Complete |

### Remaining polish toward v1.0

| Feature | Status | Notes |
|---|---|---|
| Autosave & crash recovery | ⏳ | Periodic background recovery snapshots for unsaved `.vloom` sessions |
| Count-in & punch-in / punch-out | ⏳ | Pre-roll count-in bars and automated punch markers during audio/MIDI recording |
| Pattern colours & bounce to audio clip | ⏳ | Custom colour per pattern in `Patterns` menu and in-place pattern rendering |
| Light theme | ⏳ | High-contrast light preset in `core/ThemePresets` |

## M1.5 — finish the channel rack (small)

| Repo | Work |
|---|---|
| app | Pan knob per channel (the engine already pans channels); shares the volume knob's drag / wheel / double-click behaviour |
| engine | Preview voice: a `previewSample(buffer)` path that plays one sample on the master bus outside the sequencer, cut by the next preview |
| app | Browser: click a sample to preview, stop on a second click or Esc; samples loaded on the control thread like channel samples |
| app | Feature list: pattern selector ✅ |

Done when: pan and preview work while playing and stopped; tests cover the
preview hand-off without allocation on the audio thread.

## M2 — piano roll and the first synth

| Repo | Work |
|---|---|
| engine | Note data per channel and pattern (pitch, start, length in ticks, velocity) in `SequencerState`; sample-accurate note on / off across blocks, notes held over the pattern loop point and released on stop |
| engine | Pitched sampler playback (root key, cubic interpolation for real-time pitch) |
| engine | First synth (`dsp/`): 2 oscillators (PolyBLEP), state-variable filter, amp and filter ADSR, fixed voice pool with stealing; channels are either sampler or synth |
| engine | Master pitch (semitones) applied to every instrument |
| app | Piano roll plays: the edited notes reach the engine through `ChannelRackAudio`; channels without notes keep using steps |
| app | Channel type and a basic synth editor (oscillators, filter, envelopes) |
| app | Undo / redo foundation: `core/commands/`, every edit (steps, notes, clips, mixer, channel settings) is a command; `Ctrl/Cmd+Z`, `Shift+Ctrl/Cmd+Z` |
| app | Pitch knob drives the master pitch |

Done when: a melody drawn in the piano roll plays on the synth and on a
pitched sample, sample-accurately at every buffer size; every edit undoes and
redoes; notes never hang after stop.

## M3 — playlist, song mode and WAV export

| Repo | Work |
|---|---|
| engine | Arrangement state: pattern clips on tracks (start, length, pattern id), played from the transport position; pattern mode loops one pattern, song mode plays the arrangement |
| engine | Loop region and song end |
| engine | Export through `render/OfflineRenderer` + `WavWriter`: whole song or loop region, 16/24-bit PCM or 32-bit float, sample rate choice, faster than real time, cancellable |
| app | Playlist plays its clips; `L` switches pattern / song mode for real |
| app | Export dialog (File > Export > WAV) with progress |

Done when: a song built from several patterns plays identically in the app and
in the exported WAV (a test compares the offline render with expected onsets).

## M4 — projects and CLAP plugins → first alpha

| Repo | Work |
|---|---|
| app | Project file: versioned, documented format (channels, patterns, notes, clips, mixer, plugin states, sample paths relative to the project); parser built for untrusted files and fuzzed; Open, Open Recent, Save, Save As, autosave and crash recovery |
| app | Import: samples into the browser / channels, MIDI files into the piano roll |
| app | Missing samples: report, locate, keep the project loading |
| engine | Plugin host interface: instruments on channels and effects on mixer inserts, processed with the same real-time rules; latency reported per plugin |
| app + engine | CLAP hosting: load from the scan results, parameters, state save / restore, plugin editor windows |
| app | Mixer effect slots per insert |
| both | Crash safety for plugins (see `vloom-security`): decide in-process versus sandboxed hosting before writing it |

Done when: a project with samples, a synth, notes, an arrangement and CLAP
effects saves, reopens identically and exports the same WAV. This is the first
alpha.

## M5 — VST3, built-in effects, mixer completion

| Repo | Work |
|---|---|
| engine + app | VST3 hosting (after checking the SDK licence terms), same host interface as CLAP |
| engine | Built-in effects: EQ, compressor, reverb, delay, limiter on the master |
| engine | Sends and return buses, plugin delay compensation |
| app | Routing UI for sends, effect editors, meters on sends |

Done when: VST3 and CLAP instruments and effects work side by side; a project
mixes with sends and built-in effects without latency misalignment.

## M6 — recording and MIDI input

| Repo | Work |
|---|---|
| engine | Audio input in the WASAPI, CoreAudio and ALSA backends (duplex), input monitoring, recording to disk off the audio thread |
| engine | MIDI input per OS (CoreMIDI, Windows MIDI, ALSA sequencer) with timestamps mapped to sample positions |
| app | Record arm records audio clips into the playlist and notes into the piano roll; count-in, punch in / out |
| app | MIDI keyboard plays the selected channel live |

Done when: a take recorded from an audio interface and a MIDI keyboard lines
up with the beat after latency compensation.

## Any time (UI only)

- Light theme: a light preset set in `core/ThemePresets`, the contrast check
  passing, icons recoloured.
- Accessibility pass: keyboard navigation in the editors, screen reader names.

## Cross-cutting, every milestone

- Real-time rules, tests at many block sizes and no-allocation tests for
  everything reachable from `process()`.
- Fuzz every new parser (sample decoders, project files, MIDI files, probe
  records) before it ships.
- Feature list, README roadmap and CHANGELOG updated in the PR that changes a
  feature.
