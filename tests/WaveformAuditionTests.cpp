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

    void testProtectedSignalInspection ()
    {
        using Status = AuditionSignalCheck::Status;
        for (const auto shape : { Shape::sine, Shape::saw, Shape::pulse })
        {
            auto settings { startingPoint (Mode::oscillator, shape) };
            settings.amplitude = 1.0;
            settings.brightness = 1.0;
            settings.harmonics = 1024;
            settings.pulseWidth = 0.2;
            for (const auto transpose : { -12.0, 0.0, 12.0 })
            {
                const auto report { WaveformAudition::inspectSignal (payloadFor (settings), transpose) };
                check (report.status == Status::clear && ! report.needsConfirmation () && report.channels.size () == 2
                       && report.durationSeconds == 1.0, "Normal full-scale sine/saw/pulse cycles do not trigger speculative safety warnings");
                for (const auto& channel : report.channels)
                    check (std::isfinite (channel.peak) && channel.peak <= 0.980001 && std::abs (channel.mean) < 0.02,
                           "Signal inspection measures the monitor's bounded, DC-protected stereo output");
                if (shape == Shape::sine && transpose == 0.0)
                    check (report.channels[0].rms > 0.4, "Inspection uses unity monitor gain, not the quiet default monitor level");
            }
        }

        auto shifted { startingPoint (Mode::oscillator, Shape::imported) };
        shifted.importedCycleName = "DC-offset audio cycle";
        shifted.importedCycle.resize (512);
        for (size_t frame { 0 }; frame < shifted.importedCycle.size (); ++frame)
            shifted.importedCycle[frame] = 0.6 + 0.2 * std::sin (juce::MathConstants<double>::twoPi * static_cast<double> (frame) / shifted.importedCycle.size ());
        shifted.amplitude = 1.0;
        shifted.brightness = 1.0;
        shifted.harmonics = 1024;
        shifted.offset = 0.45; // Deliberate exported DC, removed only in monitoring.
        Render source;
        check (render (shifted, source).wasOk () && source.dc > 0.44, "Offset fixture really contains substantial DC before monitor protection");
        WaveformAudition::PayloadPtr shiftedPayload;
        check (WaveformAudition::preparePayload (shifted, source, shiftedPayload).wasOk (), "Prepare imported shifted-DC monitor payload");
        const auto shiftedReport { WaveformAudition::inspectSignal (shiftedPayload, 0.0) };
        check (shiftedReport.status == Status::clear && shiftedReport.channels.size () == 2,
               "Removed source DC does not cause a warning about audio that never reaches the monitor");
        for (const auto& channel : shiftedReport.channels)
            check (std::abs (channel.mean) < 0.002 && channel.rms > 0.05, "The inspected imported cycle remains audible while actual monitored DC is negligible");

        const auto clean { payloadFor (startingPoint (Mode::oscillator, Shape::sine)) };
        const auto missing { WaveformAudition::inspectSignal ({}, 0.0) };
        check (missing.isBlocked () && ! missing.needsConfirmation () && missing.channels.empty ()
               && missing.explanation ().contains ("valid audio waveform"), "Missing payload is a hard block with the real engine start failure");
        auto cv { startingPoint (Mode::modulation, Shape::sine) };
        Render cvRender;
        check (render (cv, cvRender).wasOk (), "Render wrong-mode fixture without playing it");
        WaveformAudition::PayloadPtr rejected { clean };
        check (WaveformAudition::preparePayload (cv, cvRender, rejected).failed () && ! rejected,
               "CV mode cannot create an inspectable speaker-audition payload");
        check (WaveformAudition::inspectSignal (rejected, 0.0).isBlocked (), "A failed CV payload cannot become a warning that the user could override");
        for (const auto transpose : { std::numeric_limits<double>::quiet_NaN (), -49.0, 73.0 })
        {
            const auto report { WaveformAudition::inspectSignal (clean, transpose) };
            check (report.isBlocked () && report.channels.empty () && report.explanation ().contains ("Monitor transpose"),
                   "Invalid/hardware-out-of-range transpose returns the actual setter error without rendering");
        }
        for (const int frames : { 64, 8192 })
        {
            auto settings { startingPoint (Mode::oscillator, Shape::sine) };
            settings.cycleFrames = frames;
            const auto transpose { frames == 64 ? 60.0 : 0.0 };
            const auto report { WaveformAudition::inspectSignal (payloadFor (settings), transpose) };
            check (report.isBlocked () && ! report.needsConfirmation () && report.explanation ().contains ("20 Hz")
                   && report.explanation ().contains ("20000"), "Monitor frequency restrictions stay non-overridable hard blocks in inspection");
        }

        WaveformAudition live;
        live.prepareToPlay (48000.0);
        live.setPayload (clean);
        check (live.start ().wasOk (), "Start independent live-engine fixture");
        collect (live, 4800);
        juce::ignoreUnused (WaveformAudition::inspectSignal (shiftedPayload, 12.0));
        check (live.isActive () && ! live.isPausedForRange () && std::abs (frequency (collect (live, 24000), 0, 48000.0) - 93.75) < 0.03,
               "Inspection's temporary engine never alters live playback intent, payload, phase or transpose");
    }

    void testHardwareTransposeRange ()
    {
        auto settings { startingPoint (Mode::oscillator, Shape::sine) };
        check (WaveformAudition::maximumTransposeSemitones (settings) == 72.0, "48 kHz source permits the A8 total-pitch ceiling of +72 st");
        settings.sampleRate = 96000.0;
        check (WaveformAudition::maximumTransposeSemitones (settings) == 60.0, "96 kHz source permits the A8 total-pitch ceiling of +60 st");
        settings.sampleRate = 192000.0;
        check (WaveformAudition::maximumTransposeSemitones (settings) == 48.0 && validate (settings).failed (),
               "The helper documents 192 kHz's +48 st ceiling without silently adding unsupported export rates");
        settings.sampleRate = std::numeric_limits<double>::quiet_NaN ();
        check (WaveformAudition::maximumTransposeSemitones (settings) == -48.0, "Invalid source rate cannot grant upward transpose headroom");

        settings.sampleRate = 48000.0;
        settings.cycleFrames = 8192;
        const auto source48 { payloadFor (settings) };
        settings.sampleRate = 96000.0;
        const auto source96 { payloadFor (settings) };
        WaveformAudition player;
        check (player.setTransposeSemitones (72.0).wasOk () && ! player.isActive () && ! player.isPausedForRange (),
               "A missing payload permits at most the global +72 bound without arming playback");
        check (player.setTransposeSemitones (std::nextafter (72.0, 100.0)).failed (), "Even without a payload, transpose cannot exceed the global A8 bound");
        for (const auto deviceRate : { 44100.0, 96000.0 })
        {
            for (const auto source : { source48, source96 })
            {
                const auto ceiling { WaveformAudition::maximumTransposeSemitones (source->getSettings ()) };
                player.prepareToPlay (deviceRate);
                player.setPayload (source);
                check (player.setTransposeSemitones (ceiling).wasOk () && player.isReady () && player.start ().wasOk (),
                       "The source-rate hardware ceiling is accepted independently of the output-device rate");
                collect (player, 12000);
                const auto output { collect (player, static_cast<int> (deviceRate * 0.5)) };
                check (std::abs (frequency (output, 0, deviceRate) - 375.0) < 0.03,
                       "8192-frame cycles produce 375 Hz at +72/48 kHz or +60/96 kHz without changing export data");
                checkBounded (output);
                check (player.setTransposeSemitones (std::nextafter (ceiling, 100.0)).failed () && ! player.isPausedForRange (),
                       "Exceeding the current source-rate A8 ceiling is a hard control rejection, not a resumable frequency pause");
                collect (player, 2048);
                check (! player.isActive (), "A hardware-bound rejection fades safely to silence");
                check (player.setTransposeSemitones (ceiling - 12.0).wasOk () && ! player.isActive (),
                       "Returning from rejected hardware bounds cannot silently restart audition");
                check (player.start ().wasOk (), "Explicit start at a lower valid transpose remains available");
                collect (player, 12000);
                check (player.setTransposeSemitones (ceiling + 0.01).failed () && player.start ().wasOk (),
                       "A rejected hardware-overrun does not silently overwrite the last valid transpose");
                collect (player, 12000);
                check (std::abs (frequency (collect (player, static_cast<int> (deviceRate * 0.5)), 0, deviceRate) - 187.5) < 0.03,
                       "Restart after rejected input retains the prior valid pitch, not a silently clamped ceiling");
            }
        }

        auto bank { startingPoint (Mode::layers, Shape::sine) };
        bank.cycleFrames = 8192;
        bank.voiceCount = 2;
        bank.voices[0] = { -700.0, 0.0, -1.0, 1.0 };
        bank.voices[1] = { 350.0, 0.0, 1.0, 1.0 };
        bank.voices[7].detuneCents = 2400.0; // Unused voices must not consume headroom.
        check (WaveformAudition::maximumTransposeSemitones (bank) == 68.5, "Positive active-bank detune consumes total-pitch headroom; inactive voices do not");
        bank.sampleRate = 96000.0;
        check (WaveformAudition::maximumTransposeSemitones (bank) == 56.5, "Bank headroom is subtracted from the correct source-rate ceiling");
        const juce::NormalisableRange<double> detuneRange { -2400.0, 2400.0, 0.1 };
        for (const auto roundedCent : { detuneRange.snapToLegalValue (350.0), 350.00000000000045 })
        {
            bank.voices[1].detuneCents = roundedCent;
            check (WaveformAudition::maximumTransposeSemitones (bank) == 56.5,
                   "JUCE whole-cent snapping roundoff must not consume an extra transpose cent");
        }
        bank.voices[1].detuneCents = 1.00000000000045;
        check (WaveformAudition::maximumTransposeSemitones (bank) == 59.99,
               "Whole-cent snap tolerance accounts for full-range subtraction even near zero detune");
        bank.voices[1].detuneCents = 350.0000001;
        check (WaveformAudition::maximumTransposeSemitones (bank) == 56.49,
               "Fractional cents beyond floating-point range-arithmetic error still round the ceiling inward");
        bank.sampleRate = 48000.0;
        bank.voices[1].detuneCents = 350.001;
        check (WaveformAudition::maximumTransposeSemitones (bank) == 68.49, "Fractional-cent bank detune rounds the available 0.01-st ceiling inward");
        bank.voices[1].detuneCents = 350.0;
        player.prepareToPlay (48000.0);
        player.setPayload (payloadFor (bank));
        check (player.setTransposeSemitones (68.5).wasOk () && player.start ().wasOk (), "A bank starts at its reduced hardware-aware upper bound");
        collect (player, 12000);
        const auto bankAudio { collect (player, 24000) };
        check (std::abs (frequency (bankAudio, 1, 48000.0) - 375.0) < 0.03,
               "The highest bank voice's detune plus monitor transpose reaches, but never exceeds, the hardware total-pitch limit");
        check (player.setTransposeSemitones (68.51).failed () && ! player.isPausedForRange (), "A bank-aware bound is enforced by the engine, not just slider presentation");
        bank.voices[1].detuneCents = -350.0;
        check (WaveformAudition::maximumTransposeSemitones (bank) == 72.0, "Negative-only banks do not extend the nominal source-rate ceiling");
        player.setPayload (payloadFor (bank));
        check (player.setTransposeSemitones (72.0).wasOk () && ! player.isPausedForRange (), "Negative-only bank can use the nominal upper bound after explicit control correction");
        check (player.start ().wasOk (), "Negative-only bank still requires explicit Start after earlier rejection");

        player.stopImmediately ();
        player.setPayload (source48);
        player.setTransposeSemitones (72.0);
        check (player.start ().wasOk (), "Pending-rate-change fixture starts at +72 on a 48 kHz source");
        collect (player, 2048);
        player.setPayload (source96);
        check (! player.isReady () && player.isPausedForRange (), "Pending 96 kHz publication immediately invalidates +72 before the callback adopts it");
        collect (player, 2048);
        check (! player.isActive (), "Hardware-invalid pending source is never adopted for continued playback");
        player.setPayload (source48);
        check (player.isReady () && player.isPausedForRange () && ! player.isActive (), "Restoring a compatible source publication alone never resumes audio");
        check (player.setTransposeSemitones (72.0).wasOk () && player.isPausedForRange () && ! player.isActive (), "Reapplying identical valid controls does not auto-resume a source-change pause");
        player.setPayload (source96);
        check (player.start ().failed () && ! player.isPausedForRange (), "Explicit start checks the pending source's hardware cap and cancels failed-start intent");
        check (player.setTransposeSemitones (60.0).wasOk () && ! player.isActive (), "Correction after a failed pending-rate start remains idle");
        check (player.start ().wasOk (), "Corrected 96 kHz source can explicitly restart at +60");

        settings = startingPoint (Mode::oscillator, Shape::sine);
        settings.cycleFrames = 64;
        player.stopImmediately ();
        player.setPayload (payloadFor (settings));
        player.setTransposeSemitones (0.0);
        check (player.start ().wasOk (), "Short-cycle frequency-safety fixture starts");
        collect (player, 2048);
        const auto frequencyLimit { player.setTransposeSemitones (60.0) };
        check (frequencyLimit.failed () && player.isPausedForRange () && ! player.isReady ()
               && frequencyLimit.getErrorMessage ().contains ("A8 total-pitch") && frequencyLimit.getErrorMessage ().contains ("48000"),
               "A larger hardware allowance never bypasses the independent audible/device-Nyquist pause guard");
        collect (player, 2048);
        check (player.setTransposeSemitones (24.0).wasOk () && player.isActive () && ! player.isPausedForRange (),
               "Frequency-only overrun still resumes automatically on a valid transpose change");
        checkBounded (collect (player, 2048));
    }

    void testRangePause ()
    {
        auto settings { startingPoint (Mode::oscillator, Shape::sine) };
        const auto clean { payloadFor (settings) };
        WaveformAudition player;
        player.prepareToPlay (48000.0);
        player.setPayload (clean);
        const auto lower { 12.0 * std::log2 (20.0 / 93.75) };
        const auto below { std::nextafter (lower, -std::numeric_limits<double>::infinity ()) };
        check (player.setTransposeSemitones (below).failed () && ! player.isPausedForRange (), "An idle out-of-range edit cannot arm automatic resumption");
        check (player.start ().failed () && ! player.isPausedForRange (), "A failed start cannot arm automatic resumption");
        check (player.setTransposeSemitones (lower).wasOk () && player.isReady () && ! player.isActive (), "The exact 20 Hz floor is inclusive without auto-starting idle audio");
        check (player.start ().wasOk (), "An explicit start works at the exact lower boundary");
        collect (player, 4096);
        auto status { player.setTransposeSemitones (below) };
        check (status.failed () && player.isPausedForRange () && status.getErrorMessage ().contains ("20 Hz")
               && status.getErrorMessage ().contains ("st steps"), "Crossing the lower boundary pauses active audio with actionable range guidance");
        const auto paused { collect (player, 2048) };
        check (! player.isActive () && player.isPausedForRange () && paused.getMagnitude (1024, 1024) == 0.0f,
               "Range pause ramps fully to silence while preserving independent resume intent");
        check (player.setTransposeSemitones (-48.0).failed () && player.setTransposeSemitones (-48.0).failed () && player.isPausedForRange (),
               "Repeated out-of-range edits retain intent without repeatedly starting sound");
        check (player.setTransposeSemitones (lower).wasOk () && ! player.isPausedForRange () && player.isActive (),
               "Returning exactly to 20 Hz resumes an already-started audition automatically");
        collect (player, 12000);
        const auto boundaryAudio { collect (player, 48000) };
        check (std::abs (frequency (boundaryAudio, 0, 48000.0) - 20.0) < 0.01, "Resumed lower-boundary playback runs at its intended frequency");

        player.setTransposeSemitones (0.0);
        collect (player, 12000);
        auto before { collect (player, 1) };
        check (player.setTransposeSemitones (-48.0).failed (), "Quick-return fixture enters range pause");
        const auto fade { collect (player, 32) };
        check (player.setTransposeSemitones (0.0).wasOk () && ! player.isPausedForRange () && player.isActive (),
               "Returning before the fade finishes reverses it without waiting for silence");
        const auto resumed { collect (player, 1024) };
        double largest { std::abs (fade.getSample (0, 0) - before.getSample (0, 0)) };
        for (int frame { 1 }; frame < fade.getNumSamples (); ++frame)
            largest = std::max (largest, std::abs (static_cast<double> (fade.getSample (0, frame)) - fade.getSample (0, frame - 1)));
        largest = std::max (largest, std::abs (static_cast<double> (resumed.getSample (0, 0)) - fade.getSample (0, fade.getNumSamples () - 1)));
        for (int frame { 1 }; frame < resumed.getNumSamples (); ++frame)
            largest = std::max (largest, std::abs (static_cast<double> (resumed.getSample (0, frame)) - resumed.getSample (0, frame - 1)));
        check (largest < 0.005, "Fast out-and-back transpose changes remain smoothly ramped");
        player.setPlaying (false);
        check (player.setTransposeSemitones (-48.0).failed () && ! player.isPausedForRange (),
               "A pending explicit-stop ramp is not mistaken for active playback intent");
        check (player.setTransposeSemitones (0.0).wasOk (), "Return after explicit stop is a valid control edit");
        collect (player, 2048);
        check (! player.isActive () && ! player.isPausedForRange (), "An explicit stop wins over rapid range changes");

        for (const auto rate : { 8000.0, 48000.0 })
        {
            settings.cycleFrames = 64;
            settings.sampleRate = 96000.0;
            player.prepareToPlay (rate);
            player.setPayload (payloadFor (settings));
            player.setTransposeSemitones (0.0);
            check (player.start ().wasOk (), "Upper-boundary fixture starts");
            collect (player, 2048);
            const auto upper { 12.0 * std::log2 (std::min (20000.0, rate * 0.5) / 1500.0) };
            status = player.setTransposeSemitones (upper);
            check (status.failed () && player.isPausedForRange () && ! player.isReady (),
                   "The exact device-Nyquist/20 kHz ceiling is exclusive");
            collect (player, 2048);
            check (player.setTransposeSemitones (std::nextafter (upper, -std::numeric_limits<double>::infinity ())).wasOk ()
                   && player.isReady () && player.isActive () && ! player.isPausedForRange (),
                   "A transpose immediately below the exclusive ceiling resumes safely");
            checkBounded (collect (player, 2048));
        }

        auto bank { startingPoint (Mode::layers, Shape::sine) };
        bank.cycleFrames = 128;
        bank.voiceCount = 2;
        bank.voices[0].detuneCents = -1200.0;
        bank.voices[1].detuneCents = 1200.0;
        player.prepareToPlay (8000.0);
        player.setPayload (payloadFor (bank));
        player.setTransposeSemitones (0.0);
        check (player.start ().wasOk (), "Bank range fixture starts with both detuned voices in range");
        collect (player, 1024);
        const auto bankUpper { 12.0 * std::log2 (4000.0 / 750.0) };
        check (player.setTransposeSemitones (bankUpper).failed () && player.isPausedForRange (), "The highest detuned bank voice determines the upper guard");
        collect (player, 1024);
        check (player.setTransposeSemitones (0.0).wasOk () && player.isActive (), "The entire bank resumes together when every voice is in range");
        const auto bankLower { 12.0 * std::log2 (20.0 / 187.5) };
        check (player.setTransposeSemitones (std::nextafter (bankLower, -std::numeric_limits<double>::infinity ())).failed () && player.isPausedForRange (),
               "The lowest detuned bank voice determines the lower guard");
        check (player.setTransposeSemitones (bankLower).wasOk () && player.isActive (), "The bank's exact inclusive lower boundary is accepted");

        player.prepareToPlay (48000.0);
        player.setPayload (clean);
        player.setTransposeSemitones (0.0);
        check (player.start ().wasOk (), "Live-render range fixture starts");
        collect (player, 1024);
        auto subAudio { startingPoint (Mode::oscillator, Shape::sine) };
        subAudio.cycleFrames = 8192;
        const auto low { payloadFor (subAudio) };
        player.setPayload (low);
        check (player.isPausedForRange (), "An active render update moving frequency out of range pauses with intent");
        collect (player, 2048);
        player.setPayload (low);
        player.setPayload (clean);
        check (player.isPausedForRange () && player.isReady () && ! player.isActive (), "Later valid render publication alone never resumes a paused monitor");
        player.setMonitorGain (0.1);
        check (player.setTransposeSemitones (0.0).wasOk () && player.isPausedForRange () && ! player.isActive (),
               "A monitor-level update with unchanged transpose cannot consume range-resume intent");
        check (player.setTransposeSemitones (1.0).wasOk () && player.isActive () && ! player.isPausedForRange (),
               "An actual valid transpose adjustment resumes the newly published design");

        auto pause = [&] ()
        {
            player.setPayload (clean);
            player.setTransposeSemitones (0.0);
            check (player.start ().wasOk (), "Cancellation fixture starts explicitly");
            collect (player, 1024);
            check (player.setTransposeSemitones (-48.0).failed () && player.isPausedForRange (), "Cancellation fixture is range-paused");
        };
        auto remainsStopped = [&] ()
        {
            check (! player.isPausedForRange (), "Explicit cancellation removes pending resume intent immediately");
            player.setPayload (clean);
            player.setTransposeSemitones (0.0);
            collect (player, 2048);
            check (! player.isActive () && ! player.isPausedForRange (), "Valid later controls or payloads cannot resurrect canceled audition");
        };
        pause (); player.setPlaying (false); remainsStopped ();
        pause (); player.stopImmediately (); remainsStopped ();
        pause (); player.setPayload (nullptr); remainsStopped ();
        pause (); check (player.start ().failed (), "An explicit out-of-range start fails"); remainsStopped ();
        pause (); player.prepareToPlay (44100.0); remainsStopped ();
        pause (); player.prepareToPlay (std::numeric_limits<double>::quiet_NaN ());
        check (! player.isPausedForRange () && ! player.isReady (), "Invalid/unprepared device state clears range-resume intent");
        player.prepareToPlay (48000.0); remainsStopped ();
        for (const auto invalid : { std::numeric_limits<double>::quiet_NaN (), std::numeric_limits<double>::infinity (), 73.0, -49.0 })
        {
            pause (); check (player.setTransposeSemitones (invalid).failed (), "Invalid controls are rejected, not treated as an audible-range pause"); remainsStopped ();
        }
        for (const auto invalid : { std::numeric_limits<double>::quiet_NaN (), -0.1, 1.1 })
        { pause (); player.setMonitorGain (invalid); remainsStopped (); }

        auto silent { startingPoint (Mode::oscillator, Shape::sine) };
        silent.amplitude = 0.0;
        player.setPayload (payloadFor (silent));
        player.setTransposeSemitones (0.0);
        player.setMonitorGain (0.5);
        check (player.start ().wasOk (), "Silent waveform still has an explicit audition lifecycle");
        collect (player, 1024);
        check (player.setTransposeSemitones (-48.0).failed () && player.isPausedForRange (), "Silent PCM does not defeat fundamental-frequency guards");
        collect (player, 2048);
        check (player.setTransposeSemitones (0.0).wasOk () && player.isActive (), "Silent waveforms preserve the same pause/resume semantics");
        check (collect (player, 2048).getMagnitude (0, 2048) == 0.0f, "Resuming an all-zero source remains silent");
    }
}

void testWaveformAudition ()
{
    testProtectedSignalInspection ();
    testHardwareTransposeRange ();
    testRangePause ();
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
