#include "WaveformAudition.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

namespace
{
    constexpr double defaultMonitorGain { 0.12589254117941673 }; // -18 dB
    constexpr double outputLimit { 0.98 };
    constexpr double minimumFrequency { 20.0 };

    int orderFor (int frames)
    {
        int order { 0 };
        while ((1 << order) < frames) ++order;
        return order;
    }

    double periodicSample (const std::vector<float>& samples, double phase) noexcept
    {
        const auto position { phase * static_cast<double> (samples.size ()) };
        const auto first { std::min (static_cast<int> (samples.size ()) - 1, static_cast<int> (position)) };
        const auto second { first + 1 == static_cast<int> (samples.size ()) ? 0 : first + 1 };
        return samples[static_cast<size_t> (first)] + (samples[static_cast<size_t> (second)] - samples[static_cast<size_t> (first)]) * (position - first);
    }
}

struct WaveformAudition::State
{
    juce::CriticalSection lock;
    std::atomic<bool> active { false };
    std::atomic<bool> pausedForRange { false };
    // The callback only swaps these owners. Displaced buffers stay in pending
    // until the next non-audio-thread publication retires them outside the lock.
    PayloadPtr current, outgoing, pending;
    bool pendingReady { false }, available { false }, prepared { false }, playing { false };
    double rate { 48000.0 }, gain { defaultMonitorGain }, targetGain { defaultMonitorGain };
    double transpose { 0.0 }, pitchRatio { 1.0 }, targetPitchRatio { 1.0 };
    double gate { 0.0 }, rampStep { 1.0 / 480.0 }, smoothing { 0.0 }, dcPole { 0.0 };
    int fadeRemaining { 0 }, fadeFrames { 960 };
    std::array<double, 8> phase {}, outgoingPhase {};
    std::array<double, 2> lastInput {}, lastOutput {};

    const Payload* candidate () const noexcept { return available ? (pendingReady ? pending.get () : current.get ()) : nullptr; }

    bool transposeRange (const Payload* payload, double& lower, double& upper) const noexcept
    {
        if (! prepared || payload == nullptr || payload->frames <= 0 || payload->voices.empty ()) return false;
        lower = -std::numeric_limits<double>::infinity ();
        upper = std::numeric_limits<double>::infinity ();
        const auto ceiling { std::min (20000.0, rate * 0.5) };
        for (const auto& voice : payload->voices)
        {
            const auto frequency { payload->settings.sampleRate * voice.detune / payload->frames };
            if (! std::isfinite (frequency) || frequency <= 0.0) return false;
            lower = std::max (lower, 12.0 * std::log2 (minimumFrequency / frequency));
            upper = std::min (upper, 12.0 * std::log2 (ceiling / frequency));
        }
        return true;
    }

    bool audible (const Payload* payload, double semitones) const noexcept
    {
        double lower {}, upper {};
        // Compare in semitones so a caller using the exact computed boundary
        // gets an inclusive 20 Hz floor and an exclusive upper limit, without
        // exp2/log2 roundoff accidentally admitting Nyquist.
        return std::isfinite (semitones) && payload != nullptr && semitones >= -48.0
            && semitones <= WaveformAudition::maximumTransposeSemitones (payload->settings)
            && transposeRange (payload, lower, upper) && semitones >= lower && semitones < upper;
    }

    juce::String rangeError () const
    {
        juce::String message { pausedForRange.load () ? "Audition paused: " : "Audition unavailable: " };
        message << "every voice must be at least 20 Hz and below " << juce::String (std::min (20000.0, rate * 0.5), 0)
                << " Hz (the lower of 20 kHz and device Nyquist).";
        const auto* payload { candidate () };
        const auto hardwareMaximum { payload != nullptr ? WaveformAudition::maximumTransposeSemitones (payload->settings) : 72.0 };
        if (payload != nullptr)
            message << " The A8 total-pitch limit permits monitor transpose up to +" << juce::String (hardwareMaximum, 2)
                    << " st for this " << juce::String (payload->settings.sampleRate, 0) << " Hz source, including positive bank detune.";
        double lower {}, upper {};
        if (transposeRange (candidate (), lower, upper))
        {
            // Round inward to give genuinely permitted 0.01-st settings.
            const auto minimum { std::max (-48.0, std::ceil (lower * 100.0) / 100.0) };
            const auto maximum { std::min (hardwareMaximum, (std::ceil (upper * 100.0) - 1.0) / 100.0) };
            if (minimum <= maximum)
                message << " At 0.01 st steps, use " << juce::String (minimum, 2) << " to " << juce::String (maximum, 2) << " st for this design.";
            else message << " No transpose within -48..+" << juce::String (hardwareMaximum, 2)
                         << " st puts every voice in range; adjust cycle length or bank detuning.";
        }
        if (pausedForRange.load ()) message << " Moving transpose back into range resumes audition; Stop cancels this.";
        return message;
    }

    void stopWithRamp () noexcept
    {
        playing = false;
        pausedForRange.store (false, std::memory_order_release);
        active.store (gate > 0.0, std::memory_order_release);
    }

    void pauseForRange () noexcept
    {
        // A failed start/idle edit never arms automatic resumption. Stop-ramp
        // ownership alone is not evidence of an active audition request.
        pausedForRange.store (playing || pausedForRange.load (), std::memory_order_release);
        playing = false;
        active.store (gate > 0.0, std::memory_order_release);
    }

    void beginPlayback () noexcept
    {
        if (gate == 0.0)
        {
            fadeRemaining = 0;
            phase.fill (0.0);
            outgoingPhase.fill (0.0);
            lastInput.fill (0.0);
            lastOutput.fill (0.0);
            pitchRatio = targetPitchRatio;
            gain = targetGain;
        }
        pausedForRange.store (false, std::memory_order_release);
        playing = true;
        active.store (true, std::memory_order_release);
    }

    void resetPlayback () noexcept
    {
        playing = false;
        pausedForRange.store (false, std::memory_order_release);
        gate = 0.0;
        fadeRemaining = 0;
        gain = targetGain;
        pitchRatio = targetPitchRatio;
        phase.fill (0.0);
        outgoingPhase.fill (0.0);
        lastInput.fill (0.0);
        lastOutput.fill (0.0);
        active.store (false, std::memory_order_release);
    }

    void adoptPending () noexcept
    {
        if (! pendingReady || fadeRemaining != 0) return;
        outgoing.swap (current);
        current.swap (pending);
        pendingReady = false;
        outgoingPhase = phase;
        if (! outgoing) phase.fill (0.0);
        fadeRemaining = outgoing && gate > 0.0 ? fadeFrames : 0;
    }

    std::array<double, 2> sample (const Payload* payload, std::array<double, 8>& positions) noexcept
    {
        std::array<double, 2> mixed {};
        if (payload == nullptr || payload->voices.empty ()) return mixed;
        const auto baseFrequency { payload->settings.sampleRate * pitchRatio / payload->frames };
        for (size_t index { 0 }; index < payload->voices.size (); ++index)
        {
            const auto& voice { payload->voices[index] };
            const auto frequency { baseFrequency * voice.detune };
            const auto allowedHarmonics { rate * 0.5 / frequency };
            double value { 0.0 };
            if (allowedHarmonics >= 1.0)
            {
                size_t level { 0 };
                while (level + 1 < voice.tables.size () && voice.tables[level].harmonics > allowedHarmonics) ++level;
                const auto& fine { voice.tables[level] };
                value = periodicSample (fine.samples, positions[index]);
                if (level + 1 < voice.tables.size ())
                {
                    const auto& coarse { voice.tables[level + 1] };
                    // Both tables are already below Nyquist. Only soften the
                    // entry into the finer level, not an entire octave of its
                    // valid harmonics. The top level reaches full strength at
                    // the source's native rate (N / 2 allowed partials).
                    const auto transitionWidth { level == 0 ? 1.0 : std::max (1.0, fine.harmonics * 0.1) };
                    const auto amount { std::clamp ((allowedHarmonics - fine.harmonics) / transitionWidth, 0.0, 1.0) };
                    value = periodicSample (coarse.samples, positions[index]) * (1.0 - amount) + value * amount;
                }
            }
            mixed[0] += value * voice.left;
            mixed[1] += value * voice.right;
            positions[index] += frequency / rate;
            positions[index] -= std::floor (positions[index]);
        }
        const auto headroom { 1.0 / static_cast<double> (payload->voices.size ()) };
        mixed[0] *= headroom;
        mixed[1] *= headroom;
        return mixed;
    }
};

WaveformAudition::WaveformAudition () : state (std::make_unique<State> ()) {}
WaveformAudition::~WaveformAudition () = default;

AuditionSignalCheck::Report WaveformAudition::inspectSignal (PayloadPtr payload, double transpose)
{
    const auto blocked = [] (const juce::String& reason)
    {
        AuditionSignalCheck::Report result;
        result.status = AuditionSignalCheck::Status::blocked;
        result.reasons.add (reason);
        return result;
    };
    try
    {
        WaveformAudition monitor;
        monitor.prepareToPlay (48000.0);
        monitor.setPayload (std::move (payload));
        monitor.setMonitorGain (1.0);
        // The immutable payload has already passed PCM/mode validation. A
        // valid hardware transpose can still be outside this inspection
        // engine's audible band; that is unavailable analysis, not corrupt
        // audio. Let the live engine use its actual device band and preserve
        // its pause/resume intent when the user moves transpose out and back.
        const auto* source { monitor.state->candidate () };
        double lower {}, upper {};
        const auto outsideInspectionBand { source != nullptr && std::isfinite (transpose) && transpose >= -48.0
            && transpose <= maximumTransposeSemitones (source->settings)
            && monitor.state->transposeRange (source, lower, upper) && (transpose < lower || transpose >= upper) };
        const auto preparationFailure = [&] (const juce::String& reason)
        {
            auto report { blocked (reason) };
            if (outsideInspectionBand) report.status = AuditionSignalCheck::Status::unavailable;
            return report;
        };
        if (const auto transposed { monitor.setTransposeSemitones (transpose) }; transposed.failed ())
            return preparationFailure (transposed.getErrorMessage ());
        if (const auto started { monitor.start () }; started.failed ())
            return preparationFailure (started.getErrorMessage ());

        // Reuse one bounded two-channel buffer. Do not analyse startup ramps or
        // the DC blocker's initial condition as a characteristic of the source.
        juce::AudioBuffer<float> output (2, 48000);
        output.clear ();
        if (! monitor.process ({ &output, 0, 4800 }) || ! monitor.process ({ &output, 0, output.getNumSamples () }))
            return blocked ("The protected waveform monitor could not render this design for inspection.");
        // This is an already-rendered continuous monitor interval, not a new
        // loop made by joining arbitrary ends of the one-second observation.
        return AuditionSignalCheck::analyse (output, { 0, output.getNumSamples (), 0, 2, 48000.0, false });
    }
    catch (const std::bad_alloc&)
    {
        return blocked ("There is not enough memory to inspect waveform audition.");
    }
}

double WaveformAudition::maximumTransposeSemitones (const WaveformDesign::Settings& settings) noexcept
{
    const auto ceiling { settings.sampleRate == 48000.0 ? 72.0 : settings.sampleRate == 96000.0 ? 60.0
                        : settings.sampleRate == 192000.0 ? 48.0 : -48.0 };
    if (ceiling < 0.0) return -48.0;
    double highestPositiveDetune { 0.0 };
    if (settings.mode == WaveformDesign::Mode::layers)
    {
        if (settings.voiceCount < 1 || settings.voiceCount > static_cast<int> (settings.voices.size ())) return -48.0;
        for (int index { 0 }; index < settings.voiceCount; ++index)
        {
            const auto detune { settings.voices[static_cast<size_t> (index)].detuneCents };
            if (! std::isfinite (detune)) return -48.0;
            highestPositiveDetune = std::max (highestPositiveDetune, detune);
        }
    }
    // JUCE's 0.1-cent control spans -2400..2400. Its range arithmetic (and
    // fused multiply/add on some builds) can leave a nominal whole cent a few
    // ulps above the integer, including near zero after subtracting the range
    // start. Only absorb roundoff at that arithmetic scale; real fractional
    // cents still reserve the next entire 0.01-st transpose step.
    constexpr double controlSpan { 4800.0 };
    constexpr double roundoffTolerance { 8.0 * std::numeric_limits<double>::epsilon () * controlSpan };
    const auto nearestCent { std::round (highestPositiveDetune) };
    if (std::abs (highestPositiveDetune - nearestCent) <= roundoffTolerance)
        highestPositiveDetune = nearestCent;
    // One transpose step is one cent. Round the reserved detune up so the
    // available transpose rounds inward for an actual fractional-cent voice.
    return std::max (-48.0, (ceiling * 100.0 - std::ceil (highestPositiveDetune)) / 100.0);
}

juce::Result WaveformAudition::preparePayload (const WaveformDesign::Settings& settings,
                                              const WaveformDesign::Render& rendered, PayloadPtr& payload)
{
    // Failure must not leave a stale, previously playable design in the output.
    payload.reset ();
    if (settings.mode == WaveformDesign::Mode::modulation)
        return juce::Result::fail ("CV/modulation designs are not routed to speaker audition.");
    if (const auto valid { WaveformDesign::validate (settings) }; valid.failed ()) return valid;
    const auto count { settings.mode == WaveformDesign::Mode::layers ? settings.voiceCount : 1 };
    if (rendered.frames != settings.cycleFrames || rendered.sampleRate != settings.sampleRate || rendered.voices.size () != static_cast<size_t> (count))
        return juce::Result::fail ("The rendered audio does not match this design's rate, cycle length or voice count.");
    for (const auto& buffer : rendered.voices)
    {
        if (buffer.getNumChannels () != 1 || buffer.getNumSamples () != settings.cycleFrames)
            return juce::Result::fail ("Audition requires complete mono cycle buffers.");
        for (int frame { 0 }; frame < buffer.getNumSamples (); ++frame)
            if (! std::isfinite (buffer.getSample (0, frame)) || std::abs (buffer.getSample (0, frame)) > 1.0f)
                return juce::Result::fail ("The rendered audio contains non-finite or out-of-range samples.");
    }
    try
    {
        auto prepared { std::shared_ptr<Payload> (new Payload) };
        prepared->settings = settings;
        prepared->frames = settings.cycleFrames;
        prepared->voices.reserve (static_cast<size_t> (count));
        juce::dsp::FFT sourceFft (orderFor (settings.cycleFrames));
        std::vector<juce::dsp::Complex<float>> original (static_cast<size_t> (settings.cycleFrames)), spectrum (original.size ());
        for (int index { 0 }; index < count; ++index)
        {
            const auto& buffer { rendered.voices[static_cast<size_t> (index)] };
            for (int frame { 0 }; frame < settings.cycleFrames; ++frame)
                original[static_cast<size_t> (frame)] = buffer.getSample (0, frame);
            sourceFft.perform (original.data (), spectrum.data (), false);
            auto& voice { prepared->voices.emplace_back () };
            const auto parameters { settings.mode == WaveformDesign::Mode::layers ? settings.voices[static_cast<size_t> (index)] : WaveformDesign::Voice {} };
            voice.detune = std::exp2 (parameters.detuneCents / 1200.0);
            const auto panAngle { (parameters.pan + 1.0) * juce::MathConstants<double>::pi * 0.25 };
            voice.left = std::cos (panAngle);
            voice.right = std::sin (panAngle);
            // DC is deliberately removed for monitoring, not for export. Each
            // mip is oversampled at least eight points per highest harmonic,
            // then the callback blends only tables safe at the device rate.
            for (int harmonics { settings.cycleFrames / 2 - 1 };; harmonics /= 2)
            {
                const auto order { std::max (6, orderFor (8 * (harmonics + 1))) };
                const auto frames { 1 << order };
                juce::dsp::FFT fft (order);
                std::vector<juce::dsp::Complex<float>> filtered (static_cast<size_t> (frames)), wave (filtered.size ());
                const auto scale { static_cast<float> (frames) / settings.cycleFrames };
                for (int bin { 1 }; bin <= harmonics; ++bin)
                {
                    filtered[static_cast<size_t> (bin)] = spectrum[static_cast<size_t> (bin)] * scale;
                    filtered[static_cast<size_t> (frames - bin)] = spectrum[static_cast<size_t> (settings.cycleFrames - bin)] * scale;
                }
                fft.perform (filtered.data (), wave.data (), true);
                auto& table { voice.tables.emplace_back () };
                table.harmonics = harmonics;
                table.samples.resize (static_cast<size_t> (frames));
                for (int frame { 0 }; frame < frames; ++frame) table.samples[static_cast<size_t> (frame)] = wave[static_cast<size_t> (frame)].real ();
                if (harmonics == 1) break;
            }
        }
        payload = std::move (prepared);
        return juce::Result::ok ();
    }
    catch (const std::bad_alloc&)
    {
        return juce::Result::fail ("There is not enough memory to prepare waveform audition.");
    }
}

void WaveformAudition::setPayload (PayloadPtr payload)
{
    {
        const juce::ScopedLock guard (state->lock);
        state->pending.swap (payload);
        state->pendingReady = state->pending != nullptr;
        state->available = state->pendingReady;
        if (! state->prepared || state->candidate () == nullptr) state->stopWithRamp ();
        else if (! state->audible (state->candidate (), state->transpose)) state->pauseForRange ();
        // A valid new render can update a playing monitor, but never consumes
        // range-pause intent: resumption requires an actual transpose change.
    }
    // The old pending/retired source is released here, never in process().
}

void WaveformAudition::prepareToPlay (double deviceSampleRate)
{
    const juce::ScopedLock guard (state->lock);
    state->resetPlayback ();
    state->prepared = std::isfinite (deviceSampleRate) && deviceSampleRate >= 8000.0 && deviceSampleRate <= 768000.0;
    if (! state->prepared) return;
    state->rate = deviceSampleRate;
    state->rampStep = 1.0 / (state->rate * 0.01);
    state->fadeFrames = std::max (1, juce::roundToInt (state->rate * 0.02));
    state->smoothing = 1.0 - std::exp (-1.0 / (state->rate * 0.005));
    state->dcPole = std::exp (-juce::MathConstants<double>::twoPi * 5.0 / state->rate);
}

juce::Result WaveformAudition::start ()
{
    const juce::ScopedLock guard (state->lock);
    if (! state->prepared || state->candidate () == nullptr)
    {
        state->stopWithRamp ();
        return juce::Result::fail (! state->prepared ? "No valid audio output rate is available for waveform audition."
                                                    : "Wait for a valid audio waveform preview before starting audition. CV designs cannot be auditioned.");
    }
    if (! state->audible (state->candidate (), state->transpose))
    {
        state->stopWithRamp ();
        return juce::Result::fail (state->rangeError ());
    }
    state->targetPitchRatio = std::exp2 (state->transpose / 12.0);
    state->beginPlayback ();
    return juce::Result::ok ();
}

void WaveformAudition::setPlaying (bool playing)
{
    if (playing) { juce::ignoreUnused (start ()); return; }
    const juce::ScopedLock guard (state->lock);
    state->stopWithRamp ();
}

void WaveformAudition::stopImmediately ()
{
    const juce::ScopedLock guard (state->lock);
    state->resetPlayback ();
}

void WaveformAudition::setMonitorGain (double gain)
{
    const juce::ScopedLock guard (state->lock);
    if (! std::isfinite (gain) || gain < 0.0 || gain > 1.0)
    {
        state->targetGain = 0.0;
        state->stopWithRamp ();
    }
    else state->targetGain = gain;
}

juce::Result WaveformAudition::setTransposeSemitones (double semitones)
{
    const juce::ScopedLock guard (state->lock);
    const auto* payload { state->candidate () };
    const auto maximum { payload != nullptr ? maximumTransposeSemitones (payload->settings) : 72.0 };
    if (! std::isfinite (semitones) || semitones < -48.0 || semitones > maximum)
    {
        state->stopWithRamp ();
        auto message { "Monitor transpose must be finite and within -48..+" + juce::String (maximum, 2) + " semitones." };
        if (payload != nullptr)
            message << " This " << juce::String (payload->settings.sampleRate, 0) << " Hz source's A8 total-pitch limit includes positive bank detune.";
        return juce::Result::fail (message);
    }
    const auto changed { state->transpose != semitones };
    state->transpose = semitones;
    if (! state->prepared || state->candidate () == nullptr)
    {
        state->stopWithRamp ();
        // Remember a valid idle control value, but never arm resume intent.
        state->targetPitchRatio = std::exp2 (semitones / 12.0);
        return juce::Result::ok ();
    }
    if (! state->audible (state->candidate (), state->transpose))
    {
        state->pauseForRange ();
        return juce::Result::fail (state->rangeError ());
    }
    // Keep the previous safe ratio during an out-of-range fade. Its return
    // glides when still fading, or starts a fresh ramp when already silent.
    state->targetPitchRatio = std::exp2 (semitones / 12.0);
    if (changed && state->pausedForRange.load ()) state->beginPlayback ();
    return juce::Result::ok ();
}

bool WaveformAudition::isActive () const noexcept { return state->active.load (std::memory_order_acquire); }
bool WaveformAudition::isPausedForRange () const noexcept { return state->pausedForRange.load (std::memory_order_acquire); }

bool WaveformAudition::isReady () const
{
    const juce::ScopedLock guard (state->lock);
    return state->audible (state->candidate (), state->transpose);
}

bool WaveformAudition::process (const juce::AudioSourceChannelInfo& output) noexcept
{
    if (! isActive ()) return false;
    if (output.buffer == nullptr || output.startSample < 0 || output.numSamples <= 0
        || output.startSample > output.buffer->getNumSamples () || output.numSamples > output.buffer->getNumSamples () - output.startSample) return true;
    output.clearActiveBufferRegion ();
    const juce::ScopedTryLock guard (state->lock);
    if (! guard.isLocked ()) return true;
    if (! state->prepared) return true;
    if (state->playing) state->adoptPending ();
    const auto channels { output.buffer->getNumChannels () };
    for (int frame { 0 }; frame < output.numSamples; ++frame)
    {
        state->gate = std::clamp (state->gate + (state->playing ? state->rampStep : -state->rampStep), 0.0, 1.0);
        if (state->gate == 0.0 && ! state->playing)
        {
            state->active.store (false, std::memory_order_release);
            break;
        }
        state->gain += (state->targetGain - state->gain) * state->smoothing;
        state->pitchRatio += (state->targetPitchRatio - state->pitchRatio) * state->smoothing;
        auto sample { state->sample (state->current.get (), state->phase) };
        if (state->fadeRemaining > 0)
        {
            const auto old { state->sample (state->outgoing.get (), state->outgoingPhase) };
            const auto amount { 1.0 - static_cast<double> (state->fadeRemaining) / state->fadeFrames };
            for (size_t side { 0 }; side < sample.size (); ++side) sample[side] = old[side] * (1.0 - amount) + sample[side] * amount;
            --state->fadeRemaining;
        }
        for (size_t side { 0 }; side < sample.size (); ++side)
        {
            const auto filtered { sample[side] - state->lastInput[side] + state->dcPole * state->lastOutput[side] };
            state->lastInput[side] = sample[side];
            state->lastOutput[side] = filtered;
            sample[side] = std::clamp (filtered * state->gain * state->gate, -outputLimit, outputLimit);
        }
        if (channels == 1)
            output.buffer->setSample (0, output.startSample + frame, static_cast<float> (std::clamp ((sample[0] + sample[1]) * juce::MathConstants<double>::sqrt2 * 0.5, -outputLimit, outputLimit)));
        else if (channels > 1)
        {
            output.buffer->setSample (0, output.startSample + frame, static_cast<float> (sample[0]));
            output.buffer->setSample (1, output.startSample + frame, static_cast<float> (sample[1]));
        }
    }
    return true;
}
