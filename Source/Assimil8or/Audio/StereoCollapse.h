#pragma once

#include <JuceHeader.h>

namespace StereoCollapse
{
    enum class Mode { merge, keepLeft, keepRight };

    struct Result
    {
        juce::ValueTree editedPreset;
        int leftChannel { -1 };
        juce::Array<juce::File> createdFiles;
        struct CreatedFile
        {
            juce::File file;
            juce::uint64 identity;
            juce::int64 size;
            juce::Time modified;
            juce::String sha256;
        };
        std::vector<CreatedFile> ownership;
    };

    // Zero-based input can name either side. Returns -1 for no valid adjacent
    // pair (including orphan/chained Stereo Right modes). Does no file I/O.
    int pairLeftIndex (const juce::ValueTree& preset, int eitherChannel);

    // Run off the message/audio threads with a detached preset snapshot. All
    // occupied zones are converted; originals and the input tree stay intact.
    // Merge averages selected PCM sides; no pan/gain/pitch/markers are baked in.
    using Publish = std::function<juce::Result (const juce::File&, const juce::File&)>;
    juce::Result prepare (const juce::File& folder, const juce::ValueTree& preset,
                           int eitherChannel, Mode mode, Result& result, Publish publish = {});

    // For failed/stale, unapplied results only; retains changed files and reports
    // their paths rather than deleting another writer's data. Idempotent.
    juce::Result cleanup (Result& result);

    // After the caller's stale-snapshot check, free the live right channel's
    // Stereo Right mode BEFORE copying editedPreset into the live preset, so
    // existing left-to-right callbacks cannot repopulate the freed channel.
}
