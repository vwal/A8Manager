#pragma once

#include <JuceHeader.h>

// TODO - refactor to take a ZoneProperties VT and get the data from there
//        Will just need an function to set whether to use Sample or Loop points
class LoopPointsView : public juce::Component, public juce::SettableTooltipClient
{
public:
    LoopPointsView ();
    void setAudioBuffer (juce::AudioBuffer<float>* theAudioBuffer);
    void setLoopPoints (juce::int64 theSampleOffset, double theNumSamples, int theSide, bool theLoopSelected = false);
    std::function<void (bool start)> onContextMenu;

private:
    juce::AudioBuffer<float>* audioBuffer { nullptr };
    juce::int64 sampleOffset { 0 };
    double numSamples { 0.0 };
    int side { 0 };
    bool loopSelected { false };

    void paint (juce::Graphics& g);
    void mouseDown (const juce::MouseEvent& event) override;
};
