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
                               const juce::String& name, ExportResult& result, int presetNumber = 1);

    // Shared implementation primitives for detached preset assignment. Call off
    // the audio callback; write only to an exclusively owned staging directory.
    namespace ExportSupport
    {
        juce::String safeStem (const juce::String& name);
        juce::var namedRecipe (const Settings& settings, const juce::String& name);
        juce::Result publishExclusive (const juce::File& source, const juce::File& destination);
        juce::Result writeText (const juce::File& file, const juce::String& text);
        juce::Result writeWave (const juce::File& file, const juce::AudioBuffer<float>& audio, double rate, bool cv);
        void configureChannel (juce::ValueTree channel, const Settings& settings, int voiceIndex, int count);
        void configureZone (juce::ValueTree zone, const juce::String& filename, juce::int64 frames);
    }
}
