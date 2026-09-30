#include "Assimil8or/Audio/AudioPlayer.h"
#include "Assimil8or/Audio/AuditionSignalCheck.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace
{
    void check (bool okay, const char* message) { if (! okay) throw std::runtime_error (message); }
}

// Real sample transport and output callback, but no opened audio device or
// native dialogs. The only injected interaction is the confirmation response.
struct AuditionWarningRoutingTestAccess
{
    static void run ()
    {
        using State = AudioPlayerProperties::PlayState;
        AudioPlayer player;
        player.sampleRate = 48000.0;
        player.sampleRateRatio = 1.0;
        player.sourceSampleRate = 48000.0;
        player.signalCheckEnabled = true;
        player.audioDeviceReady = true;
        std::vector<std::function<void ()>> queuedChecks;
        player.deferSignalCheck = [&] (auto callback) { queuedChecks.push_back (std::move (callback)); return true; };
        const auto flushChecks = [&]
        {
            auto callbacks { std::move (queuedChecks) }; queuedChecks.clear ();
            for (const auto& callback : callbacks) callback ();
        };
        player.audioPlayerProperties.enableCallbacks (true);
        player.audioPlayerProperties.onPlayStateChange = [&] (auto state) { player.handlePlayState (state); };
        player.prepareAuditionResampler ();
        int prompts { 0 };
        int blockedNotices { 0 };
        std::function<void (bool)> reply;
        player.notifySignalBlocked = [&] (const juce::String& reason)
        {
            check (reason.contains ("not valid"), "Blocked PCM receives a visible non-overridable explanation");
            ++blockedNotices;
        };
        player.confirmSignalWarning = [&] (const juce::String& warning, std::function<void (bool)> complete)
        {
            check (juce::MessageManager::existsAndIsCurrentThread (), "Only the message thread presents audition warnings");
            check (warning.contains ("Turn down") && warning.contains ("does not lower"), "Warning explains monitor precaution without implying automatic attenuation");
            ++prompts; reply = std::move (complete);
        };
        auto configure = [&] (double hertz, float dc = 0.0f, int frames = 48000, bool oppositeRight = false)
        {
            player.handlePlayState (State::stop);
            player.sampleBuffer = std::make_shared<juce::AudioBuffer<float>> (2, frames);
            for (int channel { 0 }; channel < 2; ++channel)
                for (int frame { 0 }; frame < frames; ++frame)
                    player.sampleBuffer->setSample (channel, frame,
                        (dc + static_cast<float> (0.25 * std::sin (juce::MathConstants<double>::twoPi * hertz * frame / 48000.0)))
                        * (channel == 1 && oppositeRight ? -1.0f : 1.0f));
            ++player.sampleBufferRevision;
            player.sampleProperties.setLengthInSamples (frames, false);
            player.zoneProperties.setSampleStart (0, false);
            player.zoneProperties.setSampleEnd (frames, false);
            player.zoneProperties.setLoopStart (frames / 2, false);
            player.zoneProperties.setLoopLength (frames - frames / 2, false);
            player.sampleStart = 0; player.sampleLength = frames; player.selectedSourceLength = frames;
            player.sampleAuditionBlocked = false;
            player.handleChannelPitch (0.0); player.handleZonePitch (0.0);
            player.handleAuditionRate (1.0); player.handlePreservePitch (true);
        };
        juce::AudioBuffer<float> block (2, 256);
        auto process = [&]
        {
            std::thread callback ([&] { player.getNextAudioBlock ({ &block, 0, 256 }); });
            callback.join ();
            return block.getMagnitude (0, 256);
        };
        auto start = [&] (State mode)
        {
            player.audioPlayerProperties.setPlayState (State::stop, true);
            player.audioPlayerProperties.setPlayState (mode, true);
        };

        configure (440.0);
        start (State::play);
        check (process () == 0.0f && prompts == 0, "Audio callback stays silent before first off-thread signal check and never opens a dialog");
        player.processSignalCheck ();
        check (prompts == 0 && player.signalApproved && process () > 0.1f, "Ordinary selected audio proceeds without warning after measurement");

        configure (440.0);
        for (int channel { 0 }; channel < 2; ++channel)
            for (int frame { 24000 }; frame < 48000; ++frame) player.sampleBuffer->setSample (channel, frame, 0.5f);
        player.zoneProperties.setSampleEnd (24000, false); player.initSamplePoints ();
        start (State::play); player.processSignalCheck ();
        check (player.signalApproved && prompts == 0, "Analysis inspects the selected SAMPLE, not unrelated DC outside its markers");
        player.zoneProperties.setSampleEnd (48000, false); player.initSamplePoints ();
        check (process () == 0.0f, "Expanding a live selection gates new PCM before it can reach the output");
        player.processSignalCheck ();
        check (prompts == 1, "A live range expanded into sustained DC requests confirmation");
        reply (false);

        for (const auto mode : { State::play, State::loop, State::sampleIntoLoop })
        {
            configure (0.0, 0.25f, 48000, true);
            const auto before { prompts };
            start (mode); player.processSignalCheck ();
            check (prompts == before + 1 && process () == 0.0f, "ONCE, LOOP/Auto Loop, and simulation all gate anti-phase stereo DC independently");
            reply (false);
            check (player.playState == State::stop && player.audioPlayerProperties.getPlayState () == State::stop && process () == 0.0f,
                   "Declining a warning publishes stopped UI and silent output");
            start (mode); player.processSignalCheck (); reply (true);
            check (player.signalApproved && process () > 0.1f, "Explicit consent enables the exact warned selection");
            const auto acceptedPrompts { prompts };
            start (mode); player.processSignalCheck ();
            check (prompts == acceptedPrompts && process () > 0.1f, "Unchanged accepted audition is cached across STOP and replay");
        }

        configure (0.0, 0.25f);
        start (State::loop); player.processSignalCheck ();
        const auto staleStop { reply };
        player.handlePlayState (State::stop); staleStop (true);
        check (player.playState == State::stop && process () == 0.0f, "STOP invalidates an outstanding warning approval");

        start (State::loop); player.processSignalCheck ();
        const auto staleRange { reply };
        player.zoneProperties.setSampleStart (12000, false);
        player.initSamplePoints ();
        staleRange (true);
        check (! player.signalApproved && process () == 0.0f, "Changing selection boundaries invalidates an open confirmation immediately");
        player.processSignalCheck (); reply (true);
        const auto cursor { player.curSampleOffset };
        check (player.signalApproved, "Updated boundary selection can be explicitly approved");
        player.handleZonePitch (-12.0);
        check (process () == 0.0f, "Live pitch edits cannot leak unchecked signal through the audio callback");
        player.processSignalCheck (); reply (true);
        check (player.signalApproved && player.curSampleOffset == cursor, "Approved live edit resumes its position rather than retriggering");
        player.handleAuditionRate (0.5);
        check (! player.signalApproved && process () == 0.0f, "Live audition speed is rechecked before more output");
        player.processSignalCheck (); reply (true);
        player.handlePreservePitch (false);
        check (! player.signalApproved && process () == 0.0f, "Keep-pitch changes also invalidate frequency-dependent approvals");
        player.processSignalCheck (); reply (true);

        player.handleChannelPitch (-12.0); player.processSignalCheck ();
        const auto staleBuffer { reply };
        configure (440.0); start (State::play); staleBuffer (true);
        check (! player.signalApproved, "Reloaded source buffer cannot inherit another signal's approval");
        player.processSignalCheck ();
        check (player.signalApproved && process () > 0.1f, "Replacement ordinary audio is independently checked");

        configure (0.0, 0.25f); start (State::loop); player.processSignalCheck ();
        const auto staleFolder { reply };
        player.appProperties.setMostRecentFolder ("/temporary/another-bank"); staleFolder (true);
        check (! player.signalApproved, "Folder identity is part of the pending approval snapshot");
        player.invalidateSignalCheck (); player.processSignalCheck ();
        const auto staleDevice { reply };
        ++player.deviceRevision; player.invalidateSignalCheck (); staleDevice (true);
        check (! player.signalApproved, "Device changes cannot reuse a pending confirmation");
        player.processSignalCheck (); reply (true);

        configure (0.0, 0.25f); player.sampleAuditionBlocked = true;
        const auto beforeCv { prompts };
        start (State::loop); player.processSignalCheck ();
        check (prompts == beforeCv && player.playState == State::stop && process () == 0.0f,
               "Known CV remains unconditionally blocked, without an override dialog");

        configure (440.0);
        player.sampleBuffer->setSample (1, 100, std::numeric_limits<float>::quiet_NaN ());
        start (State::play); player.processSignalCheck ();
        check (player.playState == State::stop && process () == 0.0f && blockedNotices == 1,
               "Non-finite PCM is blocked and explained rather than offered as safe to continue");

        configure (440.0, 0.0f, AuditionSignalCheck::maximumFrames + 1);
        const auto beforeUnavailable { prompts };
        start (State::play); player.processSignalCheck ();
        check (prompts == beforeUnavailable && player.signalApproved, "Analysis unavailable for an oversized selection adds no speculative warning");

        configure (0.0, 0.25f); start (State::loop); player.processSignalCheck ();
        const auto staleShutdown { reply };
        player.shutdownAudio (); staleShutdown (true);
        check (player.playState == State::stop && ! player.signalApproved, "Shutdown invalidates pending approvals");

        flushChecks ();
        configure (440.0); start (State::play);
        const auto unchangedGeneration { player.signalRequestGeneration };
        player.handleAuditionRate (1.0); player.handlePreservePitch (true);
        player.handleChannelPitch (0.0); player.handleZonePitch (0.0);
        check (player.signalRequestGeneration == unchangedGeneration, "No-op normalized pitch/speed setters do not invalidate the current safety check");
        player.handleZonePitch (1.0); player.handleZonePitch (2.0);
        check (queuedChecks.size () == 1 && process () == 0.0f, "Rapid UI edits coalesce into one next-message check while unchecked audio stays silent");
        flushChecks ();
        check (player.signalApproved && process () > 0.1f, "Queued UI check resumes clear audio without waiting for a 30 Hz timer tick");
        configure (0.0, 0.25f); start (State::loop);
        const auto beforeQueuedStop { prompts };
        player.handlePlayState (State::stop); flushChecks ();
        check (prompts == beforeQueuedStop && process () == 0.0f, "STOP before the queued check prevents both warning and playback");

        std::function<void (bool)> afterDestruction;
        std::function<void ()> queuedAfterDestruction;
        {
            auto temporary { std::make_unique<AudioPlayer> () };
            temporary->signalCheckEnabled = true;
            temporary->deferSignalCheck = [&] (auto callback) { queuedAfterDestruction = std::move (callback); return true; };
            temporary->sampleRate = 48000.0; temporary->sampleRateRatio = 1.0;
            temporary->sampleStart = 0; temporary->sampleLength = 48000; temporary->selectedSourceLength = 48000;
            temporary->sampleBuffer = std::make_shared<juce::AudioBuffer<float>> (2, 48000);
            for (int channel { 0 }; channel < 2; ++channel)
                for (int frame { 0 }; frame < 48000; ++frame) temporary->sampleBuffer->setSample (channel, frame, 0.25f);
            temporary->confirmSignalWarning = [&] (const juce::String&, auto complete) { afterDestruction = std::move (complete); };
            temporary->handlePlayState (State::play); temporary->processSignalCheck ();
        }
        check (static_cast<bool> (afterDestruction), "Fixture held a real delayed warning callback");
        afterDestruction (true);
        check (static_cast<bool> (queuedAfterDestruction), "Fixture held a real queued message-thread check");
        queuedAfterDestruction ();
    }
};

void testAuditionWarningRouting ()
{
    AuditionWarningRoutingTestAccess::run ();
    std::cout << "PASS: message-thread sample audition gate, exact approvals, live-edit silence, CV and lifetime guards\n";
}
