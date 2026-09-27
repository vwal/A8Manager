#include "Assimil8or/Audio/WaveformAudition.h"
#include <atomic>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    using namespace WaveformDesign;
    void check (bool passed, const char* message) { if (! passed) throw std::runtime_error (message); }

    WaveformAudition::PayloadPtr payloadFor (const Settings& settings)
    {
        Render rendered;
        check (render (settings, rendered).wasOk (), "Render audition fixture");
        WaveformAudition::PayloadPtr payload;
        check (WaveformAudition::preparePayload (settings, rendered, payload).wasOk () && payload != nullptr, "Prepare immutable audition fixture");
        return payload;
    }

    juce::AudioBuffer<float> collect (WaveformAudition& player, int frames, int channels = 2)
    {
        juce::AudioBuffer<float> output (channels, frames);
        output.clear ();
        for (int offset { 0 }; offset < frames; offset += 257)
            player.process ({ &output, offset, std::min (257, frames - offset) });
        return output;
    }

    double frequency (const juce::AudioBuffer<float>& audio, int channel, double sampleRate)
    {
        double first { -1.0 }, last { -1.0 };
        int crossings { 0 };
        for (int frame { 1 }; frame < audio.getNumSamples (); ++frame)
        {
            const auto before { audio.getSample (channel, frame - 1) }, after { audio.getSample (channel, frame) };
            if (before <= 0.0f && after > 0.0f)
            {
                last = frame - 1.0 - before / (after - before);
                if (first < 0.0) first = last;
                ++crossings;
            }
        }
        return crossings > 1 ? (crossings - 1) * sampleRate / (last - first) : 0.0;
    }

    double spectralAmplitude (const juce::AudioBuffer<float>& audio, int channel, double hz, double sampleRate)
    {
        std::complex<double> sum {};
        for (int frame { 0 }; frame < audio.getNumSamples (); ++frame)
        {
            const auto angle { -juce::MathConstants<double>::twoPi * hz * frame / sampleRate };
            sum += static_cast<double> (audio.getSample (channel, frame)) * std::complex<double> (std::cos (angle), std::sin (angle));
        }
        return 2.0 * std::abs (sum) / audio.getNumSamples ();
    }

    void checkBounded (const juce::AudioBuffer<float>& audio)
    {
        for (int channel { 0 }; channel < audio.getNumChannels (); ++channel)
            for (int frame { 0 }; frame < audio.getNumSamples (); ++frame)
                check (std::isfinite (audio.getSample (channel, frame)) && std::abs (audio.getSample (channel, frame)) <= 0.980001f, "Audition output is finite and safety-bounded");
    }
}

void testWaveformAudition ()
{
    using namespace WaveformDesign;
    auto sine { startingPoint (Mode::oscillator, Shape::sine) };
    WaveformAudition player;
    check (player.start ().failed () && ! player.isActive () && ! player.isReady (), "Unprepared audition cannot start");
    player.prepareToPlay (48000.0);
    check (player.start ().failed (), "No-payload audition cannot start");
    const auto clean { payloadFor (sine) };
    check (clean->getCycleFrames () == 512 && clean->getVoiceCount () == 1 && clean->getSettings ().mode == Mode::oscillator, "Prepared payload retains source identity and dimensions");
    player.setPayload (clean);
    check (player.isReady () && ! player.isActive (), "Publishing a valid payload never auto-starts playback");
    check (player.start ().wasOk (), "Explicit start begins audio monitoring");
    const auto attack { collect (player, 1) };
    check (attack.getMagnitude (0, 1) < 0.001f, "Monitor starts through a low-level ramp");
    collect (player, 12000);
    auto output { collect (player, 24000) };
    check (std::abs (frequency (output, 0, 48000.0) - 93.75) < 0.02, "Raw source-rate/cycle-length frequency is not secretly transposed");
    check (std::abs (output.getRMSLevel (0, 0, output.getNumSamples ()) - 0.8 * std::pow (10.0, -18.0 / 20.0) * 0.5) < 0.002, "Default monitor is -18 dB with equal-power centre pan");
    checkBounded (output);

    player.prepareToPlay (44100.0);
    check (! player.isActive () && player.isReady (), "Device-rate changes stop playback but retain readiness for explicit restart");
    check (player.start ().wasOk (), "Restart after device-rate change");
    collect (player, 10000);
    output = collect (player, 22050);
    check (std::abs (frequency (output, 0, 44100.0) - 93.75) < 0.02, "Source/device conversion preserves frequency at 44.1 kHz");
    check (player.setTransposeSemitones (12.0).wasOk (), "Monitor-only octave transpose is accepted");
    collect (player, 10000);
    output = collect (player, 22050);
    check (std::abs (frequency (output, 0, 44100.0) - 187.5) < 0.03 && clean->getSettings ().phaseDegrees == sine.phaseDegrees,
           "Monitor transpose changes pitch without changing the prepared design");
    player.stopImmediately ();
    check (! player.isActive (), "Immediate sample takeover stops monitor without a trailing ramp");

    sine.sampleRate = 96000.0;
    player.setPayload (payloadFor (sine));
    player.setTransposeSemitones (0.0);
    check (player.start ().wasOk (), "96 kHz source starts on 44.1 kHz device");
    collect (player, 10000);
    output = collect (player, 22050);
    check (std::abs (frequency (output, 0, 44100.0) - 187.5) < 0.03, "Different source rate is applied exactly once");
    player.setPlaying (false);
    check (player.isActive (), "Normal stop retains ownership during its fade");
    const auto release { collect (player, 2048) };
    check (! player.isActive () && release.getMagnitude (1024, 1024) == 0.0f, "Stop finishes its ramp and clears the remaining block tail");
    check (player.start ().wasOk (), "Stopped monitor restarts only on request");
    player.stopImmediately ();

    auto layers { startingPoint (Mode::layers, Shape::sine) };
    layers.voiceCount = 2;
    layers.voices[0] = { -1200.0, 0.0, -1.0, 1.0 };
    layers.voices[1] = { 1200.0, 90.0, 1.0, 0.5 };
    player.prepareToPlay (48000.0);
    player.setMonitorGain (1.0);
    player.setPayload (payloadFor (layers));
    check (player.start ().wasOk (), "Detuned stereo bank starts");
    collect (player, 24000);
    output = collect (player, 48000);
    check (std::abs (frequency (output, 0, 48000.0) - 46.875) < 0.03 && std::abs (frequency (output, 1, 48000.0) - 187.5) < 0.03, "Independent detune and hard pan are audible on the correct outputs");
    check (std::abs (output.getMagnitude (0, 0, output.getNumSamples ()) - 0.4) < 0.01
           && std::abs (output.getMagnitude (1, 0, output.getNumSamples ()) - 0.2) < 0.01, "Baked voice levels are not applied twice; summing reserves voice-count headroom");
    checkBounded (output);

    auto maximum { startingPoint (Mode::layers, Shape::pulse) };
    maximum.voiceCount = 8;
    maximum.amplitude = 1.0;
    maximum.harmonics = 255;
    for (auto& voice : maximum.voices) voice = { 0.0, 0.0, -1.0, 1.0 };
    player.stopImmediately ();
    player.setPayload (payloadFor (maximum));
    check (player.start ().wasOk (), "All eight full-level voices can be monitored");
    collect (player, 24000);
    const auto eightVoices { collect (player, 4096) };
    checkBounded (eightVoices);
    check (eightVoices.getMagnitude (0, 0, 4096) > 0.5f && eightVoices.getMagnitude (1, 0, 4096) < 0.000001f,
           "Maximum-voice stress produces bounded audio on the specified side");
    maximum.voiceCount = 1;
    player.stopImmediately ();
    player.setPayload (payloadFor (maximum));
    check (player.start ().wasOk (), "Single-voice headroom comparison starts");
    collect (player, 24000);
    const auto oneVoice { collect (player, 4096) };
    for (int frame { 0 }; frame < 4096; ++frame)
        check (std::abs (oneVoice.getSample (0, frame) - eightVoices.getSample (0, frame)) < 0.000001f,
               "Eight coherent voices have the same level as one, not eight times its level");

    auto cancelling { startingPoint (Mode::layers, Shape::sine) };
    cancelling.voiceCount = 8;
    for (size_t voice { 0 }; voice < cancelling.voices.size (); ++voice)
        cancelling.voices[voice] = { 0.0, voice % 2 == 0 ? 0.0 : 180.0, 0.0, 1.0 };
    player.stopImmediately ();
    player.setPayload (payloadFor (cancelling));
    check (player.start ().wasOk (), "Opposite-phase voice bank starts");
    output = collect (player, 4096);
    check (output.getMagnitude (0, 4096) < 0.000001f, "Baked phases preserve coherent cancellation without being applied a second time");

    auto quiet { startingPoint (Mode::oscillator, Shape::sine) };
    quiet.amplitude = 0.0;
    quiet.offset = 0.9;
    player.stopImmediately ();
    player.setPayload (payloadFor (quiet));
    check (player.start ().wasOk (), "Audio design with DC offset remains safe to monitor");
    output = collect (player, 4800);
    check (output.getMagnitude (0, output.getNumSamples ()) < 0.000001f, "Monitor preparation removes DC, including offset start transients");

    // A synthetic high partial tests the actual resampler filter rather than
    // relying on the model's own harmonic setting to remove the alias first.
    auto high { startingPoint (Mode::oscillator, Shape::sine) };
    high.cycleFrames = 64;
    Render raw;
    raw.sampleRate = high.sampleRate;
    raw.frames = high.cycleFrames;
    raw.voices.emplace_back (1, high.cycleFrames);
    for (int frame { 0 }; frame < high.cycleFrames; ++frame)
    {
        const auto phase { juce::MathConstants<double>::twoPi * frame / high.cycleFrames };
        raw.voices[0].setSample (0, frame, static_cast<float> (0.2 * std::sin (phase) + 0.7 * std::sin (20 * phase)));
    }
    WaveformAudition::PayloadPtr filtered;
    check (WaveformAudition::preparePayload (high, raw, filtered).wasOk (), "Prepare known high-frequency periodic source");
    raw.voices[0].clear ();
    player.stopImmediately ();
    player.setPayload (filtered);
    player.setTransposeSemitones (12.0);
    check (player.start ().wasOk (), "Band-limited monitor at one octave up");
    collect (player, 12000);
    output = collect (player, 4096);
    const auto fundamental { spectralAmplitude (output, 0, 1500.0, 48000.0) };
    const auto alias { spectralAmplitude (output, 0, 18000.0, 48000.0) };
    check (fundamental > 0.1 && alias < fundamental * 0.0001, "Mipmap preparation owns its copy and suppresses a partial that would fold 30 kHz into 18 kHz");

    auto native { startingPoint (Mode::oscillator, Shape::sine) };
    Render bright;
    bright.sampleRate = native.sampleRate;
    bright.frames = native.cycleFrames;
    bright.voices.emplace_back (1, native.cycleFrames);
    for (int frame { 0 }; frame < native.cycleFrames; ++frame)
    {
        const auto phase { juce::MathConstants<double>::twoPi * frame / native.cycleFrames };
        bright.voices[0].setSample (0, frame, static_cast<float> (0.2 * std::sin (phase) + 0.6 * std::sin (200 * phase)));
    }
    WaveformAudition::PayloadPtr brightPayload;
    check (WaveformAudition::preparePayload (native, bright, brightPayload).wasOk (), "Prepare bright native-rate source");
    player.stopImmediately ();
    player.setTransposeSemitones (0.0);
    player.setPayload (brightPayload);
    check (player.start ().wasOk (), "Native-rate high-harmonic fixture starts");
    collect (player, 12000);
    output = collect (player, 4096);
    check (spectralAmplitude (output, 0, 18750.0, 48000.0) > 0.39,
           "A valid high native-rate harmonic is retained instead of being lost to an octave-wide mip blend");

    auto upper { high };
    upper.sampleRate = 96000.0;
    player.stopImmediately ();
    player.setPayload (payloadFor (upper));
    check (player.setTransposeSemitones (48.0).failed () && ! player.isReady () && player.start ().failed (),
           "A 24 kHz fundamental is rejected even on a 48 kHz device");
    player.prepareToPlay (32000.0);
    check (player.setTransposeSemitones (42.0).failed () && player.start ().failed (),
           "Fundamentals below 20 kHz but above the device Nyquist limit are rejected");
    player.setTransposeSemitones (0.0);
    auto mixedRange { startingPoint (Mode::layers, Shape::sine) };
    mixedRange.cycleFrames = 64;
    mixedRange.voiceCount = 2;
    mixedRange.voices[0].detuneCents = 0.0;
    mixedRange.voices[1].detuneCents = 2400.0;
    player.prepareToPlay (8000.0);
    player.setPayload (payloadFor (mixedRange));
    check (player.start ().wasOk (), "Every voice below the device ceiling is accepted");
    mixedRange.sampleRate = 96000.0;
    player.setPayload (payloadFor (mixedRange));
    collect (player, 1024);
    check (! player.isActive () && ! player.isReady () && player.start ().failed (),
           "One out-of-range detuned voice safely stops the whole bank");
    player.prepareToPlay (48000.0);

    player.stopImmediately ();
    player.setTransposeSemitones (0.0);
    sine = startingPoint (Mode::oscillator, Shape::sine);
    sine.phaseDegrees = 90.0;
    player.setPayload (payloadFor (sine));
    check (player.start ().wasOk (), "Start live-update fixture");
    auto before { collect (player, 12345) };
    const auto last { before.getSample (0, before.getNumSamples () - 1) };
    sine.invert = true;
    player.setPayload (payloadFor (sine));
    auto transition { collect (player, 2048) };
    double largestStep { std::abs (transition.getSample (0, 0) - last) };
    for (int frame { 1 }; frame < transition.getNumSamples (); ++frame)
        largestStep = std::max (largestStep, std::abs (static_cast<double> (transition.getSample (0, frame)) - transition.getSample (0, frame - 1)));
    check (largestStep < 0.03, "Phase-inverting a live waveform crossfades instead of hard-switching the source");
    player.setMonitorGain (0.0);
    output = collect (player, 4800);
    check (output.getMagnitude (4000, 800) < 0.000001f, "Monitor gain changes smoothly to silence");

    auto subAudio { startingPoint (Mode::oscillator, Shape::sine) };
    subAudio.cycleFrames = 8192;
    player.setPayload (payloadFor (subAudio));
    collect (player, 2048);
    check (! player.isActive () && ! player.isReady () && player.start ().failed (), "Inaudible low-frequency source stops and cannot start accidentally");
    check (player.setTransposeSemitones (24.0).wasOk () && player.isReady () && ! player.isActive (), "Returning to an audible monitor range requires explicit restart");
    check (player.start ().wasOk (), "Explicitly transposed low-frequency audio cycle can be monitored");
    check (player.setTransposeSemitones (std::numeric_limits<double>::infinity ()).failed (), "Nonfinite monitor pitch is rejected");
    collect (player, 2048);
    check (! player.isActive (), "Invalid monitor pitch stops playback");
    player.setTransposeSemitones (0.0);
    player.setPayload (clean);
    check (player.isReady () && ! player.isActive (), "Valid new design does not resume after a safety stop");

    auto cv { startingPoint (Mode::modulation, Shape::sine) };
    Render cvRender;
    check (render (cv, cvRender).wasOk (), "Render modulation fixture without speaker routing");
    auto rejected { clean };
    check (WaveformAudition::preparePayload (cv, cvRender, rejected).failed () && ! rejected, "CV payload is rejected and cannot retain stale playable audio");
    cv.sampleRate = 1.0e100;
    check (WaveformAudition::preparePayload (cv, cvRender, rejected).failed () && ! rejected, "CV stays blocked even with arbitrary invalid rates");
    player.setPayload (nullptr);
    check (! player.isReady () && player.start ().failed (), "Clearing payload invalidates monitor readiness");
    player.setPayload (clean);
    player.prepareToPlay (std::numeric_limits<double>::quiet_NaN ());
    check (player.start ().failed (), "Invalid device rates cannot produce runaway phases");
    player.prepareToPlay (48000.0);
    player.setMonitorGain (1.0);
    check (player.start ().wasOk (), "Restart buffer-boundary fixture");
    juce::AudioBuffer<float> region (4, 1000);
    for (int c { 0 }; c < 4; ++c) for (int i { 0 }; i < 1000; ++i) region.setSample (c, i, 0.5f);
    check (player.process ({ &region, 13, 700 }), "Active engine owns its designated output region");
    for (int c { 0 }; c < 4; ++c)
    {
        check (region.getSample (c, 12) == 0.5f && region.getSample (c, 713) == 0.5f, "Samples outside the active output slice remain untouched");
        if (c > 1) check (region.getMagnitude (c, 13, 700) == 0.0f, "Unused output channels are silent");
    }
    checkBounded (region);
    checkBounded (collect (player, 4096, 1));

    // Publish several revisions while the first crossfade is still in flight.
    // Only the latest pending design should become the next audible source.
    player.stopImmediately ();
    player.setPayload (clean);
    check (player.start ().wasOk (), "Rapid-publication fixture starts");
    collect (player, 2048);
    auto revision { startingPoint (Mode::oscillator, Shape::sine) };
    revision.cycleFrames = 128;
    player.setPayload (payloadFor (revision));
    collect (player, 32);
    for (const auto frames : { 256, 1024, 64 })
    {
        revision.cycleFrames = frames;
        player.setPayload (payloadFor (revision));
    }
    collect (player, 12000);
    output = collect (player, 24000);
    check (player.isActive () && std::abs (frequency (output, 0, 48000.0) - 750.0) < 0.03,
           "Rapid updates preserve the newest pending waveform through an active crossfade");
    checkBounded (output);

    // Track the final ownership release rather than merely counting references.
    // Retired sources must be released by publication/destruction off callback.
    std::atomic<int> retired { 0 }, callbackRetirements { 0 };
    bool inCallback { false };
    auto tracked = [&] ()
    {
        auto owner { payloadFor (startingPoint (Mode::oscillator, Shape::sine)) };
        const auto* rawPointer { owner.get () };
        return WaveformAudition::PayloadPtr (rawPointer, [owner = std::move (owner), &retired, &callbackRetirements, &inCallback] (const WaveformAudition::Payload*) mutable
        {
            if (inCallback) ++callbackRetirements;
            ++retired;
            owner.reset ();
        });
    };
    {
        WaveformAudition ownership;
        ownership.prepareToPlay (48000.0);
        ownership.setPayload (tracked ());
        check (ownership.start ().wasOk (), "Ownership fixture starts");
        juce::AudioBuffer<float> block (2, 2048);
        for (int update { 0 }; update < 4; ++update)
        {
            inCallback = true;
            ownership.process ({ &block, 0, 2048 });
            inCallback = false;
            ownership.setPayload (tracked ());
        }
        check (retired.load () > 0 && callbackRetirements.load () == 0, "Repeated live swaps retire large buffers only outside the callback");
        ownership.stopImmediately ();
    }
    check (callbackRetirements.load () == 0, "Shutdown also retires sources outside the callback");
    std::cout << "PASS: continuous waveform audition, rates/detune/pan, band-limited resampling, ramps, monitor protection and callback-safe ownership\n";
}
