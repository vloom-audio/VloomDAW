# UI guidelines

## Code

- One widget per file pair: `TransportBar.hpp` / `TransportBar.cpp`, class in `namespace vloomdaw`.
- Reusable widgets get a folder each, named after the class:
  `src/ui/widgets/TransportBar/TransportBar.{hpp,cpp}`. Big panels go in a
  feature folder instead (`src/ui/channelrack/`).
- Widgets expose **signals** for user intent (`playToggled`, `tempoChanged`) and
  **setters / slots** for display (`setPosition`, `setPlaying`). No engine calls
  inside widgets.
- Use the new `connect(sender, &Class::signal, receiver, …)` syntax only.
- Every user-visible string goes through `tr()` so it can be translated.
- Prefer layouts over fixed sizes; the UI must work from 1280×720 up to 4K and
  with system font scaling.
- Never update the UI faster than ~60 Hz; poll engine state from one `QTimer`.
- Never do slow work (file loading, plugin scanning) on the UI thread — use a
  background thread and report back with a queued signal.

## Interaction

- Every transport action has a keyboard shortcut and a tooltip mentioning it.
- Follow FL Studio conventions where users expect them (Space = play/stop,
  right-click = delete in step grids), and platform conventions otherwise
  (Cmd on macOS, Ctrl elsewhere). Use `QKeySequence` standard keys.
- Destructive actions must be undoable rather than confirmed with dialogs.

## Visual

- Dark theme is the default. Every colour lives in `ui/Theme` (palette +
  style sheet); widgets never hard-code colours. To style a widget, give it an
  object name (`setObjectName("positionDisplay")`) and add a rule for that name
  in `Theme.cpp`.
- Icons: SVG only, in `resources/icons/`, listed in `qt_add_resources` in
  `src/CMakeLists.txt` and loaded as `QIcon(":/icons/<name>.svg")`. Draw them on
  a 24×24 view box in the theme's text colour, or take one from
  [Lucide](https://lucide.dev) (ISC) and recolour it — record its origin in
  `resources/icons/README.md`.
- Tool tips name the shortcut: "Play / stop (Space)".
