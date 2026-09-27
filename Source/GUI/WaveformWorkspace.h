#pragma once

#include <JuceHeader.h>
#include "../Assimil8or/Audio/WaveformDesign.h"
#include "../Assimil8or/Audio/WaveformAudition.h"

// A separate design surface. Its host supplies audition through the existing
// audio device; creating/loading a design never edits the open preset or files.
class WaveformWorkspace : public juce::Component, private juce::Timer
{
public:
    WaveformWorkspace ();
    ~WaveformWorkspace () override;

    void setInitialFolder (juce::File folder);
    WaveformDesign::Settings getSettings () const;
    std::function<std::optional<double> (int)> onMatchDuration; // 0 file, 1 sample, 2 loop
    std::function<void (juce::File)> onOpenExportedFolder;
    std::function<void ()> onClose;
    std::function<void (WaveformAudition::PayloadPtr)> onAuditionPayload;
    std::function<juce::Result ()> onStartAudition;
    std::function<void ()> onStopAudition;
    std::function<juce::Result (double monitorDb, double transposeSemitones)> onAuditionMonitorChange;
    std::function<bool ()> isAuditionActive;
    std::function<void ()> onAudioSettings;

private:
    friend struct WaveformWorkspaceTestAccess;
    struct Impl;
    std::unique_ptr<Impl> impl;
    void timerCallback () override;
    void resized () override;
    void paint (juce::Graphics&) override;
    void visibilityChanged () override;
    void updateAuditionVisibility (bool showing);
    juce::Component* getExpandedPreview () const;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformWorkspace)
};
