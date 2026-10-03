#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

// Read-only, sample-domain warning heuristics. Run off the audio callback.
// A clear result is NOT a safety certification or a classification as audio/CV.
namespace AuditionSignalCheck
{
    constexpr int maximumFrames { 2000000 };
    constexpr int maximumChannels { 8 };
    // Additional computer-monitor attenuation only; never applied to exports.
    constexpr double attenuationDecibels { -24.0 };

    struct Request
    {
        int startFrame {}, frameCount {};
        int firstChannel {}, numChannels { 1 };
        // Source frames consumed per second AFTER playback pitch/speed changes,
        // or the device rate when the supplied buffer is already rendered.
        double effectiveSampleRate {};
        bool loop { false };
    };

    enum class Status { unavailable, clear, warning, blocked };

    struct ChannelMetrics
    {
        int channel {}; // Zero-based source channel.
        double peak {}, rms {}, mean {};
        // Energy of a 10 Hz low-pass filter after removing the region's mean,
        // not a brick-wall spectrum estimate or a spectral safety limit.
        double lowFrequencyRms {}, lowFrequencyEnergyRatio {};
        double longestDcSeconds {};
        double clippedFraction {}, longestClippedSeconds {};
        double maximumStep {}, loopSeamStep {};
    };

    struct Report
    {
        Status status { Status::unavailable };
        double durationSeconds {};
        std::vector<ChannelMetrics> channels;
        juce::StringArray reasons;

        bool hasWarning () const { return status == Status::warning; }
        bool isBlocked () const { return status == Status::blocked; }
        juce::String explanation () const;
    };

    Report analyse (const juce::AudioBuffer<float>& samples, const Request& request);
}
