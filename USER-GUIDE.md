# A8Manager 3.0.0 — User Guide

A8Manager prepares sample assignments, zones, and presets for the Rossum
Assimil8or. You can see and adjust sample boundaries, audition regions on your
computer, and save the resulting preset for use with the module.

The computer audition is an editing aid, **not a complete simulation of the
Assimil8or**. In particular, it previews the selected region and zone PITCH
OFFSET, but not all the channel processing you can configure in the editor.

## Contents

- [Your first preset](#your-first-preset)
- [Finding your way around](#finding-your-way-around)
- [Editing parameter values](#editing-parameter-values)
- [Sample regions and loop regions](#sample-regions-and-loop-regions)
- [Zooming and navigating the waveform](#zooming-and-navigating-the-waveform)
- [Auditioning a sample or loop](#auditioning-a-sample-or-loop)
- [Speed, pitch, and PITCH OFFSET](#speed-pitch-and-pitch-offset)
- [Stereo samples](#stereo-samples)
- [Zone selection voltages](#zone-selection-voltages)
- [Making consecutive slices](#making-consecutive-slices)
- [Creating waveforms and CV](#creating-waveforms-and-cv)
- [Saving and moving your work](#saving-and-moving-your-work)
- [Common questions](#common-questions)

## Your first preset

1. **Start with a working copy of your sample folder.** Keep a backup of the
   originals, especially while learning the file-management tools.
2. Choose **OPTIONS → Select Root Folder** above the file browser. Select the
   folder containing the samples and presets you want to work on, then confirm.
   Scanning starts after confirmation, not while browsing the chooser.
3. Select a preset in the preset list. Turn on its **Show All** checkbox to
   reveal unused preset slots as well as existing presets; an unused slot starts
   with a new preset. Give it a name at the top of the editor.
4. Select a channel tab, such as **CH 1**, and a numbered zone. For a new
   channel, begin with zone **1**.
5. **Double-click a sample in the file browser to assign it to the selected
   zone.** This is an assignment, not just a file-browser preview. You can also
   drag a sample file onto the zone editor.
6. Click **SMPL START** or **SMPL END** to select the sample region, then press
   **ONCE** to listen. Adjust the sample waveform handles or the numeric values
   to choose a smaller region. See the sections below for loop auditioning.
7. Click the preset's **SAVE** button when satisfied. Review any warnings or
   errors in the lower validation pane before transferring the files.

Loading samples from outside the current folder can copy or convert audio files
into it immediately. File operations are separate from saving the preset. Use
distinct filenames and backups rather than relying on SAVE as an undo boundary
for everything in the application.

## Finding your way around

- **Current path:** the line beneath the application heading shows the folder
  being used, and scan progress when applicable. It is a display, not a path
  entry field.
- **File browser:** the left pane contains folders and sample files. Its
  **Show All** checkbox includes files otherwise hidden by the file filter.
- **Preset list:** the next pane selects which preset you are editing. Its
  separate **Show All** checkbox includes unused preset slots.
- **Channel editor:** the CH tabs select one of eight channels. The parameter
  sections configure channel settings, while the numbered zones select a
  sample assignment and its individual boundaries and offsets.
- **Waveform:** the large display shows the current zone's source audio and
  editable markers. The small END/START display in the zone editor shows the
  join between the end and beginning of the selected region.
- **Validation pane:** the lower pane reports file/preset information, warnings,
  and errors. The **I**, **W**, and **E** buttons filter those categories; hiding
  a message does not resolve it.

Resize the window and use **UI size** to scale text and controls together, from
100% to 200%. Pane dividers can be dragged to redistribute space. If the window
is too small for the chosen scale, use the workspace scrollbars. The UI size and
layout are remembered.

Hover over a control for its tooltip, including useful ranges or explanations.
**Quick help** gives a short reminder of the main gestures.
**SETTINGS** at the bottom opens the computer's audition audio-device settings.
It is separate from the preset's MIDI SETUP selection.

### Jumping to a different folder

The file browser is convenient for nearby folders, but navigating through it
changes the working folder and starts scanning. To jump somewhere unrelated,
use **OPTIONS → Select Root Folder** instead. Browse freely in the chooser and
confirm only at the destination. Cancel leaves the current folder unchanged.

On macOS, press **⌘⇧G** (Command–Shift–G) inside the folder chooser to type or
paste a path. If there are unsaved preset edits, cancel the discard warning and
save before switching folders if you want to keep them.

## Editing parameter values

Most numeric parameter boxes support both direct entry and mouse adjustment.
When a numeric field gains focus, its contents are automatically selected:
typing replaces the existing value. You do not need to delete it first.

| Action | Gesture |
| --- | --- |
| Enter an exact value | Click the box, type, then press Return or move focus to another control. |
| Increase/decrease a value | Left-click and drag vertically: up increases, down decreases. No modifier is required. |
| Make fine adjustments | Hold Shift while left-dragging vertically. This disables acceleration and slows the adjustment. |
| Select text within a field | Drag horizontally. |
| Adjust with the scroll wheel | Hold ⌘ on macOS, or Ctrl on Windows/Linux, while scrolling over the field. |
| Nudge with the scroll wheel | Add Shift to the above shortcut for one minimum increment per scroll event, without acceleration. |
| Open parameter-specific options | Right-click the field. Available menus may include Clone, Default, or Revert. |

Ordinary scrolling over a numeric field does not change its value. Normal
vertical dragging is accelerated for larger ranges. Shift-drag uses one minimum
increment per 16 logical pixels of vertical movement, four times slower than the
slowest normal drag; small movements accumulate. For example, a pitch field steps
by 0.01 semitone, while an integer sample-position field steps by one frame.
At larger UI sizes, the physical distance scales with the interface. Shift-scroll
works even when the mouse/OS translates it into horizontal scrolling. For exact
positions, you can also type a value. Values are limited to each parameter's allowed
range, so a boundary field may stop moving when it reaches another boundary or
the end of the file.

**Revert** is not a general undo history: where offered, it restores the saved
or loaded reference value. **Default** restores that parameter's default.
Check the scope of a menu before applying it: parameter, zone, channel, and
preset tools affect different amounts of your work.

## Sample regions and loop regions

A sample file can contain much more audio than a particular zone needs. The
zone stores positions into that file; moving its markers does not cut or rewrite
the source audio.

There are two separate pairs of boundaries:

| Waveform indicator | Meaning | Matching zone fields |
| --- | --- | --- |
| **Red solid line, top handle** | Sample-region start | SMPL START |
| **Blue solid line, top handle** | Sample-region end | SMPL END |
| **Amber dashed line, bottom handle** | Loop start | LOOP START |
| **Pink dashed line, bottom handle** | Loop end | LOOP LENGTH, or LOOP END |
| **Yellow moving line with a small cap** | Current computer-audition position; not an editable boundary | None |

Drag the small **handles**, not the vertical lines, to move individual boundaries.
The zone panel's labels and small colour keys match the corresponding handles,
lines and overlay text. Handles point inward toward their region. Marker labels
show comma-separated frame positions and source-file timestamps; END labels also
show the region's pitch-adjusted length. Nearby labels move into separate rows
to avoid overlap. If handles are hard to separate, zoom in or use the numeric fields.

Editing a marker automatically selects **SAMPLE** or **LOOP** in the zone panel.
The area outside that selected region is dimmed; the selection also determines
which join preview and audition region the zone panel uses.

The **sample region** chooses the portion of the recording used by the zone.
The **loop region** chooses the portion to repeat when looping is used. For
example, a sample region can include an attack and a tail, while its loop region
covers a smaller, steady section in between. The two regions can also match,
which is useful when a zone represents a single repeating slice.

They are independent settings: changing the sample markers does not automatically
make the loop markers match. The editor does not force the loop to stay inside
the sample region. Check both pairs when preparing a conventional loop
within a sample.

### Moving a region without resizing it

Hold **Option on macOS / Alt elsewhere** before left-dragging horizontally
**inside a region or on one of its handles**:

- Dragging a sample handle moves the sample pair; a loop handle moves the loop pair.
- Inside overlapping regions, the current SAMPLE/LOOP selection chooses the pair.
  Click the corresponding zone-panel fields first if you need to change it.
- Inside only one region, that region is selected and moved. Outside both regions,
  an Option/Alt-drag does nothing; it cannot move a distant selection.
- Length remains fixed in both Length and End display modes, including fractional
  loop lengths.
- Add **Shift** for ten-times-finer movement, or zoom in for greater precision.
- Movement stops at the source file's beginning/end without shortening the
  region. A region spanning the whole file cannot move.

Only the chosen pair moves: moving the zone does not move its loop, and moving
the loop does not change the sample boundaries. Moving a zone here means
sliding its audio region within the same file, not reordering the zone list or
changing its voltage range. The audio file itself is not modified.

Release Option/Alt before the next drag to return to individual-handle editing
and ordinary left-drag panning. Scrollbar/horizontal scrolling and right-drag
zoom remain available. These edits update the in-memory preset; click **SAVE**
to keep them. The former Edit edges / Move zone / Move loop selector is no longer
needed.

### Positions, lengths, and units

Zone boundary values are **sample-frame positions/counts**, not milliseconds.
At a source-file sample rate of 48 kHz, 48,000 frames represent one second.
Positions refer to the original file, not to the time after changing audition
speed or pitch.

- **SMPL START** is the first frame in the sample region.
- **SMPL END** is the boundary just after that region: length = end − start.
- **LOOP START** is the loop's starting position in the file, not a distance
  measured from SMPL START.
- **LOOP LENGTH** is a duration in frames. Loop end = loop start + loop length.

For example, a loop start of `48000` and length of `24000` ends at `72000`.
At 48 kHz, this is a half-second region before audition speed/pitch adjustments.

The **LENGTH/END** selector above the zones changes whether the last loop field
is displayed as a length or an end position. In **Length** mode, moving LOOP
START normally carries the loop's end along with it by preserving the length.
In **End** mode, moving LOOP START adjusts the length to keep the end fixed,
subject to the valid range. Loop length has a four-frame minimum.

Right-click the waveform's timeline ruler to choose **Samples** or
**Minutes:Seconds**. This changes the ruler, not the units of the zone's numeric
boundary fields. Marker overlays always show both frame counts and source time.

### Reading durations

The waveform footer shows **File**, **Sample** and **Loop** durations in
`minutes:seconds`, normally with milliseconds (extra precision for very short
loops). The selected pair is capitalized. The END-marker labels repeat the
corresponding region's duration next to its end position.
The **Zones** panel also shows SAMPLE and LOOP time lengths underneath their
numeric boundary fields, with the same pitch adjustment described below.

These lengths include the current zone's **PITCH OFFSET**:
`duration = frames / source sample rate / 2^(PITCH OFFSET / 12)`.
For example, +12 semitones halves the nominal length and −12 doubles it.
The `@ … st` footer suffix identifies the pitch offset used.

**Audition speed does not change these length readouts.** Neither does the
preview's Keep pitch option. They describe nominal sampler-style duration at the
zone pitch, not necessarily the duration you hear with time stretching enabled.
Channel PITCH and external CV modulation are excluded. Marker *timestamps* remain
positions in the original file, so they are unaffected by any playback setting.

## Zooming and navigating the waveform

| Control or gesture | Result |
| --- | --- |
| **+ / −** | Zoom in/out around the centre of the visible waveform. |
| Click the **zoom percentage**, double-click the waveform, or **Zoom → Reset Zoom** | Show the whole source file and restore 100% waveform height. |
| **Zoom → Zoom to Sample Markers** | Frame the sample start/end region. |
| **Zoom → Zoom to Loop Markers** | Frame the loop region; does not start playback. |
| Vertical wheel/trackpad scrolling over the waveform | Zoom horizontally around the pointer. |
| Plain left-drag away from a handle | Pan along the file. |
| Option/Alt + left-drag inside a region or on its handle | Move the pair without resizing; add Shift for finer movement. |
| Bottom scrollbar or horizontal scrolling | Move along the file while zoomed in. |
| Control + wheel over the waveform | Magnify/reduce waveform height. On macOS this is Control, not Command. |
| Right-button drag away from a handle | Horizontal movement zooms time; vertical movement zooms waveform height. |

Zooming changes only the view. It does not alter sample boundaries, playback
volume, audio files, or audition speed. The yellow playhead follows playback
but does not automatically scroll the view; it may move off-screen when zoomed.

**Diagonal gray stripes** always identify the gap from Sample End to a later
Loop Start, so that bridge remains visible while editing—even with **No Loop**
saved in the channel. With **Loop** or **Loop/Release** enabled, the striped
extent continues through a Loop End beyond Sample End. Showing the bridge in
No Loop mode does not enable hardware looping. The waveform stays visible through
the stripes; ordinary unselected audio remains uniformly dimmed. The stripes
update with markers, channel loop mode, zoom and selected zone, in both sizes.
They describe the configured loop extent in source coordinates, independently of
the SAMPLE/LOOP audition selection—not a simulation of reverse playback, gate
release or modulation. Audition still plays only the selected region.

### Expanded view and marker tools

Click the **expand arrow** at the waveform's top left for a larger view over the
channel parameter area. The zone tabs and side panel remain accessible. It stays
expanded when you switch zones. Click **X**, or press **Escape** with the waveform
focused, to return to the compact layout. The same zoom, editing, menus and
audition controls work in both views.

The **gear button** and waveform **right-click menu** offer Zoom and Jump to
Marker commands directly under section headings, without opening submenus. The
right-click menu also has a direct **SET MARKER HERE** section. Zero Crossing
Nudge and Match Opposite Boundary remain submenus below a separator:

- **Zoom:** reset both axes, fit the sample region, or fit the loop region.
- **Jump to Marker:** centre a chosen marker without changing zoom. With the
  waveform focused, **1 / 2 / 3 / 4** jump to sample start/end and loop start/end.
  These shortcuts do not intercept typing in parameter fields.
- **Zero Crossing Nudge:** move any marker to the nearest crossing strictly to
  its left or right, on the displayed L/R side, within that marker's valid limits.
  When the crossing falls between frames, the quieter of those two frames is
  chosen. An END marker is placed **after** that frame, because ends are exclusive;
  this makes the last audible frame the one closest to zero. At high zoom, the
  end handle therefore appears one frame after the chosen zero/near-zero sample.
  If none exists, the marker stays put and the footer explains why. A zero crossing
  can help a join but does not guarantee a click-free loop.
- **Match Opposite Boundary:** move a chosen sample/loop START to match its END,
  or END to match its START, **even when that amplitude is not zero**. For example,
  **Loop End to Start** keeps Loop Start fixed and adjusts only Loop End. Each
  of the four marker choices has **Left <<** and **Right >>** options: Left
  searches earlier in the file; Right searches later. The search checks up to
  **50 ms only in the chosen direction**, within the marker's valid limits,
  using the displayed L/R side and the original sample rate. To retain material
  near an edge, move a START left or an END right rather than trimming inward. It
  chooses the smallest amplitude difference between the first and last audible
  frames; equally good matches favour the nearest position. If the existing join
  is already matched, or no improvement exists in the chosen direction, nothing
  moves and the footer reports this; the search never falls back to the other
  direction. Matching Loop Start keeps Loop End fixed **even in Length
  mode**: the length is adjusted without changing the Length/End setting.
  This is separate from true zero-crossing nudges. Matching amplitudes does not
  also match waveform slopes or the other stereo side, so audition the result;
  a perfectly smooth-looking join is not a guarantee of a click-free loop.
- **Set Marker Here** (right-click menu only): place a chosen marker at the
  right-clicked source position, clamped to its valid limits.

A right-click without movement opens the menu; right-button dragging continues
to zoom. Placing, nudging or matching a marker selects its Sample/Loop pair. A menu opened
for a previous zone/sample cannot edit a newly selected one.

## Auditioning a sample or loop

First choose **which region** to hear:

1. Click a **SMPL START / SMPL END** label or value to choose the sample region,
   or a **LOOP START / LOOP LENGTH (END)** label or value to choose the loop
   region.
2. Check the outlined/shaded box in the zone editor. It encloses the selected
   fields, small waveform, and transport buttons.
3. Press **ONCE** to play that region once, or **LOOP** to repeat that same
   region. The active button becomes **STOP**; click it to stop.

**LOOP repeats the selected region.** It can therefore repeat the whole sample
region as well as the loop region. Conversely, ONCE can play the loop region
just once. Selecting the other pair of fields or editing the other marker pair stops playback;
press ONCE or LOOP again to hear the new selection. Dragging a large-waveform
marker now selects its corresponding pair of fields for audition.

Three controls use the word “loop,” but have different jobs:

- **LOOP below the small waveform:** repeat the currently selected audition
  region on the computer; does not enable looping in the saved preset.
- **Zoom to Loop Markers** in the waveform menu: frame the loop region.
- **LOOP mode in the channel parameters** (No Loop, Loop, Loop and Release):
  a saved playback setting for the module, not the computer's transport button.

### Reading the small END/START display

This is a magnified **join preview**, not a miniature overview of the file.
It places the selected region's final frames on the **left** (END, amber) and
its first frames on the **right** (START, teal). The centre line is where the
audio jumps when that region repeats.

Both halves share automatic visual gain so quiet waveforms remain visible
without hiding their relative levels. This affects the drawing only, not audio
volume. The colours here distinguish end from start; they do **not** indicate
which sample/loop marker pair is selected.

A large discontinuity at the join can cause a click. Zoom in, adjust the start
or end, and listen repeatedly. Similar levels and slopes across the join can
help, but the picture alone cannot guarantee a seamless loop.

## Speed, pitch, and PITCH OFFSET

The waveform toolbar's **Audition speed** slider controls preview speed from
**0.0625x** (1/16 speed) to **4x**. Drag it or type a multiplier in its box;
double-click the slider to return to **1x**. You can adjust it while playing.

**PITCH OFFSET** belongs to the selected zone and is measured in semitones.
Positive values raise pitch, negative values lower it; `+12` is an octave up,
`−12` an octave down. Fractional values such as `+3.01` are also heard in the
preview. It is separate from the channel-level **PITCH** parameter.

The **Keep pitch** checkbox determines how speed and pitch interact.

### Keep pitch on — the default

Speed changes duration without transposing the sound. Zone PITCH OFFSET then
transposes it independently, without changing that duration. “Keep pitch” means
keep speed changes from altering pitch; it does **not** disable PITCH OFFSET.

### Keep pitch off — linked pitch and speed

The preview uses ordinary sampler-style variable-speed playback. Slowing down
lowers pitch; speeding up raises it. Zone PITCH OFFSET also changes the playback
rate, so its pitch change affects duration as well.

| Keep pitch | Audition speed | Zone PITCH OFFSET | Heard pitch relative to the file | Duration relative to the selected region |
| --- | --- | --- | --- | --- |
| On | 0.5x | 0 | Unchanged | Twice as long |
| On | 0.5x | +12 | One octave higher | Twice as long |
| On | 2x | −12 | One octave lower | Half as long |
| Off | 0.5x | 0 | One octave lower | Twice as long |
| Off | 1x | +12 | One octave higher | Half as long |
| Off | 0.5x | +12 | Unchanged: the two shifts cancel | Unchanged |

With Keep pitch off, the effective rate is
`audition speed × 2^(PITCH OFFSET / 12)`. For a loop, “duration” in the table is
the time taken for one repetition.

Shortening a loop makes it repeat more frequently even at an unchanged speed;
very short loops can sound like pitched tones. The speed control does not
automatically compensate for loop-length edits.

### What the preview does and does not save

- **PITCH OFFSET is part of the zone** and is written with the preset when you
  click SAVE.
- **Audition speed and Keep pitch are temporary listening controls.** They are
  shared across channels/zones during the session, reset to 1x / on when the app
  is relaunched, and are not written into the preset or sample file.
- For a stereo pair, auditioning the master/left zone applies its PITCH OFFSET
  to both sides.
- Channel PITCH, CV modulation, envelopes, level/pan processing, and other
  module behaviour are not simulated by this preview. A control being editable
  does not mean it changes the computer audition.

Time stretching can introduce smearing or other artifacts, particularly at
extreme speeds, large transpositions, or with very short loops. Keep pitch is
an editing aid, not a promise that the module will sound identical. Make the
final musical and looping checks on your Assimil8or.

## Stereo samples

The small **L / R** buttons beside FILE choose which side of a stereo source
file that zone uses. The selected side has a teal background and dark text.
These buttons change the zone's assignment; they are not just monitor-mute
buttons.

A stereo pair uses two channel tabs, for example **CH 1-L** and **CH 2-R**.
The right companion inherits several controls from the master/left channel,
so some fields are unavailable there. Edit shared boundaries/settings from the
left/master channel. **PAN and its CV controls remain independently editable
on the right channel**; these are saved hardware settings, not an audition pan
effect. Selecting a zone on either channel selects the same zone on its partner.

Dropping a replacement sample onto either side of an existing pair updates both
assignments together. Stereo files use the left and right sides respectively;
mono files use the same mono source on both channels. Loading a stereo file into
an unpaired channel can establish a new pair only when the following channel is
available; an occupied independent neighbour is not overwritten.

**Channel Tools > Default (both channels)** on a stereo pair resets both channels' channel-level
settings and returns them to independent default modes. It preserves the samples,
zone assignments and zone markers on both sides. To clear the assignments, use
**Clear All Zones** on the left/master channel; this clears both partners but does
not delete audio files. Individual **Purge this zone** also operates on both sides.

## Zone selection voltages

**MIN VOLTAGE is a lower boundary, not a centre voltage or an exact-match target.**
The first voltage beneath each populated zone's number is that same boundary.
The teal value **in parentheses below it** is the zone's **access value**: a
read-only midpoint target to send from your external CV source, not another
saved setting. Empty zones have no voltage labels.
The `+5.00` above the zone list is its overall upper boundary. Zone 1 extends
from its MIN VOLTAGE up to +5 V; each following zone extends from its own minimum
up to the preceding zone's minimum. The last populated zone extends down to −5 V.

Assimil8or selects a zone when its assigned zone-selection CV falls within that
zone's range. It does not require a hit on the boundary. In **Gate Rise** mode,
selection is read at the gate/trigger; in **Continuous** mode it follows the CV
as it changes. Selecting a zone by CV and triggering playback are distinct:
normally patch a selection CV to the assigned **ZONES CV** input and a gate or
trigger to the channel's Gate/Trig input. See Rossum's
[operation manual, chapter 10, pages 32–34](https://cdn.shopify.com/s/files/1/0277/4548/4865/files/Assimil8or_man_040618-2.pdf?v=1785520023#page=32).
**Advance** and **Random** selection modes ignore the zone-selection CV; see the
[2.0 update guide, page 8](https://cdn.shopify.com/s/files/1/0277/4548/4865/files/A8_update_manual_061820.pdf?v=1785520029#page=8).

### Send a midpoint voltage for a reliable selection

For maximum margin to either boundary, the practical target to send from your
sequencer is **(lower boundary + upper boundary) / 2**. The parenthesized access
value calculates this for you and updates automatically when boundaries change,
including the preceding zone's boundary. This is a recommendation derived from
the ranges, not another saved zone parameter. For example, with
four evenly balanced zones across −5 V to +5 V:

| Zone | MIN VOLTAGE shown/saved | Selection range, excluding exact shared boundaries | Suggested CV to send |
| --- | --- | --- | --- |
| 1 | +2.50 V | +2.50 to +5.00 V | +3.75 V |
| 2 | 0.00 V | 0.00 to +2.50 V | +1.25 V |
| 3 | −2.50 V | −2.50 to 0.00 V | −1.25 V |
| 4 | −5.00 V | −5.00 to −2.50 V | −3.75 V |

These are example boundaries, not the automatic result of every new zone you
add. Zone **TOOLS → Balance → 10V** distributes existing zones across this full
range. Check the updated access values and adjust your external CV source after
editing boundaries or adding zones. Hover over a zone tab for its range and
access-value explanation. Targets are rounded for display, using extra decimal
places when needed to stay close to the midpoint and preserve half-steps in narrow
ranges, for example `(+0.005)` for 0.00–0.01 V. `(--)`
means there is no reliable displayed target: check for reversed, zero-width,
or extremely narrow ranges. Very narrow zones still require suitably accurate
external CV hardware.

Do **not** replace MIN VOLTAGE with the suggested CV: that moves a boundary and
changes the neighbouring zone's range. Instead, leave the desired boundaries in
the preset and send the midpoint from the external CV source. Avoid exact shared
boundaries; noise, calibration error, and source timing reduce the available
margin. In Gate Rise mode, make sure the selection CV is settled when the trigger
arrives. Midpoint targeting improves voltage margin but does not fix a late CV.

## Making consecutive slices

You can use several zones to reference different parts of one recording without
creating a separate audio file for each part.

- **Copy > next** copies the current zone's sample assignment and settings,
  including its existing sample and loop boundaries, into the following zone.
- **Continue > next** copies the assignment and settings, but starts the new
  sample region at the current SMPL END. It normally keeps the current region's
  duration and sets the new loop boundaries to match the new sample region.

Both buttons select the destination zone. If it is occupied, replacement
requires confirmation and preserves that zone's voltage boundary. If it is
empty, the source zone's voltage range is split to make room for the new zone.
Review **MIN VOLTAGE** after adding zones if precise CV selection matters.

For example, a region from `10000` to `14000` continues as `14000` to `18000`.
Adjust the new end to the next musical boundary and continue again. Near the
file's end, the final slice is shortened to fit. Continuation requires at least
four frames remaining, and creates at least a four-frame slice. Neither button
can copy onward from zone 8 or a stereo-right channel.

These are preset edits, not audio-file slicing operations. Click SAVE to keep
the resulting zone assignments.

For linked stereo channels, Copy/Continue, Insert, Paste, Flip, Explode and Clear
operate on both sides together. Each side keeps its own source file and L/R
selector; voltage boundaries stay aligned. Inserting refuses to discard an
occupied final slot on either side. Replacing occupied zones and Clear require
confirmation. Settings-only Paste preserves the target samples and L/R choices.
Pasting or assigning an unpaired mono source into an existing stereo pair puts
that source on both sides; it does not silently keep the previous right sample.
An independent, occupied next channel is never automatically made stereo-right.

### Purging a zone

Right-click the numbered zone square and choose **Purge this zone…**, then
confirm **Purge**. This clears its sample assignment and settings, not the audio
file on disk. Later zones shift up to keep the zone list consecutive, and the
last occupied zone's lower voltage boundary becomes −5 V. Check your external
CV targets after deleting a zone. Purging the only occupied zone leaves an empty
channel. Click SAVE to keep the edit; there is no dedicated purge undo.

For a stereo pair, purge from the left/master channel. The corresponding right
zone is cleared and its later zones shift in step with the left. The confirmation
explicitly identifies this. A stereo-right zone cannot be purged independently.

## Saving and moving your work

**SAVE** writes the current preset to its file in the working folder. It becomes
available when the preset differs from its loaded/saved state. Switching presets
or folders with unsaved edits can prompt you to discard them: **Continue (lose
changes)** really discards the edits. Choose **Cancel**, then SAVE, to keep them.
If saving fails, an error is shown and edits remain unsaved. MIDI setup likewise
stays open after a failed save. Preset **Move** goes to the adjacent numbered slot
(also in a filtered list); if occupied, the two presets swap. The editor and Save
target follow the moved preset. A failed move attempts to restore both originals;
if recovery needs assistance, the error gives the backup location.

The preset-level **TOOLS** menu offers import/export of **Settings Only** or
**Settings and Samples**. Use the latter when you need a portable copy that
includes the audio, rather than expecting the preset settings alone to contain
it. Settings and Samples exports a ZIP archive; extract it before copying its
contents for use on the module. When transferring a folder yourself, include
the samples referenced by its presets and preserve their filenames. Verify the
result on the module.

ZIP import asks about unsaved edits first and validates a flat archive containing
one preset and its WAV samples. Identical existing samples may be reused, but a
different sample with the same name stops the import without overwriting it.
Use a separate root folder or resolve the conflicting names before retrying.

The file browser and validation tools also offer operations such as renaming,
deletion, conversion, and removal of unused samples. These operate on files;
they are not deferred until the preset's SAVE button is clicked. Review the
scope and keep backups before using them. There is no comprehensive undo/redo
history for all application operations.

Sample assignment and file-browser import preserve source files and choose an
unused WAV name for collisions or conversions. Converted output is checked before use. The
validator also stages conversion before replacing an incompatible WAV and keeps
a recoverable hidden `.a8-original-…` sibling backup. Non-WAV originals remain at
their original paths. Assignment, file-browser import and validator conversion reject files above
192 kHz or with more than two channels, with an explanation: use an audio editor
to resample/downmix first.
The validator's missing-file Locate actions also refuse to overwrite a file that
has appeared at the destination since the scan.

Renaming keeps the original file extension if you omit it, and refuses collisions,
invalid characters and new names exceeding 47 characters for files (including
the extension) or 31 for folders. An already-overlong name can be shortened.
The browser reports failures and reopens the rename dialog for another attempt.
Renaming a file does not automatically rewrite references in saved presets;
check and repair any affected sample assignments before saving or transferring.

## Creating waveforms and CV

Choose **Waveform designer** in the top toolbar. This is a separate design
surface: switching to it does not replace the sample or preset you are editing.
It has Audio Cycle, CV/Modulation, and Layer Bank modes, with starting shapes,
shaping controls, an interactive curve editor and a visual preview.

**Create A8 files** writes a new folder containing WAV files, preset 001,
`design.json` and loading instructions. An existing folder is never overwritten.
Use **Open in Sample workspace** to inspect the result; any unsaved preset is
protected by the usual confirmation. Or copy the entire generated folder onto
your Assimil8or SD card and load preset 001 on the module.

The [Waveform Designer guide](WAVEFORM-DESIGNER.md) explains the controls,
supersaw layering, sample-length matching, DC safety, and voltage calibration.
Use **Start audition** to hear an Audio Cycle or the complete Layer Bank while
shaping it. Bank audition includes each voice's detune, phase, pan and level.
Monitor level and transpose affect listening only, not exported WAVs or presets;
the monitor repeats continuously regardless of the export playback mode.
CV/Modulation is deliberately excluded from speaker audition. New CV WAVs also
carry a purpose tag that blocks their playback in **Samples**, with disabled
ONCE/LOOP buttons and a **CV sample / Speaker audition disabled** notice. Either
side of a stereo pair being CV blocks the pair. You can still edit its markers
and preset. New CV presets default to Mix Off for individual-output use; verify
the module displays Off before connecting a mix output to speakers.

Older CV exports are recognised beside their original matching recipe; arbitrary
unmarked files or files with stripped metadata cannot reliably be detected.
See the designer guide for the protection's limits and safe monitoring advice.

The compact preview keeps a single cycle easy to recognise. **Expand waveform**
opens a larger, live-updating view; closing it does not stop audition. Leaving the
designer does stop audition. The Sample workspace still previews individual
samples rather than a whole linked bank. Before using generated audio or CV on
the module, work through the [hardware test checklist](HARDWARE-TEST-CHECKLIST.md).

## Common questions

**Why is there no sound?**  
Check that the zone has a successfully loaded sample, the selected region has
nonzero length, and ONCE or LOOP is running. Open SETTINGS to check the computer's
audio output device and routing, and check its volume. Review validation errors
if the sample is missing or cannot be loaded.
If the zone displays **CV sample / Speaker audition disabled**, silence is
intentional: inspect that file visually and use a suitable meter/scope on the
Assimil8or individual output instead of computer speakers.

**Why am I hearing the wrong part of the file?**  
Check the outlined field group beside the small waveform. Click SMPL START/END
for the sample region or LOOP START/LENGTH/END for the loop region, then restart
audition. The waveform's Loop zoom button does not select the audition source.

**Why doesn't the channel PITCH control change what I hear?**  
The preview applies zone PITCH OFFSET, not the full channel-processing chain.
Configure channel settings for the preset, then test their effect on the module.

**Why did a loop change pitch or duration?**  
Check Audition speed, Keep pitch, and the selected zone's PITCH OFFSET. With
Keep pitch off they combine to set the playback rate. Also check whether you
changed the loop length itself.

**Why does a quiet sample look large in the small display?**  
The join preview is visually normalised. Its amplitude is not a level meter and
does not indicate a volume change.

**Why are markers missing?**  
They may be outside the zoomed view. Press Fit, Zone, or Loop as appropriate.
Use the top red/blue handles for the sample and bottom amber/pink handles for the loop.

**Why is SAVE disabled after changing zoom or audition speed?**  
Those controls do not modify the preset. No preset save is needed for them.

**Why did opening a sample replace my zone's assignment?**  
Double-clicking a sample in the browser assigns it to the selected channel/zone.
Select the intended destination before loading another file.

---

This guide describes A8Manager 3.0.0. Build instructions and implementation/test
notes are in [README.md](README.md) and [UI-PREVIEW.md](UI-PREVIEW.md).
