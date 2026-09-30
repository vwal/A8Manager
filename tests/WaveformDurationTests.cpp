#include "GUI/Assimil8or/Editor/WaveformDuration.h"
#include <iostream>
#include <limits>
#include <stdexcept>

void testWaveformDuration ()
{
    ZoneProperties zone;
    SampleProperties sample;
    zone.setSample ("source.wav", false);
    sample.setStatus (SampleStatus::exists, false);
    sample.setSampleRate (48000.0, false);
    sample.setLengthInSamples (480000, false);
    zone.setPitchOffset (0.0, false);
    zone.setSampleStart (-1, false);
    zone.setSampleEnd (-1, false);
    zone.setLoopStart (-1, false);
    zone.setLoopLength (-1.0, false);
    auto check = [] (bool ok, const char* message) { if (! ok) throw std::runtime_error (message); };
    auto expect = [&] (int region, double seconds)
    {
        const auto value { WaveformDuration::selected (zone, sample, region) };
        check (value.has_value () && std::abs (*value - seconds) < 1.0e-9, "Matched duration must preserve frames, sample rate and pitch");
    };
    expect (0, 10.0);
    expect (1, 10.0);
    expect (2, 10.0);
    zone.setSampleStart (48000, false);
    zone.setSampleEnd (144000, false);
    expect (2, 2.0); // Both unset loop bounds follow the selected sample.
    zone.setLoopStart (72000, false);
    expect (2, 1.5); // An unset length ends at Sample End, not EOF.
    zone.setLoopLength (48000.5, false);
    expect (1, 2.0);
    expect (2, 48000.5 / 48000.0);
    zone.setLoopStart (240000, false);
    check (! WaveformDuration::selected (zone, sample, 2), "A detached loop cannot supply a playable duration");
    const auto external { WaveformDuration::selected (zone, sample, 2, 0.0, true) };
    check (external && std::abs (*external - 48000.5 / 48000.0) < 1.0e-9, "Advanced channels can match the independent loop duration");
    zone.setLoopStart (40000, false);
    check (! WaveformDuration::selected (zone, sample, 2), "Loop Start before Sample Start cannot supply a duration");
    zone.setLoopStart (72000, false);
    zone.setLoopLength (3.99, false);
    check (! WaveformDuration::selected (zone, sample, 2), "A sub-four-frame loop cannot supply a playable duration");
    check (! WaveformDuration::selected (zone, sample, 2, 0.0, true), "Advanced duration matching still requires four loop frames");
    zone.setLoopLength (4.5, false);
    expect (2, 4.5 / 48000.0);
    zone.setLoopLength (48000.5, false);
    zone.setPitchOffset (12.0, false);
    expect (0, 5.0);
    expect (1, 1.0);
    expect (2, 48000.5 / 96000.0);
    zone.setPitchOffset (-12.0, false);
    expect (1, 4.0);
    auto combined = [&] (double channelPitch, double seconds)
    {
        const auto value { WaveformDuration::selected (zone, sample, 1, channelPitch) };
        check (value && std::abs (*value - seconds) < 1.0e-9, "Matching duration uses combined channel and zone pitch at the hardware rate ceiling");
    };
    combined (12.0, 2.0);
    zone.setPitchOffset (12.0, false);
    combined (12.0, 0.5);
    zone.setPitchOffset (60.0, false);
    combined (60.0, 2.0 / 64.0); // 48 kHz: +72 st, not +120.
    sample.setSampleRate (96000.0, false);
    combined (60.0, 1.0 / 32.0); // Same maximum physical read rate.
    sample.setSampleRate (192000.0, false);
    combined (60.0, 0.5 / 16.0);
    sample.setSampleRate (48000.0, false);
    zone.setPitchOffset (-96.0, false);
    combined (0.0, 512.0);
    check (! WaveformDuration::selected (zone, sample, 1, std::numeric_limits<double>::quiet_NaN ()), "Invalid channel pitch cannot supply a duration");
    zone.setSampleEnd (48000, false);
    check (! WaveformDuration::selected (zone, sample, 1), "Empty region cannot supply a duration");
    zone.setLoopLength (480001, false);
    check (! WaveformDuration::selected (zone, sample, 2), "Out-of-file markers cannot supply a duration");
    sample.setSampleRate (std::numeric_limits<double>::quiet_NaN (), false);
    check (! WaveformDuration::selected (zone, sample, 0), "Invalid sample rate must be rejected");
    sample.setSampleRate (48000.0, false);
    sample.setStatus (SampleStatus::uninitialized, false);
    check (! WaveformDuration::selected (zone, sample, 0), "Unloaded sample cannot supply a duration");
    sample.setStatus (SampleStatus::exists, false);
    zone.setSample ("", false);
    check (! WaveformDuration::selected (zone, sample, 0), "Purged zone cannot use a stale cache");
    check (! WaveformDuration::selected (zone, sample, 3), "Unknown duration selector must be rejected");
    zone.setSample ("tiny.wav", false);
    zone.setSampleStart (-1, false); zone.setSampleEnd (-1, false);
    zone.setLoopStart (-1, false); zone.setLoopLength (-1, false);
    zone.setPitchOffset (0.0, false);
    sample.setLengthInSamples (3, false);
    expect (0, 3.0 / 48000.0); expect (1, 3.0 / 48000.0);
    check (! WaveformDuration::selected (zone, sample, 2), "Tiny files retain SAMPLE duration without inventing a four-frame loop");
    sample.setLengthInSamples (300, false);
    zone.setSampleStart (100, false); zone.setSampleEnd (200, false);
    expect (2, 100.0 / 48000.0);
    std::cout << "PASS: combined pitch and hardware-capped file/sample/loop matching, fractional contained loops, implicit sample bounds, tiny samples and invalid data\n";
}
