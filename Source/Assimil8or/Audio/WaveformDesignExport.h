#pragma once

#include "WaveformDesign.h"

namespace WaveformDesign
{
    struct ExportResult
    {
        juce::File folder;
        juce::File preset;
        juce::Array<juce::File> waves;
        juce::File recipe;
    };

    // Creates a new uniquely named folder. Never overwrites or edits a live preset.
    // WAVs are mono 24-bit PCM; layers use Master/Link channels, not Stereo Right.
    juce::Result exportDesign (const Settings& settings, const juce::File& parentFolder,
                               const juce::String& name, ExportResult& result);
}
