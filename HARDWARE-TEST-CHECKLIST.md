# A8Manager validation: software first, hardware acceptance second

This is a test plan, not a claim that every combination has been verified on
hardware. The main question is whether A8Manager generates the correct files and
presets, not whether Assimil8or's oscillator/DAC works. Sample-level software
checks are the primary test of generation; a short hardware session checks our
preset assumptions and the analog properties that a WAV cannot establish.

**Testing today? Start with the [priority test plan](#priority-test-plan).**
Its first four checks are the initial session; use the remaining detail only
when you need instructions for a particular test.

Use a separate test folder. Keep the exported recipe (`design.json` or the
assigned WAV's `.design.json`, when present), preset, WAVs and observations
together. Advanced test-output packages also include `test-manifest.json` with
their expected values and reference-marker positions.

For A8, place that complete named folder **directly under the SD-card root**:
`SD card/PR01 - koe-01/prst001.yml`, with its WAVs beside the preset. Do not copy loose
preset/WAV files to the card root or nest a preset folder inside another folder.
**Generate & Assign → designer SAVE** now creates/refreshes this self-contained
named folder while preserving working files. Copy that generated folder, not
its loose contents or a separate export per channel. Save again after the final
assignment and wait for success. Ordinary Samples saves do not refresh a
separate hardware copy.

## What needs which kind of test?

| Property | Primary check | Hardware measurement needed? |
| --- | --- | --- |
| Shape, frame count, sample rate, amplitude, phase, DC offset, harmonics, loop seam | Generated PCM samples and independent renderer regression checks | No |
| Export fidelity, file names, metadata, recipe recall, preset settings | Reopen WAVs; parse/round-trip presets and recipes | No, except interpretation of preset settings |
| Trigger/loop behavior, stereo/link routing, marker interpretation, saved-preset recall | A small set of module acceptance tests; recordings can capture results | Module needed, scope usually not needed |
| Actual CV volts, polarity and sustained DC | Individual output measured at known gain/load | Yes: DC-coupled scope or suitable calibrated meter |
| CV excluded from both stereo mix outputs | Module displays Off **and** no corresponding signal at either mix output | Yes: DC-coupled scope preferred |
| Nominal audio pitch and relative phase/timing | Software first; an optional captured audio comparison | Scope optional |

The exporter rereads every generated PCM24 sample and compares it to the rendered
buffer within quantization tolerance. This checks export integrity, not by itself
that the renderer's mathematics are correct. Separate waveform-design tests check
the shapes, PolyBLEP correction, phase, harmonic filtering, DC preservation and
timing. See [regression coverage and run commands](tests/README.md). Passing these
does not establish voltage calibration or every module behavior.

## Priority test plan

**Start with 1–4, then take a break if needed.** Those protect your work and
check the CV hazards. Continue with 5–9 for normal musical use; 10 is a final
editing check. The rest of this document is a reference, not another mandatory
to-do list. These are expected results to verify, not completed hardware tests.

Before starting, back up the SD card and use disposable test folders. Record
the app build and A8 firmware version. Disable external modulation/MIDI and
automatic triggering unless the test calls for them. Keep every tested CV
individual output and both mix outputs disconnected from speakers/headphones.
Use the [scope setup below](#essential-analog-session-rigol-mho98): DC coupling,
1 MΩ input, and the correct probe attenuation (1× for a plain BNC-to-3.5 mm lead).
Follow its grounding precautions before patching.

For each test, write **Pass / Fail / Not tested** and one observation. If 1–2
fail, stop that workflow. If 3–4 fail, do not connect the CV to speakers or other
modules; keep the failing preset and capture for investigation. A different,
isolated audio-only test can still be useful.

### First session protecting work and checking CV

1. **Saved work survives reopening — computer only.**
   In a disposable non-001 preset, keep a stereo sample on CH 1–2, assign a
   generated audio waveform to CH 3, and set PITCH +12 / PAN −0.25 on empty CH 4.
   SAVE, switch presets, then close and reopen the app and that preset. Also
   change one MIDI setup value, save, and reopen MIDI settings.
   **Pass:** the intended preset slot, CH 3 assignment, stereo pair and empty
   CH 4 settings survive; the MIDI edit survives without changing other slots
   or unrelated files. If you already have a working preset with zone 1 empty
   and a later zone populated, include it: saving must retain the later zone's
   original number and settings. That sparse case also has an automated test;
   you do not need to hand-edit a preset file to construct it.

2. **CV cannot accidentally audition — computer only.**
   With speakers/headphones disconnected, export a new CV waveform and open it
   in Samples. Try a renamed copy of the tagged WAV without its recipe too.
   **Pass:** CV audition is unavailable in both workspaces, while marker editing
   still works; its channel has Mix and Mix modulation Off. Ordinary audio
   remains auditionable. Do not use untagged external CV as a safety test: its
   purpose cannot always be detected.

3. **Ordinary CV exports stay out of both mix outputs — A8 and scope required.**
   Use an ordinary Designer CV sine export/assignment, 2 seconds, one cycle,
   modest amplitude, zero offset/pitch, Loop playback. Confirm Mix Off on A8.
   Scope its individual output plus Mix L and Mix R, comparing stopped and
   running traces. Silence other channels and disable sampling-input monitoring.
   **Pass:** the individual output carries the CV; neither mix acquires a
   repeatable CV-shaped signal above its recorded idle/noise baseline. Record
   the scope resolution; do not require mathematically exact zero volts.
   A Test output package alone is insufficient here because it has a separate
   export path that forces every used channel's mix Off.

4. **CV has the correct polarity and holds DC — A8 and scope required.**
   Export **Test output → CV levels**, **10 seconds / 10%**. Trigger A8 CH 1.
   **Pass:** its individual output shows five approximately two-second sections:
   **zero → positive → zero → negative → zero**. The nonzero plateaus hold
   steady rather than decaying toward zero. This file plays once; retrigger for
   another capture. Then try CV sine at 10 seconds: expect one bipolar cycle
   during that window. Record actual plateau voltages and extrema; **10% is not
   an assumed 1 V**. Before patching into another module, measure the range at
   the settings/gain/load you intend to use and check that module's accepted
   input range. A low-level pass does not certify full-scale voltage safety.

### Next session normal hardware operation

5. **Files load and channels route correctly — A8, listening or recording.**
   Use audio-only test presets: one Audio Cycle, a stereo sample with clearly
   different left/right content, and a two-voice Layer Bank. Monitor at low
   level through suitable attenuation, not a direct headphone connection.
   **Pass:** no missing-file errors; the intended channels/zones contain the
   intended files; stereo sides are not swapped. The bank is Master + Link,
   not Stereo Right, and one master trigger starts both voices. Hard-panning
   those voices separates them at Mix L/R while each individual output still
   carries its own voice. Exact trigger phase lock is not required.

6. **Channel pitch and zone offset agree with A8 — recording preferred, scope optional.**
   Export **Test output → Audio tone**, **2 seconds**, modest level. It contains
   a 440 Hz tone with short endpoint fades. Use its CH 1 individual output;
   the package deliberately has Mix Off. In Samples use ONCE, Audition speed
   1× and the complete sample region. Apply the same preset pitch settings on
   A8, with no external modulation and no loop. Compare captures to these
   nominal expectations (allow for measured clock/instrument accuracy):

   | Channel PITCH | Zone PITCH OFFSET | Tone frequency | Playback duration |
   | --- | --- | --- | --- |
   | 0 | 0 | 440 Hz | 2 seconds |
   | +12 | 0 | 880 Hz | 1 second |
   | +12 | −12 | 440 Hz | 2 seconds |
   | +12 | +12 | 1,760 Hz | 0.5 seconds |

   **Pass:** both A8 and Samples follow these pitch/duration ratios. At 1×,
   toggling Keep pitch must not undo the preset pitch. In Samples only, at 0.5×
   with Keep pitch on, the pitched result takes twice as long without another pitch change.
   Measure the test voice, not the reference channel's end signal: that signal
   still marks the original scheduled window. Restore pitch to zero afterward.

7. **Ordinary playback modes behave correctly — A8, listening or recording.**
   Use a recognizable 1–2-second audio sample and separate preset copies with
   the following settings. Set channel Attack/Release to zero, AutoTrigger Off,
   and sample/loop markers to the full file. Try both short and long gates.
   **Pass:**

   - **One Shot** (Play One Shot, No Loop): one complete pass regardless of gate length.
   - **Loop** (Play One Shot, Loop): repeats after the gate ends; needs a deliberate stop.
   - **Gated loop** (Play Gated, Loop/Release): repeats while the gate is high and stops on release with zero Release.

   These are the Designer's three export combinations, not every independent
   play/loop/envelope combination. A one-cycle file is too short to make the
   once-through test easy to judge.

8. **The sample really plays into the loop — A8 and app comparison, scope optional.**
   Use audio with recognizable sections, looping enabled, zero pitch and a
   held gate or One Shot play mode. Compare the app's **Trigger sample into loop
   simulation** with A8 for a loop (a) inside the sample, (b) starting inside
   but ending beyond Sample End, and (c) starting after Sample End.
   **Pass:** playback starts at Sample Start, travels forward to the loop and
   then repeats that loop only. In (c), the striped bridge is heard, not skipped.
   In the app, SAMPLE/ONCE STOP is highlighted during the initial passage and
   bridge; LOOP/LOOP STOP takes over when playback enters the loop. Either
   active STOP stops playback. Do not require the whole sample region to play
   once before an internal loop captures playback.

9. **Hardware save and reload retains the preset meaning — A8 and computer.**
   Save a populated test preset as a separate copy on A8, reload it there,
   then copy it back and open it in A8Manager. Preserve the original export.
   **Pass:** sample references, assignments, marker positions, pitch, pan and
   playback modes remain equivalent; a CV channel still displays Mix Off.
   Text formatting/default omission may differ. An unrecognized field is a
   compatibility finding: keep the file, do not delete the field just to pass.
   Hardware normalization of empty-channel templates is separate from test 1's
   requirement that A8Manager preserve work in progress.

### Final editing check

10. **Stereo edits and waveform recall preserve intent — computer first, A8 spot check.**
    On copies, replace an existing stereo sample, change zones, purge one zone,
    then purge the stereo channel pair. Separately recall a generated waveform
    recipe and re-export it.
    **Pass:** both stereo sides update/follow/clear together; unrelated content
    and source WAV files remain intact. Reopening the saved preset retains the
    result. Recipe shaping controls return, and re-export does not overwrite
    the earlier WAVs. Designer Monitor level/Transpose are audition controls,
    not missing recipe or preset values. Load one edited result on A8 to
    confirm the intended assignments.

### What can wait

Defer exhaustive waveform/harmonic measurements, all eight bank voices, exact
interchannel trigger phase, unusual loop-before-sample/equal-boundary cases,
live CV marker motion, and extreme pitch limits. Do not listen-test ultrasonic
or very low CV-like settings. The regression suite already checks generated PCM,
file fidelity and many editing/save edge cases; keep its result for your build
rather than repeating every software assertion on a scope.

Record failures simply: **test number, preset/recipe, exact settings, expected
result, actual result**, plus a capture when useful. Do not invent a universal
millivolt or timing tolerance: record the resolution/accuracy of the measurement.

### Essential analog session: RIGOL MHO98

These are settings to verify, not a requirement to change Assimil8or's factory
calibration. For accurate absolute-voltage calibration, use an instrument with
appropriate stated accuracy; a high-resolution screen is not proof of millivolt
accuracy. RIGOL specifies warm-up of more than 30 minutes for its stated specs.

- [ ] Disconnect the tested individual and mix outputs from monitors/headphones.
  Use scope **inputs**, not its waveform-generator outputs. Start at low digital
  amplitude with no external CV, unchanged channel/zone gain and no modulation.
- [ ] Select **DC coupling, 1 MΩ input** on each used scope channel. Do **not** use
  50 Ω termination. Choose the **20 MHz bandwidth limit** for these slow tests.
- [ ] A plain BNC-to-3.5 mm TS patch cable uses a **1×** probe setting. If using a
  passive probe instead, match the scope setting to its physical switch (e.g.
  **10×**). Connect the tip to signal and the ground clip to the patch sleeve.
  Scope channel grounds are common: never attach a ground clip to a signal tip
  or measure between two active outputs as if the inputs were isolated. Do not
  remove protective earth. If uncertain about the cable/pinout, stop and check.
- [ ] For initial low-level CV, try **1 V/div** with zero visible and about
  **500 ms/div**, Auto acquisition. Adjust the vertical scale to keep the whole
  waveform visible. For a constant plateau, Auto/roll is easier than waiting for
  an edge that never arrives. These are starting settings, not pass limits.
- [ ] Measure the individual output before playback, then exported constants:
  amplitude 0%, offset 0%, +10%, and -10%, each with Loop playback. Record the
  settled levels and whether they remain steady for several seconds. Digital
  +10% does **not** mean +1 V unless that scaling has actually been established.
- [ ] Play a 2-second slow CV sine/steps at a modest level; record its extrema
  and polarity. Increase only as needed to check the voltage range you intend
  to use. Measure under the intended gain/output/load before connecting another
  module. Do not infer a safe full-scale range solely from a low-level test.
- [ ] To verify isolation, use scope CH 1 for the chosen A8 individual output,
  scope CH 2 for Mix L and scope CH 3 for Mix R. Mute other channels, disable
  sampling-input monitoring and leave the fourth lead unused or as a reference.
  Compare each mix output while stopped and while the slow CV is running.
  **Pass:** the module says Off and neither mix has a repeatable component
  matching the CV above the measured baseline/noise. Record the measurement
  resolution; do not require a mathematically exact 0 V trace. If CV appears in
  either mix, stop and report before using speaker monitoring.

These MHO98 coupling, impedance, attenuation and bandwidth-limit settings are
listed in [RIGOL's official MHO98 data sheet, pp. 6-8](https://www.rigol.com/dam/global/downloads/brochures/en/data-sheet/oscilloscopes/MHO98-DataSheet.pdf?t=1760514458060).
Assimil8or's DC-coupled playback and separate individual/mix paths are documented
by [Rossum](https://www.rossum-electro.com/products/assimil8or).

### Recording-based comparisons

A recording is often more useful than a scope screenshot for audio timing,
relative phase, missing/extra sections, loop seams and routing.

- Record the tested output and a reference simultaneously through a suitable
  interface at known sample rate, with adequate input attenuation/headroom.
  Do not assume a Eurorack output is safe for every line input. Disable AGC,
  effects, normalization, noise suppression and automatic tempo stretching.
- Preserve the original capture, export recipe/preset, channel mapping and the
  actual gate/stop sequence. Trigger and gate transitions are part of the test,
  not an arbitrary choice to discard in analysis.
- Align captures for fixed latency and account for independent recorder/module
  clock rates. Estimate drift from more than one known point over the recording;
  record any resampling/alignment applied. Do not stretch away a wrong duration
  or onset and then call the result a pass.
- Compare expected event order, section lengths, frequency ratios, relative
  timing, polarity and routing. DAC reconstruction, playback interpolation,
  analog gain/noise and the recorder's ADC mean a hardware recording should
  **not** be bit-identical to the WAV. Use stated tolerances for each property.
- An ordinary AC-coupled recording cannot establish CV DC level, polarity of a
  sustained offset or absolute output volts. Only use recordings for that if the
  entire capture path is suitable, DC-coupled and voltage-calibrated; otherwise
  use the scope/meter checks above.

### Test-output packages and recorded references

**Waveform designer → Test output...** now creates a standalone hardware test
package. It is export-only: there is no test playback through the computer and
no change to the open preset or its assignments. Ordinary exports remain unchanged.

1. Choose **Current design**, **Audio tone**, **CV levels**, **CV sine**, or
   **CV positive triangle**. Built-in signals use a 1–60 second observation
   window (default 10 seconds) and 1–25% digital full-scale level (default 10%).
   Current design preserves its samples, pitch and playback mode rather than
   being rescaled or stretched to the window.
2. Disconnect tested outputs from speakers/headphones, prepare the measurement
   setup above, and check the required safety acknowledgement. Export into a
   new folder. Copy the whole package to the SD card; retain its `README.txt`
   and `test-manifest.json` beside the WAVs and preset for analysis.
3. Inspect the channel map. Test voices start at CH 1; the following channel is
   the reference. All have Mix and Mix modulation Off, with automatic triggering
   Off. Confirm those settings on the module before triggering. A Current design
   bank can have at most seven voices: an eight-voice bank is refused, never
   truncated. Use an external recorded reference/gate for an eight-voice test.
4. Record the tested individual output and reference output simultaneously.
   The reference has distinct, short, faded start/end tone patterns at −30 dBFS
   peak. It is AUDIO-tagged; CV test files are CV-tagged and remain blocked from
   Samples audition. No reference tones are inserted into the tested audio/CV.
   Trigger/gate CH 1; the reference follows as a one-shot Link channel. Link
   follows the nearest master's trigger/gate rather than inheriting all playback
   parameters ([Assimil8or manual, Channel Modes](https://www.rossum-electro.com/fqlzron/wp-content/uploads/2018/04/Assimil8or_man_040618-2.pdf)).
5. Use the manifest's exact frame positions and sample rate to locate the
   reference patterns. Compare the tested waveform and expected interval too.
   Measure relative onset/retrigger variation before assuming channel alignment,
   and account for recorder clock drift. No automatic pass/fail or voltage
   calibration is inferred merely because reference markers are found.

For the **essential analog CV check**, start with **CV levels**. At default
settings it holds 0%, +10%, 0%, −10%, 0% FS in five equal sections, so a 10-second
file gives two seconds per section. Capture the individual output and both mix
outputs, with the reference on the fourth scope channel if desired. Record
settled plateau voltages, polarity and the idle baseline. Then use **CV sine**
(one bipolar cycle per window) or **CV positive triangle** (zero → positive
level → zero) for the varying-CV check. Levels are digital fractions, **not
assumed volts**; continue to use the gain/load and scope precautions above.

The start code is two 20 ms 1 kHz bursts, the end code three 20 ms 2 kHz bursts,
each with 2 ms fades and 40 ms spacing. The first end burst starts exactly at
the window's frame boundary; the reference file continues another 120 ms to
contain the full code. Capture a little before the trigger and beyond that tail.
For a 10-second test, try about 1 s/div or a sufficiently long roll acquisition,
then zoom the saved capture to inspect the individual bursts and CV transitions.

The reference's ending pattern marks the **scheduled observation window**, not
proof that the tested voice stopped. A current one-shot may end earlier; a
continuous loop may continue after the reference has finished. For gated/looping
tests, record the actual gate/stop event and define a deliberate stop; document
live marker edits. A shared trigger is not a guarantee of sample-accurate starts.
This feature does not emulate or verify every unusual sample/loop marker order.

Keep Current design's `design.json` if you need to recall its shaping controls.
Built-in tests have no designer recipe. The validator recognizes the genuine
test manifest/instructions as informational desktop-only files; malformed or
unrelated JSON/text still produces warnings. A manifest is an expectation record,
not a substitute for validating its WAVs or measuring the actual hardware output.

The feature may remain available as an advanced, opt-in tool in distributed
builds. It must not be advertised as speaker-safe: low digital tone levels can
be amplified, CV individual outputs remain active, hardware routing can change,
and other players or stripped metadata bypass A8Manager's protections. The safety
acknowledgement, no automatic playback, tagged CV and Mix Off reduce accidental
exposure; they are not physical protection against a wrong patch.

Ordinary captures with a documented trigger/gate sequence remain sufficient for
many tests. This package makes repeated analysis easier; it does not require
replacing working measurements or broadening the essential scope session.

## Before starting

- [ ] Record the A8Manager version/build, Assimil8or firmware version, SD card,
  and which individual or stereo-mix outputs you use.
- [ ] Back up the SD card and any presets you will edit on the module. Export
  into a new folder; copy the whole folder to the SD card's root without merging
  it into an existing preset folder. Safely eject the card before using it.
- [ ] Turn down the monitor/amplifier level before loading or triggering audio.
  For DC/slow-CV tests, disconnect the tested output from speakers/headphones;
  connect it to a suitable meter or DC-coupled oscilloscope instead.
- [ ] Begin with no external pitch/modulation CV or MIDI, no latched channel,
  and unchanged exported gain settings. Trigger/gate channel 1 explicitly.
  Automatic triggering is off in generated presets.
- [ ] Load the test folder and selected preset number (001 for a default
  standalone package). Confirm there are no missing-sample errors. Keep its
  `prstNNN.yml` and referenced WAV files together.

Record observations rather than changing several controls at once. For a failed
test, note the recipe, exact settings, output, trigger/gate sequence, expected
result and actual result. A short scope capture or recording is useful.

## Extended checks: shared-preset assignment

Use a disposable folder, not the only copy of existing samples/presets. The tests
below supplement the standalone preset-001 tests in the rest of this checklist.

- [ ] In Samples choose an empty non-001 slot, e.g. 047. Switch to Waveform
  designer: the same slot and name remain selected, and other slots are visible.
- [ ] Generate an Audio Cycle into CH 1 / zone 1. Switch back to Samples and
  inspect it. Save; confirm `prst047.yml` is created, not `prst001.yml`.
- [ ] In the same preset assign a CV LFO to CH 3 / zone 1. Confirm CH 3 Mix and
  Mix modulation are Off and locked, while CH 1 retains its audio mix settings.
  Attempt CV into CH 1 and audio into CH 3, including replacement: both must fail.
- [ ] Try the same prohibited assignments through Samples imports and zone paste.
  Confirm the original assignments survive and CV remains blocked from audition.
- [ ] Append another same-purpose design to zone 2. Inspect both zones' voltage
  ranges; zone 1's former selection range is divided without changing its WAV.
- [ ] Assign a two-voice bank to CH 5–6. On the module, trigger CH 5: CH 6 should
  follow as Link. CH 1 and CH 3 assignments should remain intact.
- [ ] With CV disconnected from speakers, load preset 047 on the module. Verify
  CV at the CH 3 individual output and **no CV contribution to the stereo mix**
  using a suitable scope. Check the hardware reports Mix Off on CH 3.
- [ ] Regenerate a design. The new WAV has a unique name; older WAVs and other
  presets referencing them must be unchanged. Reopen its `.design.json` recipe.
- [ ] Make an unsaved preset edit, then switch workspaces: it remains. Changing
  slots asks before discarding it. Cancel leaves the original slot and edits.
- [ ] Export a standalone package with a chosen non-001 Package preset number;
  verify its YAML filename/header and loading instructions agree.

### Test record

Date: __________  A8Manager build: __________  Module firmware: __________

Export folder/recipe: __________  Measured output and gain settings: __________

Fill in actual measurements and **Pass / Fail / Not tested** rather than treating
the expected values as measured results. Use a tolerance appropriate to your
instrument, and record it with the measurement.

| Test | Expected result at the listed settings | Actual result / tolerance | Pass / Fail / Not tested |
| --- | --- | --- | --- |
| Software regression/build identifier | Tests pass; record output, not hardware claims | | |
| Preset load and save/reload | Intended assignments/settings survive | | |
| One Shot / Loop / Gated loop | Behaviors in Extended 4 | | |
| Forward sample-to-loop cases | Initial passage, bridge when applicable, then repeated loop | | |
| Essential CV constants: 0 / +10% / -10% | Record actual volts; correct polarity, sustained DC | | |
| Essential CV intended operating range | Measured extrema safe for the intended destination | | |
| Essential Mix L and Mix R isolation | Both Off; no CV-correlated signal above recorded baseline | | |
| Optional 48 kHz, 512-frame sine, zero pitch | 93.75 Hz | | |
| Optional same file, hardware PITCH +12 | 187.5 Hz | | |
| Optional 96 kHz, 512-frame sine, zero pitch | 187.5 Hz | | |
| Optional amplitude 25% to 50%, same output/gain | Approximately 2x signal amplitude | | |
| Optional two linked voices, detune -10/+10 cents | Distinct pitches and slow beating | | |
| Optional CV 2 seconds, 1 cycle, zero pitch | 2-second period | | |
| Optional repeated same-name export | New folder; original files unchanged | | |

## Extended 1. Single-cycle audio: file, pitch and level

The frequency/amplitude/shape measurements here are **optional diagnostic spot
checks**. Prefer sample-level tests; do not repeat every variant on the scope.

Create an **Audio Cycle / Sine** with 512 frames, 48 kHz, bipolar output,
25% amplitude, zero offset, zero phase, symmetry 50%, no drive/fold, and
**Loop** playback. Export it with a recognizable test name.

- [ ] On the module, check CH 1 is Master; only zone 1 has the generated file.
  Sample and loop markers should cover the complete file: start 0, end/length
  512. Other channels/zones should be empty.
- [ ] Trigger CH 1. A steady sine should continue after the trigger ends.
  Its untransposed fundamental is `48000 / 512 = 93.75 Hz` (period about
  10.667 ms). Use a scope/frequency meter or sufficiently long recording;
  one stored cycle is not itself a long tuning reference.
- [ ] Set hardware channel PITCH to +12 semitones: expect 187.5 Hz, twice the
  frequency. At -12, expect 46.875 Hz. Restore PITCH to zero afterwards.
  In general, `frequency = WAV sample rate / cycle frames * 2^(channel PITCH / 12)`
  for these exports with zero zone pitch and no pitch modulation. Designer
  Monitor transpose is not part of this hardware calculation.
- [ ] Export the same 512-frame design at 96 kHz. At zero hardware pitch,
  expect 187.5 Hz. This checks that the WAV's sample rate is respected.
- [ ] Compare 25% and 50% amplitude exports using the same output and gain.
  The latter should have roughly twice the signal amplitude, without an
  unexpected extra level adjustment. Digital percentages are not voltages.
- [ ] Compare phase 0 and 90 degrees in the WAVs; optionally compare captured
  outputs with a common recorded trigger reference. Account for trigger latency
  and capture alignment rather than treating the first acquired point as sample
  zero. The steady frequency/amplitude should remain unchanged. A phase change
  alone need not sound different.
- [ ] Try triangle, saw and pulse, then brightness/harmonics, drive and fold.
  Confirm the differences are retained after exporting/reloading. Listen
  conservatively at high transpositions: band-limited file creation does not
  guarantee alias-free playback at every hardware pitch.

## Extended 2. Computer-only audition and interface checks

Use the designer's audio audition at a low **Monitor level** first. It is a
convenient preview, not a bit-exact model of Assimil8or's DAC, mixer, interpolation,
filters, modulation or complete trigger/envelope behavior. It continuously loops
audio cycles regardless of the exported playback choice, with its own headroom,
DC removal and short fades/crossfades. Those measures do not modify exported WAVs.

- [ ] Start and stop an audio design. Change its shape while auditioning and
  confirm the sound follows the updated design. Drag Brightness or Drive
  continuously for a few seconds: hear updates during the drag, not only after
  releasing the mouse. Brief background-render latency is expected.
- [ ] Start sample playback, then designer audition: only the designer should
  remain audible. Stop designer audition: the previous sample must not restart.
  Start sample playback again: it should take over from the designer.
- [ ] Change the computer audio device while auditioning: the designer should
  stop and require an explicit restart on the selected output.
- [ ] Check the header's audio-output name follows the selected device and
  reports none when no output is selected. Open Audio Settings without scrolling.
  Try light/dark appearance and UI sizes; numeric entries should remain legible.
- [ ] Right-click the small END/START preview on each half. The offered boundary
  commands should affect the corresponding end/start of the selected Sample or
  Loop. Automated zero-crossing and opposite-boundary moves beyond 50 ms must
  ask first; Cancel must leave markers unchanged. Explicit numeric entry is
  intentional and should not show that automated-movement confirmation.
- [ ] Select the right side of a stereo pair. Zoom, pan the waveform and open its
  expanded view; neither view may independently change sample/loop markers.
- [ ] Change **Transpose** in LIVE AUDITION by +12 semitones. The computer preview
  should rise an octave, but the recipe's synthesis parameters and exported preset
  channel PITCH must remain unchanged. Restore the monitor transpose to zero
  before comparing the hardware's base pitch.
- [ ] Change Monitor level, then export. It must not change the WAV amplitude,
  the exported Channel LEVEL, or per-channel mix offsets. Use the design's
  amplitude/per-voice gain controls to change exported audio instead.
- [ ] Open the export in the Samples workspace. That workspace auditions its
  selected sample/zone, not the complete linked bank. Its audition speed and
  Keep pitch controls are separate from the designer's monitor controls.
- [ ] At 1x audition speed, use channel PITCH +12 and zone PITCH OFFSET −12:
  pitch and duration should match the untransposed file. With both set to +12,
  frequency should be four times higher and duration one-quarter as long.
  Keep pitch must not alter these results at 1x. At 0.5x with Keep pitch on,
  duration doubles relative to that pitched result, without another pitch shift.
  Compare recordings against the source sample count/rate; a scope is not
  essential for this check. Extreme/ultrasonic pitches are not listening tests.
- [ ] Confirm the designer does not offer audio audition in **CV / Modulation**
  mode. This is intentional, even for CV files with an audible repetition rate.
  Do not bypass this by playing a DC/slow-CV export through ordinary monitors.
- [ ] With computer speakers/headphones disconnected, open a new CV export in
  Samples: ONCE/LOOP must be disabled with **CV sample / Speaker audition
  disabled** visible. Markers and waveform editing must still work. An ordinary
  audio export must remain playable. Either CV side of a stereo pair must block
  the entire pair's audition.
- [ ] Copy/rename a newly exported CV WAV without its recipe and load it: the
  embedded tag must still block audition. For an older untagged export, retain
  the original `voice-01.wav` beside its matching `design.json`, or re-export.
  Metadata-stripped or arbitrary unmarked external CV cannot reliably be detected;
  do not test those files through speakers expecting automatic protection.
- [ ] Try an 8,192-frame cycle at 48 kHz and zero Monitor transpose. Its roughly
  5.86 Hz fundamental should be refused for audio monitoring. Raise Monitor
  transpose by +24 semitones (about 23.44 Hz) and explicitly start again. This
  must not shorten or retune the exported file/preset. Frequencies above 20 kHz
  or the computer output's Nyquist limit are likewise not monitored.
- [ ] Expand and close the waveform preview while auditioning. The expanded
  view should remain a view of the same design, not a second audio player.

## Extended 3. Layer bank: detune, phase, pan and gain

Start with two voices and a simple sine or saw. Use 512 frames at 48 kHz,
zero overall offset, moderate amplitude, equal per-voice gains, and **Loop**.
Do not apply a spread after manually setting voices unless you intend to replace
their detune, phase, pan and gain values.

- [ ] Check CH 1 is **Master** and CH 2 is **Link**, not Stereo Right. Each has
  its own mono file in zone 1. One CH 1 trigger should start both voices.
- [ ] Set voice detunes to -10 and +10 cents. Their hardware channel PITCH
  should read -0.10 and +0.10 semitones. Individually measured frequencies
  should follow `93.75 * 2^(cents / 1200)`. Together they should produce slow
  beating; the computer designer audition should also demonstrate that beating.
- [ ] Set both detunes to zero and different phases. Check the phase difference
  in the files first. Optionally capture both individual outputs simultaneously:
  there should be no detune-induced beating. Record the relative phase and its
  repeatability across triggers rather than assuming Master/Link guarantees
  sample-accurate phase-locked onset.
- [ ] Hard-pan one voice left and the other right. Verify the stereo **mix**
  outputs separate them. Each individual channel output should still carry its
  own voice; pan is not meant to attenuate individual outputs.
- [ ] Reduce one voice's gain and export again. That WAV/individual output
  should be reduced once, not once in the WAV and again in preset channel gain.
- [ ] Expand to 8 voices. Verify CH 2-8 are Link and all eight files load. Check
  each individual output, then the stereo mix at a low monitor level.
- [ ] Check that the preset's per-channel mix offsets are negative for multiple
  voices, reserving headroom for their sum. Do not interpret a quieter mix output
  as missing voices, or compare its voltage directly with an individual output.

Computer and hardware pan laws, summation and output levels can differ. Compare
voice count, pitch, beating, phase behavior and stereo direction first; a matching
overall loudness is not proof of identical mixing.

## Extended 4. Trigger and loop behavior

Export separate copies with each playback selection so the test is repeatable.
Generated attack/release are zero and no fades are added to the sample itself.
These are the designer's three export combinations, not an exhaustive test of
all independent Play Mode and Loop Mode combinations.

| Exported choice | What to test on Assimil8or |
| --- | --- |
| One Shot | A trigger plays the file once with no loop, regardless of trigger length. A 512-frame cycle lasts only about 10.7 ms at 48 kHz; a brief sound/click or scope trace is expected, not a sustained tone. |
| Loop | One trigger starts continuous looping. Ending the gate does not stop it. Stop it by holding Play Mode and pressing the channel button. |
| Gated loop | Hold the CH 1 gate high to sustain playback. Gate release ends playback with the exported zero release. Test a short and a long gate. |

- [ ] Repeat the three tests with a two-voice bank; linked voices should follow
  CH 1. Abrupt starts/stops can click, especially at nonzero sample boundaries.
- [ ] On a copy of the preset, try a small hardware attack/release for audio
  and note the difference. Those are hardware edits, not designer monitor
  settings. Keep the original export unchanged for later comparison.
- [ ] If investigating gate-release behavior, use an audible release and a
  sample tail beyond Loop End. Zero release hides the difference between
  decaying in a Normal loop and leaving a Gated loop to play toward Sample End.
  Keep the gate trace/event record. Loop-before-sample, equal markers and live
  CV marker movement remain a separate characterization task, not a claim made
  by the forward sample-to-loop simulation.

## Extended 5. CV/DC: range, polarity and timing

Absolute volts, DC retention and Mix Off are essential analog checks; detailed
shape/step timing and repeatability can normally be checked in the WAV. A scope
capture is useful only when diagnosing hardware playback or voltage behavior.

Disconnect speakers/headphones from the tested outputs. Use an **individual
channel output** and a DC-coupled scope/meter; a normal AC-coupled audio input
cannot validate sustained DC. Do not connect the output to a destination CV
input until its range and polarity have been measured.

- [ ] Export CV / Sine, duration 2 seconds, cycles 1, bipolar, amplitude 25%,
  offset zero, **Loop**. Expect one cycle every 2 seconds, centred near the
  output's zero reference. The numeric voltage is hardware-dependent.
- [ ] Load this new CV preset and confirm **Mix Output Level displays Off**.
  The file currently writes numeric `MixLevel: -90`; verify that representation
  by saving the preset on the module and inspecting the saved value. With
  speakers disconnected, use a DC-coupled scope to confirm the individual output
  carries the CV and the stereo mix does not. If Off is not selected, stop and
  report the hardware-saved value before relying on this routing. Existing
  presets are not retroactively changed; check their mix levels separately.
- [ ] Change to unipolar, amplitude 50%, offset +10%. The stored waveform's
  range should be 10%-60% of digital full scale; measure both extremes. It is
  not automatically a 0-5 V signal.
- [ ] For a constant-level check, use amplitude zero with +10% offset, then
  -10% offset in a separate export. The output should hold the corresponding
  positive/negative level while looping, rather than decay toward zero as if
  the file had been DC-filtered. Inspect the start/stop transitions too.
- [ ] Increase calibration-test levels gradually if needed. Leave the positive
  full-scale measurement field at zero/unknown until you have established that
  value for the intended individual output, gain and load. Verify the negative
  polarity separately; do not assume perfectly symmetric rails.
- [ ] Enter an established positive full-scale measurement into the designer.
  This supplies a voltage estimate only: confirm it does not rescale the WAV
  or change the module's calibration. Recheck actual voltages after changing
  module gain, output selection or connected load.
- [ ] Try an envelope in One Shot: one trigger should output the entire baked
  contour. Its attack/decay/release are fractions of a file cycle, not a live
  hardware ADSR responding independently to every gate edge.
- [ ] Try steps with zero and nonzero glide. Check the step timing and final-to-
  first transition in the WAV, optionally with a DC-coupled capture. Try a drawn
  curve with deliberately different endpoints so the loop seam is visible, then
  make the endpoints agree.
- [ ] Repeat a seeded random export. The pattern should repeat consistently,
  rather than produce new random values forever.
- [ ] Change hardware PITCH by +12: a 2-second CV file should take approximately
  1 second. Any matched duration assumes the pitch specified when matching;
  later hardware transposition changes timing.

No software clipping warning means the file stayed within digital full scale;
it does not certify that a connected module accepts the resulting voltage.

## Extended 6. Files, recipes and repeatability

- [ ] Export twice with the same name. Confirm that the second export has a new
  numbered folder and that the original WAVs, preset and recipe are untouched.
- [ ] Reopen `design.json`, export again, and compare the design settings and
  WAV content. Monitor gain/transpose are audition-only, not recipe parameters.
- [ ] Keep any hardware-edited presets in a separate copy. Loading the original
  generated preset should restore its known initial pitch, level, markers and
  playback configuration.
- [ ] Copy the complete test folder back from the SD card if you made hardware
  edits you want to keep. Do not rename or move its WAV files independently of
  the preset's references.

For any unexpected result, retain the smallest failing export plus its recipe
and the observations recorded above. Distinguish a designer/export problem from
a monitoring-path difference or a module setting changed after loading.
