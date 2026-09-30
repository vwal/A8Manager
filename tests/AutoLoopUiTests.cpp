#include "GUI/Assimil8or/Editor/ZoneEditor.h"
#include "GUI/Assimil8or/Editor/SampleManager/SampleManagerProperties.h"
#include "Assimil8or/Audio/CvSampleSafety.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "Assimil8or/PresetManagerProperties.h"
#include "SystemServices.h"
#include "oolib/Properties/PersistentRootProperties.h"
#include "oolib/Properties/RuntimeRootProperties.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void check (bool ok, const char* message) { if (! ok) throw std::runtime_error (message); }
    void writeWave (const juce::File& file, int channels, bool cv = false)
    {
        const auto tags { CvSampleSafety::exportMetadata (cv) };
        std::unordered_map<juce::String, juce::String> metadata;
        for (int index { 0 }; index < tags.size (); ++index)
            metadata.emplace (tags.getAllKeys ()[index], tags.getAllValues ()[index]);
        std::unique_ptr<juce::OutputStream> stream { file.createOutputStream () };
        auto writer { juce::WavAudioFormat ().createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (48000)
            .withNumChannels (channels).withBitsPerSample (24).withMetadataValues (metadata)) };
        juce::AudioBuffer<float> buffer (channels, 128);
        for (int channel { 0 }; channel < channels; ++channel)
            for (int frame { 0 }; frame < 128; ++frame) buffer.setSample (channel, frame, cv ? 0.1f : std::sin (frame * 0.13f) * 0.2f);
        check (writer && writer->writeFromAudioSampleBuffer (buffer, 0, 128), "Write external import fixture");
    }
    void snapshot (juce::Component& component, const juce::String& name)
    {
        const auto path { juce::SystemStats::getEnvironmentVariable ("A8MANAGER_TEST_ARTIFACTS", {}) };
        if (path.isEmpty ()) return;
        const juce::File folder { path };
        check (folder.createDirectory ().wasOk (), "Create Auto Loop UI artifacts");
        auto stream { folder.getChildFile (name + ".png").createOutputStream () };
        check (stream && stream->setPosition (0)
               && juce::PNGImageFormat ().writeImageToStream (component.createComponentSnapshot (component.getLocalBounds ()), *stream)
               && stream->truncate ().wasOk (), "Render Samples import and Auto Loop controls");
    }
}

struct AutoLoopUiTestAccess
{
    static void run ()
    {
        using State = AudioPlayerProperties::PlayState;
        using Selection = AudioPlayerProperties::SamplePointsSelector;
        const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("a8-auto-loop-" + juce::Uuid ().toString ()) };
        check (folder.createDirectory ().wasOk (), "Create isolated Auto Loop fixture");
        struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { folder };
        const auto working { folder.getChildFile ("working") }, outside { folder.getChildFile ("external") };
        check (working.createDirectory ().wasOk () && outside.createDirectory ().wasOk (), "Create unrelated sample source and preset folders");
        const auto mono { outside.getChildFile ("Bass first.wav") }, next { outside.getChildFile ("Bass next.wav") };
        const auto stereo { outside.getChildFile ("Stereo.wav") }, cv { outside.getChildFile ("Control.wav") };
        writeWave (mono, 1); writeWave (next, 1); writeWave (stereo, 2); writeWave (cv, 1, true);
        juce::MemoryBlock originalWave;
        check (mono.loadFileAsData (originalWave), "Capture original source WAV bytes");
        const auto otherPreset { working.getChildFile ("prst002.yml") };
        check (otherPreset.replaceWithText ("Saved preset must stay unchanged"), "Create an unrelated saved preset");
        const auto originalOtherPreset { otherPreset.loadFileAsString () };

        juce::ValueTree root { "Root" };
        PersistentRootProperties persistent (root, PersistentRootProperties::WrapperType::owner, PersistentRootProperties::EnableCallbacks::no);
        RuntimeRootProperties runtime (root, RuntimeRootProperties::WrapperType::owner, RuntimeRootProperties::EnableCallbacks::no);
        AppProperties app;
        app.wrap (persistent.getValueTree (), AppProperties::WrapperType::owner, AppProperties::EnableCallbacks::no);
        app.setMostRecentFolder (working.getFullPathName ());
        app.addRecentlyUsedFile (working.getChildFile ("prst001.yml").getFullPathName ());
        AudioPlayerProperties audition (runtime.getValueTree (), AudioPlayerProperties::WrapperType::owner, AudioPlayerProperties::EnableCallbacks::no);
        SampleManagerProperties samples (runtime.getValueTree (), SampleManagerProperties::WrapperType::owner, SampleManagerProperties::EnableCallbacks::no);
        const auto defaults { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ()
            .getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType) };
        PresetProperties preset (defaults.createCopy (), PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        PresetManagerProperties presets (runtime.getValueTree (), PresetManagerProperties::WrapperType::owner, PresetManagerProperties::EnableCallbacks::no);
        presets.addPreset ("edit", preset.getValueTree ());
        presets.addPreset ("unedited", preset.getValueTree ().createCopy ());
        AudioManager audio;
        EditManager edits;
        SystemServices services (runtime.getValueTree (), SystemServices::WrapperType::owner, SystemServices::EnableCallbacks::no);
        services.setAudioManager (&audio); services.setEditManager (&edits);
        edits.init (root, preset.getValueTree ());
        ModernLookAndFeel look;
        std::vector<std::function<void ()>> deferred;
        bool active { true }, partnerActive { false }, cvActive { false };
        auto create = [&] (int channel, bool& selected)
        {
            auto editor { std::make_unique<ZoneEditor> () };
            editor->setLookAndFeel (&look);
            editor->displayToolsMenu = [] (int) {};
            editor->init (preset.getChannelVT (channel).getChild (0), preset.getChannelVT (channel).getChild (0).createCopy (), root);
            editor->setSize (182, 520);
            editor->isActiveLoadTarget = [&selected] { return selected; };
            editor->deferAutoLoop = [&] (std::function<void ()> callback) { deferred.push_back (std::move (callback)); };
            editor->deferPlaybackDisplayUpdate = [] (std::function<void ()> callback) { callback (); };
            return editor;
        };
        auto editor { create (0, active) }, partner { create (1, partnerActive) }, cvEditor { create (2, cvActive) };
        auto flush = [&]
        {
            while (! deferred.empty ())
            {
                auto callbacks { std::move (deferred) }; deferred.clear ();
                for (auto& callback : callbacks) callback ();
            }
        };
        juce::AudioBuffer<float> monoBuffer (1, 128), stereoBuffer (2, 128);
        for (int frame { 0 }; frame < 128; ++frame)
        {
            const auto value { std::sin (frame * 0.13f) * 0.2f };
            monoBuffer.setSample (0, frame, value);
            stereoBuffer.setSample (0, frame, value); stereoBuffer.setSample (1, frame, -value);
        }
        auto ready = [&] (ZoneEditor& target, juce::AudioBuffer<float>& buffer, bool isCv = false)
        {
            // Model the SampleManager producer, not writes through the UI's
            // own wrapper (includeSelfCallback=false would suppress readiness).
            SampleProperties sample (target.sampleProperties.getValueTree (), SampleProperties::WrapperType::client,
                                     SampleProperties::EnableCallbacks::no);
            sample.setStatus (SampleStatus::uninitialized, false);
            sample.setIsCv (isCv, false);
            sample.setName (target.zoneProperties.getSample (), false);
            sample.setSampleRate (48000, false);
            sample.setNumChannels (buffer.getNumChannels (), false);
            sample.setLengthInSamples (buffer.getNumSamples (), false);
            sample.setAudioBufferPtr (&buffer, false);
            sample.setStatus (SampleStatus::exists, false);
        };
        check (! audition.getAutoLoopEnabled () && ! editor->autoLoopButton.getToggleState () && ! partner->autoLoopButton.getToggleState (),
               "Auto Loop starts off and belongs only to runtime audition state");
        editor->receiveSampleLoadRequest (mono);
        ready (*editor, monoBuffer); flush ();
        check (audition.getPlayState () == State::stop && ! editor->pendingAutoLoop,
               "Importing a ready sample never starts audio while Auto Loop is off");
        juce::MemoryBlock afterImport;
        check (mono.loadFileAsData (afterImport) && afterImport == originalWave
               && working.getChildFile (editor->zoneProperties.getSample ()).existsAsFile ()
               && app.getMostRecentFolder () == working.getFullPathName () && otherPreset.loadFileAsString () == originalOtherPreset,
               "Import copies external audio into the current root without moving source files, navigating folders or changing another saved preset");
        const auto presetBeforePreference { preset.getValueTree ().createCopy () };
        editor->autoLoopButton.setToggleState (true, juce::dontSendNotification);
        editor->autoLoopButton.onClick ();
        check (audition.getAutoLoopEnabled () && partner->autoLoopButton.getToggleState () && audition.getPlayState () == State::stop
               && preset.getValueTree ().isEquivalentTo (presetBeforePreference),
               "Auto Loop is a shared session preference; enabling alone neither starts existing audio nor edits preset data");

        editor->selectLoop (true);
        editor->sampleProperties.setStatus (SampleStatus::uninitialized, false);
        editor->sampleProperties.setAudioBufferPtr (nullptr, false);
        editor->receiveSampleLoadRequest (next);
        const auto loadedNext { working.getChildFile (editor->zoneProperties.getSample ()) };
        flush ();
        check (editor->pendingAutoLoop.has_value () && audition.getPlayState () == State::stop,
               "Successful explicit load waits for the new source buffer instead of playing an old or unavailable buffer");
        ready (*editor, monoBuffer); flush ();
        check (audition.getPlayState () == State::loop && audition.getSamplePointsSelector () == Selection::SamplePoints
               && audition.getSampleSource () == std::make_tuple (0, 0) && ! editor->isLoopSelected ()
               && editor->loopPlayButton.getButtonText () == "STOP" && editor->oneShotPlayButton.getButtonText () == "ONCE",
               "Auto Loop selects SAMPLE and highlights its looping STOP without using LOOP markers or simulation");
        editor->loopPlayButton.onClick ();
        ready (*editor, monoBuffer); flush ();
        check (audition.getPlayState () == State::stop && audition.getAutoLoopEnabled () && ! editor->pendingAutoLoop,
               "Manual STOP consumes the automatic request; buffer refresh does not restart the current sample");
        editor->receiveSampleLoadRequest (loadedNext); flush ();
        check (audition.getPlayState () == State::loop, "A subsequent explicit load can auto-loop again after manual STOP");
        audition.setAutoLoopEnabled (false, true); flush ();
        check (audition.getPlayState () == State::stop && ! editor->autoLoopButton.getToggleState (),
               "Turning Auto Loop off stops its current audition and synchronizes the checkbox");
        editor->selectLoop (true); editor->loopPlayButton.onClick ();
        check (audition.getPlayState () == State::loop && audition.getSamplePointsSelector () == Selection::LoopPoints,
               "Ordinary manual LOOP-region audition remains available and independent");
        audition.setAutoLoopEnabled (true, true);
        audition.setAutoLoopEnabled (false, true);
        check (audition.getPlayState () == State::loop, "Disabling Auto Loop does not stop manually started loop-region audition");
        editor->loopPlayButton.onClick ();

        audition.setAutoLoopEnabled (true, true);
        editor->receiveSampleLoadRequest (loadedNext);
        active = false; flush (); active = true;
        ready (*editor, monoBuffer); flush ();
        check (audition.getPlayState () == State::stop, "A load request cannot start after another zone/workspace became selected");
        editor->receiveSampleLoadRequest (loadedNext);
        app.setMostRecentFolder (outside.getFullPathName ());
        app.setMostRecentFolder (working.getFullPathName ());
        flush (); ready (*editor, monoBuffer); flush ();
        check (audition.getPlayState () == State::stop, "Folder switches invalidate automatic starts even when returning to the same folder");
        editor->receiveSampleLoadRequest (loadedNext);
        app.addRecentlyUsedFile (otherPreset.getFullPathName ());
        flush (); ready (*editor, monoBuffer); flush ();
        check (audition.getPlayState () == State::stop, "Preset selection changes invalidate pending automatic audition");

        editor->sampleProperties.setStatus (SampleStatus::uninitialized, false);
        partner->sampleProperties.setStatus (SampleStatus::uninitialized, false);
        editor->receiveSampleLoadRequest (stereo);
        partner->setStereoRightChannelMode (true);
        ready (*editor, stereoBuffer); flush ();
        check (audition.getPlayState () == State::stop && editor->pendingAutoLoop.has_value (), "Stereo auto audition waits for both ready sides");
        ready (*partner, stereoBuffer); flush ();
        check (audition.getPlayState () == State::loop && audition.getSampleSource () == std::make_tuple (0, 0)
               && ! partner->autoLoopButton.isEnabled (), "Only the stereo controller starts a paired audition");
        partner->sampleProperties.setIsCv (true, false); flush ();
        check (audition.getPlayState () == State::stop && ! editor->autoLoopPlaying,
               "A CV classification on the stereo partner immediately cancels auto audition");
        partner->sampleProperties.setIsCv (false, false);
        active = false; partnerActive = true;
        partner->receiveSampleLoadRequest (stereo); ready (*partner, stereoBuffer); flush ();
        check (audition.getPlayState () == State::stop && ! partner->pendingAutoLoop, "Loading through a stereo follower never starts it independently");
        partnerActive = false; cvActive = true;
        cvEditor->receiveSampleLoadRequest (cv);
        ready (*cvEditor, monoBuffer, true); flush ();
        check (audition.getPlayState () == State::stop && ! cvEditor->pendingAutoLoop && ! cvEditor->autoLoopButton.isEnabled (),
               "A newly imported CV waveform remains silent even with Auto Loop enabled");
        cvActive = false; active = true;

        std::function<void (juce::StringArray)> importComplete;
        int chooserCalls { 0 };
        editor->chooseSampleFiles = [&] (juce::File start, std::function<void (juce::StringArray)> complete)
        {
            check (start == working, "WAV import chooser starts at the current preset without changing root");
            ++chooserCalls; importComplete = std::move (complete);
        };
        const auto beforeChooser { preset.getValueTree ().createCopy () };
        editor->importSamplesButton.onClick ();
        check (chooserCalls == 1, "Visible Import WAV button exposes any-folder import");
        importComplete ({});
        check (preset.getValueTree ().isEquivalentTo (beforeChooser), "Cancelling WAV import preserves every preset setting");
        editor->sampleNameSelectLabel.onChooseFilesRequested ();
        check (chooserCalls == 2, "Filename ellipsis shares the guarded importer");
        editor->zoneProperties.setPitchOffset (1.25, false);
        const auto changedDuringChooser { preset.getValueTree ().createCopy () };
        importComplete ({ mono.getFullPathName () });
        check (preset.getValueTree ().isEquivalentTo (changedDuringChooser), "Async import cannot overwrite a preset edited since the chooser opened");
        editor->importSamples ();
        active = false;
        importComplete ({ mono.getFullPathName () }); active = true;
        check (preset.getValueTree ().isEquivalentTo (changedDuringChooser), "Async import cannot target a now-hidden zone");
        editor->importSamples ();
        audition.setSampleSource (1, 0, true); audition.setSampleSource (0, 0, true);
        importComplete ({ mono.getFullPathName () });
        check (preset.getValueTree ().isEquivalentTo (changedDuringChooser), "Changing channels away and back invalidates a previously opened importer");
        editor->importSamples ();
        app.setMostRecentFolder (outside.getFullPathName ()); app.setMostRecentFolder (working.getFullPathName ());
        importComplete ({ mono.getFullPathName () });
        check (preset.getValueTree ().isEquivalentTo (changedDuringChooser), "Async import ignores a stale folder context even after returning");
        audition.setAutoLoopEnabled (false, true);
        editor->importSamples (); importComplete ({ mono.getFullPathName () });
        check (editor->zoneProperties.getSample ().startsWith ("Bass first")
               && working.getChildFile (editor->zoneProperties.getSample ()).existsAsFile () && audition.getPlayState () == State::stop,
               "A still-current chooser imports successfully without starting audio when Auto Loop is off");
        ready (*editor, monoBuffer); ready (*partner, monoBuffer); flush ();

        const auto previousLight { Theme::isLight () };
        struct Restore { bool light; ~Restore () { Theme::setAppearance (light); } } restore { previousLight };
        struct Panel : juce::Component { void paint (juce::Graphics& g) override { g.fillAll (Theme::panel); } } panel;
        panel.setSize (182, 520);
        panel.addAndMakeVisible (*editor);
        for (const auto light : { false, true })
        {
            Theme::setAppearance (light); Theme::refreshComponentTree (panel);
            editor->selectLoop (false);
            snapshot (panel, light ? "sample-import-auto-loop-light" : "sample-import-auto-loop-dark");
            editor->selectLoop (true);
            snapshot (panel, light ? "sample-import-auto-loop-region-light" : "sample-import-auto-loop-region-dark");
        }
        check (editor->importSamplesButton.getBottom () < editor->sampleStartLabel.getY ()
               && editor->autoLoopButton.getBottom () <= editor->loopPointsView.getY ()
               && editor->cvAuditionNotice.getBottom () < editor->copyNextButton.getY (),
               "Import, Auto Loop, waveform and existing zone tools have separate layout rows");
        panel.removeChildComponent (editor.get ());
        editor->importSamples ();
        editor.reset ();
        importComplete ({ mono.getFullPathName () }); // A delayed native chooser cannot use a destroyed editor.
        partner->setLookAndFeel (nullptr); cvEditor->setLookAndFeel (nullptr);
    }
};

void testAutoLoopUi ()
{
    AutoLoopUiTestAccess::run ();
    std::cout << "PASS: guarded any-folder WAV import, centered filename and session-only ready-gated SAMPLE Auto Loop\n";
}
