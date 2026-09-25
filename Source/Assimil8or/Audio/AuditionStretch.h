#pragma once

#include <JuceHeader.h>

// Streaming time/pitch preview. Preparation allocates the DSP state; processing
// uses bounded scratch space, not a stretched copy of the entire sample.
class AuditionStretch
{
public:
    AuditionStretch ();
    ~AuditionStretch ();
    void prepare (double sampleRate);
    void reset (const juce::AudioBuffer<float>& source, int start, int end, double cursor,
                double speed, double pitchSemitones, bool looping);
    void process (const juce::AudioSourceChannelInfo& output, double speed);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE (AuditionStretch)
};
