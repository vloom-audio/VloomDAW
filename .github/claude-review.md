# Review guide for Claude (VloomDAW)

VloomDAW is a Qt 6 desktop DAW (C++20) built on the separate, real-time
`vloom-engine`. Read `docs/architecture.md` and `docs/ui-guidelines.md` when a
change touches their area.

## What matters here

1. **Layering.** `core/` and `audio/` must not include Qt. `ui/` widgets are
   views: they emit signals and have setters; logic that can be tested without
   a window belongs in `core/` with a test. Flag business logic added to widgets.
2. **One source of truth.** The `Project` and `MixerModel` hold the state. A
   widget that keeps its own copy (names, flags, indices) drifts out of sync
   after add / remove / move / load. Check that indices are updated or rebuilt
   when tracks, channels, inserts or patterns are added, removed or moved.
3. **Engine use.** The UI never blocks on or touches the audio thread. Engine
   state is published as immutable snapshots (`ChannelRackAudio`,
   `MixerAudio`); samples load off the audio thread. Engine control calls return
   `false` when the command queue is full: check whether that can lose a
   transport command. Keep each channel's `vloom::ChannelVoices` across syncs.
4. **Input from outside.** Dropped files, browser paths, project files and
   plugin metadata are untrusted: check paths (UTF-8, traversal), sizes, and
   error handling (no exceptions escaping into Qt event handlers).
5. **Qt correctness.** Object ownership and lifetime (parents, `deleteLater`,
   lambdas capturing `this` in connections that outlive it), signals emitted
   while a model is mid-update, re-entrancy from `exec()`-based menus and
   dialogs, high-DPI and theme colours (use `Theme::colors()`, no hard-coded
   colours), keyboard access.
6. **Edge cases.** Empty project, one track (cannot delete the last), maximum
   counts (`kMaxPlaylistTracks`, `kMaxInserts`, `kMaxSteps`), very long names
   (`cleanName`), zoom extremes, the playhead past the song end.
7. **Tests and docs.** New logic in `core/` needs a test in `tests/`; user-
   visible changes need `CHANGELOG.md` (and `docs/features.md` for features).

## Conventions (only mention real violations)

- Commits: `<gitmoji> <type>(<scope>): <subject>` (checked by `scripts/check_commit_messages.sh` in CI).
- clang-format is enforced by CI: do not comment on formatting.
- Code, comments and review comments are in English.

## How to report

- Only verified problems, most severe first. For each: the file and line, what
  goes wrong and when (a concrete scenario), and a short suggested fix.
- Say "Must fix" or "Should fix". Skip style nits, speculation and praise.
- If the PR is fine, say so in one line.
