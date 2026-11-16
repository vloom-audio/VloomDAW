# Changelog

All notable changes to VloomDAW are listed here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Security
- Refused UNC and network-share paths (`\\server\share\...`, `//server/share/...`) across `.vloom` plugin slots, relative sample resolution, project load/save/summary, CLI startup paths, recent projects, launcher file dialogs, and plugin scan folders/manual files (`ProjectFile`, `FilePaths`, `MainWindow`, `LauncherDialog`, `RecentProjects`, `PluginStore`, `PluginManagerDialog`, `main.cpp`) to prevent silent Windows SMB/NTLM credential leakage.
- Hardened plugin thumbnail cache IDs and file lookups (`PluginMetadata::pluginThumbnailId`, `isValidPluginThumbnailId`, `PluginThumbnails`) against ill-formed non-UTF-8/unpaired-surrogate paths, `Builtin` vs. `Vst3` digest collisions, and directory traversal outside `thumbnails/`.

### Fixed
- Guarded asynchronous background sample-load callbacks (`MainWindow::projectGeneration_`) against project reset/open/undo/redo races and pruned unloaded plugin handles from `unpublishedInstruments_` during undo cleanup.
- Fixed `bindAudioToTrack` channel reuse to inspect per-channel step/note usage across all patterns (`isChannelUsedInAnyPattern`) rather than Pattern 1 globally, and hardened clipboard tick arithmetic, `UndoHistory` timestamp coalescing, and non-finite `±infinity` parameter/tempo guards (`TrackRouting`, `UndoHistory`, `Project`, `MixerModel`, `ChannelRackAudio`, `PlaylistView`, `PianoRollView`).
- Timeline track & Mixer strip synchronization: unified 1:1 state and routing
  across Playlist tracks and Mixer inserts (`bindAudioToTrack`, `pairTrackWithInsert`,
  and persisted `PlaylistTrack::insert`), eliminating orphaned mixer strips when
  dropping audio stems onto pre-created or named tracks and keeping Mute (`M`),
  Solo (`S`), Rename, Volume, Pan, Color, Selection, Add, Delete, and Move in
  lockstep between the Timeline and Mixer.
- Keyboard shortcuts & `Edit` menu: replaced `notYetIn` stubs in `MainWindow`
  with real Undo/Redo (`Ctrl+Z`, `Ctrl+Y`, `Ctrl+Shift+Z`) and active editor
  selection/clipboard commands (`Delete` / `Backspace`, `Ctrl+A`, `Ctrl+D`,
  `Ctrl+X`, `Ctrl+C`, `Ctrl+V`, `Escape`) across `PlaylistView`, `PianoRollView`,
  and detached `FloatingPanel` windows while delegating text editing keys cleanly
  when a `QLineEdit` has focus.
- Startup launcher window: `Start - VloomDAW` now opens standalone before
  constructing or showing `MainWindow` (or any detached editor/mixer panels),
  loading the main workspace only after choosing `Empty project` or opening a
  `.vloom` project.
- Mixer compact vertical layout: shrinking the mixer panel dynamically compacts
  each strip's FX slot rack from 9 visible slots down to 4 wheel-scrollable
  slots (with a right-edge mini scrollbar), keeping faders tall and responsive
  without squishing the slider or clipping routing labels.
- New project mixer state: new/empty projects now start with only `Master` in
  the mixer (`0` pre-populated empty inserts); dedicated inserts are created
  automatically as stems, samples, instruments, or channels are added.
- Playlist rendering & drag performance: fixed audio clip waveform painting so it
  clips strictly to the dirty region instead of redrawing the entire viewport width
  on every playhead tick or mouse drag, added multi-resolution waveform peak mipmaps
  plus a 256 px `QPixmap` waveform tile cache, split header vs. grid invalidation,
  and coalesced live `ChannelRackAudio` rebuilds during high-rate clip drags.
- Playlist: dropping multiple audio files at once now places each file on its
  own consecutive track (creating new tracks as needed, and starting on Track 1
  when dropped into an empty untouched playlist) instead of keeping only one or
  stacking on a single track. Multiple files can also be selected and dragged
  together from the sample browser and onto the channel rack.
- Windows: a plugin file that cannot be loaded (not a plugin, or missing its
  own DLLs) no longer shows a system "Bad Image" message box that stops the
  app (vloom-engine #27). The same box hung the Windows CI job.

### Added
- Studio One-style Plugins Side Browser & Plug-in Thumbnails (`PluginsPanel`,
  `PluginThumbnails`, `PluginEditorWindow`, `ParameterEditorWindow`):
  - Segmented **`Instruments`** and **`FX`** top tabs with live counts, sort/group
    modes (`By type / category`, `By vendor`, `By format`, `Flat (A-Z)`), inline
    thumbnail preview rows, a `Show Thumbnails` toggle, and a bottom plugin
    inspector card (`INST`/`FX` + format badge, name, vendor, category, preview).
  - Studio One-style plugin editor header bar (`#pluginEditorHeader`) with active
    power/bypass toggle, slot dropdown menu (`Favorite`, `Replace...`, `Disable`,
    `Remove`, `Show in Console`, **`Update Plug-in Thumbnail`**, and
    `Remove Plug-in Thumbnail`), plus a 1-click **`Update Thumbnail`** capture
    button that snapshots native VST3/CLAP editor views cleanly.
- Expanded Transport & Master Monitor Controls (`TransportBar`):
  - Full transport deck (`PAT/SONG`, `Go to Start`, `Step Back 1 Bar`,
    `Step Forward 1 Bar`, `Go to End`, `Stop`, `Play/Pause`, `Record`, and
    `Loop` cycle toggle).
  - Master volume & monitor group (`Mute` speaker toggle, `Vol -` / `Vol +`
    step buttons, `VOL` knob, and `-15 dB` `DIM` monitor attenuator).
- Mixer Console Bottom Toolbar (`#mixerFooter`) & Playlist Quick Toolbar:
  - Channel filter bank (`All`, `Audio`, `Instruments`, `With FX`), strip view
    toggles (`Wide Strips`, `FX Rack`), and console batch actions (`+ Add FX`,
    non-destructive global `Bypass All FX`, `Clear Solo`, `Clear Mute`, and
    `Reset Peaks`).
  - Playlist header quick buttons (`Snap` on/off toggle, `Q` quantize,
    `Follow Playhead`, and `-` / `+` horizontal zoom).
- Timeline Ruler Mouse-Wheel Zoom (`PlaylistView` & `PianoRollView`):
  - Pointing at the timeline ruler and scrolling the mouse wheel zooms
    horizontally in/out anchored at the cursor position without holding `Ctrl`
    (plus `Shift+Wheel` horizontal pan), alongside disjoint `event->region()`
    repainting during playback so simultaneous meter + playhead updates stay fast.
- Bounded gesture-coalescing Undo/Redo engine (`UndoHistory`): 100-step undo/redo
  across timeline clips, audio stems, piano roll notes, step sequencer pads,
  patterns, tracks, mixer faders/pans/mute/solo/colors, and instrument/effect
  plugins (preserving live plugin instances and parameter state across unload/reload).
- Detailed Studio Plugin Manager (`PluginManagerDialog`):
  - Two-pane layout with a filter sidebar (`Search`, `Favorites Only`,
    `Failed Only`, `Disabled Only`, and `Format`, `Category`, `Sub-Type`, and
    `Vendor` checklists with `All` / `Clear`, `Uncheck All Vendors`, and
    `Reset Filters`) using clean studio typography with no emojis or external icons.
  - Sortable plugin table (`On` enable checkbox, `Fav` toggle, `Plugin Name`,
    `Format`, `Category`, `Sub-Type`, `Vendor`, `Version`, `Status`, `Path`),
    multi-row context menu (`Mark/Remove Favorite`, `Enable/Disable`,
    `Set Category`, `Rescan Selected`, `Open Containing Folder`, `Remove from List`),
    `Retry Failed`, `Add Plugin File...`, and collapsible `Scan Folders` card.
  - Favorites group and enabled-only filtering integrated into `PluginsPanel`
    and `PluginPickerPopup`, persisted in `PluginStore`.
- Fender Studio-style active track & mixer strip highlighting: clicking a
  playlist track header/clip or a mixer strip visually marks the active
  channel/track with an elevated highlight frame and synchronizes selection
  between the Playlist, Mixer, and Channel Rack (plus `Up`/`Down` arrow
  navigation in the playlist).
- Studio One-style playlist track headers:
  - Horizontally resizable track header column (`160..420 px`, double-click the
    divider to reset, saved across sessions).
  - Track index badge, `[M]` / `[S]` toggle pills, waveform/MIDI type icon, and
    horizontal **Volume** & **Pan** bars linked 1:1 with each track's mixer insert
    (with `Shift` fine drag, double-click reset and dB/pan readouts).
  - Vertically stretching a track header taller (or double-clicking its bottom
    edge) progressively reveals clickable **Channel (`CH`)** and **Mixer Routing
    (`OUT`)** inspector menus.
  - Live per-track stereo **VU meters (`L | R`)** with peak hold on the right
    edge of each track header.
- Playlist instant stem drop: dropping multiple audio files immediately creates
  all tracks, channels, mixer inserts and named audio clips on the UI thread,
  while audio decoding and waveform peak extraction run in the background and
  fill in each clip's length and waveform as it finishes.
- Timeline playhead marker: the green playhead line in the playlist and piano
  roll now carries a marker head cap in the top bar-number ruler.
- Track & mixer colour picker: tracks and channels receive distinct spectrum
  colours automatically, and clicking a playlist track's left colour strip (or
  a mixer strip's top colour bar, or "Color" in right-click menus) opens a
  16-column swatch grid popup with custom colour and reset options.
- Studio One-style mixer auto-routing: channels automatically link to dedicated
  mixer inserts (growing past 16 inserts as needed) that sum into Master
  (`Channel -> Insert N -> Master`), with a routing readout on each strip.
- Built-in effects: the effects that come with VloomDAW (EQ and Gain, from
  vloom-dsp) are listed with the scanned plugins, in the Plugins panel and
  the mixer's plugin picker. They have no editor of their own: a window with
  a knob per parameter opens instead, a column per band. Projects save them
  by id (`"format": "builtin"`).
- Playlist: hold Alt while moving, trimming or slicing a clip to do it
  freely, without snapping to the grid.
- Playlist time bar scrub: click or drag the timeline ruler (time bar) on top of
  the playlist to scrub or relocate playback at any time, without having to switch
  to the Playback tool (Y).
- Mixer FX rack: each strip shows its insert's effects as slots with bypass
  switches, and an empty slot opens a plugin picker. An insert takes 9
  effects (was 10); a project saved with 10 opens with the first 9 and a
  warning.
- The File menu works:
  - **Projects:** New, Open, Open Recent (with Clear), Save and Save As read
    and write `.vloom` project files, a versioned JSON format
    (docs/project-file.md). A project holds:
    - channels, patterns, steps and notes;
    - the playlist (tracks, clips, audio clips);
    - the mixer, with every plugin and its saved settings;
    - the song's tempo, key and song mode.
  - **Unsaved changes:** they show as "*" in the window title, and New, Open
    and quitting ask about them.
  - **Import Audio:** adds audio files as new channels.
  - **Export WAV:** renders the song, or the pattern when the playlist is
    empty, plugins included, with progress and cancel. Formats are 24-bit,
    16-bit or 32-bit float.
  - **Command line:** `VloomDAW song.vloom` opens a project.
- A start window (File > Start Window, and at startup unless turned off).
  - **New project:** at a tempo you pick; the last tempo is remembered.
  - **Recent projects:** cards with an arrangement preview, tempo, length
    and plugin count.
  - **Finding one:** pin, search, and filter by all, pinned or this week.
  - **Audio status:** a status line shows the audio output.
- Opening a project file is built for untrusted files:
  - sizes, counts and nesting are capped;
  - every value is checked;
  - samples on network shares are left out;
  - only plugins the user's scan found are loaded.
- New Project now also resets the mixer, so the old project's inserts and
  effects no longer stay.
- Plugin hosting (CLAP and VST 3, in-process, vloom-engine ADR 0006 and 0009):
  drag a plugin from the Plugins panel onto a mixer strip (an effect, the
  master's too) or the channel rack (an instrument), or use its context menu.
  A strip's FX button lists its effects: open the editor, bypass, remove.
  Double-clicking a plugin channel opens its editor. Editors are the plugins'
  own windows, sized as they ask and resizable when they allow it; a timer
  runs the plugins' main-thread work and follows latency they change while
  running. Not saved with the project yet.
- The interface uses Noto Sans Thai, with Noto Sans for Latin letters and
  digits (regular, semi-bold, bold), bundled in `resources/fonts` under the SIL Open Font
  License, so it looks the same on every OS. Other scripts fall back to the
  system's fonts.
- Installers built by CI (`.github/workflows/release.yml`, see
  `docs/releasing.md`): a Windows setup (Inno Setup), a universal macOS DMG
  and a Linux AppImage. A `v*` tag drafts a GitHub Release with them and
  their SHA-256 sums; "Run workflow" builds them as artifacts. Not code-signed
  yet.

### Security
- Supply chain: GitHub Actions are pinned to commit SHAs (checkout v7.0.1, cache
  v6.1.0, install-qt-action v4.5.0; Dependabot keeps them current), the CPM.cmake
  download is checked against a pinned SHA-256 (a cached copy that does not match is
  fetched again), Catch2 is pinned to the commit of its v3.7.1 tag, and vloom-engine
  is pinned to a commit in `cmake/Dependencies.cmake`, which CI checks out too, instead
  of following the engine's `main`.

### Added
- Finer snap: the playlist snaps clips to 1/32 notes ("Snap: 1/32", and Auto
  when zoomed far in), and they play exactly there (vloom-engine places
  song clips on ticks now, not steps). The piano roll has a snap menu too:
  beat, 1/8, step (1/16, the default), 1/32 or 1/64. A clip snapped to 1/32
  notes can be one 32nd long. An audio clip's offset and length keep their
  exact place when its tempo or fit changes instead of rounding to steps.
- Mute and solo for playlist tracks: the M and S buttons on each track's
  header. A muted track's clips (pattern and audio) do not play; while any
  track is solo, only solo tracks play (mute wins over solo). Clips on a
  track that does not play are drawn faded.
- Playlist tracks of their own height: drag the bottom edge of a track's
  header (24-200 px; double-click it for the default 36 px).
- Sub-tracks: "Make Sub-track" in a track's right-click menu puts it (with
  its own sub-tracks) under the track above, up to 4 levels deep; "Move Out
  of Group" takes it out again. The arrow on a track with sub-tracks hides or
  shows them (hidden ones still play). Muting or soloing a track mutes or
  solos its sub-tracks too, and Move Up / Move Down move a track with its
  sub-tracks past its neighbour in the same group.
- The track header is wider (170 px) to fit the buttons and the indentation.
- Playlist tools, side by side in one box on the playlist's header, the one
  in use lit; "+ Track" and "−" sit together in a box of the same height
  beside them, as do the channel rack's "+ Bar" and "−" and the mixer's
  "+ Insert" and "−" (keys work while
  the playlist has the focus): Draw (P, as before), Paint (B: drag along a
  track to lay the current pattern in every slot), Delete (D: click or drag
  over clips), Mute (M: click or drag over clips; a muted clip is grey and
  silent), Select (E: drag a box, Ctrl adds or toggles, drag the selection to
  move it; Delete removes it, Ctrl+A selects all, Escape clears), Zoom (Z:
  drag a range to fill the view, click to zoom in, right-click out) and
  Playback (Y: hold to play the song from the pointer, drag to scrub). The
  header's caption explains the tool in use.
- Finer snapping in the playlist: a Snap box on its header (Auto, Bar,
  Beat, Step). Auto, the default, snaps to the finest grid line far enough
  apart to aim at for the zoom: beats at the default zoom, steps (16th notes)
  zoomed in, bars zoomed far out; the grid now shows step lines when zoomed
  in. It applies to placing, dropping, moving, trimming and cutting clips;
  Shift snaps to steps whatever the box says (it used to snap moves to beats
  instead of bars). Steps are the finest: the engine places clips on steps.
- Fit to tempo for audio clips: right-click an audio clip's title strip,
  Fit to Tempo > Stretch (keeps the pitch; Signalsmith Stretch, MIT, pinned)
  or Resample (the pitch follows). The sample's tempo is measured from its
  audio (`audio/TempoDetector`: the spacing at which its attacks repeat,
  70-180 BPM, made exact for loops of whole bars and rounded to a whole BPM
  when within a third of one; not the file name), or set by hand (Set
  Tempo…); Detect Tempo measures again. A fitted clip keeps its bars on the
  song's bars at any tempo; its title shows its tempo. Stretched copies are
  made on a background thread once the tempo settles; the clip plays
  resampled meanwhile.
- Song mode loops the song, as in FL Studio: at the end of the last clip
  (rounded up to a bar) playback goes back to the start instead of running on
  past an empty playlist. The end follows edits, audio clip lengths and the
  tempo (engine loop region, `Engine::setLoop()`).
- Browser preview strip: under the browser's list, the selected file's name,
  length and waveform (worked out on the previewer's thread, cached with the
  sample), with a play / stop button and a line that follows the preview as
  it plays; click the waveform to play from the start. Selecting a file with
  the arrow keys while Auto preview is off shows it without playing it.
- MP3, FLAC, Ogg (Vorbis, Opus), AIFF and M4A (AAC, ALAC) samples: drop them
  on the channel rack or the playlist, or preview them in the browser, like
  WAV files. FFmpeg (`audio/FileDecoder`) decodes each file once, resampling
  it to the engine's rate on the way, into a 32-bit float WAV in the app's
  cache folder (named after the file's path, size, date and the rate, so an
  edited file is decoded again); the project keeps the original path. A
  4-minute MP3 is ready in under a second (release build). FFmpeg may only
  read local files and only these containers and codecs, so a crafted file
  cannot make it fetch URLs or reach its other parsers; files longer than 20
  minutes, with more than 8 channels or over 1 GiB decoded are refused. WAV
  files still go through the engine's own hardened reader.
  Builds need FFmpeg 5.1 or later: Windows downloads a pinned LGPL build at
  configure time (its DLLs and licence ship with the app); macOS and Linux
  use the system's for development and a small static one for releases
  (`scripts/build-ffmpeg.sh`, from the signed 8.1.3 source, pinned by SHA-256).
- A dropped sample (any type) is decoded and resampled on a background
  thread, with "Loading …" in the status bar: a song several minutes long no
  longer freezes the window.
- Slice tool (C), as in FL Studio: a cut line follows the pointer, snapped
  to the nearest beat (Shift: step); click, or drag up or down across tracks,
  and every clip the line crosses is cut in two there. The second part goes
  on where the first stopped, in the pattern or the sample.
- Trim and stretch clips: with Draw, Paint or Select, drag a clip's left or
  right edge (beat grid; Shift: step). A pattern clip longer than its pattern
  repeats it; an audio clip reaches no further than its sample at either end,
  and neither kind past the start of its pattern or sample. Needs vloom-engine
  with clip offsets and lengths (`SongPatternClip::offsetSteps`,
  `SongAudioClip::offsetSteps` / `lengthSteps`).
- Colours of your own: a Color submenu (12 presets, Custom… with a colour
  dialog, Default) in the right-click menu of a playlist track's header, a
  channel's name in the channel rack and a mixer strip (inserts and the
  master). A track shows its colour as a bar on its header and a tint behind
  its clips, and its clips (pattern and audio) take its colour too, unless
  muted (grey); a channel on its name button, its lit step pads and its notes in
  the piano roll; a mixer strip as a bar along its top.
- Pan knob for each channel in the channel rack, beside the volume knob: drag
  (Shift: fine), scroll, double-click for the centre; the tooltip reads C,
  L 35, R 100.
- Browser refresh: the ⟳ button, or F5 while the browser's list has the
  keyboard (F5 still opens the playlist elsewhere), reads the folder again.
  Open folders stay open and the selection and scroll position stay; a search
  runs again.
- Reorder mixer inserts (right-click a strip, Move Left / Right) and playlist
  tracks (right-click a track name, Move Up / Down). Settings, names and
  clips go along; channels follow their insert.
- Song mode plays the playlist: with SONG (`L`) on the transport, Play runs
  through the pattern clips (each channel's steps of that pattern) and the
  audio clips from the playhead; starting or jumping into the middle of an
  audio clip plays it from there. PAT still loops the current pattern.
- Audio clips in the playlist: drop a WAV file from the browser or the
  desktop on a track (snapped to the bar; below the last track adds one). As
  in FL Studio, the clip plays through a channel of the channel rack: a new
  one named after the file, or the channel already playing it, so the
  channel's volume, pan, mute and mixer insert apply. The clip shows the
  channel's name and the waveform and is as long as the sound at the current
  tempo; drag to move (Shift: beats), right-click to delete. Dropping one in
  pattern mode turns song mode on, so Play is not silent.
- Name mixer inserts: double-click an insert's name, or right-click its strip
  and choose Rename…; an empty name gives back "Insert N". A long name is cut
  with "…" on the strip and shown whole in its tooltip; the channel rack's
  insert menu lists it as "3 · Drums". A named insert keeps its name when an
  earlier one is deleted.
- Playlist tracks: a new project starts with one track instead of 16; "+ Track"
  in the playlist header adds one (up to 500), "−"
  deletes the last, and a track name's right-click menu renames or deletes that
  track (double-click or right-click Rename… to rename inline; Enter confirms,
  Escape cancels). Deleting a track with clips asks
  first; the clips on later tracks move up with their track.
- Add and delete mixer inserts: "+ Insert" in the mixer header adds one at the
  end (up to 125, the engine's limit), "−" deletes the last, and a strip's
  right-click menu deletes that insert. The inserts after a deleted one keep
  their settings and take the numbers one lower; channels routed to it play
  into the master (it asks first when there are some), and channels on later
  inserts follow theirs. The channel rack's insert menu follows the mixer.
- More step pads in the channel rack: the "+" after the last pad, or "+ Bar" in
  the header, adds a bar of 16 pads to every channel of the current pattern (up
  to 16 bars); "−" removes the last bar, asking first when it has steps on. Each
  pattern keeps its own length, which its loop and its playlist clips follow,
  and clone and split by channel keep it.
- Sample preview in the browser: clicking a file plays it through the master
  bus, even while the transport is stopped; clicking it again stops it, and so
  does Esc or Stop. With Auto preview (the speaker button next to search,
  remembered) the arrow keys play each file they reach. Files load on a worker
  thread and the last 16 stay cached, so moving through a big folder never
  stalls the UI; a file that cannot be read says why in the status bar.
- Patterns menu (after Edit), as in FL Studio: find the first or next empty
  pattern (Shift+F4, F4, Ctrl+F4 without naming), rename (F2), transpose every
  note, insert one (Shift+Ctrl+Ins), clone (Shift+Ctrl+C), delete
  (Shift+Ctrl+Del; asks first when the pattern has steps or notes, as there is no
  undo yet), move up / down (Shift+Ctrl+Up / Down) and split by channel.
  Playlist clips follow their pattern through every change. Colours and
  rendering a pattern to an audio clip say they arrive in M3.
- Edit menu between File and Options: Undo (Ctrl/Cmd+Z), Redo (Shift+Ctrl/Cmd+Z,
  also Ctrl+Y on Windows), Cut, Copy, Paste, Duplicate, Delete and Select All with
  their shortcuts. Like Open and Save, they say when they arrive (undo at the
  start of milestone M2, the rest with the M2 editing tools); a focused text
  field keeps its own copy, paste, undo and select-all keys.
- Channel rack volume and routing: each channel has a volume knob (drag, Shift
  for fine, scroll, double-click for 0 dB) and an insert box; click it to send
  the channel to one of the mixer's 16 inserts ("-" plays straight into the
  master). Insert faders, pans, mute and solo now shape the sound, and insert
  meters show what plays through them.
- Losing the audio output (an unplugged interface or headphones) no longer leaves
  the app playing in silence: it carries on through the system default right
  away, says so in the status bar, and switches back to your device when it is
  plugged in again. Your chosen device stays saved.
- Plugin vendor and category: with "Read vendor and category" on, a scan loads each
  new or changed plugin in a separate process (`vloomdaw-plugin-probe`) and asks its
  factory for name, vendor, version and VST3 sub-categories / CLAP features. A plugin
  that crashes or hangs (20 s) is skipped and marked; unchanged plugins reuse the last
  scan. The sidebar groups by category (instruments split into Synth, Sampler, Drums,
  Keys...), vendor or format, and searches names and vendors. Right-click a plugin to
  set its category yourself (kept across scans); Rescan All reads every plugin again.
- The channel rack plays: drop a WAV file from the browser or the desktop on a
  channel (below the last one for a new channel) and its steps trigger the sample
  through vloom-engine's step sequencer. Edits apply while playing without cutting
  notes. Channels without a sample are dimmed.
- Plugins side panel (second sidebar page): lists the VST3 and CLAP plugins found
  by the last scan, grouped by format, with search. Plugins are only available after
  a scan, which the user starts in the Plugin Manager (Options > Plugin Manager…);
  nothing scans automatically. The scan runs in the background, reads file names only
  (no plugin code is loaded), covers the default folders plus folders the user adds,
  and its results are remembered across restarts.
- Main window with a transport bar: play / pause (Space), stop and return to start
  (Shift+Space), go to start (Home), metronome (Ctrl/Cmd+M)
- LCD tempo display: drag (Shift: fine), scroll or double-click to type the BPM.
  The time signature is shown next to it.
- LCD song position: bar · beat · tick, or click to switch to min : sec . ms
- File menu: New Project (Ctrl/Cmd+N: stop, rewind, reset tempo), and Open,
  Open Recent, Save, Save As, Import, Export with their shortcuts. Those report
  that they arrive with projects in milestone M4.
- Tap tempo (T)
- The mixer, piano roll and playlist can be detached into their own windows
  (button in their header) and docked back. Detached windows have minimise /
  maximise / close in the panel header (no native title bar on Windows: drag
  the header to move them) and a taskbar button, close with the main window,
  and are remembered with their position. Transport and view shortcuts work in any
  window.
- Performance overlay (View > Show Performance Overlay, Ctrl/Cmd+Shift+D):
  frames per second, paint time, refresh tick spacing, UI work per frame
  with the frame rate it could reach, and audio load
  (`core/FrameStats`)
- Smoother UI: the display refreshes at the screen's rate (30-240 Hz, was a
  fixed 30), the playhead is drawn anti-aliased at sub-pixel positions,
  editors repaint only the strip a moving playhead covers, LCDs and meters
  paint opaquely, and the refresh timer is precise (it fired every 31-47 ms
  on Windows). Qt's own repaint delay is off (`QT_QPA_UPDATE_IDLE_TIME=0`),
  which removed 15 ms frame-time spikes at 144 Hz. `VLOOM_REFRESH_HZ` forces
  a refresh rate for testing.
- Song editors in the workspace (editing only, nothing plays them yet), backed
  by a tested project model (`core/Project`: channels, patterns, steps, notes,
  playlist clips):
  - Channel rack (F6): mute light, name and 16 step pads per channel; click
    or drag to paint steps, right-click to clear, double-click a name to open
    its piano roll; add channels
  - Piano roll (F7): keyboard, ruler and grid with the key's scale shaded;
    click to add a note, drag to move, drag the right edge to resize,
    right-click to delete (Alt: no snap, Ctrl+wheel: zoom)
  - Playlist (F5): 16 tracks; click to place the current pattern, drag to
    move (Shift: snap to beats), right-click to delete, double-click to make a
    clip's pattern current
  - Playheads follow the transport in all three
- Mixer panel (F9) under the workspace: master and 16 insert tracks with pan,
  fader (cubic taper, unity at 80 %), dB readout, stereo peak meter with hold
  and clip light, mute and solo. The master track drives the engine (gain,
  pan, mute) and meters the real output; inserts get audio with channels in M1.
- Key LCD on the transport bar: root note (C .. B) and scale (major, natural /
  harmonic / melodic minor, the modes, pentatonics, blues, chromatic). Drag or
  scroll a field to step, click to pick. Scale maths in `core/Scales` for the
  piano roll's scale highlighting in M2.
- Click the audio device in the status bar to switch the output device
- FL Studio-style transport bar, in one row, with the tempo and position
  displays centred in the window: pattern / song mode (L), pattern selector
  (add, pick, delete), CPU and RAM meter, master volume knob (drives the engine), pitch
  knob, and buttons for Playlist (F5), Channel rack (F6), Piano roll (F7),
  Mixer (F9) and Browser (Alt+F8). A narrow window moves what does not fit
  behind a » button. Only Browser opens a panel so far; the
  others, song mode, patterns and pitch report the milestone they arrive in.
- Record-arm button on the transport bar (R); it only keeps the armed state
  until the engine can capture audio input
- Sidebar: an icon strip opens a resizable side panel; first panel is the
  Browser for audio files in your sample folders (add / remove folders, search)
- Settings window (Ctrl/Cmd+,) with pages General (start-up position format,
  metronome), Audio (output device and status, sample rate, buffer size,
  latency) and Theme; each page groups its settings in cards with a short
  explanation per setting. Settings are saved and the output device is
  remembered by name.
- Theme editor (Options > Theme…), applied to the whole app as you edit and
  remembered: seven dark presets shown as miniatures of the app (Midnight,
  Graphite, Deep Black, Slate, Forest, Plum, Ember); Hue, Saturation,
  Brightness, Contrast and Text knobs that tune the preset; a colour per role
  (selection, playing, record, LCD, text) with a colour picker; quick accent
  picks; and a warning when the result drops below WCAG AA contrast. Presets
  and accents are checked against WCAG contrast ratios in tests, and the
  colour maths lives in `core/ThemePresets`.
- Icons follow the theme: SVGs are drawn in placeholder colours that
  `Theme::icon()` swaps for the current text, accent and record colours at paint
  time (e.g. the browser's folder icons take the accent colour)
- Dark theme with a central palette (`ui/Theme`) and SVG icons
- macOS: tool bar in the title bar row; full screen keeps the window buttons
- Windows: no native title bar; the window buttons sit in the menu bar row and
  empty menu / tool bar space moves the window (resize, Aero Snap and the
  taskbar still work natively)
- Window size and position are remembered between sessions

### Fixed
- Deleting a playlist track in song mode stops its clips at once; they used
  to play on until the next edit.
- The pattern selector shows the project's pattern names: after deleting a
  pattern it showed renumbered names that no longer matched the panel titles,
  and deleting no longer renames the remaining patterns.
- A narrow window no longer turns the transport bar into two rows with the
  second one sometimes blank: groups that do not fit open from a » drop-down
  instead, the bar keeps its height and the displays stay centred while there
  is room. Shortcuts of the moved buttons (T, Ctrl/Cmd+M, F5-F9) keep working.
- Opening a big folder in the sample browser no longer freezes the app: folders
  are read and sorted on a background thread and their rows added at once (a
  folder of 20 000 samples now shows in about 50 ms instead of hanging the
  window for 2 s). Hidden folders and macOS "._" files stay hidden; a folder
  with more than 100 000 items shows the first 100 000 and says so.
- Browser search finds samples in every sub-folder, not only in folders that
  are open: matches show as one list (hover for the folder), drag onto the
  channel rack as before. It runs in the background once typing pauses, a new
  search cancels the previous one, and clearing it brings the folder tree
  back as it was.
- The app has its own icon (Windows executable, macOS bundle, window icon).
- No more UI stutter every 2 s while the chosen output device is missing: the
  app lists devices only when the OS reports a change, and on a background
  thread, instead of polling on the UI thread (each listing took 50-200 ms).
- macOS: detached panels (playlist, piano roll, mixer) no longer show a separate
  title bar: like the main window, the panel header takes its place, holds the
  window buttons and moves the window.
- Detached panel windows are named "VLOOM Mixer", "VLOOM Playlist" and "VLOOM
  Piano roll" in the taskbar, the Dock and window switchers.
- Detached panels show "VLOOM" before their title in the header, which is the
  window's title bar on every platform.
