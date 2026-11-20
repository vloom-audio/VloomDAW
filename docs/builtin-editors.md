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
