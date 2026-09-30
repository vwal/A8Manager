# Waveform Designer

Use the **Waveform designer** button at the top of A8Manager to create audio
cycles, CV/modulation files, and multi-channel layers. **Samples** returns to
your sample/preset editor. Neither action discards the open preset or the design.

## Shared presets: Generate & Assign

The preset-slot list is visible in both workspaces. It is the **same selected
preset**, not a separate designer bank. Select an existing slot or an empty one;
the preset name, Save button and unsaved changes are shared with Samples.
Switching slots/folders still asks before discarding unsaved preset edits, and
does not discard the waveform design. **Root folder...** selects a different
folder without stepping through intermediate locations.

1. Choose the destination preset in the left-hand list. Use **Show All** to see
   empty slots as well as existing presets.
2. Design and audition an Audio Cycle or Layer Bank, or visually design CV.
3. Choose **Target channel** and **Target zone**, then **Generate & Assign...**.
   Empty independent channels are suggested. A bank needs consecutive channels;
   stereo pairs and assignments that split an existing Link/Cycle group are
   rejected. Choose an existing zone or the next unused zone without leaving gaps.
4. Review the confirmation. Target-zone samples and markers are replaced;
   generated pitch, pan, envelope, play/loop and mix settings apply to the whole
   channel, including its other zones. Appending a zone divides the former last
   zone's CV-selection range. Other sample content remains unchanged.
5. Click the designer's **SAVE**. It creates or refreshes a self-contained
   `PRNN - <preset name>` folder, then saves the working `prstNNN.yml`.
   Assignment itself does not save the preset or discard your previous unsaved
   edits. Wait for the save result before ejecting the card or copying the folder.

**SAVE IS PENDING** appears beside Save whenever the current preset differs
from its last saved state, in both Samples and the designer. A failed save leaves
the reminder on. Changing a waveform design alone does not change the preset:
use **Generate & Assign...** to apply it, then **SAVE**.

Generated mono 24-bit WAVs and a uniquely named `.design.json` recipe are placed
directly in the current folder. Their names start with your design name, without
an added `A8-` prefix; unique suffixes and voice numbers prevent collisions.
Existing WAVs are never overwritten, including
when regenerating a design. If the preset/folder changes during generation,
assignment is canceled and its unused generated files are cleaned up. Successful
assignment keeps its files even if you later discard the preset edits; those
files may be useful independently and are not automatically deleted.

### Getting a shared preset onto Assimil8or

The app's **Root folder** is your working folder, not necessarily the SD card's
root directory. On Assimil8or, all preset and sample files must be together in
a **named folder directly under the SD-card root**, with no further nesting
(Rossum manual, chapter 6, page 13).

The designer's **SAVE** assembles this folder automatically, using the preset
number and the name entered in the shared preset header, not the waveform's
Design name. For preset 1 named `koe-01`, it creates:

```text
SD card/
└── PR01 - koe-01/
    ├── prst001.yml
    ├── audiowave-01-...-01.wav
    ├── audiowave-02-...-01.wav
    ├── audiowave-03-...-01.wav
    ├── cv-wave-04-...-01.wav
    └── matching .design.json recipes
```

The shortened WAV names above are illustrative: the copies keep their actual
filenames, matching the preset's `Sample` references. All referenced samples are
included, including ordinary samples and stereo files already in the preset,
plus available generated recipes and the selected MIDI setup file when present.
Recipes allow later designer editing; A8 does not need them for playback.

The slot uses at least two digits (`01`–`99`, then `100`–`199`). Folder names are
sanitized and capped at 31 characters for hardware compatibility; long names
are shortened only in the folder name, not in the preset itself. The resulting
folder path is shown after saving. Existing unrelated folders or externally
modified copies are not silently overwritten. Changing the preset name can
create a differently named copy; previous copies are retained, not deleted.

Your working folder and its shared files stay in place, and the preset list
does not switch folders. Repeated saves refresh the generated copy. If you
open a named `PRNN - ...` folder for that same slot, Save works in place
instead of wrapping it in another nested folder. Saving a different slot from
there creates a sibling preset folder, not a nested one. Ordinary **Samples** saves
remain in-place saves; use the designer's **SAVE** to refresh the hardware copy.

Older `A8 Preset NN - ...` folders remain recognized for in-place saving; existing
folders and files are not renamed automatically.

If you work on your computer, copy the complete generated `PRNN - ...`
folder directly onto the SD-card root. If it was created elsewhere on the card,
copy it to the root before using it on A8: the app cannot assume every working
directory is a card root. Do not copy loose files or put another folder around
the complete package. **SAVE after the final assignment**, safely eject the
card, then load that folder and preset on A8. There is no need to export each
channel as a separate package: that creates independent presets instead.

**CV and audio must use separate channels**, though they may coexist on different
channels of one preset. This includes every zone, not just the selected zone.
Even replacing the only zone with the opposite content type requires purging
that channel first. In Samples, use **Channel tools → Purge this channel...**
or right-click its CH tab, then confirm. This resets all eight zones and channel
settings, not the WAV/recipe files. A stereo pair is purged together and becomes
two independent Master channels. Click **SAVE** to keep the preset change.

Known CV channels are locked to **Mix Off** and **Mix modulation Off**, leaving
the individual output available. Samples imports and
zone paste enforce the same separation. An already mixed preset cannot be saved
until corrected. Missing/unreadable destination samples prevent assignment
because their purpose cannot be verified. Untagged third-party CV cannot be
reliably recognized: the monitoring precautions below still apply.

## Standalone package export

The lower-left **Generate & Assign** and **Recall assigned** controls work with
the current preset's target channel/zone. **Package preset** and **Export new
package** are grouped on the right and create a separate preset folder instead.
Use the top-level **Samples** button to return to the sample editor; **Open in
Sample workspace** instead opens the last exported package folder.

1. Choose a mode and a starting shape. Adjust the controls while watching the
   preview. A new preview appears after the background render finishes.
   For Audio Cycle or Layer Bank, turn down your speakers/headphones and use
   **Start audition** to hear the design before exporting.
2. Choose playback: **One shot**, **Loop** (continuous), or **Gated loop**.
3. Give the design a name, choose **Package preset** (001–199), then choose
   **Export new package...**. Select a parent folder. This does not use the
   Target channel/zone fields or modify your selected preset; voices in the new
   package start at CH 1. Use **Generate & Assign...**, then **SAVE**, to fill
   the selected channel/zone in the current preset instead.
   A8Manager creates a new, uniquely named folder based on your design name inside it.
4. **Open in Sample workspace** opens the generated folder for inspection.
   Existing unsaved preset edits still require confirmation before changing roots.
   For hardware use, copy the whole generated folder to the root of your SD card,
   safely eject it, and load the chosen preset number on Assimil8or.
5. Trigger/gate channel 1. Layer banks' Link channels follow channel 1.

The folder includes one mono **24-bit integer PCM WAV** per voice, `prstNNN.yml`,
`design.json`, and a `README.txt` with the exact settings and loading instructions.
WAVs use the chosen 48 or 96 kHz rate. Existing files and folders are not replaced;
repeated exports get a numbered folder suffix. This does not assign files to or
save over the preset currently being edited. Other zones/channels in the exported
preset are empty defaults.
In Samples, the validator identifies genuine generated recipes and instructions
as informational desktop-only files. They are not hardware presets and do not
add to sample RAM; unrelated or malformed JSON/text files still get warnings.

## Advanced hardware test output

Choose **Test output...** in the Waveform designer to export a separate test
package with a dedicated timing-reference channel. This is a file-export tool,
not a computer signal generator: it does not play the test or send CV through
your computer's audio device. It never assigns into or saves over your current
preset. Ordinary designer exports and assignments are unchanged.

Choose the signal to test:

- **Current design:** use the current Audio Cycle, CV Modulation or Layer Bank.
  Its rendered samples, pitch and playback settings are preserved; the test
  package turns its mix routing Off. Banks may use at most seven voices because
  the eighth output is needed for the reference. An eight-voice bank is rejected,
  not reduced or overwritten; record an external reference for that test instead.
- **Audio tone:** a 440 Hz sine for checking output routing and timing.
- **CV levels:** five equal sections: zero, positive level, zero, negative level,
  zero. At the default level these are 0%, +10%, 0%, −10%, 0% of digital full scale.
- **CV sine:** one slow bipolar cycle across the chosen duration.
- **CV positive triangle:** rise from zero to the positive level, then return
  to zero across the chosen duration.

The test window is 1–60 seconds, initially 10 seconds. Built-in signals use a
1–25% full-scale level, initially 10%; this control does not rescale Current
design. A design's original one-shot length or continuous/gated playback is not
changed to fit the window. The reference marks the observation window, not an
assertion that the tested voice ends there. No percentage is a guaranteed voltage.

Before exporting, disconnect the tested individual and mix outputs from speakers
and headphones, prepare a suitable scope/recording connection and acknowledge
the safety notice. Choose a parent folder: a new uniquely named folder is created
with mono PCM24 WAVs, a preset, `test-manifest.json`, and `README.txt`. A Current
design package also keeps `design.json`; built-in tests are not designer recipes.
Keep the entire package together and follow its channel map when copying it to
the Assimil8or SD card. Exporting does not start hardware playback.

Every test channel, including the reference, has **Mix Off**, **Mix modulation
Off** and **automatic triggering Off**. CV WAVs carry the usual CV purpose tag,
remain blocked from Samples audition, and must use individual outputs. These are
the same app-level precautions used for normal CV, not a physical interlock.
Always confirm the module's routing and actual output level before patching.

The reference is a separate AUDIO-tagged WAV at **−30 dBFS peak**: two 20 ms
1 kHz bursts mark the start, and three 20 ms 2 kHz bursts mark the scheduled end.
Bursts are spaced 40 ms apart and have 2 ms fades. The reference file includes a
120 ms tail after the window to contain the complete end pattern. Record its
individual output alongside the tested output. Trigger/gate CH 1; the reference
is a one-shot Link channel following that master, while the tested voices keep
their configured playback. Link follows the nearest master's trigger/gate, not
all its playback parameters ([Assimil8or manual, Channel Modes](https://www.rossum-electro.com/fqlzron/wp-content/uploads/2018/04/Assimil8or_man_040618-2.pdf)).
The manifest lists exact source frames, sample rate, output mapping,
pitch, play/loop settings and reference bursts. Normal samples and CV contain no
inserted marker tones. A shared trigger does not establish sample-accurate start
alignment: measure the relative onset and repeatability, and account for recorder
clock drift. For continuous or gated playback, also capture the actual gate/stop
event; the scheduled end pattern is not proof that the tested output stopped.

This advanced feature need not be disabled for distribution, but it should stay
explicit and opt-in. Low-level tones can still be loud with downstream gain;
DC/slow CV is not speaker material. Hardware settings can be changed, individual
outputs can be mispatched, other players can ignore tags, and converters can
strip them. Mix Off and blocked app audition do not guarantee speaker protection.
See the [test-output workflow and essential measurements](HARDWARE-TEST-CHECKLIST.md#test-output-packages-and-recorded-references).

## Recalling and editing a saved design

- In **Samples**, select the channel and zone, then choose **Preset tools → Edit selected
  waveform in designer...**.
- In the designer, choose an occupied **Target channel / Target zone** and click
  **Recall assigned...**.
- Or use **Load recipe / WAV...** to select a package's `design.json`, an assigned
  design's uniquely named `.design.json`, or its generated WAV directly.

The Samples **Preset tools** command is greyed out unless the selected zone contains a
recognized generated WAV with a valid associated recipe in the current folder.
Ordinary samples, empty zones and missing/invalid companion files cannot be
recalled this way. Any voice in a generated bank can enable the command.

Confirm replacement of the design currently held in that workspace mode. Cancel
and generate/assign or export it first if you want to keep it. Recall restores
all saved shaping controls, mode and timing; choosing any bank voice restores
the **whole bank**, including each voice's settings. It does not start audition.
When the original bank is still assigned to consecutive channels, its first
channel becomes the assignment target. Otherwise choose a destination explicitly.

Recall reads the saved recipe, not changes subsequently made to sample/loop
markers, pitch or other preset parameters. It does not alter WAV files or the
preset. After editing, use **Generate & Assign...**, review the replacement
confirmation, then **SAVE**. New WAVs are created; previous files are preserved.

Keep the recipe beside the generated WAVs with their original filenames for
automatic recall. If files were renamed, or an older WAV has no generator tag,
load its recipe explicitly. A WAV alone cannot reconstruct the generator knobs
or bank settings; missing/unrecognized recipes are reported without replacing
your current design. Assignment and export both save the recipe; unwritten
design changes are held only for the current application session.
Each Workspace mode remembers its current design while the app remains open,
so switching between Audio Cycle, CV and Layer Bank preserves those edits.
Choosing a fresh Starting point replaces the current mode's design with preset
defaults; export first if you want to retain it. Changing the Shape selector
keeps the other shaping and output controls.

## Visual preview and live audition

The compact waveform shows the generated source cycles. Choose **Expand
waveform...** for a larger, resizable view; it follows the same design and live
edits. Close it with its close button or Escape. It is another view, not another
audio player.

For **Audio Cycle** and **Layer Bank**, **Start audition** plays through the
application's selected audio output. **Stop audition** stops it. Changes to the
design become audible after the background render finishes. In Layer Bank, each
voice plays with its own detune and pan, so you can hear beating and stereo
spread rather than just an untransposed individual WAV.

- **Monitor level** changes computer-listening volume only. Start low; use the
  design's amplitude and per-voice gain controls to change exported file levels.
  It starts at -18 dB and ranges from -60 to 0 dB.
- **Transpose** in the LIVE AUDITION area changes computer-listening pitch in
  semitones. Each voice's nominal frequency is
  `source sample rate / cycle frames * 2^(detune cents / 1200 + transpose / 12)`.
  Transposing by +12 doubles all monitored frequencies. It does not change the
  cycle size, WAV sample rate or exported preset PITCH. It starts at zero, with
  a fixed lower limit of **−48 semitones**. Its upper limit follows the design's
  source/export rate: **+72 semitones at 48 kHz**, or **+60 at 96 kHz**. These are
  the two export rates currently supported by the designer. Positive Layer Bank
  detune consumes this headroom: a voice detuned +1,200 cents (+12 semitones)
  reduces the 48 kHz bank's Transpose ceiling to +60. All-negative detunes do not
  raise the base ceiling. This models combined channel-plus-zone pitch headroom;
  it is not a claim that one Assimil8or pitch parameter extends beyond +60.
- The audition continuously repeats the designed cycles even when the exported
  playback choice is **One shot** or **Gated loop**. It is a sound-design monitor,
  not a simulation of external triggers/gates or the complete hardware envelope.
- Monitor controls are not saved into `design.json` or the generated preset.
  The Samples workspace's audition speed and Keep pitch are separate controls.

Source-rate, mode, recipe and bank changes refresh the Transpose ceiling. If
the new limit forces its current value down, audition **stops**, rather than
retaining an automatic-resume request. Review the changed value and press
**Start audition** when ready. Increasing the available range never starts
playback by itself. This slider ceiling is separate from the computer-monitor
frequency check below: a selectable Transpose value can still put a voice
outside the monitor's permitted frequency range.

Starting designer audition stops any Samples-workspace audition; stopping it
does not restart the old sample. Starting sample playback takes over the audio
output in the opposite direction. An audio-device change stops designer audition;
start it again explicitly after choosing the output you want with the shared
**Audio Settings** button in the top-right bar, next to **UI size**. There is no
separate audio-settings button in the designer. Returning to Samples stops
designer audition and hides its expanded preview. Returning to the designer does
not start sound automatically.

Every voice's nominal frequency must be **at least 20 Hz** and **strictly below
the lower of 20 kHz and half the output device's sample rate** (its Nyquist
limit). For a 32 kHz output device the upper limit is therefore below 16 kHz.
The 20 Hz floor is a fixed software guard, not a measurement of your speakers,
headphones, hearing or audio interface's analog frequency response. The app can
calculate the source/transpose/device-rate limits exactly; it cannot predict
how loudly or accurately your connected hardware reproduces them. Changing
shape alone changes tone and harmonics, not this nominal cycle frequency;
cycle length, source rate, voice detune and Transpose determine the range check.

While audition is running, moving **Transpose** outside this range pauses the
whole bank, including when just one voice crosses a limit. The pause is shown
in the audition area. Move Transpose back into range to resume automatically;
**Stop audition** cancels the pending resume. Merely setting an invalid value
while stopped never arms playback, and an out-of-range Start request does not
start later by itself.

The pending resume is also canceled by changing mode/shape or starting point,
recalling a design, leaving the designer, changing audio device or starting
Samples playback. Start audition explicitly afterward.
Changing cycle length or detune can also put an active monitor out of range;
making a new render valid again does not resume it by itself. Make a deliberate
in-range Transpose adjustment, or Stop and then Start, when ready to listen.
For example, an 8,192-frame cycle at 48 kHz has a base rate of about 5.86 Hz;
raise Transpose to +24 semitones, then Start, without changing the
exported cycle. These limits apply to computer monitoring, not the exported WAV.

The monitor uses band-limited, interpolated cycle playback, stereo panning,
summed-voice headroom and short fades/crossfades to make edits easier to hear. It also removes
DC and applies an output safety bound. These monitoring measures do **not**
alter the exported audio. Consequently, computer sound/output level is not a
bit-exact emulation of Assimil8or's interpolation, mixer, DAC, modulation or
analog levels; a DC offset in an export is not reproduced as DC by the monitor.

**CV / Modulation cannot be auditioned through this monitor**, even if its
repetition rate is audible. Inspect it visually and test its generated file
with an individual Assimil8or output and a suitable scope/meter. There is no
automatic sound when simply opening a design.

## Audio Cycle

Start with **Sine**, **Triangle**, **Saw**, **Pulse**, or **Trapezoid**. Each file
contains one full cycle, without duplicating its first sample at the end.

- **Cycle frames** and sample rate determine the untransposed fundamental:
  `sample rate / cycle frames`. For example, 512 frames at 48 kHz is 93.75 Hz.
  Change channel PITCH on the module or in the preset editor to tune this to a
  specific note. The frame count is a power of two from 64 to 8,192.
- **Phase** shifts the starting point; **Symmetry** stretches the two halves
  of sine/triangle/saw/trapezoid. Pulse width controls the pulse duty cycle;
  on trapezoid it changes the width of the flattened portion.
- **Harmonics** sets the partial limit; **Brightness** rolls off higher partials.
  Its whole-number steps use a smooth, gentle logarithmic scale: 1–200 take
  about 60% of the slider, leaving about 40% for 200–1,024. There is no separate
  expanded section for 1–5. You can still type an exact integer in the value box;
  this cutoff selects harmonic numbers, not fractional harmonics. The waveform retains at most
  `cycle frames / 2 - 1` harmonics: a 512-frame cycle stops at 255, so higher
  settings sound and look the same. Longer cycles can use values above 300.
  The control limits existing harmonics; it does not add missing ones, and low
  Brightness can make the higher partials very quiet. Existing recipes keep
  their original harmonic settings; only the slider's response has changed.
- **Drive** rounds/saturates the shape; **Fold** folds it back on itself.
- **Amplitude**, **Offset**, **Invert**, and bipolar/unipolar selection set the
  final output range. These are percentages of digital full scale, not volts.

Audio saw/pulse edges use the MIT-licensed DaisySP PolyBLEP correction before
drive/fold. It rounds their discontinuities while keeping the existing polarity,
pulse width and symmetry controls. Other shapes keep their existing generation.
This is a small vendored component, not a replacement for the designer or its
pitch-aware audition engine. Re-rendering older saw/pulse recipes may therefore
sound slightly different; existing exported WAVs are not modified.

Audio shaping is oversampled, harmonic-filtered, and zero-centred/normalised
before the final amplitude, polarity and offset are applied. This limits generated
high harmonics; it does not guarantee alias-free playback at every hardware pitch.
Final values are limited to digital full scale, with a warning if clipping occurs.

The boundary-jump readout is the difference between the last and first stored
samples, not a promise of a click-free loop. A periodic waveform can legitimately
have a nonzero adjacent-sample difference. Strong drive, pulse edges, offsets,
and abrupt trigger/gate transitions deserve particular care.

## CV / Modulation

In addition to the periodic shapes, choose **Envelope**, **Steps**, **Drawn** or
**Random**. The generated file can be a one-shot modulation contour, an LFO, or
a faster repeating control waveform.

- **Duration** sets the complete file length (1 ms to 60 seconds); **Cycles**
  repeats the designed contour inside that duration. Nominal rate is
  `cycles / duration`. Fractional cycles may create a jump when the file loops.
- Tempo and beat controls provide another way to set the file duration. Tempo
  is baked into the WAV, not a live tempo-sync instruction for the module.
- **Match file/sample/loop** takes the currently selected zone's loaded file,
  sample-marker span, or loop-marker span. Matching includes channel PITCH + zone
  PITCH OFFSET at the sampler's source-rate-dependent ceiling. It excludes audition
  speed and external CV modulation; Keep pitch does not change it. The waveform
  need not be playing. Matching is a one-time copy, not a continuing link.
  Empty, unavailable, invalid, or out-of-range durations are rejected.
- Envelope attack, decay and release are fractions of a cycle, not seconds;
  their sum must fit within 100%. The remaining time sustains at the chosen
  sustain level. Curve bends the stage transitions. This is a baked envelope,
  not the hardware's interactive ADSR/gate-response generator.
- Steps have 1–16 values; draw their levels in the editor. **Step glide** smooths
  step/random transitions. At zero they are hard steps; at 100% each transition
  fills one step. In a repeating pattern, the last step leads into the first.
- Drawn curves have 33 editable points, with linear interpolation between them.
  Start and end values need to agree if a seamless loop is desired.
- Random uses a repeatable seed. It generates a repeating stepped pattern;
  it is not an unlimited random stream.

CV is **not normalised, DC-filtered or PolyBLEP-smoothed**. Its deliberate steps
and absolute levels are preserved. Unipolar conversion happens before
amplitude/depth and offset, so a unipolar 50% waveform with +10% offset occupies
10% to 60% of digital full scale. Invert flips the shape before that conversion.
Clipping warnings indicate that the requested combination exceeded the file's
representable range; lower amplitude or offset.

### Volts and safe monitoring

The application does **not** assume that a full-scale WAV sample produces 5 V
or 10 V. Sample-input range and output-voltage calibration are different things.
An optional measured positive full-scale output value can provide an estimate;
leave it at zero/unknown until measured. It does not change sample values or
calibrate the module. Verify both polarities and the intended output/gain/load
with a meter or oscilloscope before relying on voltages.

Use an **individual channel output** for CV. The stereo mix has pan and mix-level
attenuation, so its voltage is not the same. DC and slow CV should not be monitored
through speakers; ordinary AC-coupled audio interfaces cannot reproduce DC either.
Computer audition is intentionally disabled in CV / Modulation mode. Generated
CV WAVs now also carry a purpose tag: in **Samples**, ONCE/LOOP are disabled and
the zone shows **CV sample / Speaker audition disabled**. The playback engine
also rejects playback requests, including stereo pairs with CV on either side.
Waveform viewing, marker editing and hardware export remain available.

The tag survives ordinary copies and renames. A8Manager preserves it during
imports, conversion, stereo splitting and mono mixing. Older untagged exports
are recognised when the original `voice-01.wav` remains beside its matching
`design.json`; importing that file into another folder embeds the tag in the new
copy without changing the original. Re-export older designs to obtain tagged WAVs.
Arbitrary unmarked CV, older files moved alone, and files with metadata stripped
by another tool cannot reliably be identified. This protection applies only in
A8Manager, not other players; never use another player to send CV to speakers.

Generated CV channels use **Mix Output Level = Off**, leaving the individual
output available. The exported numeric value is `MixLevel: -90`, the application's
minimum. Confirm the module displays **Off** and test both outputs with a scope
before relying on it; the exact file representation still needs a hardware
round-trip check. Mix modulation is also Off. A known CV channel cannot have its
Mix controls reenabled in the editor; other audio channels retain their own mix.

See the [CV-generation discussion review](CV-GENERATION-REVIEW.md) for planned
possibilities such as zone-linked companion CV, triggers and envelope following.

## Layer Bank / Supersaw

Start with the supersaw preset, or another periodic shape. Choose 1–8 voices.
The **Supersaw (7 voices)** button resets the design and displayed controls to
seven saw voices, ±24 cents detune, phases from 0° to 300°, pan from −0.8 to
+0.8, and the preset's per-voice levels.

Spread sliders update the voices, waveform preview and running audition as you
drag—there is no separate Apply step. Detune and pan spread evenly around zero;
phase spreads from zero to the selected angle (negative angles reverse the spread).
Each slider changes **only its own setting** across the active voices, preserving
the other settings, individual gains and inactive voices. Start audition first
to hear the changes; editing does not start stopped playback or resume a paused
audition. Increasing detune can lower the available Transpose limit; if the
current transpose exceeds it, audition stops and the UI explains the adjustment.

Each voice can still be edited independently for detune, phase, pan and level.
For custom voice positions, the spread sliders display the outermost offsets,
with a **Custom voice positions** note. Moving a spread slider restores an even
distribution for that setting only. Recalling a recipe or switching modes
refreshes these summaries without changing the saved voice settings.

Changing Voice count retains each voice's settings rather than resetting them.
After changing the count, adjust the spreads if you want a fresh even distribution.
Spreads require at least two active voices; the individual controls remain
available for a single voice.

Each voice becomes a separate mono WAV on a separate channel. The first assigned
channel (channel 1 for a standalone package) is **Master**; subsequent channels
are **Link**, not Stereo Right. Trigger the first channel of that group. The preset stores
each voice's detune in channel PITCH and its pan separately. Phase and level are
already baked into the WAV and are not applied twice. Detune creates real beating
when the hardware plays the channels together; phase variation alone does not.

Mix-level offsets reserve headroom for the sum; these do not attenuate individual
outputs. Pan only affects the stereo mix. The visual preview overlays source
cycles; it is not a time-domain simulation of the changing detuned mix. The
designer's live audition does play the detuned, panned voices together. In
contrast, the **Samples** workspace auditions its selected file/zone, not the
complete Link bank, but does apply that channel's PITCH and zone PITCH OFFSET. Test the assembled sound
on Assimil8or for the final hardware result.

## Playback choices

- **One Shot:** no loop; one trigger plays the whole generated file.
- **Loop (continuous):** One Shot play mode plus Normal Loop. One trigger starts
  indefinite looping; gate release does not stop it. Use the module's manual
  stop controls (hold Play Mode and press the channel button).
- **Gated Loop:** Gated play mode plus Gated Loop. Hold the group's first channel gate high
  to sustain playback; releasing it ends the gated playback.

Exported presets disable automatic triggering and set attack/release to zero.
They do not add fades to the waveform or configure external CV/MIDI routing.
Abrupt starts/stops can still click or step. Sample and loop markers cover the
whole file. Exact hardware timing, analog voltage calibration, and every unusual
play/loop marker ordering are not emulated by this first workspace.

For a repeatable check of audio pitch, layers, triggering, CV and file safety,
follow the [hardware test checklist](HARDWARE-TEST-CHECKLIST.md). It is a test plan,
not a claim that all combinations have already been verified on a module.
