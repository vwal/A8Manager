#pragma once

#include "WaveformDesign.h"

// Explicit raw-audio import, not generated-recipe recall. No writes, audition or
// live preset mutation. Call off the audio callback. Unknown audio is never
// classified as CV merely because of its frequency or waveform shape.
namespace RawCycleImport
{
    enum class StereoChannel { unspecified, left, right, average };
    struct Info
    {
        juce::File file;
        juce::String name;
        double sampleRate {};
        int frames {}, channels {}, bitsPerSample {};
        bool floatingPoint {};
        juce::StringArray warnings;
    };

    // 4..8192 frames, mono/stereo integer PCM or IEEE float WAV. CV provenance,
    // hardware-test records, linked files, invalid samples and long recordings
    // are rejected. inspect validates PCM too, without choosing a stereo side.
    juce::Result inspect (const juce::File& file, Info& info);
    // Stereo requires an explicit side or average. Atomic: output Settings and
    // optional Info stay unchanged on failure. Chooses the next power-of-two
    // output length (64..8192), retaining every source frame in the recipe.
    juce::Result load (const juce::File& file, StereoChannel channel,
                       WaveformDesign::Settings& settings, Info* info = nullptr);
}
