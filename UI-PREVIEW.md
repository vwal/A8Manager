# A8Manager 3.0.0

For day-to-day use, see the [User Guide](USER-GUIDE.md). This document records
the workspace changes and developer/testing details.

This independently developed version started from upstream `82397b9` (including
the accepted fixes). Work lives on `version-3.0.0` in `my-new-version`; the sibling
`main` checkout is unchanged. JUCE is now pinned to the 9.0.2 release; oolib is
unchanged.

## What's different

- **Consistent stereo-pair editing:** replacing a sample from either channel
  updates both assignments, while preserving the controlling left zone's settings
  and clamping its markers to the new file. Zone selection follows across the
  pair. Right-channel Pan and its CV controls are independently editable and
  visually undimmed. Channel Tools > Default resets both partners' channel
  settings and unlinks them without deleting samples or zone data; the right-side
  Tools menu exposes only that safe reset action.
  Channel Revert also now restores the matching saved channel's settings rather
  than mistakenly reading the preset's first channel.
- **Jump to a new root:** Options > Select Root Folder opens a
  native directory chooser. Browse anywhere, then confirm to scan only that
  destination. Cancel leaves the current root unchanged. On macOS, press
  Command-Shift-G in the chooser to type or paste a path. Unsaved preset edits
  require confirmation before switching. The current path remains at the far
  left above the browser; the existing local folder navigation is unchanged.
- **Readable, scalable workspace:** the UI size selector offers 100%, 125%, 150%,
  175%, and 200%, scaling fonts, controls, and hit targets together. The choice is
  saved. The channel editor fills its available space instead of remaining a
  fixed rectangle. Small windows scroll instead of clipping essential controls;
  splitter limits preserve the minimum useful editor size.
- **Direct value dragging:** drag a numeric field vertically without a modifier.
  Existing range-aware acceleration and validation are retained. Hold Shift for
  unaccelerated fine steps (16 logical pixels per increment, four times slower
  than the normal drag's minimum sensitivity). Fields select their contents on
  focus; type to replace and press Return, or move focus, to commit. Horizontal
  drags select text. Right-click menus remain
  available. Command-wheel on macOS / Ctrl-wheel elsewhere adjusts values;
  add Shift for one minimum increment per scroll event, including macOS's
  Shift-scroll horizontal-axis mapping. Ordinary scrolling no longer
  accidentally changes a numeric value.
- **Waveform editing:** a wider display, +/− buttons, a clickable zoom percentage
  (reset horizontal fit and vertical 100%), and a horizontal scrollbar. Wheel
  zoom, plain drag-to-pan, right-drag zoom, and double-click-to-fit remain.
  Sample start/end markers are red/blue; loop start/end are amber/pink, matching
  labels and colour keys in the zone panel. Always-visible, collision-aware marker
  overlays show grouped frame counts, source timestamps and pitch-adjusted region
  lengths. The active Sample/Loop selection follows marker editing; its surrounding
  area is dimmed. The Sample End → later Loop Start bridge always has translucent
  diagonal gray stripes, including when No Loop is saved. With hardware Loop or
  Loop/Release enabled the striped extent continues to a later Loop End. This hint
  is independent of audition selection; it does not emulate gate/reverse behavior.
  Option/Alt-drag inside a region or on its handle moves that pair
  without resizing, with Shift for ten-times-finer travel. In overlapping regions
  the selected pair wins; handles identify their own pair. Outside both regions
  the modifier drag does nothing. This replaces the old movement-mode selector.
  Fractional lengths, both Length/End modes and file-edge clamping are retained.
  The expand arrow overlays the channel controls while leaving zones accessible;
  the same component stays expanded across zone switches until X/Escape closes it.
  Gear and right-click menus offer top-level Reset/Fit Sample/Fit Loop and Jump to Marker (keys
  1–4 with waveform focus), per-marker left/right zero-crossing nudges, and a
  separate Match Opposite Boundary command. Matching searches within ±50 ms at
  the source rate for a smaller first/last-frame amplitude difference, including
  nonzero joins, on the displayed stereo side. It keeps the opposite edge fixed
  even in Loop Length mode, favours the nearest equally good match, and makes no
  edit when no improvement exists. It does not guarantee matching slopes or a
  click-free stereo join. The
  context menu additionally offers top-level Set Marker Here. Zoom, Jump and Set
  sections have headings; zero nudges and boundary matching remain submenus.
  Native-menu callbacks reject
  stale zone/sample selections. An app-local ruler adds grouped frame counts
  without modifying the pinned oolib submodule.
  File/sample/loop lengths use source rate and zone PITCH OFFSET, excluding preview
  speed, Keep pitch, channel pitch and CV. Source-position timestamps never shift.
  Changed boundaries remain normal preset edits saved with Save.
  A yellow playhead with a small triangular cap follows ONCE/LOOP audition at
  30 Hz. It uses original sample-frame positions, including after resampling,
  and stays aligned with zoom/pan. It hides when stopped, when viewing another
  channel/zone, or when playback is outside the visible range (no auto-scrolling).
- **Clear loop-join preview:** the small display compares the selected region's
  END (amber, left) with its START (teal, right). Shared automatic visual gain
  makes quiet audio visible without changing playback volume or concealing level
  differences across the join. ONCE/LOOP buttons sit below the trace. The drawing
  also handles four-sample loops and exact file-end boundaries safely.
- **Audition speed:** the waveform toolbar has a logarithmic slider and editable
  multiplier, from 0.0625x (1/16 speed) to 4x. Double-click the slider to reset to
  1x. It works while playing ONCE or LOOP. **Keep pitch** (on by default) changes
  duration without transposing the sound; the zone's **PITCH OFFSET**, including
  fractional semitones, is applied independently and responds during playback.
  At 0.5x a sample lasts twice as long; +12 semitones then raises it an octave
  without halving that duration. At 1x with zero offset, audio bypasses the
  stretcher. Turn Keep pitch off for ordinary sampler-style varispeed, where
  both speed and zone pitch affect pitch and duration together.
  The rate stays fixed as loop points change. Preview mode/rate are shared across
  channels/zones for this session, reset to Keep pitch / 1x on relaunch, and never
  modify the preset. A stereo pair uses the auditioned master zone's offset on
  both sides. Channel-level Pitch/CV, envelopes, phase modulation and the other
  hardware processing are not simulated.
  Time stretching can smear transients or produce artifacts, especially with
  extreme speeds/transpositions or very short loops. It is an editing aid, not
  a hardware emulation: [Rossum documents normal Assimil8or playback as linking
  pitch and speed](https://www.rossum-electro.com/products/assimil8or).
- **Sequential zones:** Copy > next duplicates the sample and settings. Continue
  > next starts the next slice at the current sample end with the same duration,
  clamped to the file end, and matching loop points. Both select the next zone.
  Continuing requires at least four samples remaining. An occupied target needs
  confirmation and retains its voltage boundary. An empty target splits the
  source zone's voltage range. Zone IDs and the copy clipboard are not changed;
  zone 8 and stereo-right channels cannot copy onward.
- **Zone access voltages:** each populated tab shows its saved lower boundary
  followed by a teal, parenthesized midpoint target. Targets update after boundary
  edits, balancing, copying and deleting zones. These display-only values do not
  change the preset; narrow ranges get extra precision, and invalid or
  unrepresentable ranges show (--). Tooltips explain the range and target.
- **Visual refresh:** dark panels, higher-contrast text, flat channel tabs,
  rounded buttons, consistent accents, visible list selection, and Quick help.
  Parameter section headings are left-aligned, bold, teal, and the same font
  size as the regular parameter labels, with more space between sections.
  Stereo channel tabs consistently use CH n-L and CH n-R.
  The sample-side L/R buttons are larger, with dark text on teal when selected
  and light text on a dark background otherwise.
  This is a first visual pass, not a wholesale redesign of every dialog.

## Safety and testing

Version 3.0.0 keeps the **A8Manager** name without the **UI Preview** subtitle or
title-bar suffix. It retains the existing
`OmOhmProductions/A8Manager-UI-Preview` preferences directory so settings carry
over from the preview without changing the upstream application's preferences.
Preset/sample files are still real
files: use a copied sample folder while evaluating it. Copying or continuing a
zone edits the in-memory preset; use Save to write it to disk.

Build and run tests from this directory:

CMake 3.24+ and Git are required. The first configure downloads pinned versions
of [Signalsmith Stretch 1.4.0](https://github.com/Signalsmith-Audio/signalsmith-stretch)
and [Signalsmith Linear 0.6.4](https://github.com/Signalsmith-Audio/linear), both
MIT-licensed, into `cmake_build/_deps`. Subsequent builds use the cached checkouts
offline. Standard FetchContent source-directory overrides can supply local copies.
MIT notices are copied into the app bundle's `Contents/Resources/licenses` on
macOS, or beside the executable under `licenses` elsewhere. The spectral DSP
translation unit is optimised even in Clang/GCC Debug builds. Prefer Release for
listening with MSVC; its Debug runtime checks are retained.

Run `git submodule update --init --recursive` before configuring after a pull.
JUCE 9.0.2 fixes WAV reading when only a final chunk's padding byte is missing,
plus CoreAudio device setup issues. The software MP3 decoder remains explicitly
disabled as before the upgrade; native platform decoders are unchanged.

```sh
cmake -S . -B cmake_build -DCMAKE_BUILD_TYPE=Debug -DA8MANAGER_BUILD_TESTS=ON
cmake --build cmake_build --config Debug --target A8Manager A8ManagerRegressionTests A8ManagerInteractionTests
ctest --test-dir cmake_build -C Debug --output-on-failure
```

On macOS, launch `cmake_build/A8Manager_artefacts/Debug/A8Manager.app`.
The original parser/CV and stereo-split probes are retained. Added tests cover
slice boundaries, copied settings, short/end-of-file tails, source preservation,
and synthetic numeric mouse gestures (fine adjustment, typing, horizontal
selection, bounds, disabled fields, and scroll safety).
The root-folder regression checks that only a confirmed destination emits a
scan request, cancellation preserves the root, unsaved-edit confirmation is
respected, and invalid/unchanged destinations and expired callbacks are harmless.
The playback regression drives the real audio callback without opening a device:
it verifies resampled positions, stereo contents, loop wrapping, manual stop,
one-shot tails (including exact-block endings), invalid buffers, and message-thread
delivery of playback notifications. The callback now keeps its existing audition
lock over the buffer/range/cursor snapshot; UI notifications are published by a
message-thread timer, not the audio callback.
Variable-speed checks measure output pitch, one-shot duration, fractional cursor
positions, live speed/range changes, stereo preservation and invalid-rate
clamping. JUCE's filtered resampler uses separate read-ahead and audible cursors
so prefetching cannot prematurely stop a one-shot or move the playhead ahead.
Keep-pitch tests measure distinct stereo tones across 0.0625x–4x, independently
verify octave/fractional zone offsets, and check duration, neutral bypass, live
edits and tiny-loop wrapping. Spectral input/output latency is compensated when
starting or re-aligning playback; no full stretched file is allocated.
The loop-preview rendering test checks quiet/loud visibility, shared gain,
unchanged audio, tiny/file-end loops, silent channels and invalid inputs.
Zone-voltage tests cover midpoint math/formatting, empty and invalid ranges,
neighbouring boundaries, unchanged data and tab rendering at 100%, 150%, 200%.
Region-move tests exercise real mouse handling, fine motion, mode routing,
cancellation, fixed length at file edges, fractional/implicit/tiny loops and
valid start/end ordering during property notifications.
Waveform-workflow tests use the actual component to cover its new menus, marker
commands, duration/position distinction, contextual region moves, selection
shading, label placement, expanded zone switching and disabled editing.

The subsequent refinement pass adds a confirmed Purge action on zone tabs
(compacting paired stereo zones together, without deleting audio), time lengths
in the zone panel, red sample-start keys and a dark/teal envelope graph.
Zero-crossing nudges choose the quieter adjacent frame and account for exclusive
end boundaries. File and validator rename dialogs share validated, extension-
preserving rename handling. Audition start now restores the local Sample/Loop
choice. Stereo source preparation retains an unpaired R side, ignores unrelated
next-channel unloads and leaves only R silent if its paired sample is missing.
Source/device rate changes refresh the playback range after rebuilding audio.
New tests cover these actual UI/audio paths and temporary-file rename recovery;
zone/envelope renders are checked offscreen without disturbing the running app.

Live macOS checks use a copied fixture under the ignored `cmake_build/ui-smoke`
directory: load a 0–4000 slice, Continue to zone 2, verify 4000–8000, zoom, and
change UI size. No hardware playback or Windows/Linux GUI testing has been done.
The audition refinement was also checked in the rebuilt macOS window at 150%:
larger UI-size text, separate loop-preview transport row and speed control. Setting
the control to 0.5x left the preset unmodified; it was restored to 1x afterward.
The subsequent keep-pitch pass was checked with a stereo sample temporarily
assigned to a blank preset: selected L/R contrast, larger buttons and the Keep
pitch control were visible. The temporary assignment was discarded without
saving any preset or sample files. Listening tests on the module remain open.

Potential follow-ups, not implemented here: undo/redo for all preset edits,
search/filtering for large preset libraries, keyboard navigation between zones,
and multi-zone batch operations.

### End-to-end audit fixes (26 September 2026)

All fifteen reported findings are addressed. Sample import/conversion preserves
originals and avoids name collisions; unsupported rates above 192 kHz are rejected
instead of pretending to resample. Preset/MIDI failures retain unsaved state.
Preset lists validate slot IDs, retain row/slot identity when filtered, safely
swap files and rebind Save after moving. ZIP import validates and stages contents,
respects unsaved edits, and refuses conflicting audio instead of overwriting it.
Sample reload invalidates every borrower before changing buffers and publishes
missing/corrupt status. Stereo zone copy/continue/insert/paste/flip/explode/clear
operate on the pair, with replacement confirmations and full-slot protection.
Loop End dragging and field nudges now share the waveform's boundary semantics;
automatic folder renaming uses the 31-character limit.

Four new permanent regression groups cover audio-file safety, preset workflows,
sample-cache/field-edit behavior and paired zone edits. See
`REVIEW-2026-09-26.md` for the finding-to-test mapping and remaining validation
limits. The waveform menus now put separators before Jump/Set headings, not
under them; the first Zoom heading has no separator. WAV-marker import remains
deferred pending a representative file.
