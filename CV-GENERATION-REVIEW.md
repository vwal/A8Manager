# CV generation review

Reviewed 27 September 2026 against the current working tree. This is a design review, not a claim that hardware tests have passed.

Software verification: Debug and Release application builds succeeded, and all 25 registered tests passed in each configuration. The CV notice/disabled transports were also checked in an offscreen UI render. Hardware voltage, routing and timing checks remain outstanding.

## Scope and sources

The supplied [CV-generation discussion](../reference/cv_generation.md) describes useful workflows and opinions; it is not authoritative hardware documentation. The comparison below uses the [Rossum Assimil8or manual](../reference/Rossum_Assimil8or_manual.pdf) and Rossum's [2.0 firmware update guide](https://cdn.shopify.com/s/files/1/0277/4548/4865/files/A8_update_manual_061820.pdf?v=1785520029). Page references are the documents' printed page numbers.

## Current coverage

| Discussed idea | Status | Current behavior / remaining work |
| --- | --- | --- |
| Standalone ramps, pulses, envelopes and other CV shapes | Implemented | CV mode generates mono, DC-preserving WAVs with duration, cycles, phase, amplitude, offset, polarity, steps and drawn curves. The existing envelope is synthesized, **not** an audio envelope follower. |
| User-defined voltage range | Partial | Amplitude/offset are digital full-scale fractions. Optional measured full-scale volts provide an estimate; they do not calibrate the module or rescale the PCM data. No fixed ±5 V output mapping is assumed. |
| Whole-sample or selected-region timing | Partial | Match copies one selected file/sample/loop duration, including zone pitch offset. It does not create a relationship to the source, copy its zones, or update when markers move. |
| Regular triggers / tempo-based curves | Partial | Pulse shape, cycles, tempo and beats can generate a fixed pattern. There is no trigger-width-in-milliseconds control or live external-clock tracking. |
| Companion CV channel for an already-zoned audio sample | Missing | Export creates a new standalone preset with zone 1 covering the generated file, not a companion channel attached to existing audio zones. |
| Ramp or start/end trigger for every audio zone | Missing | Requires source-frame-aware zone mapping, explicit pulse timing, and a policy for overlapping zones and different source files. |
| Regenerate after source edits | Missing | Requires an explicit source/recipe association, stale-output detection and safe replacement workflow. |
| Envelope following from source audio | Missing | Requires source analysis plus attack/release, peak/RMS and scaling choices. |
| Keep CV out of the hardware stereo mix | Newly implemented; hardware verification pending | CV exports now write `MixLevel: -90`, the application's minimum, with the intention of selecting hardware **Off**. Confirm the module displays Off; its exact serialized sentinel is not established by the manual. Audio exports retain their existing mix-headroom settings. |
| Prevent known CV from computer speaker audition | Implemented and regression-tested | CV exports carry a purpose tag. Samples audition rejects known CV, including either side of a stereo pair. A bounded matching-recipe fallback recognizes older untagged exports. This is provenance-based protection, not automatic recognition of every CV file. |

Code references: [generation model](Source/Assimil8or/Audio/WaveformDesign.h), [renderer](Source/Assimil8or/Audio/WaveformDesign.cpp), [workspace controls](Source/GUI/WaveformWorkspace.cpp), [duration matching](Source/GUI/Assimil8or/Editor/WaveformDuration.h), [exporter](Source/Assimil8or/Audio/WaveformDesignExport.cpp), [CV identification](Source/Assimil8or/Audio/CvSampleSafety.h), and [Samples audition guard](Source/Assimil8or/Audio/AudioPlayer.cpp).

The new WAV tag survives ordinary file copying/renaming; tools that rewrite or strip metadata can remove it. Legacy fallback depends on the original `voice-01.wav` remaining beside a matching `design.json`. Untagged external files are not classified from their filename, amplitude or DC content. The guard does not control other players or the hardware.

## Hardware facts that affect the design

### Electrical range and calibration

- The signal path is DC-coupled. The manual's **±5 V specification describes CV inputs**, while sample inputs accept up to ±10 V full scale; neither establishes the voltage produced by a full-scale WAV at an output (manual pp. 7–8).
- Gate/trigger rising-edge threshold is 1.6 V (p. 7). A generated trigger must cross that threshold at the receiving input under the actual pitch, gain, envelope and load conditions; a nominal digital amplitude alone is insufficient.
- Output offset can be calibrated, but signal-path gain is not calibrated. Outputs have approximately 1 kΩ impedance and their gain depends on the connected load (pp. 59–61). Measure the relevant individual output under the intended conditions. Positive full-scale measurement alone does not establish both polarities or all gain settings.
- Mix Output Level **Off** removes a channel from the stereo mix without changing its individual output. The documented numeric mix range is −89.9 to +6 dB, followed by Off at the bottom (p. 49). Verify the newly written −90 preset value on the module before relying on it for CV routing.

### Stereo/Right versus Master/Link

Stereo/Right follows its master channel's trigger/gate and parameter settings, except pan; the 2.0 guide also permits an independent sample assignment on the right. Therefore, a single two-channel WAV is **not** a hardware requirement for an audio/CV pair (manual p. 29; 2.0 guide pp. 5–6, 9).

However, Stereo/Right does not document an independent Mix Output Level exception. Shared level, envelope and modulation can also change the CV's amplitude or timing. Do not promise that audio can remain in the stereo mix while its Stereo/Right CV partner is independently muted there. Current [channel editing](Source/GUI/Assimil8or/Editor/ChannelEditor.cpp) disables Mix Level on Stereo/Right, consistent with the shared-parameter model.

Possible designs to test:

1. **Master + Link companion:** shared triggering with independently configurable CV mix mute, level and envelope. Explicitly copy relevant zone-selection, marker and pitch settings; verify synchronization on hardware. Link alone does not establish all those relationships.
2. **Stereo/Right companion:** retain the hardware's shared timing/parameters, use individual outputs, and keep the pair out of the stereo mix if necessary. Explain the consequences of inherited amplitude/envelope settings.

The application's [ordinary sample assignment](Source/GUI/Assimil8or/Editor/EditManager.cpp) deliberately redirects a drop on Stereo/Right to the left/master and replaces both sides. A companion operation needs a dedicated assignment path; ordinary drag/drop is not an independent-right-channel authoring interface.

## Timing and zone mapping requirements

### Preserve source-frame alignment

A companion should derive timing from the original source sample rate and integer frame positions. The existing Match control returns a **zone-pitch-adjusted duration**; using that to generate a companion that then inherits the same pitch would apply the timing adjustment twice. Audition speed and monitor transpose must never enter exported timing.

Use explicit end-exclusive ranges `[start, end)` throughout generation and document the hardware mapping. Keep source audio unchanged. A companion recipe should record source identity, sample rate, frame count, zone boundaries, pitch/selection settings and generation parameters so later edits can be detected.

### Overlapping zones are not one unambiguous timeline

Two overlapping zones can demand different ramp values or trigger events at the same source frame. Zones can also reference different audio files. One shared CV WAV cannot necessarily satisfy all such requests. Choose and document a policy: for example, a separate CV file per zone with matching marker offsets, or reject conflicting shared-timeline requests. Do not silently overwrite one zone's curve with another's.

### Pulses need explicit boundary semantics

- A start pulse needs a defined low baseline, amplitude and width. Repeated playback must provide a low interval so each iteration has a new rising edge.
- In a range ending at frame `N`, the last played frame is `N−1`. A pulse contained in `[N−width, N)` starts **before** the audio ends. A pulse beginning at `N` is not played without extending the range, which changes timing.
- Decide whether an end event should precede the boundary, extend beyond it, or use a separate scheduling convention. Do not label these alternatives sample-exact equivalents.
- Pitch changes alter the duration of a baked pulse. Hardware interpolation, envelope/level processing and gate delay also affect the usable edge. The 2.0 guide p. 5 documents a global 0–5 ms gate delay; measure any self-triggering patch rather than assuming zero latency.

### Retriggering and zone selection are separate

A trigger alone does not imply “play the next zone.” **Advance** is already exposed by the application and documented in the 2.0 guide p. 8: each new trigger advances through zones, wrapping after zone 8. Gate Rise instead selects according to the assigned zone-selection CV; other selection modes have different behavior (manual pp. 32–34). The companion workflow must state which behavior it configures.

Hardware attack affects playback; release acts in gated/latch operation, while looped One Shot can continue after the gate falls (manual pp. 35–36). These settings are material for ramps, sustained DC and trigger amplitude—not merely audio presentation choices.

## Recommended implementation sequence

1. **Complete hardware verification of the safeguards:** automated checks cover CV metadata, copies/imports, legacy recognition and both stereo sides. Verify exported Mix Off and individual-output behavior on hardware. Observe the documented limitations for untagged/metadata-stripped external files.
2. **Add an explicit companion workflow:** choose an available channel, routing model and zone-selection policy; show affected channels before changing them. Preserve source files and write new, uniquely named CV outputs.
3. **Start with per-zone ramps and start triggers:** retain exact source-frame alignment, specify overlap handling and expose pulse width/baseline. Add source/recipe association and a clear “out of date” state before offering regeneration.
4. **Add end-trigger/self-chaining options only with defined timing semantics:** test low gaps, threshold, Advance behavior, pitch changes and gate delay with the module and a scope.
5. **Add envelope following later:** specify analysis and scaling controls. Treat real-time clock-following as a separate feature; tempo/beat-based file generation is a fixed recording, not a clock-synchronized processor.

No companion-generation or envelope-following implementation is proposed as already completed by this report. Use the existing [hardware checklist](HARDWARE-TEST-CHECKLIST.md) for standalone designs, extending it when companion features are implemented.
