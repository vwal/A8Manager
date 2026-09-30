#include "Assimil8or/Audio/AuditionSignalCheck.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    void require (bool condition, const char* message) { if (! condition) throw std::runtime_error (message); }

    juce::AudioBuffer<float> tone (int frames, double rate, double frequency, float amplitude = 0.8f)
    {
        juce::AudioBuffer<float> buffer (1, frames);
        for (int frame { 0 }; frame < frames; ++frame)
            buffer.setSample (0, frame, amplitude * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * frequency * frame / rate)));
        return buffer;
    }

    AuditionSignalCheck::Report inspect (const juce::AudioBuffer<float>& buffer, double rate = 48000.0, bool loop = false)
    {
        return AuditionSignalCheck::analyse (buffer, { 0, buffer.getNumSamples (), 0, buffer.getNumChannels (), rate, loop });
    }
}

void testAuditionSignalCheck ()
{
    using namespace AuditionSignalCheck;
    auto sine { tone (96000, 48000, 440) };
    const juce::AudioBuffer<float> unchanged { sine };
    const auto sineReport { inspect (sine) };
    require (sineReport.status == Status::clear && ! sineReport.needsConfirmation () && ! sineReport.isBlocked ()
             && sineReport.channels.size () == 1 && std::abs (sineReport.channels[0].mean) < 1.0e-7
             && sineReport.channels[0].lowFrequencyEnergyRatio < 0.01,
             "Ordinary audio tone has finite measured facts and no high-confidence signal warning");
    require (sineReport.explanation ().contains ("not a safety certification") && sineReport.explanation ().contains ("hardware"),
             "A clear result explicitly avoids certifying unknown output hardware or listening safety");
    for (int frame { 0 }; frame < sine.getNumSamples (); ++frame)
        require (sine.getSample (0, frame) == unchanged.getSample (0, frame), "Analysis does not modify the supplied PCM buffer");
    const auto repeatReport { inspect (sine) };
    require (repeatReport.status == sineReport.status && repeatReport.channels[0].mean == sineReport.channels[0].mean
             && repeatReport.channels[0].lowFrequencyRms == sineReport.channels[0].lowFrequencyRms,
             "Repeated analysis is deterministic");

    juce::AudioBuffer<float> saw (1, 96000), pulse (1, 96000);
    for (int frame { 0 }; frame < saw.getNumSamples (); ++frame)
    {
        saw.setSample (0, frame, static_cast<float> (-1.0 + 2.0 * (frame % 480) / 479.0));
        pulse.setSample (0, frame, frame % 480 < 240 ? 1.0f : -1.0f);
    }
    for (const auto loop : { false, true })
    {
        const auto sawReport { inspect (saw, 48000, loop) }, pulseReport { inspect (pulse, 48000, loop) };
        require (sawReport.status == Status::clear && pulseReport.status == Status::clear
                 && sawReport.channels[0].maximumStep >= 1.99 && pulseReport.channels[0].maximumStep == 2.0,
                 "Normal full-scale audio saw and pulse edges are measured but do not trigger a warning");
        require (pulseReport.channels[0].clippedFraction == 1.0 && pulseReport.channels[0].longestClippedSeconds == 2.0,
                 "Sustained full-scale plateaus alone remain metrics, not a warning criterion");
    }
    juce::AudioBuffer<float> biasedPulse (1, 480);
    for (int frame { 0 }; frame < biasedPulse.getNumSamples (); ++frame)
        biasedPulse.setSample (0, frame, frame < 96 ? 1.0f : -1.0f);
    const auto biasedPulseReport { inspect (biasedPulse, 48000, true) };
    require (biasedPulseReport.status == Status::warning && biasedPulseReport.channels[0].mean < -0.59
             && biasedPulseReport.explanation ().contains ("DC average"),
             "A duty-imbalanced pulse warns for its genuine sustained DC, not simply because it has pulse edges");
    juce::AudioBuffer<float> silence (1, 48000);
    silence.clear ();
    const auto silentReport { inspect (silence) };
    require (silentReport.status == Status::clear && silentReport.channels[0].rms == 0.0
             && silentReport.channels[0].lowFrequencyEnergyRatio == 0.0, "Silence does not divide by zero or require a warning");
    auto quietLfo { tone (96000, 48000, 2, 0.05f) };
    require (inspect (quietLfo).status == Status::clear, "A quiet low-frequency signal below the substantial-amplitude threshold does not cause a popup");

    juce::AudioBuffer<float> dc (2, 48000);
    for (int frame { 0 }; frame < dc.getNumSamples (); ++frame)
    {
        dc.setSample (0, frame, 0.4f);
        dc.setSample (1, frame, -0.4f);
    }
    const auto dcReport { inspect (dc) };
    require (dcReport.status == Status::warning && dcReport.needsConfirmation () && dcReport.channels.size () == 2
             && dcReport.channels[0].mean > 0.39 && dcReport.channels[1].mean < -0.39
             && dcReport.channels[0].longestDcSeconds == 1.0 && dcReport.reasons.size () >= 2,
             "Sustained opposite-polarity DC is assessed independently in stereo, not hidden by averaging the channels");
    juce::AudioBuffer<float> belowDcThreshold (1, 48000);
    juce::FloatVectorOperations::fill (belowDcThreshold.getWritePointer (0), 0.2f, belowDcThreshold.getNumSamples ());
    require (inspect (belowDcThreshold).status == Status::clear && inspect (belowDcThreshold, 48000, true).status == Status::clear,
             "Removing the mean before subaudio measurement does not bypass the separate 25-percent DC warning threshold");
    juce::AudioBuffer<float> shortDc (1, 4800);
    juce::FloatVectorOperations::fill (shortDc.getWritePointer (0), 0.8f, shortDc.getNumSamples ());
    require (inspect (shortDc).status == Status::clear && inspect (shortDc, 48000, true).status == Status::warning,
             "A short one-shot plateau is not treated as sustained DC, but repeating its biased cycle is");
    juce::AudioBuffer<float> embeddedDc (1, 192000);
    embeddedDc.clear ();
    juce::FloatVectorOperations::fill (embeddedDc.getWritePointer (0, 48000), 0.7f, 14400);
    const auto embeddedReport { inspect (embeddedDc) };
    require (embeddedReport.status == Status::warning && embeddedReport.channels[0].mean < 0.25
             && embeddedReport.channels[0].longestDcSeconds >= 0.25,
             "A sustained DC-like section is detected even when long surrounding silence dilutes the whole-file mean");

    auto lfo { tone (96000, 48000, 2) };
    const auto lfoReport { inspect (lfo) };
    require (lfoReport.status == Status::warning && lfoReport.channels[0].lowFrequencyRms > 0.2
             && lfoReport.channels[0].lowFrequencyEnergyRatio >= 0.90 && lfoReport.explanation ().contains ("subaudible"),
             "A substantial waveform strongly dominated by subaudio produces a factual warning");
    require (inspect (lfo, 48000 * 32.0, true).status == Status::clear,
             "The same source samples played faster are assessed at their effective audio frequency, not their original rate");
    auto highRateTone { tone (192000, 192000, 1000) };
    require (inspect (highRateTone, 192000).status == Status::clear, "High-rate ordinary audio is not aliased into a false low-frequency warning");
    auto highRateLfo { tone (192000, 192000, 2) };
    require (inspect (highRateLfo, 192000).status == Status::warning, "Subaudio checks use the declared playback rate at high source sample rates too");

    juce::AudioBuffer<float> transient (1, 48000);
    transient.clear ();
    juce::FloatVectorOperations::fill (transient.getWritePointer (0, 12000), -1.0f, 480);
    juce::FloatVectorOperations::fill (transient.getWritePointer (0, 12480), 1.0f, 480);
    const auto transientReport { inspect (transient) };
    require (transientReport.status == Status::clear && transientReport.channels[0].maximumStep == 2.0
             && transientReport.channels[0].clippedFraction > 0.0,
             "An isolated sharp full-scale transient records clipping/jump facts without triggering the high-confidence warning");
    juce::AudioBuffer<float> oneSaw (1, 480);
    for (int frame { 0 }; frame < oneSaw.getNumSamples (); ++frame)
        oneSaw.setSample (0, frame, static_cast<float> (-1.0 + 2.0 * frame / 479.0));
    const auto seamReport { inspect (oneSaw, 48000, true) };
    require (seamReport.status == Status::clear && seamReport.channels[0].loopSeamStep == 2.0
             && seamReport.channels[0].lowFrequencyRms == 0.0,
             "A normal periodic saw seam is measured without low-pass-startup or edge-only warnings");
    require (inspect (oneSaw, 480, true).status == Status::warning,
             "Slowing the same repeating cycle into subaudio changes its warning result");
    const auto shortened { analyse (dc, { 0, 4800, 0, 1, 48000, false }) };
    require (shortened.status == Status::clear && std::abs (shortened.durationSeconds - 0.1) < 1e-12,
             "Only the selected one-shot range and its actual duration contribute to the warning");
    require (analyse (embeddedDc, { 62400, 48000, 0, 1, 48000, false }).status == Status::clear
             && analyse (embeddedDc, { 48000, 14400, 0, 1, 48000, false }).status == Status::warning,
             "Moving or shortening a selection excludes unplayed risky content and detects the selected DC interval");
    juce::AudioBuffer<float> isolatedChannel (2, 48000);
    isolatedChannel.clear ();
    juce::FloatVectorOperations::fill (isolatedChannel.getWritePointer (0), 0.5f, 48000);
    require (analyse (isolatedChannel, { 0, 48000, 1, 1, 48000, false }).status == Status::clear,
             "An unselected source channel does not contaminate the selected audition channel's result");

    for (const auto request : { Request { -1, 100, 0, 1, 48000, false }, Request { 0, std::numeric_limits<int>::max (), 0, 1, 48000, false },
                               Request { 0, 100, -1, 1, 48000, false }, Request { 0, 100, 1, 1, 48000, false },
                               Request { 0, 100, 0, 0, 48000, false }, Request { 0, 100, 0, 1, 0, false },
                               Request { 0, 100, 0, 1, std::numeric_limits<double>::quiet_NaN (), false } })
        require (analyse (sine, request).isBlocked (), "Invalid ranges, channels and playback rates block rather than claiming a successful analysis");
    juce::AudioBuffer<float> invalid (1, 64);
    invalid.clear ();
    invalid.setSample (0, 63, std::numeric_limits<float>::quiet_NaN ());
    require (inspect (invalid).isBlocked () && analyse (invalid, { 0, 32, 0, 1, 48000, false }).status == Status::clear,
             "Non-finite selected PCM blocks, while data outside the actual audition region is not read");
    invalid.setSample (0, 63, std::numeric_limits<float>::infinity ());
    require (inspect (invalid).isBlocked (), "Infinite selected PCM also blocks");
    juce::AudioBuffer<float> excessive (1, maximumFrames + 1);
    excessive.clear ();
    const auto unavailable { inspect (excessive) };
    require (unavailable.status == Status::unavailable && ! unavailable.needsConfirmation () && ! unavailable.isBlocked ()
             && unavailable.channels.empty () && unavailable.explanation ().contains ("not been verified"),
             "Long regions remain quietly unverified rather than being subsampled into a misleading clear result");
    require (analyse (sine, { 0, 0, 0, 1, 48000, false }).status == Status::unavailable,
             "An empty selection is explicitly not analysed");
    std::cout << "PASS: audition signal checks (high-confidence DC/subaudio, effective rate/region/stereo, quiet/loud/edge false-positive controls, bounded validation and no mutation)\n";
}
