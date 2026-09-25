#include "AudioPlayerProperties.h"
#include <cmath>

void AudioPlayerProperties::initValueTree ()
{
    setPlayState (PlayState::stop, false);
    setSampleSource (-1, -1, false);
    setSamplePointsSelector (SamplePointsSelector::SamplePoints, false);
    setPlaybackPosition (-1.0, false);
    setAuditionRate (1.0, false);
    setPreservePitch (true, false);
}

void AudioPlayerProperties::setPreservePitch (bool preserve, bool includeSelfCallback)
{
    setValue (preserve, PreservePitchPropertyId, includeSelfCallback);
}

bool AudioPlayerProperties::getPreservePitch ()
{
    return data.hasProperty (PreservePitchPropertyId) ? getValue<bool> (PreservePitchPropertyId) : true;
}

void AudioPlayerProperties::setAuditionRate (double rate, bool includeSelfCallback)
{
    setValue (std::isfinite (rate) ? juce::jlimit (minAuditionRate, maxAuditionRate, rate) : 1.0,
              AuditionRatePropertyId, includeSelfCallback);
}

double AudioPlayerProperties::getAuditionRate ()
{
    const auto rate { data.hasProperty (AuditionRatePropertyId) ? getValue<double> (AuditionRatePropertyId) : 1.0 };
    return std::isfinite (rate) ? juce::jlimit (minAuditionRate, maxAuditionRate, rate) : 1.0;
}

void AudioPlayerProperties::setPlaybackPosition (double position, bool includeSelfCallback)
{
    setValue (position, PlaybackPositionPropertyId, includeSelfCallback);
}

double AudioPlayerProperties::getPlaybackPosition ()
{
    return getValue<double> (PlaybackPositionPropertyId);
}

void AudioPlayerProperties::setPlayState (PlayState playState, bool includeSelfCallback)
{
    setValue (static_cast<int> (playState), PlayStatePropertyId, includeSelfCallback);
}

void AudioPlayerProperties::setSampleSource (int channelIndex, int zoneIndex, bool includeSelfCallback)
{
    const auto channelAndZoneIndiciesString { juce::String (channelIndex) + "," + juce::String (zoneIndex) };
    setValue (channelAndZoneIndiciesString, SampleSourcePropertyId, includeSelfCallback);
}

void AudioPlayerProperties::setSamplePointsSelector (SamplePointsSelector samplePointsSelector, bool includeSelfCallback)
{
    setValue (static_cast<int> (samplePointsSelector), SamplePointsSelectorPropertyId, includeSelfCallback);
}

void AudioPlayerProperties::showConfigDialog (bool includeSelfCallback)
{
    toggleValue (ShowConfigDialogPropertyId, includeSelfCallback);
}

AudioPlayerProperties::PlayState AudioPlayerProperties::getPlayState ()
{
    return static_cast<PlayState> (getValue<int> (PlayStatePropertyId));
}

std::tuple<int, int> AudioPlayerProperties::getSampleSource ()
{
    const auto channelAndZoneIndiciesStrings { juce::StringArray::fromTokens (getValue<juce::String> (SampleSourcePropertyId), ",", "") };
    jassert (channelAndZoneIndiciesStrings.size () == 2);
    return { channelAndZoneIndiciesStrings[0].getIntValue (), channelAndZoneIndiciesStrings [1].getIntValue () };
}

AudioPlayerProperties::SamplePointsSelector AudioPlayerProperties::getSamplePointsSelector ()
{
    return static_cast<SamplePointsSelector> (getValue<int> (SamplePointsSelectorPropertyId));
}

void AudioPlayerProperties::valueTreePropertyChanged (juce::ValueTree& treeWhosePropertyHasChanged, const juce::Identifier& property)
{
    if (treeWhosePropertyHasChanged == data)
    {
        if (property == PreservePitchPropertyId)
        {
            if (onPreservePitchChange != nullptr)
                onPreservePitchChange (getPreservePitch ());
        }
        else if (property == AuditionRatePropertyId)
        {
            if (onAuditionRateChange != nullptr)
                onAuditionRateChange (getAuditionRate ());
        }
        else if (property == PlaybackPositionPropertyId)
        {
            if (onPlaybackPositionChange != nullptr)
                onPlaybackPositionChange (getPlaybackPosition ());
        }
        else if (property == PlayStatePropertyId)
        {
            if (onPlayStateChange != nullptr)
                onPlayStateChange (getPlayState ());
        }
        else if (property == SampleSourcePropertyId)
        {
            if (onSampleSourceChanged != nullptr)
                onSampleSourceChanged (getSampleSource ());
        }
        else if (property == SamplePointsSelectorPropertyId)
        {
            if (onSamplePointsSelectorChanged != nullptr)
                onSamplePointsSelectorChanged (getSamplePointsSelector ());
        }
        else if (property == ShowConfigDialogPropertyId)
        {
            if (onShowConfigDialog != nullptr)
                onShowConfigDialog ();
        }
    }
}
