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
    enum class PlayState { stop, play, loop };
    void setPlayState (PlayState playState, bool includeSelfCallback);
    void setSampleSource (int channelIndex, int zoneIndex, bool includeSelfCallback);
    void setSamplePointsSelector (SamplePointsSelector samplePointsSelector, bool includeSelfCallback);
    // Original sample frames; -1 means no active audition. Message-thread only.
    void setPlaybackPosition (double position, bool includeSelfCallback);
    // Runtime audition controls, never preset data.
    static constexpr double minAuditionRate { 0.0625 };
    static constexpr double maxAuditionRate { 4.0 };
    void setAuditionRate (double rate, bool includeSelfCallback);
    void setPreservePitch (bool preserve, bool includeSelfCallback);
    void showConfigDialog (bool includeSelfCallback);

    PlayState getPlayState ();
    std::tuple<int, int> getSampleSource ();
    SamplePointsSelector getSamplePointsSelector ();
    double getPlaybackPosition ();
    double getAuditionRate ();
    bool getPreservePitch ();

    std::function<void (PlayState playState)> onPlayStateChange;
    std::function<void (std::tuple<int, int> channelAndZoneIndecies)> onSampleSourceChanged;
    std::function<void (SamplePointsSelector samplePointsSelector)> onSamplePointsSelectorChanged;
    std::function<void ()> onShowConfigDialog;
    std::function<void (double)> onPlaybackPositionChange;
    std::function<void (double)> onAuditionRateChange;
    std::function<void (bool)> onPreservePitchChange;

    static inline const juce::Identifier AudioConfigTypeId { "AudioPlayer" };
    static inline const juce::Identifier PlayStatePropertyId            { "playState" };
    static inline const juce::Identifier SampleSourcePropertyId         { "sampleSource" };
    static inline const juce::Identifier SamplePointsSelectorPropertyId { "samplePointsSelector" };
    static inline const juce::Identifier ShowConfigDialogPropertyId     { "showConfigDialog" };
    static inline const juce::Identifier PlaybackPositionPropertyId     { "playbackPosition" };
    static inline const juce::Identifier AuditionRatePropertyId         { "auditionRate" };
    static inline const juce::Identifier PreservePitchPropertyId        { "preservePitch" };

    void initValueTree ();
    void processValueTree () {}

private:
    void valueTreePropertyChanged (juce::ValueTree& treeWhosePropertyHasChanged, const juce::Identifier& property) override;
};
