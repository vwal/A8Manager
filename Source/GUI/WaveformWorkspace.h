#pragma once

#include <JuceHeader.h>
#include "../Assimil8or/Audio/WaveformDesign.h"
#include "../Assimil8or/Audio/WaveformAudition.h"
#include "../Assimil8or/Audio/WaveformDesignAssignment.h"
#include "PresetEditSession.h"

// A separate design surface. Its host supplies audition through the existing
// audio device. Designing stays separate; explicit assignment edits the host's
// current preset only after generated files and its original context validate.
class WaveformWorkspace : public juce::Component, private juce::Timer
{
public:
    WaveformWorkspace ();
    ~WaveformWorkspace () override;

    void setInitialFolder (juce::File folder);
    using AssignmentContext = PresetEditSession::Snapshot;
    void refreshAssignmentContext ();
    void recallAssigned (int channel, int zone); // Zero-based selected preset coordinates.
    WaveformDesign::Settings getSettings () const;
    std::function<std::optional<AssignmentContext> ()> onGetAssignmentContext;
    std::function<juce::Result (const AssignmentContext&, const WaveformDesign::AssignmentResult&)> onApplyAssignment;
    std::function<std::optional<double> (int)> onMatchDuration; // 0 file, 1 sample, 2 loop
    std::function<void (juce::File)> onOpenExportedFolder;
    std::function<void ()> onClose;
    std::function<void (WaveformAudition::PayloadPtr)> onAuditionPayload;
    std::function<juce::Result ()> onStartAudition;
    std::function<void ()> onStopAudition;
    std::function<juce::Result (double monitorDb, double transposeSemitones)> onAuditionMonitorChange;
    std::function<bool ()> isAuditionActive;

private:
    friend struct WaveformWorkspaceTestAccess;
    std::function<void (const juce::String&, std::function<void (bool)>)> confirmAssignment;
    std::function<void (const juce::String&, std::function<void (bool)>)> confirmRecall;
    std::function<void (const WaveformDesign::Settings&, juce::File, const juce::String&, int)> launchTestOutput;
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
