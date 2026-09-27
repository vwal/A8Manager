# Regression probes

These are repository-hosted versions of the temporary parser/CV and stereo split
probes used while validating the PR. They reconstruct the same checks, with
additional diagnostic-type, recovery, sample-rate, and finite-sample assertions.
They compile the real application implementation, use the pinned JUCE/oolib
submodules, and need neither an Assimil8or nor external sample files. Application
logging is stubbed out. These are focused regression probes, not a full test suite
or GUI/threading stress tests.

From the project root:

```sh
git submodule update --init --recursive
cmake -S . -B cmake_build -DCMAKE_BUILD_TYPE=Debug -DA8MANAGER_BUILD_TESTS=ON
cmake --build cmake_build --config Debug --target A8ManagerRegressionTests
cmake --build cmake_build --config Debug --target A8ManagerInteractionTests
ctest --test-dir cmake_build -C Debug --output-on-failure
```

The `--config`/`-C` options also select Debug for multi-configuration generators
such as Visual Studio. Tests are disabled by default; normal application builds
are unchanged. The probes were verified on macOS; other platforms have not been
verified. They do not open an application window or audio device.

CTest registers twenty-five tests, with 60- or 90-second timeouts. Use `ctest --test-dir
cmake_build -C Debug -V` for detailed output or add `-R ParserCvRegression` /
`-R StereoSplitRegression` to select one test. Failed checks return a nonzero exit
code, including in Release builds.

- **ParserCvRegression:** unknown parameters at global, preset, channel, and zone
  scopes produce diagnostics without losing the scope of subsequent known
  parameters. The same parser is reused 100 times, including a zone-to-channel
  transition. Checks `CV A` to `0A` normalization for Zones CV, `CV B` to `0B`
  normalization in CV/amount formatting, a missing CV/amount delimiter, and error
  reset/recovery on the next parse.
- **StereoSplitRegression:** generates 16-bit and 24-bit, 48 kHz stereo WAVs with
  different left/right signals and 8,193 samples per channel, then calls
  `AudioManager::splitStereoIntoTwoMono()`. Checks both mono outputs' channel
  counts, exact lengths, bit depths, sample rates, and every sample (including the
  tails). Comparisons allow one PCM quantization step plus a 10% margin for float
  re-encoding, not arbitrary waveform differences. Generated files use unique
  temporary names and are removed on normal completion or a failed assertion.

The stereo content checks catch the original byte-count bug: copying 16-/24-bit
file-sized byte counts from a float buffer leaves part of each output channel
uncopied. Merely checking output file existence and length would miss it.

The UI Preview adds **ZoneContinuationRegression** (copied settings, adjacent
slice boundaries, file-end clamping, invalid inputs, and source preservation)
and **ValueDragRegression** (synthetic mouse events for direct dragging, fine
steps, text selection, direct entry, bounds, disabled state, and scroll safety).
Fine-gesture checks cover double, integer and 64-bit sample-position fields:
Shift-drag is slower than a normal first drag event, accumulates small movements,
reverses correctly and resets acceleration when Shift changes mid-drag.
Shift-command/Ctrl scroll nudges exactly one increment without acceleration,
including X-only macOS Shift-wheel events, two-axis input, reversed scrolling,
zero movement, bounds, and disabled controls.
The interaction test links JUCE GUI support and may require a graphical session
on Linux. It does not drive the user's mouse or keyboard.

**RootFolderRegression** uses the real current-folder component and property
notifications with temporary folders. It checks confirmed selection, unsaved-edit
approval/cancellation, unchanged/invalid/deleted destinations, duplicate pending
requests, and callbacks after destruction. It counts scanner notifications rather
than running the scanner; native chooser browsing/cancellation is not covered by
this automated test. To check it manually, use Options > Select Root Folder, browse to a
different location and cancel (the current path must stay unchanged), then repeat
and confirm (only the final destination should become the scan root).

**PlaybackRegression** drives the actual audition audio callback on a worker
thread with in-memory stereo audio. It verifies original-frame cursor positions
at integer and non-integer resampling ratios, nonzero sample/loop offsets,
multiple loop wraps, channel contents, silent tails/unused channels, output
subregion boundaries, exact-block and partial one-shot completion, manual stop,
empty/missing buffers, and message-thread-only notifications. No audio device is
opened. Use `-R PlaybackRegression` to run it alone.
It also checks speed-dependent one-shot duration, fractional cursor positions,
live speed/range changes, finite stereo output, invalid-rate clamping and
unchanged zone data. Sine-wave zero-crossing counts verify actual output pitch
at 0.0625x, 0.5x, 1x, 2x and 4x rather than just trusting the cursor telemetry.
Keep-pitch cases use different left/right sine frequencies to verify preserved
pitch at those speeds and independent +12, -12 and +3.01 semitone zone offsets
(a tolerance of two crossings over 16,384 measured frames). They also check
unprocessed neutral playback, latency-compensated one-shot duration, live
speed/pitch edits, four-frame loop wrapping, sampler-style offset duration and
unchanged preset data when toggling the preview mode. These numerical probes do
not replace listening tests on transient-rich or polyphonic samples.
Intermittent failures have been observed in the existing 0.0625x keep-pitch
case: 207 crossings versus 204.8 expected, or 167 versus 163.84 (a two-crossing
tolerance). Ten consecutive passing repeats were also observed. Signalsmith randomises phases at extreme stretch
ratios, so this appears to be sensitivity of that measurement to random phase.
The gesture fix does not change audio processing or relax that assertion.

**LoopPointsRegression** renders the actual loop-join component offscreen.
It verifies that quiet and loud signals have comparable visibility, both halves
share visual gain (preserving their level difference), and drawing does not
modify audio. It also exercises four-sample/file-end loops, silent/missing
channels, negative/overflowing ranges, missing buffers and non-finite samples.
Use `-R LoopPointsRegression` alone. Optionally set `A8MANAGER_TEST_ARTIFACTS` to
an output directory to save `loop-preview.png` for visual inspection; ordinary
test runs write no images.

**ZoneVoltageRegression** checks read-only midpoint access voltages, signed and
extra-precision formatting, neighbouring boundary changes, empty/invalid ranges,
unchanged properties and actual tab rendering at 100%, 150% and 200%. The optional
`A8MANAGER_TEST_ARTIFACTS` directory receives `zone-access-150.png`.

**RegionMoveRegression** exercises the real region translation helpers and mouse
component: fixed lengths at both file edges, moving farther than a region's length,
safe notification ordering, fractional/default/four-frame loops, unchanged other
zone settings, invalid ranges, Shift fine movement, navigation versus editing,
mouse-up/cancellation, disabled editing and marker hit routing during modifier movement.

**WaveformWorkflowRegression** uses the real waveform component and in-memory
stereo samples. It checks pitch-adjusted durations independent of audition speed,
comma grouping, sample/time labels, directional and bounded zero crossings,
context versus gear menus (top-level command order, section headings, shortcut
column and read-only guards), marker placement, all four boundary-matching actions
(including fixed fractional loop ends in Length mode and no-op/disabled guards),
Length/End behaviour, Sample/Loop
selection callbacks, Option/Alt movement inside regions and on handles, overlap
selection, outside-region rejection, zoom reset/jumps, right-click versus drag,
expanded zone switching, inactive-area dimming, disabled editing and collision-free
label placement in compact/expanded sizes. Menu checks include exact separator
placement. Loop-extension checks cover the always-visible bridge in No Loop,
Loop and Loop/Release, fractional ends, the pre-loop bridge, diagonal band pixels,
uniform off-region dimming, zoom clipping, zone/source changes and audition-selection
independence. Optional artifacts are `waveform-compact.png`, `waveform-expanded.png`
and `waveform-loop-extension.png`, plus `waveform-bridge-compact.png` and
`waveform-bridge-expanded.png` for the bridge with No Loop saved. It does not launch the user's
application or change their presets/preferences.

**EditorRefinementRegression** checks quieter-side and exact-zero nudges,
exclusive end markers, bounds and stereo-side selection. Boundary-matching checks
cover nonzero joins versus true zeros, both directions and endpoints, source-rate
search limits, nearest equal-quality matches, imperfect improvements, fractional
ends, EOF/frame zero, silence, nonfinite samples and invalid inputs. Temporary files exercise
overlong-name recovery, extension preservation, invalid names, missing sources
and collision protection. Preset fixtures check middle/last/only-zone purging,
paired right-zone alignment, default clearing and final −5 V boundaries. The real
ZoneEditor verifies per-zone audition selection and live pitch-adjusted duration
labels; the real envelope component is rendered in its new theme. Optional artifacts
are `zone-refinements.png` and `envelope-refinements.png`. Native confirmation
dialogs are not clicked automatically.

**StereoPreviewRegression** runs the actual AudioPlayer source preparation and
property callbacks without opening an audio device. It covers unpaired L/R,
paired stereo with different rates/lengths, silent missing-right tails, right
reloads, unrelated next-channel unloads, mono fallback, channel eight, device-rate
changes, implicit loop lengths and isolation of SAMPLE versus LOOP edits. Channel
eight must release its optional partner bindings without invalid-tree assertions;
changes to the old partner are ignored and switching back restores real bindings.

**AudioAuditRegression** checks real shared-cache reloads, invalidation before
buffer resizing, shorter stereo-to-mono replacement, missing/corrupt files and
recovery. Actual AudioPlayer/WaveformDisplay consumers must see the new buffers
and lengths. It drives the real LOOP END drag callback and parameter-field zero
nudges, including exclusive end coordinates and Length/End-mode compensation.

**AudioFileSafetyRegression** uses disposable generated WAV/AIFF files and the
actual EditManager/shared conversion service. It checks original preservation,
internal float WAV conversion, unique bounded collision names, failed/rejected
imports, stereo assignment without commandeering occupied independent channels,
mono assignment to an existing pair, recoverable conversion failure and folder
versus file auto-name limits. Direct RIFF fixtures cover JUCE 9.0.2's missing-final-
padding fix for 8-bit and 24-bit PCM and cue-label metadata: decoded samples,
lengths and imported metadata are checked, while genuinely truncated data must
still be rejected without changing the source or publishing an output. No real
samples are used. CV fixtures also verify purpose-tag preservation through
PCM copying, float conversion, stereo split/mix and legacy-recipe imports,
including unchanged original files and decoded sample contents.
AIFF instrument-loop fields must not be reinterpreted as WAV loop metadata
when splitting or mixing channels.

**StereoAssignmentRegression** exercises actual EditManager assignment from both
sides of an occupied pair, shorter stereo replacements, mono duplication, batch
appends, CV boundaries, left-side range/setting preservation and stale right-side
occupancy. It checks channel seven/eight pairs, Link/Cycle controlling modes,
independent neighbor protection and no-op failure for invalid pairs, gaps,
oversized batches and missing files.

**StereoChannelUiRegression** exercises the real channel editors and parent
callbacks: paired zone selection, independent right-channel pan/CV controls,
safe right-channel Tools access and pair-aware Default while preserving zone
trees and channel IDs. It covers collapsing a previously expanded waveform when
its channel becomes Stereo Right, stale/reset callbacks after edits/destruction,
and Channel Revert restoring the correct saved channel. Optional visual artifacts
(`stereo-right-pan-150.png`) are written only when
`A8MANAGER_TEST_ARTIFACTS` is supplied. The tests use an in-memory preset, not
the user's preferences or audio device. They render offscreen; native-window
visibility/focus and actual listening are not automated. Debug assertion output
fails this regression even if the process returns success.

**PresetWorkflowAuditRegression** checks failed preset/MIDI saves and dirty-state
retention, valid filename slots, filtered row/slot mapping, move/save rebinding
helpers, transactional swaps (including injected installation failure), target
occupancy for Paste, dirty guards and staged ZIP imports. ZIP fixtures include
same/different-content collisions, unsafe paths and malformed/ambiguous presets.

**PairedZoneEditRegression** exercises the production pair-edit helper for
Copy/Continue, Insert, full/settings-only Paste, Flip, Explode and Clear. It checks
distinct L/R files and selectors, slot IDs, voltage synchronization, refusal to
drop a full final slot, incomplete pairs and final slice remainders. Confirmation
dialogs themselves are not automated.

## Waveform workspace

The waveform-design checks compile the production renderer, exporter and GUI.
They use generated data and disposable directories; no real samples, preferences,
audio devices or hardware are used.

- **WaveformDesignRegression** checks all audio shapes, harmonic filtering after
  drive/fold, phase/gain, layer metadata separation, deterministic CV/random
  curves, preserved DC/unipolar offsets, clipping and boundary statistics,
  envelope timing, drawn interpolation, validation and recipe round-trips.
- **WaveformDesignExportRegression** exports real PCM24 WAVs and parses the
  generated preset. It verifies every decoded sample against the renderer,
  frame counts/rates, Master/Link voice assignments, pan/detune, mix headroom,
  playback choices, full default trees, safe names, file/folder collisions,
  invalid inputs, cleanup and preservation of shared defaults. CV/audio purpose
  tags survive read-back, copies and renames; bounded legacy-recipe checks reject
  malformed/mismatched inputs, invalid UTF-8 and excessively nested/trailing JSON,
  and avoid filename/DC-content guesses. CV presets
  round-trip numeric MixLevel -90 while audio/bank mix headroom is unchanged;
  hardware Off interpretation remains a separate module check.
- **CvAuditionRegression** drives the actual sample cache, audio callback and
  ZoneEditor without an audio device. Tagged/legacy CV is silent, direct and
  stale requests are refused, same-path reloads update classification, and CV on
  either stereo side blocks the pair. Ordinary audio remains audible and the
  independent designer route is preserved. Optional offscreen UI rendering uses
  `A8MANAGER_TEST_ARTIFACTS` (`cv-zone-audition-disabled.png`).
- **WaveformWorkspaceRegression** drives the designer's real controls and
  background preview, including CV matching, curve editing and layer settings.
  A fake audio host checks explicit start/stop, monitor-only controls, live
  updates during continuous dragging, bank payloads, CV exclusion and workspace
  lifecycle. Compact and expanded previews are rendered without native windows;
  optional offscreen screenshots use `A8MANAGER_TEST_ARTIFACTS`.
- **WaveformAuditionRegression** checks the production monitor's generated audio
  blocks, including tuning, layered voice mixing, rate conversion, DC removal,
  fades, live updates, bounded output and invalid/CV rejection. No device opens.
- **WaveformAuditionRoutingRegression** exercises the real AudioPlayer callback
  with in-memory sources: exclusive sample/designer routing, cleared stale
  completion state, explicit restart after device changes, stopped-source
  silence, and unchanged sample audition settings.
- **WaveformDurationRegression** checks source/sample/loop duration matching,
  zone pitch, fractional loop ends and invalid/unloaded data. The existing
  StereoChannelUiRegression also checks the real selected-editor binding,
  including the stereo right channel following its master.

Native file-chooser interactions, analog voltage calibration, actual linked
hardware playback and listening quality remain manual tests. A rendered screenshot
or a passing preset-parser test is not a claim of hardware verification. Use the
[hardware checklist](../HARDWARE-TEST-CHECKLIST.md) to record module and listening
tests separately.
