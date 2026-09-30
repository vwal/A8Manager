#include "AuditionSignalCheck.h"
#include <algorithm>
#include <cmath>

namespace AuditionSignalCheck
{
    namespace
    {
        constexpr double dcThreshold { 0.25 }, dcSeconds { 0.25 }, dcWindowSeconds { 0.05 };
        constexpr double lowPassHz { 10.0 }, lowFrequencySeconds { 0.5 };
        constexpr double lowFrequencyRmsThreshold { 0.2 }, lowFrequencyEnergyThreshold { 0.90 };
        constexpr double clippedThreshold { 0.999 };

        // Bilinear-transform second-order Butterworth low-pass. Every source
        // frame is processed: decimation could alias a normal audio tone into
        // a false subaudio warning. State is entirely local to this analysis.
        struct LowPass
        {
            explicit LowPass (double rate)
            {
                const auto k { std::tan (juce::MathConstants<double>::pi * lowPassHz / rate) };
                const auto inverse { 1.0 / (1.0 + std::sqrt (2.0) * k + k * k) };
                b0 = k * k * inverse;
                b1 = 2.0 * b0;
                b2 = b0;
                a1 = 2.0 * (k * k - 1.0) * inverse;
                a2 = (1.0 - std::sqrt (2.0) * k + k * k) * inverse;
            }
            double process (double input)
            {
                const auto output { b0 * input + z1 };
                z1 = b1 * input - a1 * output + z2;
                z2 = b2 * input - a2 * output;
                return output;
            }
            double b0 {}, b1 {}, b2 {}, a1 {}, a2 {}, z1 {}, z2 {};
        };

        Report refused (Status status, const juce::String& reason)
        {
            Report report;
            report.status = status;
            report.reasons.add (reason);
            return report;
        }
    }

    juce::String Report::explanation () const
    {
        juce::String text;
        switch (status)
        {
            case Status::unavailable: text = "Signal analysis is unavailable; this audio has not been verified."; break;
            case Status::blocked: text = "Signal analysis could not proceed safely; audition is blocked."; break;
            case Status::clear: text = "No high-confidence DC or strongly subaudible warning was found in the analysed region."; break;
            case Status::warning: text = "The analysed signal has a sustained DC or strongly subaudible component that warrants caution before speaker/headphone playback."; break;
        }
        if (! reasons.isEmpty ()) text += "\n\n" + reasons.joinIntoString ("\n");
        return text + "\n\nThis is not a safety certification. Output level, connected hardware and speaker/headphone suitability are not known. "
                      "Loudness, clipping and sharp edges alone do not trigger this warning.";
    }

    Report analyse (const juce::AudioBuffer<float>& samples, const Request& request)
    {
        if (! std::isfinite (request.effectiveSampleRate) || request.effectiveSampleRate <= 0.0 || request.effectiveSampleRate > 1.0e9)
            return refused (Status::blocked, "The effective playback sample rate is invalid.");
        if (request.startFrame < 0 || request.frameCount < 0 || request.firstChannel < 0 || request.numChannels <= 0
            || request.startFrame > samples.getNumSamples () || request.frameCount > samples.getNumSamples () - request.startFrame)
            return refused (Status::blocked, "The selected sample range or channel count is invalid.");
        if (samples.getNumChannels () == 0 && request.firstChannel == 0 && request.frameCount == 0)
            return refused (Status::unavailable, "There is no sample buffer to analyse.");
        if (request.firstChannel >= samples.getNumChannels () || request.numChannels > samples.getNumChannels () - request.firstChannel)
            return refused (Status::blocked, "The selected channels are outside the supplied sample buffer.");
        if (request.frameCount == 0)
            return refused (Status::unavailable, "The selected sample range is empty.");
        if (request.frameCount > maximumFrames || request.numChannels > maximumChannels)
            return refused (Status::unavailable, "The selected region exceeds the bounded analysis limit of " + juce::String (maximumFrames)
                + " frames and " + juce::String (maximumChannels) + " channels. No signal classification was made.");

        Report report;
        report.durationSeconds = request.frameCount / request.effectiveSampleRate;
        if (! std::isfinite (report.durationSeconds))
            return refused (Status::blocked, "The selected duration is not finite at this playback rate.");
        report.status = Status::clear;
        report.channels.reserve (static_cast<size_t> (request.numChannels));
        const auto dcWindowFrames { static_cast<int> (std::min (static_cast<double> (maximumFrames), std::max (1.0, std::ceil (request.effectiveSampleRate * dcWindowSeconds)))) };

        for (int channel { request.firstChannel }; channel < request.firstChannel + request.numChannels; ++channel)
        {
            const auto* input { samples.getReadPointer (channel, request.startFrame) };
            ChannelMetrics metrics;
            metrics.channel = channel;
            double sum {}, squared {}, windowSum {};
            int clipped {}, clippedRun {}, longestClipped {}, windowFrames {}, dcRun {}, longestDc {}, dcSign {};
            const auto finishDcWindow = [&]
            {
                const auto average { windowSum / windowFrames };
                const auto sign { average >= dcThreshold ? 1 : average <= -dcThreshold ? -1 : 0 };
                dcRun = sign == 0 ? 0 : (sign == dcSign ? dcRun : 0) + windowFrames;
                dcSign = sign;
                longestDc = std::max (longestDc, dcRun);
                windowFrames = 0;
                windowSum = 0.0;
            };
            for (int frame { 0 }; frame < request.frameCount; ++frame)
            {
                const auto value { static_cast<double> (input[frame]) };
                if (! std::isfinite (value))
                    return refused (Status::blocked, "Channel " + juce::String (channel + 1) + " contains a non-finite sample in the selected range.");
                const auto magnitude { std::abs (value) };
                metrics.peak = std::max (metrics.peak, magnitude);
                sum += value;
                squared += value * value;
                windowSum += value;
                if (++windowFrames == dcWindowFrames) finishDcWindow ();
                if (magnitude >= clippedThreshold) { ++clipped; ++clippedRun; }
                else clippedRun = 0;
                longestClipped = std::max (longestClipped, clippedRun);
                if (frame != 0) metrics.maximumStep = std::max (metrics.maximumStep, std::abs (value - input[frame - 1]));
            }
            if (windowFrames != 0) finishDcWindow ();
            metrics.mean = sum / request.frameCount;
            metrics.rms = std::sqrt (squared / request.frameCount);
            metrics.longestDcSeconds = longestDc / request.effectiveSampleRate;
            metrics.clippedFraction = static_cast<double> (clipped) / request.frameCount;
            metrics.longestClippedSeconds = longestClipped / request.effectiveSampleRate;
            if (request.loop) metrics.loopSeamStep = std::abs (static_cast<double> (input[0]) - input[request.frameCount - 1]);

            const auto acEnergy { std::max (0.0, squared - sum * metrics.mean) };
            double lowEnergy {};
            // The periodic signal's lowest non-DC frequency is 1/period. A
            // short repeated table has no sub-10-Hz AC content, regardless of
            // its edges. Its DC component is assessed separately below.
            if (acEnergy > 0.0 && ! (request.loop && report.durationSeconds < 1.0 / lowPassHz))
            {
                if (request.effectiveSampleRate <= 2.0 * lowPassHz)
                    lowEnergy = acEnergy; // All representable AC is subaudio.
                else
                {
                    LowPass filter (request.effectiveSampleRate);
                    if (request.loop)
                    {
                        // Warm with the preceding periodic samples, not zeros.
                        // At least 250 ms suppresses filter-start artefacts; for
                        // eligible loops this is bounded by three source passes.
                        const auto warmFrames { static_cast<int> (std::min (3.0 * request.frameCount, std::ceil (0.25 * request.effectiveSampleRate))) };
                        const auto first { (request.frameCount - warmFrames % request.frameCount) % request.frameCount };
                        for (int frame { 0 }; frame < warmFrames; ++frame)
                            filter.process (static_cast<double> (input[(first + frame) % request.frameCount]) - metrics.mean);
                    }
                    for (int frame { 0 }; frame < request.frameCount; ++frame)
                    {
                        const auto filtered { filter.process (static_cast<double> (input[frame]) - metrics.mean) };
                        lowEnergy += filtered * filtered;
                    }
                }
            }
            metrics.lowFrequencyRms = std::sqrt (lowEnergy / request.frameCount);
            metrics.lowFrequencyEnergyRatio = acEnergy > 0.0 ? std::clamp (lowEnergy / acEnergy, 0.0, 1.0) : 0.0;
            const auto prefix { "Channel " + juce::String (channel + 1) + ": " };
            if (request.loop && std::abs (metrics.mean) >= dcThreshold)
                report.reasons.add (prefix + "the repeating cycle has a DC average of " + juce::String (metrics.mean * 100.0, 1) + "% of digital full scale.");
            else if (metrics.longestDcSeconds >= dcSeconds)
                report.reasons.add (prefix + "same-polarity 50 ms window averages stay at or above 25% of digital full scale for "
                    + juce::String (metrics.longestDcSeconds, 3) + " seconds (sustained DC-like bias).");
            if ((request.loop || report.durationSeconds >= lowFrequencySeconds)
                && metrics.lowFrequencyRms >= lowFrequencyRmsThreshold && metrics.lowFrequencyEnergyRatio >= lowFrequencyEnergyThreshold)
                report.reasons.add (prefix + "the 10 Hz low-pass component has " + juce::String (metrics.lowFrequencyRms * 100.0, 1)
                    + "% full-scale RMS and " + juce::String (metrics.lowFrequencyEnergyRatio * 100.0, 1)
                    + "% of the signal's non-DC energy (strongly subaudible content).");
            report.channels.push_back (metrics);
        }
        if (! report.reasons.isEmpty ()) report.status = Status::warning;
        return report;
    }
}
