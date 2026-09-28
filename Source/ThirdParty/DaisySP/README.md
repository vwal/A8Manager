# DaisySP PolyBLEP excerpt

Upstream: https://github.com/daisyaudio/DaisySP

Pinned revision: `2c72eaf9eac5fc0dca1919d65d606da907832618`.
Source: `Source/Synthesis/oscillator.cpp`, the `Polyblep` function.
`LICENSE.txt` preserves the complete upstream MIT license/attribution file.

Only this stateless polynomial correction is vendored. Local changes are a
standalone inline header, an application-specific namespace/name, and double
precision to match the offline waveform renderer. No full DaisySP library,
hardware dependencies or optional DaisySP-LGPL components are included.

Audio Cycle and Layer Bank apply the correction to saw/pulse discontinuities
before drive/fold and the existing oversampled FFT harmonic filter. The saw keeps
A8Manager's ascending polarity; pulse keeps its existing gain and duty cycle.
Other shapes retain their existing FFT-filtered generation, and audition retains
pitch-dependent band-limited tables. CV generation does not use this correction:
its deliberate steps and DC levels must remain intact.

The upstream stateful oscillator is intentionally not used: single-cycle export
needs deterministic phase and no integrator startup history. This excerpt does
not promise alias-free nonlinear shaping or playback at every hardware pitch.

CMake includes the license in the macOS app bundle and beside Windows/Linux
executables. Updating the pin requires reviewing the helper and license together
and rerunning waveform generation, export, audition and CV regression tests.
