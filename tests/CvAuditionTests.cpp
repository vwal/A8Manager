#include "Assimil8or/Audio/AudioPlayer.h"
#include "Assimil8or/Audio/CvSampleSafety.h"
#include "GUI/Assimil8or/Editor/SampleManager/SampleManager.h"
#include "GUI/Assimil8or/Editor/ZoneEditor.h"
#include "GUI/ModernTheme.h"
#include "Assimil8or/PresetManagerProperties.h"
#include "SystemServices.h"
#include "oolib/Properties/PersistentRootProperties.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void check (bool ok, const char* message) { if (! ok) throw std::runtime_error (message); }

    void writeFixture (const juce::File& file, bool cv, bool tagged = true)
    {
        auto output { file.createOutputStream () };
        check (output != nullptr && output->setPosition (0) && output->truncate ().wasOk (), "Create isolated CV/audio fixture");
        std::unique_ptr<juce::OutputStream> stream { std::move (output) };
        const auto metadata { tagged ? CvSampleSafety::exportMetadata (cv) : juce::StringPairArray {} };
        std::unordered_map<juce::String, juce::String> writerMetadata;
        for (int item { 0 }; item < metadata.size (); ++item)
            writerMetadata.emplace (metadata.getAllKeys ()[item], metadata.getAllValues ()[item]);
        juce::WavAudioFormat format;
        auto writer { format.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (48000).withNumChannels (1)
                                              .withBitsPerSample (24).withMetadataValues (writerMetadata)) };
        check (writer != nullptr, "Create marked fixture writer");
        juce::AudioBuffer<float> audio (1, 4096);
        for (int frame { 0 }; frame < audio.getNumSamples (); ++frame) audio.setSample (0, frame, cv ? 0.75f : 0.25f);
        check (writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples ()), "Write marked fixture");
    }
}

struct CvAuditionTestAccess
{
    static void run ()
    {
        using State = AudioPlayerProperties::PlayState;
        const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-cv-audition", "", false) };
        check (folder.createDirectory ().wasOk (), "Create isolated CV audition folder");
        struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { folder };
        const auto audioFile { folder.getChildFile ("audio.wav") }, cvFile { folder.getChildFile ("cv.wav") };
        writeFixture (audioFile, false); writeFixture (cvFile, true);

        // Fully shared runtime trees and real cache/playback/UI implementations,
        // but no AudioPlayer::init (which would open a native audio device).
        juce::ValueTree root { "Root" };
        PersistentRootProperties persistent (root, PersistentRootProperties::WrapperType::owner, PersistentRootProperties::EnableCallbacks::no);
        RuntimeRootProperties runtime (root, RuntimeRootProperties::WrapperType::owner, RuntimeRootProperties::EnableCallbacks::no);
        AppProperties preferences;
        preferences.wrap (persistent.getValueTree (), AppProperties::WrapperType::owner, AppProperties::EnableCallbacks::no);
        preferences.setMostRecentFolder (folder.getFullPathName ());
        AudioManager audioManager;
        EditManager edits;
        SystemServices services (runtime.getValueTree (), SystemServices::WrapperType::owner, SystemServices::EnableCallbacks::no);
        services.setAudioManager (&audioManager); services.setEditManager (&edits);
        AudioPlayer player;
        player.sampleManagerProperties.wrap (runtime.getValueTree (), SampleManagerProperties::WrapperType::owner, SampleManagerProperties::EnableCallbacks::no);
        player.audioPlayerProperties.wrap (runtime.getValueTree (), AudioPlayerProperties::WrapperType::owner, AudioPlayerProperties::EnableCallbacks::yes);
        player.audioPlayerProperties.onPlayStateChange = [&] (State state) { player.handlePlayState (state); };
        player.audioPlayerProperties.onSampleSourceChanged = [&] (auto source) { player.initFromZone (source); };
        PresetManagerProperties presets (runtime.getValueTree (), PresetManagerProperties::WrapperType::owner, PresetManagerProperties::EnableCallbacks::no);
        presets.addPreset ("edit", player.presetProperties.getValueTree ());
        presets.addPreset ("unedited", player.presetProperties.getValueTree ().createCopy ());
        SampleManager manager;
        manager.audioManager = &audioManager;
        manager.currentFolder = folder;
        for (int channelIndex { 0 }; channelIndex < 8; ++channelIndex)
        {
            ChannelProperties channel (player.presetProperties.getChannelVT (channelIndex), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            channel.setChannelMode (ChannelProperties::ChannelMode::master, false);
            for (int zoneIndex { 0 }; zoneIndex < 8; ++zoneIndex)
            {
                auto& slot { manager.zoneAndSamplePropertiesList[channelIndex][zoneIndex] };
                slot.zoneProperties.wrap (channel.getZoneVT (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                slot.zoneProperties.setSampleStart (-1, false); slot.zoneProperties.setSampleEnd (-1, false);
                slot.zoneProperties.setLoopStart (-1, false); slot.zoneProperties.setLoopLength (-1, false);
                slot.sampleProperties.wrap (player.sampleManagerProperties.getSamplePropertiesVT (channelIndex, zoneIndex), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
                check (! slot.sampleProperties.getIsCv (), "New runtime slots are not classified as CV by default");
            }
        }
        auto load = [&] (int channel, int zone, const juce::String& name)
        {
            manager.zoneAndSamplePropertiesList[channel][zone].zoneProperties.setSample (name, false);
            manager.handleSampleChange (channel, zone, name);
        };
        auto& left { manager.zoneAndSamplePropertiesList[0][0] };
        auto& right { manager.zoneAndSamplePropertiesList[1][0] };
        bool expectedCv { false };
        int validNotifications { 0 };
        SampleProperties observer (left.sampleProperties.getValueTree (), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::yes);
        observer.onStatusChange = [&] (SampleStatus status)
        {
            if (status == SampleStatus::exists)
            {
                ++validNotifications;
                check (observer.getIsCv () == expectedCv, "Classification precedes every valid-buffer/status notification");
            }
        };
        load (0, 0, "audio.wav");
        load (0, 1, "audio.wav"); // Same cached file, a second reference.
        player.initFromZone ({ 0, 0 });
        player.prepareToPlay (128, 48000);
        juce::AudioBuffer<float> block (2, 128);
        auto process = [&]
        {
            for (int channel { 0 }; channel < 2; ++channel)
                for (int frame { 0 }; frame < 128; ++frame) block.setSample (channel, frame, 0.99f);
            player.getNextAudioBlock ({ &block, 0, 128 });
        };
        auto silent = [&] { process (); return block.getMagnitude (0, 128) == 0.0f; };
        auto play = [&] (State state) { player.audioPlayerProperties.setPlayState (State::stop, true); player.audioPlayerProperties.setPlayState (state, true); };
        play (State::loop); process ();
        check (block.getMagnitude (0, 128) > 0.1f, "Ordinary audio plays through the actual shared output callback");

        expectedCv = true; writeFixture (audioFile, true); manager.update ();
        check (left.sampleProperties.getIsCv () && manager.zoneAndSamplePropertiesList[0][1].sampleProperties.getIsCv ()
               && left.sampleProperties.getStatus () == SampleStatus::exists && left.sampleProperties.getAudioBufferPtr () != nullptr,
               "Same-path reload updates all cached classifications while preserving editable CV buffers");
        check (player.playState == State::stop && silent (), "Reloading playing audio as CV immediately silences sample audition");
        for (auto state : { State::play, State::loop })
        {
            play (state);
            check (silent () && player.playState == State::stop, "Direct one-shot and loop requests cannot audition classified CV");
            // Inject both a playable nonzero stale buffer and play state, so
            // silence cannot pass merely because invalidation cleared data.
            player.sampleBuffer = std::make_unique<juce::AudioBuffer<float>> (2, 4096);
            for (int channel { 0 }; channel < 2; ++channel)
                for (int frame { 0 }; frame < 4096; ++frame) player.sampleBuffer->setSample (channel, frame, 0.75f);
            player.sampleRateRatio = 1.0; player.sampleStart = 0; player.sampleLength = 4096;
            player.curSampleOffset = 0; player.resetAuditionResampler = true; player.playState = state;
            check (silent () && player.playState == State::stop, "Audio callback independently rejects a playable stale CV buffer and playback state");
            player.prepareSampleForPlayback ();
        }
        player.timerCallback ();
        check (player.audioPlayerProperties.getPlayState () == State::stop, "Blocked requests publish stopped UI state");
        expectedCv = false; writeFixture (audioFile, false); manager.update ();
        check (! left.sampleProperties.getIsCv () && ! manager.zoneAndSamplePropertiesList[0][1].sampleProperties.getIsCv () && player.playState == State::stop,
               "Reloading CV as ordinary audio clears classification without automatically playing");
        play (State::play); process ();
        check (block.getMagnitude (0, 128) > 0.1f, "Ordinary audio recovers after a same-path CV replacement");

        load (1, 0, "cv.wav");
        ChannelProperties rightChannel (player.presetProperties.getChannelVT (1), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        rightChannel.setChannelMode (ChannelProperties::ChannelMode::stereoRight, false);
        player.initFromZone ({ 0, 0 }); play (State::loop);
        check (player.sampleAuditionBlocked && silent (), "CV on the right blocks both outputs of a selected stereo pair");
        player.initFromZone ({ 1, 0 }); play (State::play);
        check (player.sampleAuditionBlocked && silent (), "Direct selection of a CV stereo-right channel is blocked");
        expectedCv = true; load (0, 0, "cv.wav"); load (1, 0, "audio.wav");
        player.initFromZone ({ 1, 0 }); play (State::loop);
        check (player.sampleAuditionBlocked && silent (), "Direct stereo-right selection is also blocked when its left partner is CV");
        player.initFromZone ({ 0, 0 });
        rightChannel.setChannelMode (ChannelProperties::ChannelMode::master, false);
        expectedCv = false; load (0, 0, "audio.wav"); load (1, 0, "cv.wav");
        player.initFromZone ({ 0, 0 }); play (State::loop); process ();
        check (! player.sampleAuditionBlocked && block.getMagnitude (0, 128) > 0.1f, "An unrelated CV channel does not block independent audio");
        left.sampleProperties.setIsCv (true, false);
        check (player.playState == State::stop && silent (), "Runtime classification changes immediately invalidate a playing sample cache");

        auto settings { WaveformDesign::startingPoint (WaveformDesign::Mode::oscillator, WaveformDesign::Shape::sine) };
        WaveformDesign::Render rendered;
        WaveformAudition::PayloadPtr payload;
        check (WaveformDesign::render (settings, rendered).wasOk () && WaveformAudition::preparePayload (settings, rendered, payload).wasOk (), "Prepare independent audio designer fixture");
        player.setWaveformAuditionPayload (payload);
        check (player.setWaveformMonitor (-18, 0).wasOk () && player.startWaveformAudition ().wasOk (), "Audio designer remains available with selected CV sample");
        left.sampleProperties.setIsCv (false, false); left.sampleProperties.setIsCv (true, false);
        play (State::loop);
        for (int iteration { 0 }; iteration < 10; ++iteration) process ();
        check (player.waveformSelected && player.isWaveformAuditionActive () && block.getMagnitude (0, 128) > 0.001f,
               "CV classification notifications and blocked sample requests do not interrupt independent designer audition");
        player.stopWaveformAudition ();
        // Stop is intentionally ramped. Drain the independent designer route
        // before subsequent CV-silence assertions; a blocked sample request
        // must not commandeer or hard-stop that unrelated route.
        for (int iteration { 0 }; iteration < 32; ++iteration) process ();
        check (! player.isWaveformAuditionActive () && block.getMagnitude (0, 128) == 0.0f,
               "Independent designer stop completes its fade before later sample-only silence checks");

        ModernLookAndFeel look;
        ZoneEditor editor;
        editor.setLookAndFeel (&look);
        editor.displayToolsMenu = [] (int) {};
        editor.init (left.zoneProperties.getValueTree (), left.zoneProperties.getValueTree ().createCopy (), root);
        editor.setStereoRightChannelMode (false);
        editor.setSize (180, 440);
        check (! editor.oneShotPlayButton.isEnabled () && ! editor.loopPlayButton.isEnabled () && editor.cvAuditionNotice.isVisible ()
               && editor.sampleStartTextEditor.isEnabled () && editor.oneShotPlayButton.getTooltip ().contains ("control voltage"),
               "Actual zone UI disables both transports and explains CV safety while retaining marker editing");
        const auto sourceBefore { player.audioPlayerProperties.getSampleSource () };
        editor.oneShotPlayButton.onClick (); editor.loopPlayButton.onClick ();
        check (player.audioPlayerProperties.getSampleSource () == sourceBefore, "Even stale direct CV button callbacks cannot start or change source");
        const auto artifacts { juce::SystemStats::getEnvironmentVariable ("A8MANAGER_TEST_ARTIFACTS", {}) };
        if (artifacts.isNotEmpty ())
        {
            juce::File destination { artifacts };
            check (destination.createDirectory ().wasOk (), "Create CV UI artifact folder");
            auto output { destination.getChildFile ("cv-zone-audition-disabled.png").createOutputStream () };
            check (output != nullptr && output->setPosition (0) && output->truncate ().wasOk (), "Create CV UI artifact");
            check (juce::PNGImageFormat ().writeImageToStream (editor.createComponentSnapshot (editor.getLocalBounds (), true, 2.0f), *output), "Write CV UI artifact");
        }
        left.sampleProperties.setIsCv (false, false);
        check (editor.oneShotPlayButton.isEnabled () && ! editor.cvAuditionNotice.isVisible (), "Zone UI responds when the source is reclassified as audio");
        rightChannel.setChannelMode (ChannelProperties::ChannelMode::stereoRight, false);
        check (! editor.oneShotPlayButton.isEnabled () && editor.cvAuditionNotice.isVisible (), "Zone UI also explains CV on a stereo partner");
        right.sampleProperties.setIsCv (false, false);
        check (editor.oneShotPlayButton.isEnabled (), "Pair becomes auditionable only after neither side is classified CV");
        expectedCv = false; load (0, 0, "");
        check (! left.sampleProperties.getIsCv () && left.sampleProperties.getStatus () == SampleStatus::uninitialized && ! editor.oneShotPlayButton.isEnabled (),
               "Clearing a zone clears runtime CV classification and remains unplayable");
        check (validNotifications >= 4, "Actual loads and reloads exercised classification-before-ready ordering");
        const auto legacyFile { folder.getChildFile ("voice-01.wav") };
        writeFixture (legacyFile, true, false);
        auto legacySettings { WaveformDesign::startingPoint (WaveformDesign::Mode::modulation, WaveformDesign::Shape::triangle) };
        legacySettings.durationSeconds = 4096.0 / 48000.0;
        check (folder.getChildFile ("design.json").replaceWithText (juce::JSON::toString (WaveformDesign::toJson (legacySettings))), "Write disposable legacy CV recipe");
        load (2, 0, "voice-01.wav");
        player.initFromZone ({ 2, 0 }); play (State::loop);
        check (manager.zoneAndSamplePropertiesList[2][0].sampleProperties.getIsCv (),
               "Legacy untagged CV export is classified during the actual cache load");
        check (player.sampleAuditionBlocked && player.playState == State::stop,
               "Legacy CV load blocks a direct loop request in the central sample engine");
        check (silent (), "Legacy CV cannot reach the output callback after independent designer fade-out");
        editor.setLookAndFeel (nullptr);
        std::cout << "PASS: CV sample classification/load/cache/reload ordering, central mono/stereo/direct playback block, designer isolation and actual zone UI safety\n";
    }
};

void testCvAudition () { CvAuditionTestAccess::run (); }
