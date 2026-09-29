#pragma once

#include "WaveformDesign.h"

namespace WaveformDesign
{
    struct AssignmentResult
    {
        juce::ValueTree editedPreset;
        juce::Array<juce::File> createdFiles;
        juce::Array<juce::File> waves;
        juce::File recipe;

        // Ownership evidence for rollback: never delete a path that another
        // writer has replaced/edited while an asynchronous apply was pending.
        struct CreatedFile
        {
            juce::File file;
            juce::uint64 identity;
            juce::int64 size;
            juce::Time modified;
        };
        std::vector<CreatedFile> ownership;
    };

    // sourcePreset must be a detached snapshot, not a live message-thread tree.
    // Render/write off-thread; caller applies editedPreset only after verifying
    // its live preset still matches that snapshot. No preset YAML is written.
    // Channels/zones are zero-based. A bank occupies contiguous channels and
    // uses Master + Link; sparse zones and unsafe CV/audio mixtures are rejected.
    juce::Result prepareAssignment (const Settings& settings, const juce::File& folder,
                                    const juce::String& name, const juce::ValueTree& sourcePreset,
                                    int firstChannel, int zone, AssignmentResult& result);

    // Call only for a failed/stale unapplied result. Successful live assignment
    // keeps its files for the shared preset Save workflow. Idempotent; returns
    // an error (and retains ownership details) if an owned file cannot be removed.
    juce::Result cleanupAssignmentFiles (AssignmentResult& result);
}
