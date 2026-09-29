#include "AudioPlayer.h"
#include "SampleLoopSimulation.h"
#include <cmath>
#include <limits>
#include "../../Assimil8or/PresetManagerProperties.h"
#include "oolib/Debug/DebugLog.h"
#include "oolib/Properties/PersistentRootProperties.h"
#include "oolib/Properties/RuntimeRootProperties.h"

#define LOG_AUDIO_PLAYER 0
#if LOG_AUDIO_PLAYER
#define LogAudioPlayer(text) DebugLog ("AudioPlayer", text);
#else
#define LogAudioPlayer(text) ;
#endif

#define LOG_AUDIO_PLAY_BACK 0
#if LOG_AUDIO_PLAY_BACK
#define LogAudioPlayback(text) DebugLog ("AudioPlayer", text);
#else
#define LogAudioPlayback(text) ;
#endif

void AudioPlayer::init (juce::ValueTree rootPropertiesVT)
{
    PersistentRootProperties persistentRootProperties (rootPropertiesVT, PersistentRootProperties::WrapperType::client, PersistentRootProperties::EnableCallbacks::no);
    RuntimeRootProperties runtimeRootProperties (rootPropertiesVT, RuntimeRootProperties::WrapperType::client, RuntimeRootProperties::EnableCallbacks::no);

    PresetManagerProperties presetManagerProperties (runtimeRootProperties.getValueTree (), PresetManagerProperties::WrapperType::owner, PresetManagerProperties::EnableCallbacks::no);
    presetProperties.wrap (presetManagerProperties.getPreset ("edit"), PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::yes);
    sampleManagerProperties.wrap (runtimeRootProperties.getValueTree (), SampleManagerProperties::WrapperType::client, SampleManagerProperties::EnableCallbacks::no);

    appProperties.wrap (persistentRootProperties.getValueTree (), AppProperties::WrapperType::owner, AppProperties::EnableCallbacks::yes);

    audioSettingsProperties.wrap (persistentRootProperties.getValueTree (), AudioSettingsProperties::WrapperType::owner, AudioSettingsProperties::EnableCallbacks::yes);
    audioSettingsProperties.onConfigChange = [this] (juce::String config)
    {
            // TODO - do we need this callback?
        //configureAudioDevice (deviceName);
    };

    audioPlayerProperties.wrap (runtimeRootProperties.getValueTree (), AudioPlayerProperties::WrapperType::owner, AudioPlayerProperties::EnableCallbacks::yes);
    audioPlayerProperties.onShowConfigDialog = [this] () { showConfigDialog (); };
    audioPlayerProperties.onAuditionRateChange = [this] (double rate) { handleAuditionRate (rate); };
    audioPlayerProperties.onPreservePitchChange = [this] (bool preserve) { handlePreservePitch (preserve); };
    handlePreservePitch (audioPlayerProperties.getPreservePitch ());
    handleAuditionRate (audioPlayerProperties.getAuditionRate ());
    audioPlayerProperties.onPlayStateChange = [this] (AudioPlayerProperties::PlayState newPlayState)
    {
        LogAudioPlayer ("init: audioPlayerProperties.onPlayStateChange");
        handlePlayState (newPlayState);
    };
    // Clients call this to setup the sample source
    audioPlayerProperties.onSampleSourceChanged = [this] (std::tuple<int, int> channelAndZoneIndecies)
    {
        LogAudioPlayer ("init: audioPlayerProperties.onSampleSourceChanged");
        initFromZone (channelAndZoneIndecies);
    };
    // Clients call this to change which sample points are used, Sample or Loop
    audioPlayerProperties.onSamplePointsSelectorChanged = [this] (AudioPlayerProperties::SamplePointsSelector)
    {
        LogAudioPlayer ("init: audioPlayerProperties.onSamplePointsSelectorChanged");
        initSamplePoints ();
    };
    audioDeviceManager.addChangeListener (this);
    configureAudioDevice (audioSettingsProperties.getConfig ());
    startTimerHz (30);
}

void AudioPlayer::initFromZone (std::tuple<int, int> channelAndZoneIndecies)
{
    juce::ScopedLock sourceLock (dataCS);
    if (playState == AudioPlayerProperties::PlayState::sampleIntoLoop)
    {
        handlePlayState (AudioPlayerProperties::PlayState::stop);
        playbackFinished.store (true);
    }
    playbackPosition.store (-1.0);
    LogAudioPlayer ("initFromZone");
    auto [channelIndex, zoneIndex] { channelAndZoneIndecies };
    if (channelIndex < 0 || channelIndex >= 8 || zoneIndex < 0 || zoneIndex >= 8)
    {
        handlePlayState (AudioPlayerProperties::PlayState::stop);
        playbackFinished.store (true); // Publish the cleared source's STOP on the message timer.
        sampleBuffer.reset ();
        sampleStart = sampleLength = 0;
        return;
    }
    channelProperties.wrap (presetProperties.getChannelVT (channelIndex), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::yes);
    zoneProperties.wrap (channelProperties.getZoneVT (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::yes);
    zoneProperties.onPitchOffsetChange = [this] (double semitones) { handleZonePitch (semitones); };
    handleZonePitch (zoneProperties.getPitchOffset ());
    sampleProperties.wrap (sampleManagerProperties.getSamplePropertiesVT (channelIndex, zoneIndex), SampleProperties::WrapperType::owner, SampleProperties::EnableCallbacks::yes);

    if (channelIndex > 0)
    {
        previousChannelProperties.wrap (presetProperties.getChannelVT (channelIndex - 1), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::yes);
        previousSampleProperties.wrap (sampleManagerProperties.getSamplePropertiesVT (channelIndex - 1, zoneIndex), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::yes);
    }
    else
    {
        previousChannelProperties.enableCallbacks (false);
        previousSampleProperties.enableCallbacks (false);
        previousChannelProperties.release ();
        previousSampleProperties.release ();
    }

    if (channelIndex < 7)
    {
        const auto nextChannelIndex { channelIndex + 1 };
        nextChannelProperties.wrap (presetProperties.getChannelVT (nextChannelIndex), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::yes);
        nextZoneProperties.wrap (nextChannelProperties.getZoneVT (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::yes);
        nextSampleProperties.wrap (sampleManagerProperties.getSamplePropertiesVT (nextChannelIndex, zoneIndex), SampleProperties::WrapperType::owner, SampleProperties::EnableCallbacks::yes);
    }
    else
    {
        // Channel eight has no partner. An invalid client wrap is an error and
        // retains the previous tree; release the optional bindings explicitly.
        nextChannelProperties.enableCallbacks (false);
        nextZoneProperties.enableCallbacks (false);
        nextSampleProperties.enableCallbacks (false);
        nextChannelProperties.release ();
        nextZoneProperties.release ();
        nextSampleProperties.release ();
    }

    channelProperties.onChannelModeChange = [this] (int)
    {
        LogAudioPlayer ("channelProperties.onChannelModeChange");
        initSamplePoints ();
        prepareSampleForPlayback ();
    };
    zoneProperties.onSampleChange = [this] (juce::String)
    {
        LogAudioPlayer ("zoneProperties.onSampleChange");
        initSamplePoints ();
        prepareSampleForPlayback ();
    };
    zoneProperties.onSideChange = [this] (int)
    {
        LogAudioPlayer ("zoneProperties.onSideChange");
        initSamplePoints ();
        prepareSampleForPlayback ();
    };
    nextChannelProperties.onChannelModeChange = [this] (int)
    {
        LogAudioPlayer ("nextChannelProperties.onChannelModeChange");
        prepareSampleForPlayback ();
    };
    nextZoneProperties.onSampleChange = [this] (juce::String)
    {
        LogAudioPlayer ("nextZoneProperties.onSampleChange");
        if (isStereoPair ()) prepareSampleForPlayback ();
    };
    nextZoneProperties.onSideChange = [this] (int)
    {
        LogAudioPlayer ("nextZoneProperties.onSideChange");
        if (isStereoPair ()) prepareSampleForPlayback ();
    };

    // Only the selected pair may change the audition range.
    zoneProperties.onSampleStartChange = [this] (std::optional<juce::int64>)
    {
        if (audioPlayerProperties.getPlayState () == AudioPlayerProperties::PlayState::sampleIntoLoop ||
            audioPlayerProperties.getSamplePointsSelector () == AudioPlayerProperties::SamplePointsSelector::SamplePoints) initSamplePoints ();
    };
    zoneProperties.onSampleEndChange = [this] (std::optional<juce::int64>)
    {
        if (audioPlayerProperties.getPlayState () == AudioPlayerProperties::PlayState::sampleIntoLoop ||
            audioPlayerProperties.getSamplePointsSelector () == AudioPlayerProperties::SamplePointsSelector::SamplePoints) initSamplePoints ();
    };
    zoneProperties.onLoopStartChange = [this] (std::optional<juce::int64>)
    {
        if (audioPlayerProperties.getPlayState () == AudioPlayerProperties::PlayState::sampleIntoLoop ||
            audioPlayerProperties.getSamplePointsSelector () == AudioPlayerProperties::SamplePointsSelector::LoopPoints) initSamplePoints ();
    };
    zoneProperties.onLoopLengthChange = [this] (std::optional<double>)
    {
        if (audioPlayerProperties.getPlayState () == AudioPlayerProperties::PlayState::sampleIntoLoop ||
            audioPlayerProperties.getSamplePointsSelector () == AudioPlayerProperties::SamplePointsSelector::LoopPoints) initSamplePoints ();
    };
    sampleProperties.onStatusChange = [this] (SampleStatus status)
    {
        if (status == SampleStatus::exists)
        {
            LogAudioPlayer ("sampleProperties.onStatusChange: SampleStatus::exists");
            initSamplePoints ();
            prepareSampleForPlayback ();
        }
        else
        {
            // TODO - reset somethings?
            LogAudioPlayer ("sampleProperties.onStatusChange: NOT SampleStatus::exists");
            audioPlayerProperties.setPlayState (AudioPlayerProperties::PlayState::stop, true);
            {
                juce::ScopedLock sl (dataCS);
                sampleStart = 0;
                sampleLength = 0;
                sampleBuffer.reset ();
            }
        }
    };
    nextSampleProperties.onStatusChange = [this] (SampleStatus status)
    {
        juce::ignoreUnused (status);
        // An unrelated channel must never silence the selected preview. A
        // missing paired right sample makes only the right output silent.
        if (isStereoPair ()) prepareSampleForPlayback ();
    };
    sampleProperties.onIsCvChange = [this] (bool) { prepareSampleForPlayback (); };
    nextSampleProperties.onIsCvChange = [this] (bool) { if (isStereoPair ()) prepareSampleForPlayback (); };
    previousSampleProperties.onIsCvChange = [this] (bool)
    {
        if (channelProperties.getChannelMode () == ChannelProperties::ChannelMode::stereoRight) prepareSampleForPlayback ();
    };
    previousChannelProperties.onChannelModeChange = [this] (int)
    {
        if (channelProperties.getChannelMode () == ChannelProperties::ChannelMode::stereoRight) prepareSampleForPlayback ();
    };

    // create local copy of audio data, with resampling if needed
    prepareSampleForPlayback ();

    // setup local sample start and sample length based on samplePointsSource
    initSamplePoints ();
}

void AudioPlayer::initSamplePoints ()
{
    juce::ScopedLock sl (dataCS);
    if (playState == AudioPlayerProperties::PlayState::sampleIntoLoop)
    {
        if (! initSimulationPoints ())
        {
            playState = AudioPlayerProperties::PlayState::stop;
            simulationPhase.store (AudioPlayerProperties::SimulationPhase::inactive);
            playbackPosition.store (-1.0);
            playbackFinished.store (true);
        }
        return;
    }
    simulationRangeActive = false;
    if (! std::isfinite (sampleRateRatio) || sampleRateRatio <= 0.0 || ! zoneProperties.isValid ())
    {
        sampleStart = sampleLength = 0;
        return;
    }
    const auto loop { audioPlayerProperties.getSamplePointsSelector () == AudioPlayerProperties::SamplePointsSelector::LoopPoints };
    const auto start { static_cast<double> (loop ? zoneProperties.getLoopStart ().value_or (0) : zoneProperties.getSampleStart ().value_or (0)) };
    const auto fileLength { static_cast<double> (sampleProperties.getLengthInSamples ()) };
    const auto end { loop ? start + zoneProperties.getLoopLength ().value_or (fileLength - start)
                          : static_cast<double> (zoneProperties.getSampleEnd ().value_or (sampleProperties.getLengthInSamples ())) };
    const auto limit { sampleBuffer != nullptr ? static_cast<double> (sampleBuffer->getNumSamples ()) : 0.0 };
    if (! std::isfinite (start) || ! std::isfinite (end) || end <= start)
    {
        sampleStart = sampleLength = 0;
        return;
    }
    sampleStart = static_cast<int> (std::clamp (start * sampleRateRatio, 0.0, limit));
    const auto endFrame { static_cast<int> (std::clamp (end * sampleRateRatio, static_cast<double> (sampleStart), limit)) };
    sampleLength = endFrame - sampleStart;
    if (curSampleOffset < sampleStart || curSampleOffset >= sampleStart + sampleLength)
        curSampleOffset = sampleStart;
}

bool AudioPlayer::initSimulationPoints ()
{
    const auto range { SampleLoopSimulation::resolve (zoneProperties, sampleProperties.getLengthInSamples ()) };
    if (! range || ! std::isfinite (sampleRateRatio) || sampleRateRatio <= 0.0 || sampleBuffer == nullptr ||
        sampleBuffer->getNumChannels () < 2 || sampleAuditionBlocked)
        return false;
    const auto limit { static_cast<double> (sampleBuffer->getNumSamples ()) };
    const auto toFrame = [this, limit] (double frame) { return static_cast<int> (std::clamp (frame * sampleRateRatio, 0.0, limit)); };
    const auto start { toFrame (static_cast<double> (range->sampleStart)) };
    const auto loopStart { toFrame (static_cast<double> (range->loopStart)) };
    const auto end { toFrame (range->loopEnd) };
    if (end <= start || end <= loopStart) return false;
    if (sampleStart != start || sampleStart + sampleLength != end || simulationLoopStart != loopStart)
        resetAuditionResampler = true;
    sampleStart = start;
    sampleLength = end - start;
    simulationLoopStart = loopStart;
    simulationRangeActive = true;
    // Once captured, moving a loop keeps the cursor in the edited loop. While
    // traversing the intro, marker changes preserve its forward progress.
    if (simulationPhase.load () == AudioPlayerProperties::SimulationPhase::loop)
    {
        if (curSampleOffset < loopStart || curSampleOffset >= end) curSampleOffset = loopStart;
    }
    else
    {
        if (curSampleOffset < start) curSampleOffset = start;
        if (curSampleOffset >= end) curSampleOffset = loopStart;
        if (curSampleOffset >= loopStart) simulationPhase.store (AudioPlayerProperties::SimulationPhase::loop);
    }
    return true;
}

bool AudioPlayer::isStereoPair ()
{
    return channelProperties.isValid () && channelProperties.getChannelMode () != ChannelProperties::ChannelMode::stereoRight &&
           channelProperties.getId () >= 1 && channelProperties.getId () < 8 && nextChannelProperties.isValid () &&
           nextChannelProperties.getChannelMode () == ChannelProperties::ChannelMode::stereoRight;
}

bool AudioPlayer::selectedSampleIsCv ()
{
    if (sampleProperties.isValid () && sampleProperties.getIsCv ()) return true;
    if (isStereoPair () && nextSampleProperties.isValid () && nextSampleProperties.getIsCv ()) return true;
    // Also fail closed for a direct/programmatic selection of the right half
    // of a valid pair. Never allow CV on either output of a stereo audition.
    return channelProperties.isValid () && channelProperties.getChannelMode () == ChannelProperties::ChannelMode::stereoRight
           && previousChannelProperties.isValid () && previousChannelProperties.getChannelMode () != ChannelProperties::ChannelMode::stereoRight
           && previousSampleProperties.isValid () && previousSampleProperties.getIsCv ();
}

void AudioPlayer::prepareSampleForPlayback ()
{
    juce::ScopedLock sl (dataCS);
    // A replacement file, stereo route, or device must not inherit a running
    // simulation from a different source. Marker edits never enter this path.
    if (playState == AudioPlayerProperties::PlayState::sampleIntoLoop)
    {
        playState = AudioPlayerProperties::PlayState::stop;
        playbackFinished.store (true);
    }
    simulationPhase.store (AudioPlayerProperties::SimulationPhase::inactive);
    resetAuditionResampler = true;
    playbackPosition.store (-1.0);
    sampleBuffer.reset ();
    sampleAuditionBlocked = selectedSampleIsCv ();
    if (sampleAuditionBlocked)
    {
        playState = AudioPlayerProperties::PlayState::stop;
        simulationPhase.store (AudioPlayerProperties::SimulationPhase::inactive);
        playbackFinished.store (true);
        sampleStart = sampleLength = 0;
        curSampleOffset = 0.0;
        return;
    }
    if (zoneProperties.isValid () && sampleProperties.isValid () && sampleProperties.getStatus () == SampleStatus::exists &&
        sampleProperties.getAudioBufferPtr () != nullptr && sampleProperties.getAudioBufferPtr ()->getNumChannels () > 0 &&
        std::isfinite (sampleProperties.getSampleRate ()) && sampleProperties.getSampleRate () > 0.0 && std::isfinite (sampleRate) && sampleRate > 0.0)
    {
        const auto sourceFrames { std::clamp (sampleProperties.getLengthInSamples (), juce::int64 { 0 },
            static_cast<juce::int64> (sampleProperties.getAudioBufferPtr ()->getNumSamples ())) };
        const auto outputFrames { static_cast<double> (sourceFrames) * sampleRate / sampleProperties.getSampleRate () };
        if (! std::isfinite (outputFrames) || outputFrames > std::numeric_limits<int>::max ())
        {
            sampleStart = sampleLength = 0;
            curSampleOffset = 0.0;
            return;
        }
        juce::AudioSource* leftAudioSource { nullptr };
        juce::AudioSource* rightAudioSource { nullptr };
        int leftAudioSourceChannel { 0 };
        int rightAudioSourceChannel { 0 };

        LogAudioPlayer ("prepareSampleForPlayback: sample is ready");
        LogAudioPlayer ("prepareSampleForPlayback: num channels: " + juce::String (sampleProperties.getAudioBufferPtr ()->getNumChannels ()));
        std::unique_ptr <juce::MemoryAudioSource> leftReaderSource;
        std::unique_ptr<juce::ResamplingAudioSource> leftResamplingAudioSource;
        std::unique_ptr <juce::MemoryAudioSource> rightReaderSource;
        std::unique_ptr<juce::ResamplingAudioSource> rightResamplingAudioSource;

        if (channelProperties.getChannelMode () != ChannelProperties::ChannelMode::stereoRight && ! isStereoPair ())
        {
            LogAudioPlayer ("prepareSampleForPlayback: master channel only");
            leftReaderSource = std::make_unique<juce::MemoryAudioSource> (*sampleProperties.getAudioBufferPtr (), false, false);
            leftResamplingAudioSource = std::make_unique<juce::ResamplingAudioSource> (leftReaderSource.get (), false, 2);
            sampleRateRatio = sampleRate / sampleProperties.getSampleRate (); // we use the master channel sample rate, since we use the sample points from that
            leftResamplingAudioSource->setResamplingRatio (sampleProperties.getSampleRate () / sampleRate);
            leftResamplingAudioSource->prepareToPlay (blockSize, sampleRate);
            leftAudioSource = leftResamplingAudioSource.get ();
            leftAudioSourceChannel = juce::jlimit (0, std::min (1, sampleProperties.getAudioBufferPtr ()->getNumChannels () - 1), zoneProperties.getSide ());

            rightReaderSource = std::make_unique<juce::MemoryAudioSource> (*sampleProperties.getAudioBufferPtr (), false, false);
            rightResamplingAudioSource = std::make_unique<juce::ResamplingAudioSource> (rightReaderSource.get (), false, 2);
            rightResamplingAudioSource->setResamplingRatio (sampleProperties.getSampleRate () / sampleRate);
            rightResamplingAudioSource->prepareToPlay (blockSize, sampleRate);
            rightAudioSource = rightResamplingAudioSource.get ();
            rightAudioSourceChannel = leftAudioSourceChannel;
        }
        else if (isStereoPair ())
        {
            LogAudioPlayer ("prepareSampleForPlayback: master and stereo/right");
            leftReaderSource = std::make_unique<juce::MemoryAudioSource> (*sampleProperties.getAudioBufferPtr (), false, false);
            leftResamplingAudioSource = std::make_unique<juce::ResamplingAudioSource> (leftReaderSource.get (), false, 2);
            sampleRateRatio = sampleRate / sampleProperties.getSampleRate (); // we use the master channel sample rate, since we use the sample points from that
            leftResamplingAudioSource->setResamplingRatio (sampleProperties.getSampleRate () / sampleRate);
            leftResamplingAudioSource->prepareToPlay (blockSize, sampleRate);
            leftAudioSource = leftResamplingAudioSource.get ();
            leftAudioSourceChannel = juce::jlimit (0, std::min (1, sampleProperties.getAudioBufferPtr ()->getNumChannels () - 1), zoneProperties.getSide ());

            if (nextSampleProperties.getStatus () == SampleStatus::exists && nextSampleProperties.getAudioBufferPtr () != nullptr &&
                nextSampleProperties.getAudioBufferPtr ()->getNumChannels () > 0 && std::isfinite (nextSampleProperties.getSampleRate ()) && nextSampleProperties.getSampleRate () > 0.0)
            {
                rightReaderSource = std::make_unique<juce::MemoryAudioSource> (*nextSampleProperties.getAudioBufferPtr (), false, false);
                rightResamplingAudioSource = std::make_unique<juce::ResamplingAudioSource> (rightReaderSource.get (), false, 2);
                rightResamplingAudioSource->setResamplingRatio (nextSampleProperties.getSampleRate () / sampleRate);
                rightResamplingAudioSource->prepareToPlay (blockSize, sampleRate);
                rightAudioSource = rightResamplingAudioSource.get ();
                rightAudioSourceChannel = juce::jlimit (0, std::min (1, nextSampleProperties.getAudioBufferPtr ()->getNumChannels () - 1), nextZoneProperties.getSide ());
            }
        }
        else
        {
            jassertfalse;
            sampleBuffer = std::make_unique<juce::AudioBuffer<float>> (2, 0);
            curSampleOffset = 0;
            return;
        }

        jassert (leftAudioSource != nullptr || rightAudioSource != nullptr);
        std::unique_ptr<LeftRightCombinerAudioSource> leftRightCombinerAudioSource { std::make_unique<LeftRightCombinerAudioSource> (leftAudioSource, leftAudioSourceChannel,
                                                                                                                                     rightAudioSource, rightAudioSourceChannel, false) };

        sampleBuffer = std::make_unique<juce::AudioBuffer<float>> (2, static_cast<int> (outputFrames));

        leftRightCombinerAudioSource->getNextAudioBlock (juce::AudioSourceChannelInfo (*sampleBuffer.get ()));
        curSampleOffset = 0;
        initSamplePoints (); // sampleRateRatio may have changed with this source/device
    }
    else
    {
        LogAudioPlayer ("prepareSampleForPlayback: sample is NOT ready");
        sampleStart = sampleLength = 0;
        curSampleOffset = 0.0;
    }
}

void AudioPlayer::shutdownAudio ()
{
    stopTimer ();
    {
        juce::ScopedLock sl (dataCS);
        audioDeviceReady = false;
        waveformSelected = false;
        waveformAudition.stopImmediately ();
        playState = AudioPlayerProperties::PlayState::stop;
        simulationPhase.store (AudioPlayerProperties::SimulationPhase::inactive);
        playbackFinished.store (false);
    }
    audioDeviceManager.removeChangeListener (this);
    audioSourcePlayer.setSource (nullptr);
    audioDeviceManager.removeAudioCallback (&audioSourcePlayer);
    audioDeviceManager.closeAudioDevice ();
    audioPlayerProperties.setOutputDeviceName ({}, false);
    playbackPosition.store (-1.0);
}

void AudioPlayer::setWaveformAuditionPayload (WaveformAudition::PayloadPtr payload)
{
    waveformAudition.setPayload (std::move (payload));
}

juce::Result AudioPlayer::startWaveformAudition ()
{
    {
        juce::ScopedLock sl (dataCS);
        if (! audioDeviceReady)
        {
            waveformAudition.stopImmediately ();
            return juce::Result::fail ("No audio output is available. Choose an output in Audio settings, then press Audition again.");
        }
        if (const auto result { waveformAudition.start () }; result.failed ())
            return result;
        waveformSelected = true;
        playState = AudioPlayerProperties::PlayState::stop;
        simulationPhase.store (AudioPlayerProperties::SimulationPhase::inactive);
        resetAuditionResampler = true;
        playbackFinished.store (false);
        playbackPosition.store (-1.0);
    }
    // Notify sample UI without re-entering its playback handler or touching
    // the preset, source selection, markers or audition-speed preferences.
    audioPlayerProperties.setPlayState (AudioPlayerProperties::PlayState::stop, false);
    audioPlayerProperties.setSimulationPhase (AudioPlayerProperties::SimulationPhase::inactive, false);
    audioPlayerProperties.setPlaybackPosition (-1.0, false);
    return juce::Result::ok ();
}

void AudioPlayer::stopWaveformAudition ()
{
    waveformAudition.setPlaying (false);
    // Keep the route silent when the fade completes; never resume an old sample.
}

juce::Result AudioPlayer::setWaveformMonitor (double decibels, double semitones)
{
    juce::ScopedLock sl (dataCS);
    if (! audioDeviceReady)
    {
        waveformAudition.stopImmediately ();
        return juce::Result::fail ("No audio output is available. Choose an output in Audio settings, then press Audition again.");
    }
    if (! std::isfinite (decibels) || decibels < -60.0 || decibels > 0.0)
    {
        waveformAudition.setPlaying (false);
        return juce::Result::fail ("Monitor level must be finite and within -60..0 dB.");
    }
    waveformAudition.setMonitorGain (juce::Decibels::decibelsToGain (decibels));
    return waveformAudition.setTransposeSemitones (semitones);
}

bool AudioPlayer::isWaveformAuditionActive () const
{
    return waveformAudition.isActive ();
}

bool AudioPlayer::isWaveformAuditionPausedForRange () const
{
    return waveformAudition.isPausedForRange ();
}

void AudioPlayer::configureAudioDevice (juce::String config)
{
    juce::String audioConfigError;
    if (config.isEmpty ())
    {
        audioConfigError = audioDeviceManager.initialise (0, 2, nullptr, true);
    }
    else
    {
        auto audioConfigXml { juce::XmlDocument::parse (config) };
        audioConfigError = audioDeviceManager.initialise (0, 2, audioConfigXml.get (), true, {}, nullptr);
    }

    if (! audioConfigError.isEmpty ())
    {
        jassertfalse;
    }

    audioDeviceManager.addAudioCallback (&audioSourcePlayer);
    audioSourcePlayer.setSource (this);
    publishOutputDevice ();
}

void AudioPlayer::publishOutputDevice ()
{
    // Device-manager change notifications run on the message thread. Never
    // touch the UI's ValueTree from prepareToPlay/releaseResources callbacks.
    auto* device { audioDeviceManager.getCurrentAudioDevice () };
    const auto name { device != nullptr && device->isOpen () && device->isPlaying ()
                      && ! device->getActiveOutputChannels ().isZero () ? device->getName () : juce::String {} };
    if (audioPlayerProperties.getOutputDeviceName () != name)
        audioPlayerProperties.setOutputDeviceName (name, false);
}

void AudioPlayer::handlePlayState (AudioPlayerProperties::PlayState newPlayState)
{
    juce::ScopedLock sl (dataCS);
    const auto wasSimulation { simulationRangeActive };
    simulationPhase.store (AudioPlayerProperties::SimulationPhase::inactive);
    if (newPlayState != AudioPlayerProperties::PlayState::stop && sampleAuditionBlocked)
    {
        // Block direct and stale UI requests before touching the independent
        // designer route. Only message-thread classification reads the file.
        playState = AudioPlayerProperties::PlayState::stop;
        playbackPosition.store (-1.0);
        playbackFinished.store (true);
        return;
    }
    if (newPlayState != AudioPlayerProperties::PlayState::stop)
    {
        waveformAudition.stopImmediately ();
        waveformSelected = false;
    }
    resetAuditionResampler = true;
    if (newPlayState == AudioPlayerProperties::PlayState::sampleIntoLoop)
    {
        if (! initSimulationPoints ())
        {
            playState = AudioPlayerProperties::PlayState::stop;
            simulationPhase.store (AudioPlayerProperties::SimulationPhase::inactive);
            playbackPosition.store (-1.0);
            playbackFinished.store (true);
            return;
        }
        curSampleOffset = sampleStart;
        simulationPhase.store (curSampleOffset >= simulationLoopStart ? AudioPlayerProperties::SimulationPhase::loop
                                                                      : AudioPlayerProperties::SimulationPhase::sample);
    }
    else if (wasSimulation)
    {
        playState = newPlayState;
        initSamplePoints (); // Restore the selected ordinary audition range.
    }
    if (newPlayState == AudioPlayerProperties::PlayState::stop)
    {
        LogAudioPlayer ("AudioPlayer::handlePlayState: stop");
    }
    else if (newPlayState == AudioPlayerProperties::PlayState::loop)
    {
        LogAudioPlayer ("AudioPlayer::handlePlayState: loop");
        curSampleOffset = sampleStart;
    }
    else if (newPlayState == AudioPlayerProperties::PlayState::play)
    {
        LogAudioPlayer ("AudioPlayer::handlePlayState: play");
        curSampleOffset = sampleStart;
    }
    playState = newPlayState;
    playbackFinished.store (false);
    playbackPosition.store (newPlayState == AudioPlayerProperties::PlayState::stop || sampleRateRatio <= 0.0
                                ? -1.0 : curSampleOffset / sampleRateRatio);
}

void AudioPlayer::showConfigDialog ()
{
    juce::DialogWindow::LaunchOptions o;
    o.escapeKeyTriggersCloseButton = true;
    o.dialogBackgroundColour = juce::Desktop::getInstance ().getDefaultLookAndFeel ().findColour (juce::ResizableWindow::backgroundColourId);
    o.dialogTitle = "Audio Settings";
    audioSetupComp.setBounds (0, 0, 400, 600);
    o.content.set (&audioSetupComp, false);
    o.launchAsync ();
}

void AudioPlayer::prepareToPlay (int samplesPerBlockExpected, double newSampleRate)
{
    juce::ScopedLock sl (dataCS);
    waveformAudition.prepareToPlay (newSampleRate);
    waveformSelected = false;
    audioDeviceReady = std::isfinite (newSampleRate) && newSampleRate > 0.0 && samplesPerBlockExpected > 0;
    LogAudioPlayer ("prepareToPlay");
    sampleRate = newSampleRate;
    blockSize = samplesPerBlockExpected;
    prepareSampleForPlayback ();
    prepareAuditionResampler ();
}

void AudioPlayer::releaseResources ()
{
    juce::ScopedLock sl (dataCS);
    audioDeviceReady = false;
    waveformSelected = false;
    waveformAudition.stopImmediately ();
    if (playState == AudioPlayerProperties::PlayState::sampleIntoLoop)
    {
        playState = AudioPlayerProperties::PlayState::stop;
        simulationPhase.store (AudioPlayerProperties::SimulationPhase::inactive);
        playbackPosition.store (-1.0);
        playbackFinished.store (true);
    }
    auditionResampler.releaseResources ();
}

void AudioPlayer::handleAuditionRate (double rate)
{
    juce::ScopedLock sl (dataCS);
    auditionRate = std::isfinite (rate) ? juce::jlimit (AudioPlayerProperties::minAuditionRate,
                                                     AudioPlayerProperties::maxAuditionRate, rate) : 1.0;
    auditionResampler.setResamplingRatio (effectiveAuditionRate ());
    // Re-align spectral look-ahead to the audible position at the new rate.
    // Varispeed can change ratio in place without resetting its phase.
    if (preservePitch) resetAuditionResampler = true;
}

void AudioPlayer::handlePreservePitch (bool preserve)
{
    juce::ScopedLock sl (dataCS);
    preservePitch = preserve;
    auditionResampler.setResamplingRatio (effectiveAuditionRate ());
    resetAuditionResampler = true;
}

void AudioPlayer::handleZonePitch (double semitones)
{
    juce::ScopedLock sl (dataCS);
    zonePitchOffset = std::isfinite (semitones) ? juce::jlimit (-90.0, 60.0, semitones) : 0.0;
    auditionResampler.setResamplingRatio (effectiveAuditionRate ());
    if (preservePitch) resetAuditionResampler = true;
}

double AudioPlayer::effectiveAuditionRate () const
{
    return auditionRate * (preservePitch ? 1.0 : std::pow (2.0, zonePitchOffset / 12.0));
}

void AudioPlayer::prepareAuditionResampler ()
{
    // Allocate enough input capacity for the fastest rate, even when starting
    // slowly. Changing speed need not allocate or reprocess the entire sample.
    // Zone offset can add five octaves in varispeed mode (x32).
    const auto capacity { std::ceil (blockSize * AudioPlayerProperties::maxAuditionRate * 32.0 / effectiveAuditionRate ()) };
    auditionResampler.prepareToPlay (static_cast<int> (std::min (capacity, static_cast<double> (std::numeric_limits<int>::max ()))), sampleRate);
    auditionStretch.prepare (sampleRate);
    resetAuditionResampler = true;
}

void AudioPlayer::changeListenerCallback (juce::ChangeBroadcaster*)
{
    publishOutputDevice ();
    LogAudioPlayer ("audio device settings changed");
    auto audioDeviceSettings { audioDeviceManager.createStateXml () };
    if (audioDeviceSettings != nullptr)
    {
        auto xmlString { audioDeviceSettings->toString () };
        audioSettingsProperties.setConfig (xmlString, false);
    }
}

void AudioPlayer::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    bufferToFill.clearActiveBufferRegion ();
    // Audition already uses a lock. Keep the buffer, range and playback cursor
    // in the same snapshot, including while the UI reloads/resamples a sample.
    juce::ScopedLock sl (dataCS);
    if (waveformSelected)
    {
        waveformAudition.process (bufferToFill);
        return;
    }
    if (sampleAuditionBlocked)
    {
        playState = AudioPlayerProperties::PlayState::stop;
        simulationPhase.store (AudioPlayerProperties::SimulationPhase::inactive);
        playbackPosition.store (-1.0);
        playbackFinished.store (true);
        return;
    }
    if (playState == AudioPlayerProperties::PlayState::stop)
        return;

    auto finishPlayback = [this] ()
    {
        playState = AudioPlayerProperties::PlayState::stop;
        simulationPhase.store (AudioPlayerProperties::SimulationPhase::inactive);
        playbackPosition.store (-1.0);
        playbackFinished.store (true);
    };
    if (sampleBuffer == nullptr || sampleBuffer->getNumChannels () < 2 || sampleStart < 0 || sampleLength <= 0 ||
        sampleStart >= sampleBuffer->getNumSamples () || sampleRateRatio <= 0.0)
    {
        finishPlayback ();
        return;
    }

    const auto end { sampleStart + juce::jmin (sampleLength, sampleBuffer->getNumSamples () - sampleStart) };
    const auto simulating { playState == AudioPlayerProperties::PlayState::sampleIntoLoop };
    const auto loopStart { simulating ? simulationLoopStart : sampleStart };
    if (simulating && (loopStart < 0 || loopStart >= end))
    {
        finishPlayback ();
        return;
    }
    const auto activeStart { simulating && simulationPhase.load () == AudioPlayerProperties::SimulationPhase::loop ? loopStart : sampleStart };
    if (curSampleOffset < activeStart || curSampleOffset >= end)
        curSampleOffset = activeStart;
    const auto looping { playState == AudioPlayerProperties::PlayState::loop || simulating };
    const auto stretching { preservePitch && (std::abs (auditionRate - 1.0) > 1.0e-9 || std::abs (zonePitchOffset) > 1.0e-9) };
    const auto speed { effectiveAuditionRate () };
    if (resetAuditionResampler || renderedStart != sampleStart || renderedEnd != end || renderedLoopStart != loopStart)
    {
        // The input may have read beyond the audible cursor. Discard that
        // look-ahead when the source/range changes or playback restarts.
        if (! stretching) curSampleOffset = std::floor (curSampleOffset);
        readSampleOffset = static_cast<int> (curSampleOffset);
        renderedStart = sampleStart;
        renderedEnd = end;
        renderedLoopStart = loopStart;
        auditionResampler.flushBuffers ();
        if (stretching)
            auditionStretch.reset (*sampleBuffer, activeStart, end, curSampleOffset, speed, zonePitchOffset, looping,
                                   simulating ? loopStart : -1);
        resetAuditionResampler = false;
    }
    const auto count { looping ? bufferToFill.numSamples
                               : static_cast<int> (std::min (static_cast<double> (bufferToFill.numSamples),
                                                             std::ceil ((end - curSampleOffset) / speed))) };
    if (count > 0)
    {
        const juce::AudioSourceChannelInfo output { bufferToFill.buffer, bufferToFill.startSample, count };
        if (stretching) auditionStretch.process (output, speed);
        else auditionResampler.getNextAudioBlock (output);
    }
    curSampleOffset += count * speed;
    if (simulating && curSampleOffset >= loopStart)
        simulationPhase.store (AudioPlayerProperties::SimulationPhase::loop);
    if (curSampleOffset >= end)
    {
        if (looping)
            curSampleOffset = loopStart + std::fmod (curSampleOffset - loopStart, end - loopStart);
        else
        {
            finishPlayback ();
            return; // the one-shot tail is already silent
        }
    }
    playbackPosition.store (curSampleOffset / sampleRateRatio);
}

void AudioPlayer::renderAuditionInput (const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Called only by auditionResampler inside getNextAudioBlock's dataCS lock.
    // Feed the resampler, but never finish playback based on its read-ahead.
    bufferToFill.clearActiveBufferRegion ();
    const auto channels { juce::jmin (bufferToFill.buffer->getNumChannels (), sampleBuffer->getNumChannels ()) };
    auto written { 0 };
    while (written < bufferToFill.numSamples)
    {
        if (readSampleOffset >= renderedEnd)
        {
            if (playState == AudioPlayerProperties::PlayState::loop || playState == AudioPlayerProperties::PlayState::sampleIntoLoop)
                readSampleOffset = renderedLoopStart;
            else
                return;
        }
        const auto count { juce::jmin (bufferToFill.numSamples - written, renderedEnd - readSampleOffset) };
        for (auto channel { 0 }; channel < channels; ++channel)
            bufferToFill.buffer->copyFrom (channel, bufferToFill.startSample + written, *sampleBuffer, channel, readSampleOffset, count);
        written += count;
        readSampleOffset += count;
    }
}

void AudioPlayer::timerCallback ()
{
    // All ValueTree/UI notifications stay on the message thread. The audio
    // callback only publishes atomics, including natural one-shot completion.
    if (playbackFinished.exchange (false))
        audioPlayerProperties.setPlayState (AudioPlayerProperties::PlayState::stop, true);
    const auto phase { simulationPhase.load () };
    audioPlayerProperties.setSimulationPhase (phase, true);
    if (phase != AudioPlayerProperties::SimulationPhase::inactive)
        audioPlayerProperties.setSamplePointsSelector (phase == AudioPlayerProperties::SimulationPhase::loop
                                                          ? AudioPlayerProperties::SamplePointsSelector::LoopPoints
                                                          : AudioPlayerProperties::SamplePointsSelector::SamplePoints, true);
    audioPlayerProperties.setPlaybackPosition (playbackPosition.load (), true);
}
