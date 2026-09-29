#pragma once

#include <JuceHeader.h>
#include <atomic>
#include "AudioPlayerProperties.h"
#include "AuditionStretch.h"
#include "WaveformAudition.h"
#include "AudioSettingsProperties.h"
#include "../Preset/ChannelProperties.h"
#include "../Preset/PresetProperties.h"
#include "../Preset/ZoneProperties.h"
#include "../../AppProperties.h"
#include "../../Assimil8or/Assimil8orPreset.h"
#include "../../GUI/Assimil8or/Editor/SampleManager/SampleManagerProperties.h"
#include "../../GUI/Assimil8or/Editor/SampleManager/SampleProperties.h"

class AudioPlayer : public juce::AudioSource,
                    public juce::ChangeListener,
                    private juce::Timer
{
public:
    void init (juce::ValueTree rootProperties);
    void shutdownAudio ();

    // Designer monitoring shares the existing device and excludes sample playback.
    void setWaveformAuditionPayload (WaveformAudition::PayloadPtr payload);
    juce::Result startWaveformAudition ();
    void stopWaveformAudition ();
    juce::Result setWaveformMonitor (double decibels, double semitones);
    bool isWaveformAuditionActive () const;
    bool isWaveformAuditionPausedForRange () const;
    void showAudioSettings () { showConfigDialog (); }

private:
    friend struct AudioPlayerTestAccess;
    friend struct AudioAuditTestAccess;
    friend struct WaveformAuditionRoutingTestAccess;
    friend struct CvAuditionTestAccess;
    AudioSettingsProperties audioSettingsProperties;
    AudioPlayerProperties audioPlayerProperties;
    AppProperties appProperties;
    PresetProperties presetProperties;
    SampleManagerProperties sampleManagerProperties;
    ChannelProperties channelProperties;
    ZoneProperties zoneProperties;
    SampleProperties sampleProperties;
    ChannelProperties nextChannelProperties;
    ZoneProperties nextZoneProperties;
    SampleProperties nextSampleProperties;
    ChannelProperties previousChannelProperties;
    SampleProperties previousSampleProperties;

    juce::AudioDeviceManager audioDeviceManager;
    juce::AudioSourcePlayer audioSourcePlayer;
    std::unique_ptr < juce::AudioBuffer<float>> sampleBuffer;
    juce::AudioDeviceSelectorComponent audioSetupComp { audioDeviceManager, 0, 0, 0, 256, false, false, true, false };

    juce::CriticalSection dataCS;
    WaveformAudition waveformAudition;
    bool waveformSelected { false }, audioDeviceReady { false };
    bool sampleAuditionBlocked { false }; // Cached off the audio callback, protected by dataCS.
    AudioPlayerProperties::PlayState playState { AudioPlayerProperties::PlayState::stop };
    double curSampleOffset { 0.0 }; // audible cursor, independent of resampler read-ahead
    int sampleStart { 0 };
    int sampleLength { 0 };
    std::atomic<double> playbackPosition { -1.0 };
    std::atomic<bool> playbackFinished { false };
    std::atomic<AudioPlayerProperties::SimulationPhase> simulationPhase { AudioPlayerProperties::SimulationPhase::inactive };
    int simulationLoopStart { 0 };
    bool simulationRangeActive { false };

    double sampleRate { 44100.0 };
    int blockSize { 128 };
    double sampleRateRatio { 0.0 };
    double auditionRate { 1.0 };
    double zonePitchOffset { 0.0 };
    bool preservePitch { true };
    AuditionStretch auditionStretch;
    int readSampleOffset { 0 };
    int renderedStart { 0 }, renderedEnd { 0 }, renderedLoopStart { 0 };
    bool resetAuditionResampler { true };

    class AuditionInputSource : public juce::AudioSource
    {
    public:
        explicit AuditionInputSource (AudioPlayer& player) : owner (player) {}
        void prepareToPlay (int, double) override {}
        void releaseResources () override {}
        void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override { owner.renderAuditionInput (info); }
    private:
        AudioPlayer& owner;
    };
    AuditionInputSource auditionInput { *this };
    juce::ResamplingAudioSource auditionResampler { &auditionInput, false, 2 };

    class LeftRightCombinerAudioSource : public juce::AudioSource
    {
    public:
        LeftRightCombinerAudioSource (AudioSource* leftInputSource, int leftInputIndex, AudioSource* rightInputSource, int rightInputIndex, const bool deleteInputWhenDeleted)
            : leftInput { leftInputSource, deleteInputWhenDeleted },
              rightInput { rightInputSource, deleteInputWhenDeleted },
              leftChannelIndex { leftInputIndex },
              rightChannelIndex { rightInputIndex }
        {
            jassert (leftInput != nullptr || rightInput != nullptr);
        }

        void prepareToPlay (int, double) {}
        void releaseResources () {}
        void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
        {
            jassert (bufferToFill.buffer->getNumChannels () == 2);
            tempBuffer.setSize (2, bufferToFill.buffer->getNumSamples ());
            juce::AudioSourceChannelInfo tempBufferChannelInfo (&tempBuffer, 0, bufferToFill.numSamples);

            bufferToFill.buffer->clear ();
            if (leftInput != nullptr)
            {
                tempBuffer.clear ();
                leftInput->getNextAudioBlock (tempBufferChannelInfo);
                bufferToFill.buffer->copyFrom (0, bufferToFill.startSample, tempBufferChannelInfo.buffer->getReadPointer (leftChannelIndex, 0),
                    juce::jmin (tempBufferChannelInfo.buffer->getNumSamples (), bufferToFill.numSamples));
            }
            if (rightInput != nullptr)
            {
                tempBuffer.clear ();
                rightInput->getNextAudioBlock (tempBufferChannelInfo);
                bufferToFill.buffer->copyFrom (1, bufferToFill.startSample, tempBufferChannelInfo.buffer->getReadPointer (rightChannelIndex, 0),
                    juce::jmin (tempBufferChannelInfo.buffer->getNumSamples (), bufferToFill.numSamples));
            }
        }
    private:
        juce::AudioBuffer<float> tempBuffer;
        juce::OptionalScopedPointer<AudioSource> leftInput;
        juce::OptionalScopedPointer<AudioSource> rightInput;
        int leftChannelIndex { 0 };
        int rightChannelIndex { 0 };
    };

    void configureAudioDevice (juce::String deviceName);
    void publishOutputDevice ();
    void handlePlayState (AudioPlayerProperties::PlayState playState);
    void handleAuditionRate (double rate);
    void handlePreservePitch (bool preserve);
    void handleZonePitch (double semitones);
    double effectiveAuditionRate () const;
    void prepareAuditionResampler ();
    void renderAuditionInput (const juce::AudioSourceChannelInfo& bufferToFill);
    void initFromZone (std::tuple<int, int> channelAndZoneIndecies);
    void initSamplePoints ();
    bool initSimulationPoints ();
    bool isStereoPair ();
    bool selectedSampleIsCv ();
    void prepareSampleForPlayback ();
    void showConfigDialog ();

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources () override;
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void timerCallback () override;
};
