#include "Assimil8or/Audio/AudioPlayer.h"
#include <iostream>
#include <stdexcept>

struct WaveformAuditionRoutingTestAccess
{
    static void run ()
    {
        using State = AudioPlayerProperties::PlayState;
        auto check = [] (bool ok, const char* message) { if (! ok) throw std::runtime_error (message); };
        auto payloadFor = [&] (int frames)
        {
            auto settings { WaveformDesign::startingPoint (WaveformDesign::Mode::oscillator, WaveformDesign::Shape::sine) };
            settings.cycleFrames = frames;
            WaveformDesign::Render rendered;
            check (WaveformDesign::render (settings, rendered).wasOk (), "Render routing fixture");
            WaveformAudition::PayloadPtr payload;
            check (WaveformAudition::preparePayload (settings, rendered, payload).wasOk (), "Prepare routing fixture off callback");
            return payload;
        };

        // Use the real shared output callback and properties, but never init an
        // audio device or access the user's preset/preferences/sample files.
        AudioPlayer player;
        player.audioPlayerProperties.enableCallbacks (true);
        player.audioPlayerProperties.onPlayStateChange = [&] (State state) { player.handlePlayState (state); };
        player.audioPlayerProperties.onAuditionRateChange = [&] (double value) { player.handleAuditionRate (value); };
        player.audioPlayerProperties.onPreservePitchChange = [&] (bool value) { player.handlePreservePitch (value); };
        const auto payload { payloadFor (512) };
        player.setWaveformAuditionPayload (payload);
        check (player.setWaveformMonitor (-18.0, 0.0).getErrorMessage ().contains ("Audio settings"),
               "Pre-start monitor setup explains an absent device instead of a misleading pitch error");
        check (player.startWaveformAudition ().failed () && ! player.waveformSelected, "Designer cannot start without a prepared output device");
        player.prepareToPlay (128, 48000.0);
        player.sampleRateRatio = 1.0;
        player.sampleStart = 0;
        player.sampleLength = 4096;
        player.sampleBuffer = std::make_unique<juce::AudioBuffer<float>> (2, 4096);
        for (int frame { 0 }; frame < 4096; ++frame)
        {
            player.sampleBuffer->setSample (0, frame, 0.2f);
            player.sampleBuffer->setSample (1, frame, -0.1f);
        }
        player.audioPlayerProperties.setAuditionRate (1.5, true);
        player.audioPlayerProperties.setPreservePitch (false, true);
        const auto presetBefore { player.presetProperties.getValueTree ().createCopy () };
        const auto sourceBefore { player.audioPlayerProperties.getSampleSource () };
        juce::AudioBuffer<float> block (2, 128);
        auto process = [&] ()
        {
            for (int channel { 0 }; channel < 2; ++channel)
                for (int frame { 0 }; frame < 128; ++frame) block.setSample (channel, frame, 0.99f);
            player.getNextAudioBlock ({ &block, 0, 128 });
        };
        auto drain = [&] () { for (int count { 0 }; count < 30; ++count) process (); };
        player.audioPlayerProperties.setPlayState (State::loop, true);
        process ();
        check (block.getMagnitude (0, 0, 128) > 0.1f && player.playState == State::loop, "Existing sample audition works before designer takeover");

        player.playbackFinished.store (true); // A sample ended immediately before its UI timer ran.
        player.playbackPosition.store (100.0);
        check (player.setWaveformMonitor (-18.0, 0.0).wasOk () && player.startWaveformAudition ().wasOk (), "Designer starts with existing device");
        check (player.waveformSelected && player.playState == State::stop && player.audioPlayerProperties.getPlayState () == State::stop
               && ! player.playbackFinished.load () && player.playbackPosition.load () == -1.0,
               "Designer takeover clears stale completion/cursor and stops both sample engine and UI");
        player.timerCallback ();
        check (player.isWaveformAuditionActive (), "A delayed sample completion cannot cancel newly started designer audio");
        drain ();
        bool positive { false }, negative { false };
        for (int count { 0 }; count < 10; ++count)
        {
            process ();
            for (int frame { 0 }; frame < 128; ++frame)
            {
                positive |= block.getSample (0, frame) > 0.001f;
                negative |= block.getSample (0, frame) < -0.001f;
            }
        }
        check (positive && negative, "Shared device callback renders designer waveform, not stale DC sample");
        player.handlePlayState (State::stop); // e.g. old sample invalidated by a scan/cache notification.
        check (player.isWaveformAuditionActive () && player.waveformSelected, "Unrelated sample-stop callbacks cannot interrupt designer route");
        player.stopWaveformAudition ();
        drain ();
        check (! player.isWaveformAuditionActive () && block.getMagnitude (0, 128) < 1.0e-8f && player.playState == State::stop,
               "Designer stop fades to silence and never resumes the old sample");

        check (player.startWaveformAudition ().wasOk (), "Designer explicitly restarts after stop");
        player.audioPlayerProperties.setPlayState (State::loop, true);
        check (! player.isWaveformAuditionActive () && ! player.waveformSelected, "Sample Play immediately relinquishes designer ownership");
        drain ();
        check (block.getMagnitude (0, 0, 128) > 0.1f, "Sample playback resumes only after its explicit Play request");
        player.setWaveformAuditionPayload (payloadFor (8192));
        check (player.startWaveformAudition ().failed () && ! player.waveformSelected && player.playState == State::loop,
               "Rejected sub-audio design does not commandeer a currently playing sample");
        player.setWaveformAuditionPayload (payload);
        check (player.setWaveformMonitor (-9.0, 12.0).wasOk () && player.startWaveformAudition ().wasOk (), "Monitor gain and transpose can change independently");
        check (player.audioPlayerProperties.getAuditionRate () == 1.5 && ! player.audioPlayerProperties.getPreservePitch ()
               && player.audioPlayerProperties.getSampleSource () == sourceBefore && player.presetProperties.getValueTree ().isEquivalentTo (presetBefore),
               "Designer monitoring never changes sample audition preferences, assignments or preset values");
        player.setWaveformAuditionPayload (nullptr);
        drain ();
        check (! player.isWaveformAuditionActive () && block.getMagnitude (0, 128) < 1.0e-8f && player.startWaveformAudition ().failed (),
               "CV/invalid-source clearing stops the designer and blocks stale payload restart");
        player.setWaveformAuditionPayload (payload);
        check (player.startWaveformAudition ().wasOk (), "A new valid payload is restartable");
        player.releaseResources ();
        check (! player.isWaveformAuditionActive () && player.startWaveformAudition ().failed (), "Device removal stops audio and refuses restart");
        player.prepareToPlay (128, 44100.0);
        check (! player.isWaveformAuditionActive () && player.startWaveformAudition ().wasOk (), "Device-rate change requires explicit restart with retained design");
        player.shutdownAudio ();
        process ();
        check (! player.isWaveformAuditionActive () && player.startWaveformAudition ().failed () && block.getMagnitude (0, 128) < 1.0e-8f,
               "Shutdown leaves shared output silent and unavailable");
        std::cout << "PASS: actual AudioPlayer designer/sample arbitration, stale completion isolation, monitor independence, invalidation and device shutdown/restart\n";
    }
};

void testWaveformAuditionRouting () { WaveformAuditionRoutingTestAccess::run (); }
