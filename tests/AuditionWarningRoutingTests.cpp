#include "Assimil8or/Audio/AudioPlayer.h"
#include "Assimil8or/Audio/AuditionSignalCheck.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace { void check (bool okay, const char* message) { if (! okay) throw std::runtime_error (message); } }

// Real shared transport/output callback; no opened device, user files or native dialogs.
struct AuditionWarningRoutingTestAccess
{
    static void run ()
    {
        using State = AudioPlayerProperties::PlayState;
        AudioPlayer player;
        player.sampleRate = player.sourceSampleRate = 48000.0; player.sampleRateRatio = 1.0;
        player.signalCheckEnabled = player.audioDeviceReady = true;
        std::vector<std::function<void ()>> queued;
        player.deferSignalCheck = [&] (auto callback) { queued.push_back (std::move (callback)); return true; };
        auto flush = [&] { auto callbacks { std::move (queued) }; queued.clear (); for (const auto& callback : callbacks) callback (); };
        player.audioPlayerProperties.enableCallbacks (true);
        player.audioPlayerProperties.onPlayStateChange = [&] (auto state) { player.handlePlayState (state); };
        player.audioPlayerProperties.onAutoReduceAuditionChange = [&] (bool value) { player.handleAutoReduceAudition (value); };
        AudioPlayerProperties observer (player.audioPlayerProperties.getValueTree (), AudioPlayerProperties::WrapperType::client,
                                        AudioPlayerProperties::EnableCallbacks::yes);
        observer.onAuditionSignalWarningChange = [] (bool)
        { check (juce::MessageManager::existsAndIsCurrentThread (), "Warning UI changes stay on message thread"); };
        observer.onAuditionAttenuatedChange = [] (bool)
        { check (juce::MessageManager::existsAndIsCurrentThread (), "Reduction UI changes stay on message thread"); };
        check (observer.getAutoReduceAudition (), "Reduction defaults on");
        player.prepareAuditionResampler ();
        int blockedNotices {};
        player.notifySignalBlocked = [&] (const juce::String& reason)
        { check (reason.contains ("not valid"), "Invalid PCM receives a non-overridable explanation"); ++blockedNotices; };
        auto configure = [&] (double hertz, float dc = 0.0f, int frames = 48000)
        {
            player.handlePlayState (State::stop);
            player.sampleBuffer = std::make_shared<juce::AudioBuffer<float>> (2, frames);
            for (int channel {}; channel < 2; ++channel)
                for (int frame {}; frame < frames; ++frame)
                    player.sampleBuffer->setSample (channel, frame,
                        (dc + static_cast<float> (0.25 * std::sin (juce::MathConstants<double>::twoPi * hertz * frame / 48000.0)))
                        * (channel == 1 ? -1.0f : 1.0f));
            ++player.sampleBufferRevision;
            player.sampleProperties.setLengthInSamples (frames, false);
            player.zoneProperties.setSampleStart (0, false); player.zoneProperties.setSampleEnd (frames, false);
            player.zoneProperties.setLoopStart (frames / 2, false); player.zoneProperties.setLoopLength (frames - frames / 2, false);
            player.sampleStart = 0; player.sampleLength = frames; player.selectedSourceLength = frames;
            player.sampleAuditionBlocked = false;
            player.handleChannelPitch (0.0); player.handleZonePitch (0.0);
            player.handleAuditionRate (1.0); player.handlePreservePitch (true);
            player.audioPlayerProperties.setAutoReduceAudition (true, true);
        };
        juce::AudioBuffer<float> block (2, 256);
        auto process = [&]
        {
            std::thread callback ([&] { player.getNextAudioBlock ({ &block, 0, 256 }); }); callback.join ();
            return block.getMagnitude (0, 256);
        };
        auto settle = [&] { for (int i {}; i < 30; ++i) process (); return block.getMagnitude (0, 256); };
        auto start = [&] (State mode)
        { player.audioPlayerProperties.setPlayState (State::stop, true); player.audioPlayerProperties.setPlayState (mode, true); };
        const auto reduced { 0.25f * juce::Decibels::decibelsToGain (static_cast<float> (AuditionSignalCheck::attenuationDecibels)) };

        configure (440.0); start (State::play);
        check (process () == 0.0f, "Initial source waits for finite preflight off callback");
        player.processSignalCheck ();
        check (player.signalApproved && process () > 0.2f && ! observer.getAuditionSignalWarning () && ! observer.getAuditionAttenuated (),
               "Ordinary audio keeps original monitor level");
        for (const auto mode : { State::play, State::loop, State::sampleIntoLoop })
        {
            configure (0.0, 0.25f); start (mode); player.processSignalCheck ();
            check (observer.getAuditionSignalWarning () && observer.getAuditionAttenuated (), "All sample play modes report warning/reduction without approval");
            const auto first { process () };
            check (first > 0.005f && first < reduced * 1.1f, "First warned block is nonzero and already reduced");
            check (std::abs (settle () - reduced) < 0.0001f, "Stereo anti-phase DC gets equal -24 dB reduction");
            const auto cursor { player.curSampleOffset };
            player.audioPlayerProperties.setAutoReduceAudition (false, true);
            check (player.curSampleOffset == cursor && observer.getAuditionSignalWarning () && ! observer.getAuditionAttenuated (),
                   "Disable leaves warning and cursor intact");
            const auto release { process () };
            check (release > reduced && release < 0.1f, "Disable restores gain by ramp, not jump");
            check (std::abs (settle () - 0.25f) < 0.0001f, "Disabled reduction restores original gain");
            player.audioPlayerProperties.setAutoReduceAudition (true, true);
            check (std::abs (settle () - reduced) < 0.0001f, "Reduction can be reenabled live");
            player.handlePlayState (State::stop);
            check (! observer.getAuditionSignalWarning () && ! observer.getAuditionAttenuated () && process () == 0.0f, "Stop clears route indicators");
            start (mode); player.processSignalCheck ();
            check (observer.getAuditionSignalWarning () && process () < reduced * 1.1f, "Cached warned replay stays reduced");
        }

        configure (0.0, 0.25f); start (State::loop); player.processSignalCheck (); process ();
        auto staleKey { player.signalCheckKey () }; auto staleGeneration { player.signalRequestGeneration };
        player.zoneProperties.setSampleStart (1000, false); player.initSamplePoints ();
        const auto afterEdit { player.curSampleOffset };
        check (player.signalApproved && process () > 0.005f, "Live marker edits do not silence finite audio");
        player.finishSignalCheck (staleGeneration, staleKey, true, false);
        check (player.sampleSignalWarning, "Stale clear result cannot release new warning");
        player.processSignalCheck ();
        check (player.curSampleOffset > afterEdit && player.playState == State::loop, "Rechecking preserves cursor progress");
        const auto beforePitch { player.curSampleOffset };
        player.handleZonePitch (-12.0); player.processSignalCheck ();
        check (player.curSampleOffset == beforePitch && process () > 0.005f, "Pitch edits do not retrigger or mute");
        player.handleAuditionRate (0.5); player.processSignalCheck (); player.handlePreservePitch (false); player.processSignalCheck ();
        check (process () > 0.005f, "Speed and Keep pitch edits retain audible transport");

        configure (440.0);
        for (int channel {}; channel < 2; ++channel)
            for (int frame { 24000 }; frame < 48000; ++frame) player.sampleBuffer->setSample (channel, frame, 0.5f);
        player.zoneProperties.setSampleEnd (24000, false); player.initSamplePoints (); start (State::loop); player.processSignalCheck (); process ();
        check (! observer.getAuditionSignalWarning (), "Unselected DC does not warn");
        player.zoneProperties.setSampleEnd (48000, false); player.initSamplePoints (); player.publishAuditionSignalStatus ();
        check (observer.getAuditionAttenuated () && process () > 0.001f, "Pending live edit reduces provisionally without silence");
        player.processSignalCheck ();
        check (observer.getAuditionSignalWarning () && observer.getAuditionAttenuated (), "Expanded DC range warns without interruption");
        player.zoneProperties.setSampleEnd (24000, false); player.initSamplePoints (); player.processSignalCheck ();
        check (! observer.getAuditionSignalWarning () && ! observer.getAuditionAttenuated () && settle () > 0.2f, "Clear range restores level");
        configure (0.0, 0.25f); player.zoneProperties.setSampleEnd (4, false); player.initSamplePoints (); start (State::loop); player.processSignalCheck ();
        for (int i {}; i < 5; ++i)
        {
            player.zoneProperties.setSampleEnd (4 + i, false); player.initSamplePoints ();
            check (process () > 0.0f && player.playState == State::loop, "Tiny loop drags stay playable without warning-related silence");
            player.processSignalCheck ();
        }

        configure (0.0, 0.25f); player.sampleAuditionBlocked = true; player.audioPlayerProperties.setAutoReduceAudition (false, true);
        start (State::loop); player.processSignalCheck ();
        check (player.playState == State::stop && process () == 0.0f && ! observer.getAuditionSignalWarning (), "Known CV cannot bypass protection");
        configure (440.0); player.audioPlayerProperties.setAutoReduceAudition (false, true);
        player.sampleBuffer->setSample (1, 47000, std::numeric_limits<float>::quiet_NaN ());
        player.zoneProperties.setSampleEnd (24000, false); player.initSamplePoints (); start (State::play); player.processSignalCheck ();
        check (player.playState == State::stop && process () == 0.0f && blockedNotices == 1, "Whole-buffer finite preflight blocks hidden NaNs with reduction off");
        configure (440.0, 0.0f, AuditionSignalCheck::maximumFrames + 1);
        player.sampleBuffer->setSample (1, AuditionSignalCheck::maximumFrames, std::numeric_limits<float>::infinity ());
        start (State::play); player.processSignalCheck ();
        check (player.playState == State::stop && blockedNotices == 2, "Finite checks extend beyond heuristic frame cap");
        configure (440.0, 0.0f, AuditionSignalCheck::maximumFrames + 1); start (State::play); player.processSignalCheck ();
        check (player.signalApproved && ! observer.getAuditionSignalWarning (), "Oversized finite audio adds no speculative warning");

        flush (); configure (440.0); start (State::loop); player.processSignalCheck ();
        const auto generation { player.signalRequestGeneration };
        player.handleAuditionRate (1.0); player.handlePreservePitch (true); player.handleChannelPitch (0.0); player.handleZonePitch (0.0);
        check (player.signalRequestGeneration == generation, "No-op edits do not recheck");
        player.handleZonePitch (1.0); player.handleZonePitch (2.0);
        check (queued.size () == 1 && process () > 0.001f, "Rapid edits coalesce without silence"); flush ();
        staleKey = player.signalCheckKey (); staleGeneration = player.signalRequestGeneration;
        player.handlePlayState (State::stop); player.finishSignalCheck (staleGeneration, staleKey, true, true); flush ();
        check (! observer.getAuditionSignalWarning () && process () == 0.0f, "Stop invalidates queued and stale results");

        auto settings { WaveformDesign::startingPoint (WaveformDesign::Mode::oscillator, WaveformDesign::Shape::sine) };
        settings.cycleFrames = 512;
        WaveformDesign::Render rendered; WaveformAudition::PayloadPtr payload;
        check (WaveformDesign::render (settings, rendered).wasOk () && WaveformAudition::preparePayload (settings, rendered, payload).wasOk (), "Prepare designer fixture");
        player.waveformAudition.prepareToPlay (48000.0); player.setWaveformAuditionPayload (payload); player.setWaveformSignalWarning (false);
        check (player.setWaveformMonitor (-18.0, 0.0).wasOk () && player.startWaveformAudition ().wasOk (), "Designer uses common device");
        const auto baseline { settle () }; check (baseline > 0.01f, "Designer fixture audible");
        player.setWaveformSignalWarning (true); const auto lowered { settle () };
        check (observer.getAuditionSignalWarning () && observer.getAuditionAttenuated () && lowered > 0.0f && lowered < baseline * 0.07f,
               "Designer shares the -24 dB final monitor reduction");
        player.stopWaveformAudition (); player.setWaveformSignalWarning (false);
        check (! observer.getAuditionSignalWarning () && ! observer.getAuditionAttenuated (), "Designer Stop clears indicators immediately");
        check (process () < baseline * 0.07f, "Stop fade never rises when UI clears warning"); settle ();
        check (process () == 0.0f, "Designer stop drains to silence");
        player.setWaveformSignalWarning (true); check (player.startWaveformAudition ().wasOk (), "Warned designer restarts without approval");
        check (process () < baseline * 0.07f, "Warned designer has no full-level initial burst");
        player.setWaveformMonitor (-18.0, -48.0);
        check (! observer.getAuditionSignalWarning () && ! observer.getAuditionAttenuated (), "Range-pause hides audible-route warning");
        player.setWaveformMonitor (-18.0, 0.0);
        check (observer.getAuditionSignalWarning () && observer.getAuditionAttenuated (), "Range-resume restores warning");
        player.shutdownAudio ();
        check (! observer.getAuditionSignalWarning () && ! observer.getAuditionAttenuated () && process () == 0.0f, "Shutdown clears route status");

        std::function<void ()> afterDestruction;
        {
            auto temporary { std::make_unique<AudioPlayer> () }; temporary->signalCheckEnabled = true;
            temporary->deferSignalCheck = [&] (auto callback) { afterDestruction = std::move (callback); return true; };
            temporary->sampleLength = 48000; temporary->selectedSourceLength = 48000; temporary->handlePlayState (State::play);
        }
        check (static_cast<bool> (afterDestruction), "Fixture retains queued check"); afterDestruction ();
    }
};

void testAuditionWarningRouting ()
{
    AuditionWarningRoutingTestAccess::run ();
    std::cout << "PASS: shared optional audition reduction, uninterrupted edits, route indicators, CV/nonfinite and lifetime guards\n";
}
