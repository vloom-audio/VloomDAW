# Editors of the built-in effects

The built-in effects without a picture of their own open
`ParameterEditorWindow`: a knob per parameter. The ones with a dedicated
editor follow the approved designs (FabFilter-like: the display first, a
floating panel of knobs over it) and are built on one shell,
`BuiltinEditorWindow`, in `src/ui/plugins/builtin/`. The equalizer
(`EqEditorWindow`) and the synthesizer (`SynthEditorWindow`) came before the
shell and keep their own windows.

## What the shell gives you

```
┌ header 40 px ─ ⏻ Name ───────── ‹ Preset › ───────── [A|B] ┐
│                                                            │
│   display: yours (setDisplay), fills the window            │
│                                                            │
│        ┌ panel(): knobs and switches, 100 px ┐             │
│        └─────────────────────────────────────┘             │
├ footer() 32 px: readouts left ……………………………… readouts right ┤
```

- **Header**: bypass (`bypassToggled()`, `setBypassed()`), the plugin's
  name, presets (`PresetBar`: "Default", the presets you pass to the
  constructor, and the user's own, saved in `presets/<plugin id>.json`), and
  A/B compare.
- **Parameters**: the shell reads every parameter and its value, and owns
  them. `change(id, value)` keeps it in range and steps, sets the plugin
  (`PluginInstance::setParameter()`, as `ParameterEditorWindow` does), updates
  every control bound to the id, stars the preset, calls
  `parameterChanged()` and emits `edited()` (the project's unsaved changes).
  Presets and A/B go through `change()` too.
- **Bound controls**, all made unparented for you to place:
  - `knob(id, label, scale)`: an `ArcKnob` (48 px dial, 270° arc in the
    accent, from zero for a range across it). Drag up or down (Shift: fine),
    wheel, arrow keys, double-click for the default. A choice parameter
    (`ParameterInfo::valueLabels`) steps through its names; a click moves it
    to the next.
  - `toggle(id, label, layout)`: a `PillToggle` for a 0/1 switch;
    `Layout::Stacked` (label under it, in the panel) or `Layout::Inline`
    (label before it, in the footer).
  - `readout(id, label, scale)`: a footer `Readout` ("Detector Peak") that
    turns like a knob, or steps on a click for a choice. `setLabel({})` for a
    value that follows a switch with the name.
  - `readout(label)`: an unbound `Readout` for a meter or a state; set it with
    `setText()`.
  - `bind(id, show)`: anything else that follows a parameter (a marker
    line, a curve). `show` runs at once and after every change.
- **Scales** (`core/ParameterScale.hpp`): `Log` for Hz, ms, s and ratios
  above zero, `Power` for times from zero, `Linear` otherwise; pass a scale
  to override. Values read as the designs show them (`formatParameterValue()`:
  "−18.0 dB", "4.0 : 1", "120 ms", "6.00 kHz", "100 %"); a knob or readout can
  take its own `setFormatter()`.
- **Meters**: call `startMetering()` and override `meter(const Meter&)`. It
  runs about 30 times a second while the window shows, with the plugin's
  scope tap on only then:
  - `running`: audio went through since the last reading;
  - `outputDb`: the peak of what the plugin put out (left and right averaged;
    the dry sound while bypassed);
  - `reductionDb`: `AudioProcessor::gainReductionDb()`, 0 while bypassed,
    `kNoLevel` for a plugin without one.

  The engine has no input level meter. A dynamics editor works the input out
  with `inputFromOutputDb(output, reduction, added gain)` when that is true
  (no dry mix, not listening to the key) and pushes `kNoLevel` otherwise.
  Never draw made-up data: no audio means gaps (`kNoLevel`).
- **Readouts**: `instance().processor().readouts()` lists the live values a
  built-in publishes (vloom-dsp `HasReadouts`, engine ADR 0019); look one
  up by id once and poll `readoutValue(index)` on a timer (lock-free).
- **Header**: `addHeaderControl()` puts a control before A / B (the
  analyzer's Freeze).
- **Look**: `editor_style` (`EditorStyle.hpp`) has the designs' colours,
  taken from the theme (the accent is the theme's), the fonts and the sizes.
  Shared displays: `LevelHistoryView` (scrolling levels with a draggable
  marker line), `TransferCurve` (an inset curve), `MeterBars` (upright
  meters). `ControlPanel::addDivider()` / `addSpacing()` group controls.

## Adding an editor

1. `src/ui/plugins/builtin/<Name>Editor.{hpp,cpp}`: a class derived from
   `BuiltinEditorWindow` with the constructor
   `(std::shared_ptr<vloom::PluginInstance>, const QString& title, double sampleRate, QWidget* parent)`.
   Use the dsp header's ids (`vloom::dsp::Chorus::kRate`), never numbers.
2. In the constructor:
   ```cpp
   ChorusEditor::ChorusEditor(std::shared_ptr<vloom::PluginInstance> instance, const QString& title,
       double sampleRate, QWidget* parent)
       : BuiltinEditorWindow(std::move(instance), title, sampleRate,
             {{tr("Wide"), {{Chorus::kWidth, 100.0}, {Chorus::kMix, 40.0}}}}, parent)
   {
       display_ = new ChorusDisplay(...);       // your picture
       setDisplay(display_);
       panel()->add(knob(Chorus::kRate));
       panel()->add(knob(Chorus::kDepth));
       footer()->addLeft(readout(Chorus::kVoices, tr("Voices")));
       bind(Chorus::kRate, [this](double) { display_->update(); });
       startMetering();                          // only if it shows levels
   }
   ```
3. Add one line to `kEditors` in `BuiltinEditors.cpp`:
   `Entry{"vloom.chorus", &make<ChorusEditor>},`. `MainWindow` then opens it
   instead of the generic window, with bypass and edited() wired up.
4. Add the files to `vloomdaw_ui` in `src/CMakeLists.txt`.
5. Put pure logic (curve maths, mappings) in `src/core/` with tests in
   `tests/core/`; the UI is not unit tested. To look at the window, render it
   offscreen (`QT_QPA_PLATFORM=offscreen`, `QWidget::grab()`); never open an
   audio device.

Where the design and the dsp parameters differ, the parameters win: map the
design's controls to real ids, put extra parameters in the panel or footer
in the same style, and note the difference in the class comment.

## Phase 1 editors

- `CompressorEditor` (`vloom.compressor`): levels with the draggable
  threshold, transfer-curve inset, In / GR / Out meters; threshold, ratio,
  knee, attack, release, makeup, mix, auto makeup; footer: detector low cut
  (the sidechain filter) and its frequency, Listen, detector (Peak/RMS),
  stereo link, gain reduction.
- `LimiterEditor` (`vloom.limiter`): levels with the draggable ceiling;
  gain (input gain), ceiling, release, lookahead, true peak; footer: latency
  (`PluginInstance::latencyFrames()`), stereo link, gain reduction.
- `GateEditor` (`vloom.gate`): the input in grey and what the gate lets
  through in the accent, the draggable threshold; threshold, range,
  attack, hold, release, hysteresis; footer: detector low cut and its
  frequency, Listen, open or closed (from the gain reduction: the engine
  does not report `dsp::Gate::isOpen()`).

## Phase 2: tone editors

Shared pieces they added (usable by any editor):

- `ChoiceButtons`: a captioned row of joined buttons for a choice parameter
  in the panel (bind it with `bind()` / `change()`).
- `DisplayHelpers.hpp` (`display::`): the editor's settings by id
  (`settingsOf()`), a choice's labels, the 20 Hz .. 20 kHz log axis with its
  grid, and a wave path.
- `core/EffectPreview.hpp`: pictures worked out with the effects' own code,
  run offline on a test signal (a sine, hits, a steady stereo pair) or with
  their maths (`dsp::Filter::magnitudeDb()`, the de-esser's biquads). Tested
  in `tests/core/EffectPreviewTests.cpp`.

The editors (each class comment lists its differences from the design):

- `SaturatorEditor` (`vloom.saturator`): the shaping curve with a drive
  handle, and a sine before and after; character (Soft, Tube, Tape, Hard),
  drive, tone, output, mix; footer: oversampling, latency, output peak.
- `UtilityEditor` (`vloom.utility`): gain per side, the stereo field of a
  fan of panned sounds, balance, width and polarity; gain, pan, width, Mono,
  Invert L / R; footer: Swap, DC block, output peak.
- `FilterEditor` (`vloom.filter`, 640 x 420): the response with a cutoff /
  resonance handle; type, frequency, resonance, drive; footer: slope, output
  peak.
- `BitcrusherEditor` (`vloom.bitcrusher`): a sine before and after, the
  quantizer's levels; bits, downsample, jitter, drive, mix, anti-alias;
  footer: dither, rate, output, peak.
- `DeEsserEditor` (`vloom.deesser`): the output spectrum, the estimated input,
  the detection band with the reduction, a frequency / threshold handle;
  frequency, threshold, range, Listen; footer: mode, reduction.
- `TransientEditor` (`vloom.transient`): four hits before and after the
  shaper; attack, sustain, output, mix; footer: speed, output peak.

## Time and modulation editors

The shell also knows the song's tempo: `tempo()` (120 until set), and
`tempoChanged()` when `MainWindow` passes a new one (`setTempo()`, at once
and on every refresh tick). An editor that shows a time or a rate in notes
uses it. A "Note" parameter reads its names from the plugin ("1/8 D").

`TimeEffectParts.hpp` has what these editors share: `HandleDisplay` (a
display with one round handle to drag), `ResponseDisplay` (a frequency
response, filled, with its sweep dashed and a handle on the 0 dB line),
`SegmentedChoice` (Tremolo | Pan), `OutputLevel` (the footer's held output
peak) and a time axis. The maths is in `core/TimeEffects` and its tests
check it against the effects themselves.

- `DelayEditor` (`vloom.delay`): a centred hit and its echoes, left above
  the line and right below, in dB, the feedback's decay dashed; the handle
  on the second echo drags the time (sideways; to the nearest note while
  synced) and the feedback (up and down). Sync, Ping-pong, time (or note),
  feedback, low cut, high cut, width, mix; footer: tempo, the delay of each
  side, stereo offset, output.
- `ReverbEditor` (`vloom.reverb`): the dry hit, the pre-delay, the first
  trips round the lines and the tail falling 60 dB in the decay time, to
  scale, with the highs (8 kHz, shortened by damping) dashed; the handle on
  the tail drags the decay. Size, decay, pre-delay, diffusion, damping,
  width, mix, Freeze; footer: low cut, high cut, output.
- `ChorusEditor` (`vloom.chorus`): each voice's delay over a few LFO cycles.
  Rate (or note), depth, delay, feedback, width, mix; footer: voices,
  spread, LFO sync, output.
- `FlangerEditor` (`vloom.flanger`): the comb up to 8 kHz at Delay, and at
  the end of the sweep dashed; the handle at the lowest notch drags the
  delay. Rate (or note), depth, delay, feedback, stereo phase, mix; footer:
  LFO sync, output.
- `PhaserEditor` (`vloom.phaser`): the notches at the centre, and at both
  ends of the sweep dashed; the handle drags the centre. Rate (or note),
  depth, centre, feedback, stereo phase, mix; footer: stages, LFO sync,
  output.
- `TremoloEditor` (`vloom.tremolo`): the gain of each side over a few LFO
  cycles. Mode (Tremolo | Pan), rate (or note), depth, shape, stereo phase
  (dimmed for Pan); footer: Sync, output.

## Instruments

The shell serves the built-in instruments too (`MainWindow` wires the power
button to the channel's instrument, and it reads "Instrument on/off").
`SynthEditorWindow` keeps its own window.

- `SamplerEditor` (`vloom.sampler`): the sample's waveform with Start and the
  loop (Loop Start, Loop End) as markers to drag, the sample's end, the time
  along it and the amp envelope over it at the root key. The panel has three
  pages: Amp (the design's row), Filter and Play. Footer: the sample (click
  to replace it, × to remove it), root key, key range, loop mode, voices.
- `DrumSamplerEditor` (`vloom.drums`): the 16 pads (key and sample name;
  click to choose, drop files to load), and the chosen pad's waveform with
  its decay point to drag, knobs and readouts, made the first time a pad is
  chosen. The floating panel is hidden. Footer: the pad's file, pads loaded,
  master gain.

Samples load through `ui/plugins/SampleFiles.hpp` (as in
`ParameterEditorWindow`): `AudioProcessor::loadSample()`, with the host's
resolver decoding the other types and refusing network paths. The engine does
not hand its sample data to the UI, so `SampleWaveforms`
(`SampleParts.hpp`) reads the file each slot reports again on a worker
thread (the same WAV the engine plays, never a network path) and keeps its
peaks. The pictures' maths (peaks, the dsp's envelope, the time line) is in
`core/SampleView.hpp`, tested in `tests/core/SampleViewTests.cpp`.

## Analysis

- `TunerEditor` (`vloom.tuner`): the note with its octave (scientific
  pitch, A4 = Reference), the frequency, a ±50 cent meter with the in-tune
  zone (±5 cents, `kInTuneCents`) and the cents off, "—" when there is no
  pitch or the tuner is bypassed. It polls the readouts (`frequency_hz`,
  `note`, `cents`, `has_pitch`, `level_db`) every 33 ms while it shows; the
  needle is smoothed (`NeedleSmoother`, 80 ms) and jumps on a new note.
  Reference, Mute output; footer: Input (a choice) and the level. The maths
  is in `core/TunerReading.hpp`, tested in `tests/core/TunerReadingTests.cpp`.
  Animated: the needle eases between readings with a short glowing trail,
  the zone blooms once the pitch holds in tune for 0.25 s, the note glows
  with the input level and pulses on a new note (its `level_db` counts as
  sound: `inputPeakDb()`).
- `AnalyzerEditor` (`vloom.analyzer`): the spectrum, 20 Hz .. 20 kHz on the
  log axis, filled, with a dB grid (0 dB at the top, Range tall), the
  peak-hold line (double-click to clear it) and a cursor readout. Freeze in
  the header; Speed, Tilt, Range, Peak hold; footer: Resolution, Channel
  and the processor's `peak_db`. The processor passes audio through: the
  FFT is done here from `PluginInstance::readScopeStereo()` by
  `core/AnalyzerSpectrum` (vloom-dsp's `Fft`, Hann window, a one-pole power
  average over `Analyzer::averagingSeconds()`, `Analyzer::tiltDb()`), tested
  in `tests/core/AnalyzerSpectrumTests.cpp`. The scope keeps 8192 frames,
  so Resolution 16384 is analysed at 8192. Animated: each analysis leaves
  an afterimage that sinks and fades as mist under the live line (longer
  at a slow Speed), the held line sparkles where a new peak is set, the
  loudest point and the cursor's dot glow with the level; Freeze holds it
  all still.

## Animation

The displays move with the sound: a reverb's mist, a compressor's breathing
gain reduction. Everything they show comes from the real signal and the
parameters; with no sound they are calm and still. The layer is in
`src/ui/plugins/builtin/Animation.hpp` (the clock, the setting, the
interface), `AnimationPaint.hpp` (drawing helpers) and
`src/core/AudioFeatures.hpp` (what the sound is doing, unit tested in
`tests/core/AudioFeaturesTests.cpp`). `ReverbEditor` and `CompressorEditor`
(`LevelHistoryView`, `TransferCurve`) are the worked examples.

### How it runs

- **One clock**: `FrameClock`, a 60 Hz `QTimer` shared by every editor. It
  runs only while some editor wants frames and stops when none does.
- **When an editor gets frames**: once it has an animated display, while
  the window is shown, not minimised, the setting is on, and sound ran
  lately. With no sound for `AudioFeatures::kSettleSeconds` (2 s) and no
  display still moving (or one more second, whatever they say), the editor
  leaves the clock; `rest()` is called on each display. The 30 Hz meter
  timer notices sound again and brings the frames back. No sound, no CPU.
- **Each frame** the shell reads what the plugin put out since the last
  frame (the scope tap: left and right averaged), updates
  `motion::AudioFeatures` with the real frame time `dt`, and calls
  `advance(audio, dt)` on each animated display.
- **The setting**: Settings → General → "Animate plugin displays"
  (`AppSettings::animatePluginDisplays`, on by default), and the system's
  reduced motion (Windows "Animation effects" off, macOS "Reduce motion";
  Qt has no hint for it). Either one off: `AnimationSettings::enabled()` is
  false, every display gets `setAnimationOn(false)` and draws its static
  picture. `VLOOMDAW_IGNORE_REDUCED_MOTION=1` ignores the system's
  preference (renders and measurements on a machine with it set).
- **What the plugin hears**: an editor whose plugin reports its input
  level overrides `inputPeakDb()`; above silence it counts as sound (it
  wakes the clock and keeps `settled()` false) although the output is
  silent. The tuner does (it mutes its output by default).
- **Later**: `motion::AudioInput` has room for what the engine may report
  (left and right on their own, the input level). Fill them in
  `BuiltinEditorWindow::frame()` when the engine has them; the displays
  keep the same interface.

### Making a display audio-reactive

1. Derive the display from `Animated` too, and hand it to the shell:
   ```cpp
   class ChorusDisplay : public QWidget, public Animated {
   public:
       bool advance(const motion::AudioFeatures& audio, double dt) override;
       void rest() override;                       // drop the motion, update()
       void animationSwitched(bool) override { rest(); }
   protected:
       void paintEvent(QPaintEvent*) override;
   };

   // In the editor's constructor, after setDisplay():
   animate(display_);   // starts the metering too
   ```
2. In `advance()` move what you draw by `dt` (never by "one frame"), call
   `update()` (or `update(rect)` for a part) and return whether anything
   still moves. Read the sound from `audio`:
   - `level()` (peak, fast, 0 .. 1 over −60 .. 0 dB) for brightness;
     `envelope()` (RMS, slower) for a size or a spread;
   - `onset()` / `onsetStrength()` for a hit; `pulse()` decays after one;
   - `waveform()` (the newest 2048 samples) for a ribbon or a trace;
   - `reductionDb()` (smoothed gain reduction);
   - `bands()` once the editor called `enableSpectrum(24)` (not done by
     default: an FFT a frame);
   - `liveliness()`: 1 while sound plays, easing to 0 in 2 s after it
     stops. Multiply what moves by it so the display comes to rest in time.
3. Tie the motion to the parameters: a decay time fades by
   `motion::decayFactor(dt, decaySeconds, 60.0)`, a rate moves by
   `rate * dt`, a time axis scrolls at its own px per second.
4. In `paintEvent()`:
   ```cpp
   const AnimationPaintTimer timer;          // counts the paint cost
   QPainter p(this);
   back_.draw(p, *this, stateKey(), [this](QPainter& l) { paintStatic(l); });
   if (animationOn())
       paintMotion(p);                       // the moving layer
   ```
   With `animationOn()` false draw exactly the static picture.
5. A parameter change that a display shows moving without sound (rare):
   `wakeAnimation()` gives frames until it has settled.

Rules: never draw made-up motion. Random numbers may spread particles an
onset spawned, never start anything. No sound: calm.

### Performance rules

The budget is 2 to 3 % of one core per open editor (900 × 480) while sound
plays, nothing when silent.

- Cache what follows only the parameters (grids, axes, curves, labels) in a
  `motion_paint::LayerCache`, keyed by the values it shows
  (`motion_paint::keyWith()`); size and theme colours are added for you.
- Allocate nothing per frame: `ParticlePool` is fixed, keep sample copies
  in `std::array`s, reuse buffers.
- No full-frame blur, no per-pixel CPU work per frame. Glow is a few wide
  strokes (`glowStroke()`) or a cached sprite (`glowDot()`); mist is a
  small `TrailBuffer` at a third of the resolution, smoothed up when drawn;
  noise is a precomputed texture (`mistTexture()`) scrolled.
- Never stroke wider than 1 px or fill antialiased paths every frame:
  Qt's raster engine draws a 1 px antialiased line on a fast path (a
  225-point trace: 0.07 ms) and anything wider through the stroker, 15
  times slower (1.1 ms; a three-layer wide glow: 6 ms). Fill areas
  without antialiasing and put a 1 px line on their edge.
- Repaint only what moves: `update(rect)` when the motion stays in part of
  the display; the cached layers are then blitted only there.
- Measure: `FrameClock::instance().stats()` (frames, paints and their
  time), or run with `VLOOMDAW_ANIMATION_STATS=1` for an overlay in the
  window's corner (fps, paint ms, frame ms, % of a core).

### Recipes (`AnimationPaint.hpp`, namespace `motion_paint`)

- **Glow**: `glowStroke(p, path, colour, intensity, radius)` draws the
  path 1 px wide with glow sprites stamped along it (keep it short: a
  trace's newest stretch). `glowLine(p, line, colour, intensity, radius)`
  for a straight edge or marker of any length (a gradient band).
  `glowDot(p, centre, radius, colour, intensity)` for one sprite; a
  `GlowBatch` for many (sparks): the painter is set up once, each `dot()`
  blits a sprite cached by colour and size.
- **Particles**: `motion::ParticlePool<State, N>`; `spawn(state, life)` on
  an onset (it reuses the oldest when full), `advance(dt, move)` in
  `advance()`, `forEach()` in the paint, `progress()` (0 .. 1) to fade.
- **Trails and afterimages**: a `TrailBuffer` over part of the display.
  Each frame `advance(keep, motion, soften)` (`keep` from the decay,
  `motion` a `QTransform` in display coordinates: a step right for time
  passing, a scale about a line for spreading), then `ink()` the new
  content, and `paint()` it. A slow fade is saved up and applied in 4 %
  steps, so 8-bit rounding neither loses it nor leaves a ghost.
- **Mist**: `TrailBuffer::paint(p, opacity, mist, drift, fadeTop)`:
  `mist` breaks the trail up with the scrolled noise texture, `drift`
  scrolls it, `fadeTop` darkens and fades its top (the reverb's damping).
- **Waveform ribbon**: `paintWaveformRibbon(p, samples, rect, columns,
  fill, edge, glow)`, the band of a waveform on a log scale about a centre
  line, as a bar a column and two 1 px edges; draw it, or call it inside
  `ink()` so it leaves mist.
- **Smoothing**: `motion::approach(current, target, dt, tau)`,
  `EnvelopeFollower` (attack and release), `OnsetDetector`,
  `smoothingFactor()`: all by `dt`, the same at any frame rate.
