#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>

// Saved-file bank assembly only. Run discovery/export on a worker thread; no
// live preset mutation, recursive scan, overwrite or audition takes place.
namespace PresetBankExport
{
    struct Candidate { juce::File presetFile; int slot {}; juce::String name; };
    struct Entry { juce::File presetFile; int targetSlot {}; };
    struct Report
    {
        juce::File folder;
        int presetCount {}, waveCount {}, midiCount {}, renamedWaves {};
        juce::int64 ramBytes {};
        juce::StringArray warnings;
    };
    using Cancel = std::function<bool ()>;
    using Progress = std::function<void (double, const juce::String&)>;

    juce::Result discover (const juce::File& folder, std::vector<Candidate>& candidates, Cancel cancel = {});
    juce::Result validateDestination (const juce::File& destination);
    juce::Result exportBank (const std::vector<Entry>& entries, const juce::File& destination,
                             Report& report, Cancel cancel = {}, Progress progress = {});
    // Bounded ownership-schema recognition, not a hash of the bank contents.
    bool isManifest (const juce::File& file);
    bool isBankFolder (const juce::File& folder);
}
