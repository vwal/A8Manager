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

CTest registers 50 tests, with 60-, 90- or 120-second timeouts. Use `ctest --test-dir
cmake_build -C Debug -V` for detailed output or add `-R ParserCvRegression` /
`-R StereoSplitRegression` to select one test. Failed checks return a nonzero exit
code, including in Release builds.

Both test executables use the same required C++20 dialect as the application.
**AuditionSignalCheckRegression** exercises high-confidence DC/subaudio
heuristics and normal-audio false-positive cases, channel/range/pitch handling,
invalid data, and bounded/inconclusive analysis. **AuditionWarningRoutingRegression**
checks the central Samples transport gate, warning approval reuse, stale/STOP
rejection and silent waiting. Designer and WaveformAudition probes also check
the protected monitor signal, preflight confirmation and live-edit interception.
No test establishes physical speaker safety.

**RawCycleImportRegression** covers bounded PCM/float WAV decoding, stereo
choices, imported shaping, periodic resampling, DC removal and level retention,
embedded recipe round-trips, malformed-input rejection and CV/test safeguards.
**AutoLoopUiRegression** covers external-file import into the current preset,
stale chooser rejection, default-off and deferred sample-only audition, STOP,
stereo readiness and CV guards. **WaveformWorkspaceRegression** also exercises
raw import confirmation, cancellation, stale-context/source rejection and recall.
It also checks explicit Audio Cycle → Layer Bank conversion: all source shaping
and imported frames survive, the original cycle and canceled bank are retained,
audition stops, destination channels refresh, stale/destroyed callbacks are inert,
and the converted imported bank exports and recalls as seven separate voices.

**HardwareTestOutputRegression** checks the actual isolated test-package exporter:
built-in audio/CV samples and reference timing, PCM24 readback, purpose tags,
parsed preset routing and Mix Off, preserved current designs, input rejection and
non-overwriting publication. **HardwareTestOutputUiRegression** checks explicit
safety acknowledgement, settings snapshots, busy controls and export-only UI.
WaveformWorkspaceRegression also verifies that entering this workflow stops
computer audition without assigning a preset or modifying the current design.
These software tests do not establish actual output volts, hardware channel
synchronization, or the interpretation of Mix Off on a particular module.

EditorRefinementRegression checks the fixed header layout from the minimum
800-pixel window width through wide layouts, including non-overlapping Audio
Settings, UI size, Quick help, live output status, appearance and workspace controls. StereoChannelUiRegression
and EditorRefinementRegression also verify the scope-specific tool-button labels.

**PresetBankExportRegression** covers flat saved-preset collection, explicit
destination slots, sample and MIDI collisions, generated recipe recall and CV
safety, cancellation, non-overwriting publication and unchanged source folders.
**BankExportUiRegression** covers the bank dialog's slot validation, explicit
folder collection, original-preservation/open-after-export options and lifecycle.
The global header layout also checks the bank button at compact and wide sizes.

**AppearanceRegression** checks Dark/Light switching, saved-preference migration
and XML round-trip, text/marker contrast, live label and compact-combo palettes,
validation/error and disabled numeric colours, monospaced endpoint/slider fields,
unchanged numeric values and safe handling of destroyed colour-binding targets.
EditorRefinementRegression also checks real resized zone endpoint fonts and can
render dark/light zone and envelope artifacts. WaveformWorkspaceRegression can
render the light designer. Set `A8MANAGER_TEST_ARTIFACTS` to a temporary directory.

**AudioAuditRegression** checks all four mini END/START and numeric boundary-menu
routes, directional nudges/matches, >50 ms approval/cancellation, stale menu
rejection and unrestricted intentional numeric entry. **WaveformWorkflowRegression**
checks that read-only waveform navigation cannot edit markers, select audition
regions, start simulation, or commit an older pending confirmation.
**WaveformAuditionRoutingRegression** also checks live-output property
notifications and clearing a stale device name without opening an audio device;
physical hot-plug remains a manual integration check.

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
Additional pitch-parity cases combine channel Pitch and zone Pitch Offset,
check the 24/48/96/192 kHz source-rate ceilings independently of the output
device, exercise live edits and source changes, and retain the full negative
parameter range. Preset pitch changes duration in both Keep pitch modes; only
the separate audition-speed transposition is compensated.
Intermittent failures have been observed in the existing 0.0625x keep-pitch
case: 207 crossings versus 204.8 expected, or 167 versus 163.84 (a two-crossing
tolerance). Ten consecutive passing repeats were also observed. Signalsmith randomises phases at extreme stretch
ratios, so this appears to be sensitivity of that measurement to random phase.
The simulation work retains this assertion and the existing ordinary-audition path.

**SampleLoopSimulationRegression** renders the real AudioPlayer offline. Exact
stereo frame checks cover loops entirely inside the sample, shared boundaries,
and implicit full-sample loops. Out-of-sample loops are rejected. It verifies
sample-to-loop phase changes against the audible cursor rather than resampler
read-ahead, repeated wraps, output subregions, STOP, and returning to ordinary
audition. Additional cases cover equal boundaries, invalid/undersized loops,
source-rate conversion, 0.0625x–4x audition,
Keep pitch, zone transposition, live marker/rate edits, CV blocking and source
changes. Phase/selection notifications must stay off the audio thread.

**SimulationUiRegression** drives actual ZoneEditor controls and their
ChannelEditor/waveform wiring. It checks SAMPLE/ONCE STOP during the intro,
LOOP/LOOP STOP after entry, both stop buttons, the queued-phase handoff race,
immediate retrigger state, source isolation, CV/stereo/missing-audio guards,
expanded waveform continuity and unchanged preset data. Deferred display
callbacks use an injected queue in this console test, executing the production
SafePointer closures to verify stale-state and destroyed-editor safety without
relying on a native event loop. WaveformWorkflowRegression also exercises the
new toolbar/menu dispatch, disabled actions and responsive layout.

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
column and read-only guards), marker placement, all eight directional boundary-matching actions
(including fixed fractional loop ends in Length mode, opposite-side exclusion,
no-op/disabled guards and confirmation for matches farther than 50 ms),
nearby inexact crossings winning over distant exact matches, all eight
zero-crossing nudges with confirmation (including fixed-length loop movement),
accepted/canceled/stale approvals, mutual command supersession, the exact 50 ms threshold, and queued
large-file searches canceled by source unload or view destruction. Confirmation
callbacks are exercised without opening a native dialog.
It also checks Length/End behaviour, Sample/Loop
selection callbacks, Option/Alt movement inside regions and on handles, overlap
selection, outside-region rejection, zoom reset/jumps, right-click versus drag,
expanded zone switching, inactive-area dimming, disabled editing and collision-free
label placement in compact/expanded sizes. Menu checks include exact separator
placement. Post-loop tail checks cover Loop and Loop/Release, the absence of
stripes in No Loop, fractional ends, diagonal band pixels, uniform off-region
dimming, zoom clipping, zone/source changes and active sample-to-loop simulation.
Optional artifacts show the regular and expanded waveform views. It does not launch the user's
application or change their presets/preferences.

**ZoneSampleRangesRegression** exercises automatic loops, explicit loop pushing
in both Length/End modes, four-frame minima, fractional lengths, all editing
directions, and shared model/UI constraints. **PresetLoopRangeRegression** tests
hardware-facing automatic-loop values, editor save/reload defaults, external
marker edits taking precedence over editor comments, and the real preset-load
path preserving external loops and enabling their channel override, versus
resetting physically invalid/sub-four-frame loops, with a notice and dirty
baseline, never an automatic disk write. The channel option persists in an
editor-only YAML comment and defaults off. Copy/Continue full-file fit and 100% vertical zoom are covered by the
stereo-channel UI tests.

Advanced-loop checks cover independent marker motion, direct LOOP audition,
four-frame limits, stripe suppression and the distinction from static forward
simulation (external CV is not emulated). Name previews cover the six-character
Select window and the Channels display's first 10 characters plus `...` and
last two for long names, without `.wav`, as well as typing/recall and compact UI
layouts. Preset-folder tests cover new `Pnn` names and saving existing `PRnn` or
`A8 Preset nn` folders in place without renaming or nesting them.

Manual hardware loading check: if opening a `P02 - ...` folder initially shows
`001 ~empty~`, select `002` to load its `prst002.yml`. A missing preset 001 is
not an export failure and does not require renumbering or re-exporting; the
automated export tests verify the requested preset number, not the module's
initial slot selection.

**EditorRefinementRegression** checks quieter-side and exact-zero nudges,
exclusive end markers, bounds and stereo-side selection. Boundary-matching checks
cover nonzero joins versus true zeros, strict left/right searches for both endpoints,
nearest target-level crossings instead of global amplitude optimisation, unchanged
markers when the first crossing cannot improve the join, unrestricted distance
within legal bounds, quiet scaled signals, fractional
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
safe right-channel Tools access (Default and Purge) and pair-aware Default while
preserving zone trees and channel IDs. It covers keeping an expanded waveform navigable
when its channel becomes Stereo Right while blocking independent edits, stale/reset callbacks after edits/destruction,
and Channel Revert restoring the correct saved channel. Genuine exported audio
and assigned-bank fixtures verify the actual Samples TOOLS recall item's enabled
state and selected channel/zone dispatch. Ordinary, empty, missing and nonflat
sample references, missing/malformed recipes, folder changes and missing callbacks
disable recall; stale menu actions recheck and destroyed-editor callbacks are safe.
Optional visual artifacts (`stereo-right-pan-150.png`) are written only when
`A8MANAGER_TEST_ARTIFACTS` is supplied. The tests use an in-memory preset, not
the user's preferences or audio device. They render offscreen; native-window
visibility/focus and actual listening are not automated. Debug assertion output
fails this regression even if the process returns success.

**ChannelPurgeUiRegression** drives the real channel-menu action and confirmation
callback with the live EditManager and SampleManager. It verifies independent
channel and both stereo-side entry points, all-eight-zone/channel-setting reset,
Master-mode restoration, first-zone selection, immediate sample-cache unload,
audition stop and dirty-state marking. Other channels, live tree identities,
the saved baseline/preset, WAVs and recipes are preserved. Cancellation, reused
approvals, intervening edits (including change-and-revert), folder/preset switches,
stale menu actions and editor destruction cannot apply an old purge. The test
checks that CH tabs have their right-click tools callback installed; it invokes
menu/confirmation callbacks without opening or clicking a native popup/dialog.

**PresetWorkflowAuditRegression** checks failed preset/MIDI saves and dirty-state
retention, valid filename slots, filtered row/slot mapping, move/save rebinding
helpers, transactional swaps (including injected installation failure), target
occupancy for Paste, dirty guards and staged ZIP imports. ZIP fixtures include
same/different-content collisions, unsafe paths and malformed/ambiguous presets.
Save/load cases also retain later populated zones when zone 1 is empty,
settings-only channels and empty-zone parameters. They check all known min/max
parameter families, relative CV aliases, valid Unicode, and rejection of malformed
numbers, duplicate sections/keys, unknown fields, invalid encoding and embedded
NULs without replacing existing files.

**MidiSavingRegression** exercises the file reader/writer and actual MIDI dialog:
known values round-trip while unknown flat fields, comments and line endings
survive edits; only dirty slots are written. Unsupported/malformed files are
protected, failed or externally conflicted saves retain pending edits, and the
destination stays bound to the originally loaded folder. No audio device opens.

**PairedZoneEditRegression** exercises the production pair-edit helper for
Copy/Continue, Insert, full/settings-only Paste, Flip, Explode and Clear. It checks
distinct L/R files and selectors, slot IDs, voltage synchronization, refusal to
drop a full final slot, incomplete pairs and final slice remainders. Whole-channel
purge checks cover independent channels and both sides of stereo pairs, including
CH 7/8, retained tree identities, unpair-before-clear notification order and
malformed/orphan-pair rejection without partial edits. Real CV/audio fixtures
verify that purging clears the CV Mix lock despite cached CV metadata and allows
immediate audio reassignment without changing the original CV file. Confirmation
dialogs themselves are not automated.

## Waveform workspace

The waveform-design checks compile the production renderer, exporter and GUI.
They use generated data and disposable directories; no real samples, preferences,
audio devices or hardware are used.

- **WaveformDesignRegression** checks the DaisySP PolyBLEP polynomial's edge
  conventions and source alias-energy reduction against naive saw/pulse shapes,
  corrected audio phase/polarity, layer consistency and unchanged sharp CV levels.
  It also checks the 255-partial cap for 512-frame cycles, useful partials above
  300 on longer cycles, unchanged 1,024-harmonic recipe recall, and harmonic filtering after
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
  The harmonic slider uses one gentle logarithmic mapping, with approximately
  60% of travel for 1–200 and 40% for 200–1,024. Tests check its anchors,
  monotonic round-trip, exact integer entry and preserved 1–1,024 range.
  A fake audio host checks explicit start/stop, monitor-only controls, live
  updates during continuous dragging, bank payloads, CV exclusion and workspace
  lifecycle. A second path connects the real monitor engine to the workspace:
  the slider retains its −48-semitone floor and uses a source-rate-dependent
  ceiling (+72 at 48 kHz, +60 at 96 kHz), reduced by positive active-bank detune.
  Rate, mode and bank changes refresh that ceiling; forced downward
  clamps stop playback, preserve their explanation across renders and require
  an explicit Start. Expanding the range cannot start idle or paused audio.
  Pending render checks apply deliberate Transpose changes to their matching
  source, not an older source with different pitch/frequency bounds, without
  reviving a previously stopped or paused monitor. Independently,
  out-of-frequency-range playback pauses,
  Stop remains accessible after the fade, and an intentional valid Transpose
  change resumes. Gain-only edits and newly valid renders cannot restart it;
  failed Start never arms it. Shape/starting-point, mode, navigation and device
  changes cancel the pending resume. Compact and expanded previews are rendered
  without native windows;
  optional offscreen screenshots use `A8MANAGER_TEST_ARTIFACTS`.
- **WaveformAuditionRegression** checks the production monitor's generated audio
  blocks, including tuning, layered voice mixing, rate conversion, DC removal,
  fades, live updates, bounded output and invalid/CV rejection. Hardware-pitch
  checks enforce source-rate ceilings independently of device rate, reserve
  positive bank detune (rounded inward to the control's 0.01-semitone steps),
  exclude inactive voices and do not increase the ceiling for negative-only
  banks. They check real output frequency at the extended limits and explicit
  restart after rejected controls. The 192 kHz helper case does not enable that
  unsupported generator export rate. Range-pause tests
  check the inclusive 20 Hz floor, exclusive 20 kHz/device-Nyquist ceiling,
  both detuned-bank extremes, full silence after the fade, smooth quick returns
  and resumption at the intended pitch. Idle edits, failed starts, explicit
  stops, missing payloads, invalid controls and device resets cannot leave a
  latent restart; new renders or gain-only changes cannot resume a paused
  monitor. These are exact software frequency/lifecycle checks, not measurements
  of speaker response. No device opens.
- **WaveformAuditionRoutingRegression** exercises the real AudioPlayer callback
  with in-memory sources: exclusive sample/designer routing, cleared stale
  completion state, explicit restart after device changes, stopped-source
  silence, and unchanged sample audition settings. A fully faded range pause
  cannot leak an old sample; valid Transpose resumes the existing designer route.
  Sample takeover, CV/missing-payload clearing, device removal and invalid
  monitor levels cancel resume intent so later control edits cannot reclaim
  the output unexpectedly.
- **WaveformDurationRegression** checks source/sample/loop duration matching,
  combined channel/zone pitch and source-rate ceilings, fractional loop ends
  and invalid/unloaded data. The existing
  StereoChannelUiRegression also checks the real selected-editor binding,
  including the stereo right channel following its master.

Native file-chooser interactions, analog voltage calibration, actual linked
hardware playback and listening quality remain manual tests. A rendered screenshot
or a passing preset-parser test is not a claim of hardware verification. Use the
[hardware checklist](../HARDWARE-TEST-CHECKLIST.md) to record module and listening
tests separately.

## Shared waveform/preset integration

`WaveformDesignAssignmentRegression` exercises generated audio/CV/bank assignment
into detached preset snapshots, unique flat files and recipes, numbered package
exports, stereo/link protection, voltage-range splitting, content-type isolation
and ownership-checked rollback. `SharedPresetSessionRegression` checks the actual
shared edit/baseline adapter, non-001 saves, identity preservation, and stale or
changed-and-reverted folder/preset rejection. `ChannelCvSafetyRegression` checks
sample-import isolation (including stereo), Mix locks, paste validation and Save
protection. Workspace and stereo-channel UI tests cover the corresponding controls
and asynchronous confirmation/commit behavior. These are software regressions,
not hardware voltage/mix-routing measurements.

Stereo-neighbor regressions also exercise the reported CH 1/2 stereo pair →
suggested CH 3 workflow: actual asynchronous UI assignment, live SampleManager
loading and editor notifications, preservation of both stereo channels, and
Save/readback of CH 3 in the selected preset. Separate package export is checked
to leave that shared preset unchanged.

`GeneratedSidecarValidatorRegression` passes real oscillator, CV, layer-bank and
assignment outputs through the actual file validator. It checks informational
recognition of generated recipes/README files, unchanged WAV/preset memory
accounting, and continued warnings for unrelated, malformed, oversized, deeply
nested or unsupported-version recipe files.

`WaveformDesignRecallRegression` recalls actual audio/CV exports and every voice
of eight-voice banks from both standalone packages and assignments. It checks
complete settings restoration, recipe association, safe format/dimension/purpose
checks, malformed JSON/RIFF handling and explicit recipe loading for legacy files.
Workspace tests cover recall confirmation/cancellation, changed designs/presets/
recipes, callback lifetime, original target selection, whole-bank recall and
unchanged preset/files. Editor tests check exact selected channel/zone dispatch.
