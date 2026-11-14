# The .vloom project file

A VloomDAW project is one UTF-8 JSON file with the extension `.vloom`.
`src/core/ProjectFile.cpp` reads and writes it; this page is the format.

## Top level

```json
{
  "format": "vloomdaw-project",
  "version": 1,
  "app": "VloomDAW 0.1.0",
  "song": { "tempo": 140, "key": 9, "scale": "natural-minor", "songMode": true },
  "channels": [ ... ],
  "patterns": [ ... ],
  "tracks": [ ... ],
  "clips": [ ... ],
  "audioClips": [ ... ],
  "mixer": [ ... ]
}
```

- `format` must be `vloomdaw-project`.
- `version` is the format version, 1 for now. A file from a newer version is
  refused rather than read in part. A change that older versions can still
  read (a new optional key) keeps the version; one they cannot read raises it.
- Unknown keys are ignored. Missing keys take their defaults.
- `app` is informative only.

## Song

| Key | Meaning |
|---|---|
| `tempo` | BPM, 10 .. 999 |
| `key` | root pitch class, 0 = C .. 11 = B |
| `scale` | `major`, `natural-minor`, `harmonic-minor`, `melodic-minor`, `dorian`, `phrygian`, `lydian`, `mixolydian`, `locrian`, `major-pentatonic`, `minor-pentatonic`, `blues` or `chromatic` |
| `songMode` | plays the playlist instead of looping the pattern |

## Channels, patterns, playlist

- **Channel:**
  - `name` and `muted`;
  - `sample`: a UTF-8 path. Inside the project's folder it is relative, with
    `/` separators, so the folder can move. Elsewhere it is absolute;
  - `volume`: fader position, 0 .. 1 (0.8 = unity);
  - `pan`: -1 .. 1;
  - `insert`: the mixer insert, 0 = master;
  - `color`: `#rrggbb`;
  - `instrument`: a plugin (below).
- **Pattern:**
  - `name`;
  - `steps`: the number of step pads, 1 .. 256;
  - `channels`: one entry per channel, in channel order, each holding:
    - `steps`: a string with one character per pad, `x` = on, `.` = off;
    - `notes`: a list of `[key, start, length]`, in ticks (960 per beat).
- **Track (playlist):**
  - `name` and `named` (false: shown as "Track N");
  - `muted`, `solo` and `collapsed`;
  - `height`: 24 .. 200;
  - `depth`: 0 .. 4, the sub-track outline;
  - `color`.
- **Clip:**
  - `pattern`: an index;
  - `track`;
  - `start`, `offset` and `length`, in ticks (`length` 0 = the whole
    pattern);
  - `muted`.
- **Audio clip:**
  - `channel`: whose sample plays;
  - `track`, `start`, `offset`, `length` and `muted`, as for a clip;
  - `fit`: `off`, `stretch` or `resample`;
  - `sourceBpm`: the sample's own tempo.

## Mixer

`mixer` is the master first, then the inserts. Each holds:
- `name` and `named`;
- `fader`, `pan`, `muted`, `soloed` and `color`;
- `effects`: a list of plugins, in processing order.

## Plugins

```json
{ "format": "vst3", "path": "C:/Program Files/Common Files/VST3/FabFilter/FabFilter Pro-L 2.vst3",
  "id": "…", "name": "Pro-L 2", "bypassed": false, "state": "base64…" }
```

- `format`: `clap` or `vst3`.
- `id`: the plugin's id in its library (vloom-engine's `PluginInfo::id`).
- `state`: what the plugin's `saveState()` returned, as base64. It may be
  missing; the plugin then starts at its defaults.

## Opening untrusted files

A project file can come from anyone, so reading it is defensive:

- **Size and nesting:** the file is at most 256 MiB, and JSON nesting at
  most 32 deep (checked before parsing).
- **Counts:** 1000 channels, 999 patterns, 500 tracks, 100 000 clips of each
  kind, 100 000 notes per channel and pattern, 125 inserts, 10 effects per
  insert, and 64 MiB per plugin state. Past these the rest is left out with
  a warning.
- **Values:** every value is range-checked and goes through the model's own
  setters (`Project::restored()`). A clip of a pattern or channel that does
  not exist is dropped.
- **Network paths:** sample paths on network shares (`\\server\share`,
  `//server/share`) are left out. Opening one would send the user's Windows
  credentials to that server.
- **Plugins:** a plugin is loaded only if the user's plugin scan found it,
  with the same format and path. A project cannot make VloomDAW load a
  library from anywhere else. Plugins that are not loaded stay in the
  project, so saving it again keeps them, and the user is told which.
- **Saving:** the file is written to `<name>.vloom.saving` and then renamed
  over the old one, so a failed save never leaves a broken project.
