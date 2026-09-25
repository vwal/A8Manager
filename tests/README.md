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

CTest registers nine tests, each with a 60-second timeout. Use `ctest --test-dir
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
mouse-up/cancellation, disabled editing and marker hit routing in Move mode.
