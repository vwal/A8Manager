#pragma once

#include <JuceHeader.h>
#include "oolib/ValueTree/ValueTreeWrapper.h"

class AudioPlayerProperties : public ValueTreeWrapper<AudioPlayerProperties>
{
public:
    AudioPlayerProperties () noexcept : ValueTreeWrapper<AudioPlayerProperties> (AudioConfigTypeId) {}
    AudioPlayerProperties (juce::ValueTree vt, WrapperType wrapperType, EnableCallbacks shouldEnableCallbacks)
        : ValueTreeWrapper<AudioPlayerProperties> (AudioConfigTypeId, vt, wrapperType, shouldEnableCallbacks) {}

    enum class SamplePointsSelector { SamplePoints, LoopPoints };
    enum class PlayState { stop, play, loop, sampleIntoLoop };
    enum class SimulationPhase { inactive, sample, loop };
    void setPlayState (PlayState playState, bool includeSelfCallback);
    void setSampleSource (int channelIndex, int zoneIndex, bool includeSelfCallback);
    void setSamplePointsSelector (SamplePointsSelector samplePointsSelector, bool includeSelfCallback);
    // Original sample frames; -1 means no active audition. Message-thread only.
    void setPlaybackPosition (double position, bool includeSelfCallback);
    // Audible simulation phase, published by the player on its message timer.
    void setSimulationPhase (SimulationPhase phase, bool includeSelfCallback);
    // Runtime audition controls, never preset data.
    static constexpr double minAuditionRate { 0.0625 };
    static constexpr double maxAuditionRate { 4.0 };
    void setAuditionRate (double rate, bool includeSelfCallback);
    void setPreservePitch (bool preserve, bool includeSelfCallback);
    // Session-only preference for explicit Samples workspace loads, not a preset setting.
    void setAutoLoopEnabled (bool enabled, bool includeSelfCallback);
    bool getAutoLoopEnabled ();
    // Monitor-only preference and runtime status; never written into a preset.
    void setAutoReduceAudition (bool enabled, bool includeSelfCallback);
    bool getAutoReduceAudition ();
    void setAuditionSignalWarning (bool warning, bool includeSelfCallback);
    bool getAuditionSignalWarning ();
    void setAuditionAttenuated (bool attenuated, bool includeSelfCallback);
    bool getAuditionAttenuated ();
    void showConfigDialog (bool includeSelfCallback);
    // Actual active computer output, not the last saved device configuration.
    void setOutputDeviceName (const juce::String& name, bool includeSelfCallback);
    juce::String getOutputDeviceName ();

    PlayState getPlayState ();
    std::tuple<int, int> getSampleSource ();
    SamplePointsSelector getSamplePointsSelector ();
    double getPlaybackPosition ();
    SimulationPhase getSimulationPhase ();
    double getAuditionRate ();
    bool getPreservePitch ();

    std::function<void (PlayState playState)> onPlayStateChange;
    std::function<void (std::tuple<int, int> channelAndZoneIndecies)> onSampleSourceChanged;
    std::function<void (SamplePointsSelector samplePointsSelector)> onSamplePointsSelectorChanged;
    std::function<void ()> onShowConfigDialog;
    std::function<void (juce::String)> onOutputDeviceNameChange;
    std::function<void (double)> onPlaybackPositionChange;
    std::function<void (SimulationPhase)> onSimulationPhaseChange;
    std::function<void (double)> onAuditionRateChange;
    std::function<void (bool)> onPreservePitchChange;
    std::function<void (bool)> onAutoLoopEnabledChange;
    std::function<void (bool)> onAutoReduceAuditionChange;
    std::function<void (bool)> onAuditionSignalWarningChange;
    std::function<void (bool)> onAuditionAttenuatedChange;

    static inline const juce::Identifier AudioConfigTypeId { "AudioPlayer" };
    static inline const juce::Identifier PlayStatePropertyId            { "playState" };
    static inline const juce::Identifier SampleSourcePropertyId         { "sampleSource" };
    static inline const juce::Identifier SamplePointsSelectorPropertyId { "samplePointsSelector" };
    static inline const juce::Identifier ShowConfigDialogPropertyId     { "showConfigDialog" };
    static inline const juce::Identifier PlaybackPositionPropertyId     { "playbackPosition" };
    static inline const juce::Identifier SimulationPhasePropertyId      { "simulationPhase" };
    static inline const juce::Identifier AuditionRatePropertyId         { "auditionRate" };
    static inline const juce::Identifier PreservePitchPropertyId        { "preservePitch" };
    static inline const juce::Identifier OutputDeviceNamePropertyId     { "outputDeviceName" };
    static inline const juce::Identifier AutoLoopEnabledPropertyId       { "autoLoopEnabled" };
    static inline const juce::Identifier AutoReduceAuditionPropertyId    { "autoReduceAudition" };
    static inline const juce::Identifier AuditionSignalWarningPropertyId { "auditionSignalWarning" };
    static inline const juce::Identifier AuditionAttenuatedPropertyId    { "auditionAttenuated" };

    void initValueTree ();
    void processValueTree () {}

private:
    void valueTreePropertyChanged (juce::ValueTree& treeWhosePropertyHasChanged, const juce::Identifier& property) override;
};
