#pragma once

#include <JuceHeader.h>

namespace SampleRename
{
    struct Result
    {
        juce::ValueTree editedPreset;
        juce::String filename;
        int references { 0 };
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

    juce::Result validateName (const juce::String& requestedName, juce::String& finalFilename);
    using Publish = std::function<juce::Result (const juce::File&, const juce::File&)>;
    // Off-thread, detached snapshot only. Copies the file, never renames the
    // original or saves YAML. All references in this preset follow the copy.
    // Caller must reject stale results before applying the edited snapshot.
    juce::Result prepare (const juce::File& folder, const juce::ValueTree& detachedPreset,
                           const juce::String& sourceFilename, const juce::String& requestedName,
                           Result& result, Publish publish = {});
    // Failed/stale results only. Changed/replaced files are retained and reported.
    juce::Result cleanup (Result& result);
}
