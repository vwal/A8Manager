#include "Assimil8or/Audio/AudioPlayer.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <limits>

// Configure the actual audition engine without opening an audio device.
struct AudioPlayerTestAccess
{
    static void runRouting ()
    {
        auto check = [] (bool condition, const char* message) { if (! condition) throw std::runtime_error (message); };
        AudioPlayer player;
        player.sampleRate = 48000.0;
        player.sampleRateRatio = 1.0;
        player.audioPlayerProperties.enableCallbacks (true);
        player.audioPlayerProperties.onSamplePointsSelectorChanged = [&] (auto) { player.initSamplePoints (); };
        player.audioPlayerProperties.onPlayStateChange = [&] (auto state) { player.handlePlayState (state); };
        juce::AudioBuffer<float> left (2, 1024), right (2, 256), mono (1, 1024);
        for (auto i { 0 }; i < 1024; ++i)
        {
            left.setSample (0, i, 0.25f); left.setSample (1, i, -0.5f); mono.setSample (0, i, 0.4f);
        }
        for (auto i { 0 }; i < 256; ++i) { right.setSample (0, i, 0.1f); right.setSample (1, i, 0.75f); }
        for (auto c { 0 }; c < 8; ++c)
        {
            auto channel { player.presetProperties.getChannelVT (c) };
            ChannelProperties channelProperties (channel, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            for (auto z { 0 }; z < 8; ++z)
            {
                auto tree { channelProperties.getZoneVT (z) };
                ZoneProperties zone (tree, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                zone.setSample ("fixture.wav", false);
                zone.setSampleStart (100, false); zone.setSampleEnd (900, false);
                zone.setLoopStart (200, false); zone.setLoopLength (100, false);
            }
            SampleProperties sample (player.sampleManagerProperties.getSamplePropertiesVT (c, 0), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
            sample.setAudioBufferPtr (c == 1 ? &right : &left, false);
            sample.setLengthInSamples (c == 1 ? 256 : 1024, false);
            sample.setSampleRate (c == 1 ? 24000.0 : 48000.0, false);
            sample.setNumChannels (2, false);
            sample.setStatus (SampleStatus::exists, false);
        }
        player.initFromZone ({ 0, 0 });
        auto value = [&] (int side, int frame) { return player.sampleBuffer->getSample (side, frame); };
        check (std::abs (value (0, 128) - 0.25f) < 0.01f && std::abs (value (1, 128) - 0.25f) < 0.01f, "Unpaired L side reaches both preview outputs");
        player.zoneProperties.setSide (1, true);
        check (std::abs (value (0, 128) + 0.5f) < 0.01f && std::abs (value (1, 128) + 0.5f) < 0.01f, "Unpaired R side must survive resampling on both outputs");
        player.nextSampleProperties.setStatus (SampleStatus::uninitialized, true);
        check (player.sampleBuffer != nullptr && std::abs (value (0, 128) + 0.5f) < 0.01f, "Unrelated next-channel unload must not clear this preview");
        player.nextSampleProperties.setStatus (SampleStatus::exists, true);
        player.zoneProperties.setSide (0, true);
        player.nextZoneProperties.setSide (1, true);
        player.nextChannelProperties.setChannelMode (ChannelProperties::ChannelMode::stereoRight, true);
        check (std::abs (value (0, 128) - 0.25f) < 0.01f && std::abs (value (1, 128) - 0.75f) < 0.01f, "Paired preview routes distinct L/R with differing source rates");
        check (player.sampleBuffer->getNumSamples () == 1024 && std::abs (value (1, 900)) < 0.001f, "Shorter right file is zero-padded to left duration");
        player.nextSampleProperties.setStatus (SampleStatus::doesNotExist, true);
        check (std::abs (value (0, 128) - 0.25f) < 0.01f && player.sampleBuffer->getMagnitude (1, 0, 1024) == 0.0f, "Missing stereo right silences only R");
        player.nextSampleProperties.setStatus (SampleStatus::exists, true);
        check (std::abs (value (1, 128) - 0.75f) < 0.01f, "Reloaded stereo right restores routing");
        using Selector = AudioPlayerProperties::SamplePointsSelector;
        player.audioPlayerProperties.setSamplePointsSelector (Selector::SamplePoints, true);
        player.zoneProperties.setLoopStart (400, true); player.zoneProperties.setLoopLength (40, true);
        check (player.sampleStart == 100 && player.sampleLength == 800, "Loop edits cannot leak into SAMPLE audition");
        player.audioPlayerProperties.setSamplePointsSelector (Selector::LoopPoints, true);
        player.zoneProperties.setSampleStart (80, true); player.zoneProperties.setSampleEnd (950, true);
        check (player.sampleStart == 400 && player.sampleLength == 40, "Sample edits cannot leak into LOOP audition");
        player.zoneProperties.setLoopLength (-1, true);
        player.zoneProperties.setLoopStart (500, true);
        check (player.sampleLength == 524, "Implicit loop length tracks remaining file, not full file length");
        player.audioPlayerProperties.setSamplePointsSelector (Selector::SamplePoints, true);
        check (player.sampleStart == 80 && player.sampleLength == 870, "Switching back restores edited sample boundaries");
        player.sampleProperties.setStatus (SampleStatus::uninitialized, true);
        check (player.sampleBuffer == nullptr, "Unloading selected sample releases stale preview audio");
        player.sampleProperties.setAudioBufferPtr (&mono, true);
        player.sampleProperties.setNumChannels (1, true);
        player.zoneProperties.setSide (1, true);
        player.sampleProperties.setStatus (SampleStatus::exists, true);
        check (std::abs (value (0, 128) - 0.4f) < 0.01f, "Stale R side on replacement mono file falls back safely to channel zero");
        const auto previousRightTree { player.nextChannelProperties.getValueTree () };
        player.initFromZone ({ 7, 0 });
        check (! player.nextChannelProperties.isValid () && ! player.nextZoneProperties.isValid () && ! player.nextSampleProperties.isValid (),
               "Channel eight releases all optional partner bindings, without invalid client wraps");
        player.playbackPosition.store (42.0);
        ChannelProperties previousRight (previousRightTree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        previousRight.setChannelMode (ChannelProperties::ChannelMode::master, false);
        previousRight.setChannelMode (ChannelProperties::ChannelMode::stereoRight, false);
        check (player.playbackPosition.load () == 42.0, "Detached previous partner changes cannot rebuild channel-eight audio");
        player.zoneProperties.setSide (1, true);
        check (! player.isStereoPair () && std::abs (value (0, 128) + 0.5f) < 0.01f && std::abs (value (1, 128) + 0.5f) < 0.01f, "Channel eight has no stale right-channel pairing");
        player.sampleRate = 24000.0;
        player.prepareSampleForPlayback ();
        check (player.sampleBuffer->getNumSamples () == 512 && player.sampleStart == 50 && player.sampleLength == 400, "Device rate change updates audio and range together");
        player.initFromZone ({ 0, 0 });
        check (player.isStereoPair () && player.nextChannelProperties.getValueTree () == previousRightTree &&
               player.nextZoneProperties.isValid () && player.nextSampleProperties.isValid (), "Returning from channel eight restores real partner bindings");
        std::cout << "PASS: actual stereo source preparation, sides, rates, missing/reloaded partner, channel eight and SAMPLE/LOOP isolation\n";
    }

    static void run ()
    {
        auto check = [] (bool ok, const char* message)
        {
            if (! ok) throw std::runtime_error (message);
        };
        AudioPlayer player;
        player.audioPlayerProperties.enableCallbacks (true);
        player.audioPlayerProperties.onPlayStateChange = [&] (auto state) { player.handlePlayState (state); };
        player.audioPlayerProperties.onAuditionRateChange = [&] (double rate) { player.handleAuditionRate (rate); };
        player.audioPlayerProperties.onPreservePitchChange = [&] (bool preserve) { player.handlePreservePitch (preserve); };
        player.zoneProperties.enableCallbacks (true);
        player.zoneProperties.onPitchOffsetChange = [&] (double semitones) { player.handleZonePitch (semitones); };
        player.prepareAuditionResampler ();
        AudioPlayerProperties observer (player.audioPlayerProperties.getValueTree (), AudioPlayerProperties::WrapperType::client,
                                        AudioPlayerProperties::EnableCallbacks::yes);
        check (observer.getPreservePitch (), "New preview sessions should default to Keep pitch");
        auto notifications { 0 };
        observer.onPlaybackPositionChange = [&] (double)
        {
            check (juce::MessageManager::existsAndIsCurrentThread (), "Playhead notifications must stay on the message thread");
            ++notifications;
        };
        observer.onPlayStateChange = [&] (auto)
        {
            check (juce::MessageManager::existsAndIsCurrentThread (), "Completion notifications must stay on the message thread");
        };
        auto configure = [&] (int start, int length, double ratio, AudioPlayerProperties::PlayState state)
        {
            player.handlePlayState (AudioPlayerProperties::PlayState::stop);
            player.audioPlayerProperties.setPreservePitch (false, true);
            player.zoneProperties.setPitchOffset (0.0, true);
            player.audioPlayerProperties.setAuditionRate (1.0, true);
            player.sampleBuffer = std::make_unique<juce::AudioBuffer<float>> (2, 64);
            for (auto i { 0 }; i < 64; ++i)
            {
                player.sampleBuffer->setSample (0, i, static_cast<float> (i + 1) / 100.0f);
                player.sampleBuffer->setSample (1, i, -static_cast<float> (i + 1) / 100.0f);
            }
            player.sampleStart = start;
            player.sampleLength = length;
            player.sampleRateRatio = ratio;
            player.audioPlayerProperties.setPlayState (state, false);
            player.handlePlayState (state);
        };
        auto render = [&] (juce::AudioBuffer<float>& output, int start, int count)
        {
            std::thread audioThread ([&] () { player.getNextAudioBlock ({ &output, start, count }); });
            audioThread.join ();
        };

        configure (8, 12, 2.0, AudioPlayerProperties::PlayState::play);
        juce::AudioBuffer<float> output (3, 16);
        for (auto ch { 0 }; ch < 3; ++ch)
            for (auto i { 0 }; i < 16; ++i) output.setSample (ch, i, 0.75f);
        render (output, 2, 4);
        check (notifications == 0, "Audio rendering must not write the UI ValueTree");
        player.timerCallback ();
        check (std::abs (observer.getPlaybackPosition () - 6.0) < 1e-9, "Map resampled cursor back to original frames");
        for (auto i { 0 }; i < 4; ++i)
        {
            const auto expected { static_cast<float> (9 + i) / 100.0f };
            check (std::abs (output.getSample (0, 2 + i) - expected) < 1e-6f &&
                   std::abs (output.getSample (1, 2 + i) + expected) < 1e-6f, "Preserve stereo channel contents");
            check (output.getSample (2, 2 + i) == 0.0f, "Unused output channels must be silent");
        }
        check (output.getSample (0, 0) == 0.75f && output.getSample (0, 6) == 0.75f, "Respect the output subregion");
        render (output, 0, 8); // exact boundary, not a partial final block
        player.timerCallback ();
        check (observer.getPlayState () == AudioPlayerProperties::PlayState::stop && observer.getPlaybackPosition () < 0.0,
               "Exact-block completion must stop and hide the cursor immediately");
        render (output, 0, 8);
        check (output.getMagnitude (0, 8) == 0.0f, "Do not repeat the final sample after stopping");

        configure (8, 3, 1.0, AudioPlayerProperties::PlayState::play);
        render (output, 2, 8);
        check (output.getMagnitude (5, 5) == 0.0f, "Partial one-shot tail must be silent");
        player.timerCallback ();
        check (observer.getPlaybackPosition () < 0.0, "Partial one-shot completion must hide cursor");

        configure (8, 12, 2.0, AudioPlayerProperties::PlayState::loop);
        juce::AudioBuffer<float> loops (2, 29);
        render (loops, 0, 29);
        player.timerCallback ();
        check (std::abs (observer.getPlaybackPosition () - 6.5) < 1e-9, "Loop playhead must wrap to the selected range");
        for (auto i { 0 }; i < 29; ++i)
            check (std::abs (loops.getSample (0, i) - static_cast<float> (9 + i % 12) / 100.0f) < 1e-6f,
                   "Loop contents must wrap correctly across a block");

        player.audioPlayerProperties.setSamplePointsSelector (AudioPlayerProperties::SamplePointsSelector::LoopPoints, false);
        player.zoneProperties.setLoopStart (6, false);
        player.zoneProperties.setLoopLength (4.0, false);
        player.initSamplePoints ();
        player.handlePlayState (AudioPlayerProperties::PlayState::loop);
        render (output, 0, 10);
        player.timerCallback ();
        check (std::abs (observer.getPlaybackPosition () - 7.0) < 1e-9, "Loop-point audition must include its nonzero offset");

        configure (0, 64, 48000.0 / 44100.0, AudioPlayerProperties::PlayState::play);
        render (output, 0, 10);
        player.timerCallback ();
        check (std::abs (observer.getPlaybackPosition () - 9.1875) < 1e-9, "Non-integer resampling ratio must be respected");
        player.handlePlayState (AudioPlayerProperties::PlayState::stop);
        player.timerCallback ();
        check (observer.getPlaybackPosition () < 0.0, "Manual stop must hide cursor");

        configure (8, 12, 1.0, AudioPlayerProperties::PlayState::play);
        observer.setAuditionRate (0.5, true);
        render (output, 0, 16);
        player.timerCallback ();
        check (observer.getPlayState () == AudioPlayerProperties::PlayState::play &&
               std::abs (observer.getPlaybackPosition () - 16.0) < 1e-9, "Half speed must double the duration, not finish on read-ahead");
        render (output, 0, 8);
        player.timerCallback ();
        check (observer.getPlayState () == AudioPlayerProperties::PlayState::stop, "Half speed must stop exactly after 24 output frames");

        configure (8, 13, 1.0, AudioPlayerProperties::PlayState::play);
        observer.setAuditionRate (2.0, true);
        render (output, 2, 10);
        player.timerCallback ();
        check (observer.getPlayState () == AudioPlayerProperties::PlayState::stop && output.getMagnitude (9, 3) == 0.0f,
               "Double speed must round a fractional output length up and silence the remaining tail");
        check (output.getMagnitude (2, 7) > 0.0f, "Variable-rate one-shot must output audio");

        configure (8, 4, 2.0, AudioPlayerProperties::PlayState::loop);
        observer.setAuditionRate (0.0625, true);
        render (output, 0, 10);
        player.timerCallback ();
        check (std::abs (observer.getPlaybackPosition () - 4.3125) < 1e-9, "Minimum speed must retain fractional cursor precision");
        observer.setAuditionRate (4.0, true); // change while playing, without resetting the phase
        render (loops, 0, 29);
        player.timerCallback ();
        check (std::abs (observer.getPlaybackPosition () - 4.3125) < 1e-9, "Live speed change and multiple wraps must preserve phase");
        player.sampleStart = 20;
        player.sampleLength = 4;
        render (output, 0, 10);
        player.timerCallback ();
        check (std::abs (observer.getPlaybackPosition () - 10.0) < 1e-9, "Changing the loop range must discard stale resampler read-ahead");

        // Measure the actual pitch of a seamless 64-frame sine, including both
        // interpolation and filtering. This catches a cursor-only speed change.
        for (const auto rate : { 0.0625, 0.5, 1.0, 2.0, 4.0 })
        {
            configure (0, 64, 1.0, AudioPlayerProperties::PlayState::loop);
            for (auto i { 0 }; i < 64; ++i)
            {
                const auto value { static_cast<float> (0.5 * std::sin (juce::MathConstants<double>::twoPi * i / 64.0)) };
                player.sampleBuffer->setSample (0, i, value);
                player.sampleBuffer->setSample (1, i, -value);
            }
            observer.setAuditionRate (rate, true);
            juce::AudioBuffer<float> tone (2, 8192);
            // Device-sized blocks also exercise fractional positions across callbacks.
            for (auto offset { 0 }; offset < tone.getNumSamples (); offset += 128)
                render (tone, offset, 128);
            auto crossings { 0 };
            for (auto i { 4096 }; i < tone.getNumSamples () - 1; ++i)
            {
                if (tone.getSample (0, i) <= 0.0f && tone.getSample (0, i + 1) > 0.0f) ++crossings;
                check (std::isfinite (tone.getSample (0, i)) &&
                       std::abs (tone.getSample (0, i) + tone.getSample (1, i)) < 1e-6f,
                       "Rate conversion must preserve finite stereo contents");
            }
            check (std::abs (crossings - 64.0 * rate) <= 1.0, "Audible pitch must follow the requested speed");
        }

        observer.setAuditionRate (0.0, true);
        check (observer.getAuditionRate () == 0.0625 && player.auditionRate == 0.0625, "Zero speed must clamp safely");
        observer.setAuditionRate (100.0, true);
        check (observer.getAuditionRate () == 4.0 && player.auditionRate == 4.0, "Excessive speed must clamp safely");
        observer.setAuditionRate (std::numeric_limits<double>::quiet_NaN (), true);
        check (observer.getAuditionRate () == 1.0 && player.auditionRate == 1.0, "Non-finite speed must reset safely");
        const auto zoneBefore { player.zoneProperties.getValueTree ().createCopy () };
        observer.setAuditionRate (0.25, true);
        check (zoneBefore.isEquivalentTo (player.zoneProperties.getValueTree ()), "Audition speed must not change preset data");

        configure (8, 12, 1.0, AudioPlayerProperties::PlayState::play);
        observer.setPreservePitch (true, true);
        render (output, 0, 4);
        check (std::abs (output.getSample (0, 0) - 0.09f) < 1e-6f,
               "Neutral Keep pitch must bypass spectral processing and preserve original samples");

        configure (8, 12, 1.0, AudioPlayerProperties::PlayState::play);
        observer.setPreservePitch (true, true);
        observer.setAuditionRate (0.5, true);
        player.zoneProperties.setPitchOffset (12.0, true);
        render (output, 0, 16);
        player.timerCallback ();
        check (observer.getPlayState () == AudioPlayerProperties::PlayState::play &&
               std::abs (observer.getPlaybackPosition () - 16.0) < 1e-9,
               "Keep pitch must double duration at half speed, independent of zone transposition/read-ahead");
        render (output, 0, 12);
        player.timerCallback ();
        check (observer.getPlayState () == AudioPlayerProperties::PlayState::stop && output.getMagnitude (8, 4) == 0.0f,
               "Stretched one-shot must stop at the requested duration with a silent tail");

        // Real spectral rendering: distinct stereo tones ensure both sides keep
        // their own content while speed and zone transposition remain independent.
        for (const auto settings : { std::pair<double, double> { 0.5, 0.0 }, { 2.0, 0.0 }, { 0.0625, 0.0 },
                                      { 4.0, 0.0 }, { 0.5, 12.0 }, { 2.0, -12.0 }, { 0.75, 3.01 } })
        {
            const auto [speed, pitch] { settings };
            configure (0, 4800, 1.0, AudioPlayerProperties::PlayState::loop);
            player.sampleBuffer = std::make_unique<juce::AudioBuffer<float>> (2, 4800);
            for (auto i { 0 }; i < 4800; ++i)
                for (auto channel { 0 }; channel < 2; ++channel)
                    player.sampleBuffer->setSample (channel, i, static_cast<float> (0.5 * std::sin (
                        juce::MathConstants<double>::twoPi * i / (channel == 0 ? 100.0 : 80.0))));
            observer.setPreservePitch (true, true);
            observer.setAuditionRate (speed, true);
            player.zoneProperties.setPitchOffset (pitch, true);
            juce::AudioBuffer<float> tone (2, 32768);
            for (auto offset { 0 }; offset < tone.getNumSamples (); offset += 128)
                render (tone, offset, 128);
            for (auto channel { 0 }; channel < 2; ++channel)
            {
                auto crossings { 0 };
                for (auto i { 16384 }; i < tone.getNumSamples () - 1; ++i)
                {
                    const auto value { tone.getSample (channel, i) };
                    check (std::isfinite (value) && std::abs (value) < 2.0f, "Stretch output must remain finite and bounded");
                    if (value <= 0.0f && tone.getSample (channel, i + 1) > 0.0f) ++crossings;
                }
                const auto expected { 16384.0 / (channel == 0 ? 100.0 : 80.0) * std::pow (2.0, pitch / 12.0) };
                if (std::abs (crossings - expected) > 2.0)
                    std::cerr << "speed=" << speed << ", pitch=" << pitch << ", channel=" << channel
                              << ", crossings=" << crossings << ", expected=" << expected << '\n';
                check (std::abs (crossings - expected) <= 2.0,
                       "Keep pitch must preserve frequency across speeds and honour fractional zone Pitch Offset");
            }
            player.timerCallback ();
            check (std::abs (observer.getPlaybackPosition () - std::fmod (32768.0 * speed, 4800.0)) < 1e-7,
                   "Stretched playhead must follow duration, not transposed pitch or FFT look-ahead");
        }

        configure (8, 56, 2.0, AudioPlayerProperties::PlayState::loop);
        observer.setPreservePitch (true, true);
        observer.setAuditionRate (0.3, true);
        render (loops, 0, 17);
        observer.setAuditionRate (0.4, true);
        render (loops, 0, 11);
        player.zoneProperties.setPitchOffset (3.01, true);
        render (loops, 0, 10);
        player.timerCallback ();
        check (std::abs (observer.getPlaybackPosition () - 10.75) < 1e-7,
               "Live stretching rate/pitch changes must preserve the fractional audible cursor");
        player.sampleStart = 30;
        player.sampleLength = 4;
        render (loops, 0, 29);
        player.timerCallback ();
        check (std::abs (observer.getPlaybackPosition () - 16.8) < 1e-7,
               "Stretched tiny loops must wrap and discard the previous range's look-ahead");

        configure (8, 12, 1.0, AudioPlayerProperties::PlayState::play);
        player.zoneProperties.setPitchOffset (12.0, true);
        render (output, 0, 10);
        player.timerCallback ();
        check (observer.getPlayState () == AudioPlayerProperties::PlayState::stop && output.getMagnitude (6, 4) == 0.0f,
               "Varispeed mode must also apply zone Pitch Offset, including its duration change");
        const auto beforeModeChange { player.zoneProperties.getValueTree ().createCopy () };
        observer.setPreservePitch (true, true);
        check (beforeModeChange.isEquivalentTo (player.zoneProperties.getValueTree ()), "Preview mode must not change preset data");

        // Exercise full legal pitch extremes, reconfiguration and malformed
        // pitch input. Pitch accuracy at these extremes is a listening concern,
        // but neither processing mode may produce NaNs or run past the source.
        for (const auto keepPitch : { false, true })
            for (const auto pitch : { -90.0, 60.0 })
            {
                configure (60, 4, 1.0, AudioPlayerProperties::PlayState::loop);
                observer.setPreservePitch (keepPitch, true);
                observer.setAuditionRate (pitch < 0.0 ? 0.0625 : 4.0, true);
                player.zoneProperties.setPitchOffset (pitch, true);
                player.prepareAuditionResampler ();
                render (output, 0, 16);
                for (auto i { 0 }; i < 16; ++i)
                    check (std::isfinite (output.getSample (0, i)) && std::isfinite (output.getSample (1, i)),
                           "Extreme zone pitch and rate must remain finite at a file-end loop");
            }
        player.zoneProperties.setPitchOffset (std::numeric_limits<double>::infinity (), true);
        check (player.zonePitchOffset == 0.0, "Non-finite pitch must use a safe neutral audition value");
        configure (0, 64, 1.0, AudioPlayerProperties::PlayState::play);
        player.sampleBuffer = std::make_unique<juce::AudioBuffer<float>> (1, 64);
        observer.setPreservePitch (true, true);
        observer.setAuditionRate (0.5, true);
        render (output, 0, 16);
        check (output.getMagnitude (0, 16) == 0.0f, "Malformed internal channel buffers must be silent rather than read out of bounds");

        configure (0, 0, 1.0, AudioPlayerProperties::PlayState::loop);
        render (output, 0, 10);
        player.timerCallback ();
        check (output.getMagnitude (0, 10) == 0.0f && observer.getPlayState () == AudioPlayerProperties::PlayState::stop,
               "An empty range must stop rather than leave a stuck playhead");
        configure (0, 64, 1.0, AudioPlayerProperties::PlayState::play);
        player.sampleBuffer.reset ();
        render (output, 0, 10);
        player.timerCallback ();
        check (output.getMagnitude (0, 10) == 0.0f && observer.getPlaybackPosition () < 0.0, "Missing audio must remain safe and silent");
        std::cout << "PASS: playback cursor, varispeed/time-stretch pitch and duration, zone Pitch Offset, live rate/range changes, sample-rate mapping, one-shot/loop boundaries, stereo contents and message-thread notifications\n";
    }
};

void testPlayback () { AudioPlayerTestAccess::run (); }
void testStereoPreview () { AudioPlayerTestAccess::runRouting (); }
