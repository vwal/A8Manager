#include "Assimil8or/Audio/AudioPlayer.h"
#include <iostream>
#include <limits>
#include <stdexcept>

struct WaveformAuditionRoutingTestAccess
{
    static void run ()
    {
        using State = AudioPlayerProperties::PlayState;
        auto check = [] (bool ok, const char* message) { if (! ok) throw std::runtime_error (message); };
        auto payloadFor = [&] (int frames, double sourceRate = 48000.0)
        {
            auto settings { WaveformDesign::startingPoint (WaveformDesign::Mode::oscillator, WaveformDesign::Shape::sine) };
            settings.cycleFrames = frames;
            settings.sampleRate = sourceRate;
            WaveformDesign::Render rendered;
            check (WaveformDesign::render (settings, rendered).wasOk (), "Render routing fixture");
            WaveformAudition::PayloadPtr payload;
            check (WaveformAudition::preparePayload (settings, rendered, payload).wasOk (), "Prepare routing fixture off callback");
            return payload;
        };

        // Use the real shared output callback and properties, but never init an
        // audio device or access the user's preset/preferences/sample files.
        AudioPlayer player;
        AudioPlayerProperties observer (player.audioPlayerProperties.getValueTree (), AudioPlayerProperties::WrapperType::client,
                                        AudioPlayerProperties::EnableCallbacks::yes);
        check (observer.getOutputDeviceName ().isEmpty (), "No device is reported before an output is actually open");
        juce::String displayedDevice;
        observer.onOutputDeviceNameChange = [&] (juce::String name) { displayedDevice = name; };
        player.audioPlayerProperties.setOutputDeviceName ("Test output", false);
        check (displayedDevice == "Test output", "Output device updates propagate to the UI observer");
        player.publishOutputDevice ();
        check (displayedDevice.isEmpty () && observer.getOutputDeviceName ().isEmpty (),
               "An absent live device clears a stale name instead of reporting a saved configuration");
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
        player.selectedSourceLength = 4096.0;
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
        check (player.setWaveformMonitor (-18.0, -48.0).failed () && player.isWaveformAuditionPausedForRange (),
               "AudioPlayer exposes range-pause intent separately from output activity");
        drain ();
        check (! player.isWaveformAuditionActive () && player.isWaveformAuditionPausedForRange () && player.waveformSelected
               && block.getMagnitude (0, 128) == 0.0f && player.playState == State::stop,
               "A fully faded range pause keeps the designer route silent without leaking the old sample");
        check (player.setWaveformMonitor (-18.0, 0.0).wasOk () && player.isWaveformAuditionActive () && ! player.isWaveformAuditionPausedForRange (),
               "An in-range transpose return resumes through the shared output without a new Start request");
        drain ();
        player.handlePlayState (State::stop); // e.g. old sample invalidated by a scan/cache notification.
        check (player.isWaveformAuditionActive () && player.waveformSelected, "Unrelated sample-stop callbacks cannot interrupt designer route");
        player.stopWaveformAudition ();
        drain ();
        check (! player.isWaveformAuditionActive () && block.getMagnitude (0, 128) < 1.0e-8f && player.playState == State::stop,
               "Designer stop fades to silence and never resumes the old sample");

        check (player.startWaveformAudition ().wasOk (), "Designer explicitly restarts after stop");
        drain ();
        check (player.setWaveformMonitor (-18.0, -48.0).failed () && player.isWaveformAuditionPausedForRange (), "Sample takeover starts from an armed range pause");
        player.audioPlayerProperties.setPlayState (State::loop, true);
        check (! player.isWaveformAuditionActive () && ! player.isWaveformAuditionPausedForRange () && ! player.waveformSelected,
               "Sample Play immediately cancels range-resume intent and relinquishes designer ownership");
        check (player.setWaveformMonitor (-18.0, 0.0).wasOk () && ! player.isWaveformAuditionActive (),
               "Later valid transpose cannot resurrect designer playback over a selected sample");
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
        drain ();
        check (player.setWaveformMonitor (-9.0, -48.0).failed () && player.isWaveformAuditionPausedForRange (), "Null-payload cancellation starts from range pause");
        player.setWaveformAuditionPayload (nullptr);
        drain ();
        check (! player.isWaveformAuditionActive () && ! player.isWaveformAuditionPausedForRange () && block.getMagnitude (0, 128) < 1.0e-8f && player.startWaveformAudition ().failed (),
               "CV/invalid-source clearing stops the designer and blocks stale payload restart");
        player.setWaveformAuditionPayload (payload);
        check (player.setWaveformMonitor (-18.0, 0.0).wasOk () && ! player.isWaveformAuditionActive (), "New valid payload and transpose remain idle after null invalidation");
        check (player.startWaveformAudition ().wasOk (), "A new valid payload is restartable");
        drain ();
        check (player.setWaveformMonitor (-18.0, -48.0).failed () && player.isWaveformAuditionPausedForRange (), "Device removal cancellation starts from range pause");
        player.releaseResources ();
        check (! player.isWaveformAuditionActive () && ! player.isWaveformAuditionPausedForRange () && player.startWaveformAudition ().failed (), "Device removal clears range intent, stops audio and refuses restart");
        player.prepareToPlay (128, 44100.0);
        player.setWaveformMonitor (-18.0, 0.0);
        check (! player.isWaveformAuditionActive () && player.startWaveformAudition ().wasOk (), "Device-rate change requires explicit restart with retained design");
        drain ();
        player.setWaveformMonitor (-18.0, -48.0);
        check (player.isWaveformAuditionPausedForRange (), "Invalid monitor-level fixture pauses");
        check (player.setWaveformMonitor (std::numeric_limits<double>::quiet_NaN (), 0.0).failed () && ! player.isWaveformAuditionPausedForRange (),
               "Nonfinite monitor level is a hard cancellation, not a resumable range event");
        check (player.setWaveformMonitor (-18.0, 0.0).wasOk (), "Normal controls remain usable after invalid level cancellation");
        drain ();
        check (! player.isWaveformAuditionActive (), "Correcting invalid monitor controls does not silently restart audio");
        player.setWaveformAuditionPayload (payloadFor (8192, 48000.0));
        check (player.setWaveformMonitor (-18.0, 72.0).wasOk () && player.startWaveformAudition ().wasOk (),
               "AudioPlayer permits a 48 kHz source's +72 monitor ceiling on the existing 44.1 kHz device");
        drain ();
        player.setWaveformAuditionPayload (payloadFor (8192, 96000.0));
        check (player.isWaveformAuditionPausedForRange () && ! player.waveformAudition.isReady (),
               "A pending 96 kHz source immediately lowers the enforced engine ceiling even before audio adoption");
        drain ();
        check (player.waveformSelected && ! player.isWaveformAuditionActive () && block.getMagnitude (0, 128) == 0.0f,
               "Rate-ceiling invalidation keeps the selected designer route silent without returning to a stale sample");
        player.stopWaveformAudition (); // UI stops before silently adjusting a shrunken slider range.
        check (player.setWaveformMonitor (-18.0, 60.0).wasOk () && ! player.isWaveformAuditionPausedForRange () && ! player.isWaveformAuditionActive (),
               "Stop-before-clamp leaves the corrected rate ceiling idle until explicit Start");
        check (player.startWaveformAudition ().wasOk (), "96 kHz source explicitly restarts at its +60 ceiling");
        check (player.setWaveformMonitor (-18.0, 60.01).failed () && ! player.isWaveformAuditionPausedForRange (),
               "AudioPlayer rejects hardware-overrun input rather than silently clamping or preserving resume intent");
        drain ();
        player.setWaveformAuditionPayload (payload);
        check (player.setWaveformMonitor (-18.0, 0.0).wasOk () && ! player.isWaveformAuditionActive (),
               "Restoring the ordinary source after a rejected rate overrun remains stopped");
        check (player.startWaveformAudition ().wasOk (), "Explicit restart before shutdown test");
        drain ();
        player.setWaveformMonitor (-18.0, -48.0);
        player.shutdownAudio ();
        check (observer.getOutputDeviceName ().isEmpty (), "Shutdown publishes no active output");
        process ();
        check (! player.isWaveformAuditionActive () && ! player.isWaveformAuditionPausedForRange () && player.startWaveformAudition ().failed () && block.getMagnitude (0, 128) < 1.0e-8f,
               "Shutdown leaves shared output silent and unavailable");
        std::cout << "PASS: actual AudioPlayer designer/sample arbitration, stale completion isolation, monitor independence, invalidation and device shutdown/restart\n";
    }
};

void testWaveformAuditionRouting () { WaveformAuditionRoutingTestAccess::run (); }
