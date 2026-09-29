# Waveform Designer: hardware test checklist

This is a test plan, not a claim that every combination has already been verified
on hardware. Use a separate test folder and start with simple settings before
trying complex layers or modulation. Keep the exported `design.json` and
`README.txt` with your observations so a result can be reproduced.

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
- [ ] Load the test folder and preset 001. Confirm there are no missing-sample
  errors. Keep `prst001.yml` and all `voice-XX.wav` files together.

Record observations rather than changing several controls at once. For a failed
test, note the recipe, exact settings, output, trigger/gate sequence, expected
result and actual result. A short scope capture or recording is useful.

## Shared-preset assignment checks

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
| 48 kHz, 512-frame sine, zero pitch | 93.75 Hz | | |
| Same file, hardware PITCH +12 | 187.5 Hz | | |
| 96 kHz, 512-frame sine, zero pitch | 187.5 Hz | | |
| Amplitude 25% to 50%, same output/gain | Approximately 2x signal amplitude | | |
| Two linked voices, detune -10/+10 cents | Distinct pitches and slow beating | | |
| One Shot / Loop / Gated loop | Behaviors in section 4 | | |
| CV 2 seconds, 1 cycle, zero pitch | 2-second period | | |
| CV constant positive/negative offsets | Sustained levels of corresponding polarity | | |
| Repeated same-name export | New folder; original files unchanged | | |

## 1. Single-cycle audio: file, pitch and level

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
- [ ] Compare phase 0 and 90 degrees on a triggered scope capture. The starting
  phase should move by a quarter cycle without changing the frequency or
  steady-state amplitude. A phase change alone need not sound different.
- [ ] Try triangle, saw and pulse, then brightness/harmonics, drive and fold.
  Confirm the differences are retained after exporting/reloading. Listen
  conservatively at high transpositions: band-limited file creation does not
  guarantee alias-free playback at every hardware pitch.

## 2. Computer audition versus exported settings

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

## 3. Layer bank: detune, phase, pan and gain

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
- [ ] Set both detunes to zero and different phases. The relative phase should
  now stay fixed: there should not be detune-induced beating. Compare a
  triggered scope capture rather than assuming phase must alter the solo sound.
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

## 4. Trigger and loop behavior

Export separate copies with each playback selection so the test is repeatable.
Generated attack/release are zero and no fades are added to the sample itself.

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

## 5. CV/DC: range, polarity and timing

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
  first transition on a scope. Try a drawn curve with deliberately different
  endpoints so the loop seam is visible, then make the endpoints agree.
- [ ] Repeat a seeded random export. The pattern should repeat consistently,
  rather than produce new random values forever.
- [ ] Change hardware PITCH by +12: a 2-second CV file should take approximately
  1 second. Any matched duration assumes the pitch specified when matching;
  later hardware transposition changes timing.

No software clipping warning means the file stayed within digital full scale;
it does not certify that a connected module accepts the resulting voltage.

## 6. Files, recipes and repeatability

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
