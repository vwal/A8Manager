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
5. Click **SAVE** to write the selected `prstNNN.yml`. Assignment itself does not
   save the preset or discard your previous unsaved edits.

Generated mono 24-bit WAVs and a uniquely named `.design.json` recipe are placed
directly in the current folder. Existing WAVs are never overwritten, including
when regenerating a design. If the preset/folder changes during generation,
assignment is canceled and its unused generated files are cleaned up. Successful
assignment keeps its files even if you later discard the preset edits; those
files may be useful independently and are not automatically deleted.

**CV and audio must use separate channels**, though they may coexist on different
channels of one preset. This includes every zone, not just the selected zone.
Even replacing the only zone with the opposite content type requires purging
that channel first. Known CV channels are locked to **Mix Off** and **Mix
modulation Off**, leaving the individual output available. Samples imports and
zone paste enforce the same separation. An already mixed preset cannot be saved
until corrected. Missing/unreadable destination samples prevent assignment
because their purpose cannot be verified. Untagged third-party CV cannot be
reliably recognized: the monitoring precautions below still apply.

## Standalone package export

1. Choose a mode and a starting shape. Adjust the controls while watching the
   preview. A new preview appears after the background render finishes.
   For Audio Cycle or Layer Bank, turn down your speakers/headphones and use
   **Start audition** to hear the design before exporting.
2. Choose playback: **One shot**, **Loop** (continuous), or **Gated loop**.
3. Give the design a name, choose **Package preset** (001–199), then choose
   **Export package...**. Select a parent folder.
   A8Manager creates a new, uniquely named `A8-...` folder inside it.
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

Use **Load recipe...** to reopen a package's `design.json` or an assigned design's
uniquely named `.design.json`. Assignment and export both save the recipe;
unwritten design changes are held only for the current application session.
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
  semitones. At zero, a voice's frequency is
  `sample rate / cycle frames * 2^(detune cents / 1200)`.
  Transposing by +12 doubles all monitored frequencies. It does not change the
  cycle size, WAV sample rate or exported preset PITCH. Its range is -48 to
  +48 semitones; it starts at zero.
- The audition continuously repeats the designed cycles even when the exported
  playback choice is **One shot** or **Gated loop**. It is a sound-design monitor,
  not a simulation of external triggers/gates or the complete hardware envelope.
- Monitor controls are not saved into `design.json` or the generated preset.
  The Samples workspace's audition speed and Keep pitch are separate controls.

Starting designer audition stops any Samples-workspace audition; stopping it
does not restart the old sample. Starting sample playback takes over the audio
output in the opposite direction. An audio-device change stops designer audition;
start it again explicitly after choosing the output you want with **Audio
settings...**. Returning to Samples stops designer audition and hides its expanded
preview. Returning to the designer does not start sound automatically.

Every voice's monitored fundamental must be at least 20 Hz and strictly below
20 kHz or the audio device's Nyquist limit, whichever is lower. Invalid settings
are refused, or stop an active audition. For example, a large 8,192-frame cycle
at 48 kHz has a base frequency of only about 5.86 Hz; raise Monitor transpose
to hear it as audio without changing the exported cycle.

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
  Harmonics above the waveform's Nyquist limit are omitted.
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
  sample-marker span, or loop-marker span. Matching includes zone PITCH OFFSET
  and excludes audition speed/Keep pitch and channel processing. The waveform
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
Spread controls provide initial detune, phase and pan values; each voice can then
be edited independently for detune in cents, phase in degrees, pan and level.
Applying a spread resets those per-voice values, including their levels.

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
complete Link bank or its channel PITCH processing. Test the assembled sound
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
