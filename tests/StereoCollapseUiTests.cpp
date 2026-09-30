#include "GUI/Assimil8or/Editor/Assimil8orEditorComponent.h"
#include "GUI/Assimil8or/Editor/SampleManager/SampleManager.h"
#include "Assimil8or/Audio/StereoCollapse.h"
#include "Assimil8or/Audio/CvSampleSafety.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "Assimil8or/PresetFileOperations.h"
#include "Assimil8or/PresetManagerProperties.h"
#include "SystemServices.h"
#include "oolib/Properties/PersistentRootProperties.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace
{
    void check (bool passed, const char* message) { if (! passed) throw std::runtime_error (message); }

    struct MenuAction
    {
        bool present { false }, enabled { false };
        std::function<void ()> invoke;
    };

    MenuAction findAction (const juce::PopupMenu& menu, const juce::String& name)
    {
        juce::PopupMenu::MenuItemIterator items (menu);
        while (items.next ())
        {
            const auto& item { items.getItem () };
            if (item.text.containsIgnoreCase (name)) return { true, item.isEnabled, item.action };
            if (item.subMenu != nullptr)
            {
                auto found { findAction (*item.subMenu, name) };
                if (found.present) return found;
            }
        }
        return {};
    }

    juce::StringArray fileNames (const juce::File& folder)
    {
        juce::StringArray names;
        for (const auto& file : folder.findChildFiles (juce::File::findFiles, false)) names.add (file.getFileName ());
        names.sort (false);
        return names;
    }
}

struct StereoCollapseUiTestAccess
{
    static void run ()
    {
        const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-stereo-collapse-ui", "", false) };
        check (folder.createDirectory ().wasOk (), "Create owned stereo-collapse UI fixture");
        struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { folder };
        const auto wave { folder.getChildFile ("stereo.wav") };
        {
            std::unique_ptr<juce::OutputStream> stream { wave.createOutputStream () };
            auto writer { juce::WavAudioFormat ().createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (48000).withNumChannels (2).withBitsPerSample (24)) };
            juce::AudioBuffer<float> audio (2, 256);
            for (int frame { 0 }; frame < 256; ++frame) { audio.setSample (0, frame, 0.25f); audio.setSample (1, frame, -0.5f); }
            check (writer && writer->writeFromAudioSampleBuffer (audio, 0, 256) && writer->flush (), "Write distinct left/right samples for the real stereo-collapse worker");
        }
        juce::MemoryBlock originalWave;
        check (wave.loadFileAsData (originalWave), "Record original stereo WAV bytes");
        juce::ValueTree root { "Root" };
        PersistentRootProperties persistent (root, PersistentRootProperties::WrapperType::owner, PersistentRootProperties::EnableCallbacks::no);
        RuntimeRootProperties runtime (root, RuntimeRootProperties::WrapperType::owner, RuntimeRootProperties::EnableCallbacks::no);
        AppProperties preferences;
        preferences.wrap (persistent.getValueTree (), AppProperties::WrapperType::owner, AppProperties::EnableCallbacks::no);
        preferences.setMostRecentFolder (folder.getFullPathName ());
        const auto presetFile { folder.getChildFile ("prst023.yml") };
        preferences.addRecentlyUsedFile (presetFile.getFullPathName ());
        AudioPlayerProperties audition (runtime.getValueTree (), AudioPlayerProperties::WrapperType::owner, AudioPlayerProperties::EnableCallbacks::no);
        GuiControlProperties gui (runtime.getValueTree (), GuiControlProperties::WrapperType::owner, GuiControlProperties::EnableCallbacks::no);
        DirectoryDataProperties directory (runtime.getValueTree (), DirectoryDataProperties::WrapperType::owner, DirectoryDataProperties::EnableCallbacks::no);
        PresetManagerProperties presets (runtime.getValueTree (), PresetManagerProperties::WrapperType::owner, PresetManagerProperties::EnableCallbacks::no);
        const auto defaults { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType) };
        presets.addPreset ("edit", defaults.createCopy ());
        presets.addPreset ("unedited", defaults.createCopy ());
        const auto tree { presets.getPreset ("edit") }, baseline { presets.getPreset ("unedited") };
        PresetProperties preset (tree, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        preset.setId (23, false);
        AudioManager audio;
        SystemServices services (runtime.getValueTree (), SystemServices::WrapperType::owner, SystemServices::EnableCallbacks::no);
        services.setAudioManager (&audio);
        services.setAudioPlayer (nullptr); // This headless fixture intentionally has no device-backed player.
        SampleManager samples;
        samples.init (root);
        EditManager edits;
        services.setEditManager (&edits);
        edits.init (root, tree);
        for (int side { 0 }; side < 2; ++side)
        {
            ChannelProperties channel (tree.getChild (side), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            channel.setChannelMode (side == 1 ? ChannelProperties::stereoRight : ChannelProperties::master, false);
            channel.setPan (side == 1 ? 0.6 : -0.4, false);
            for (const auto zoneIndex : { 0, 1, 6 })
            {
                ZoneProperties zone (channel.getZoneVT (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                zone.setSample (wave.getFileName (), false);
                zone.setSide (side, false);
                zone.setSampleStart (8 + zoneIndex, false); zone.setSampleEnd (240 - zoneIndex, false);
                zone.setLoopStart (32 + zoneIndex, false); zone.setLoopLength (128.0, false);
                zone.setPitchOffset (2.5, false);
                zone.setMinVoltage (5.0 - 1.25 * (zoneIndex + 1), false);
            }
        }
        check (PresetFileOperations::save (presetFile, tree, baseline).wasOk (), "Save the stereo-collapse fixture before testing unsaved UI edits");
        const auto original { tree.createCopy () }, clean { baseline.createCopy () };
        const auto savedBytes { presetFile.loadFileAsString () };
        const auto originalFiles { fileNames (folder) };

        // Test the real worker and its UI completion, but dispatch the queued
        // message explicitly: no native windows, audio device or global loop.
        std::mutex completionMutex;
        std::vector<std::function<void ()>> completions;
        std::atomic<int> preparations { 0 };
        std::atomic<bool> failPreparation { false };
        int prompts { 0 }, notices { 0 };
        bool lastNoticeWasError { false };
        juce::String heading, message, lastNotice;
        std::function<void (bool)> answer;
        ModernLookAndFeel look;
        auto editor { std::make_unique<Assimil8orEditorComponent> () };
        const auto dispatch = [&] (std::function<void ()> completion)
        {
            const std::lock_guard<std::mutex> lock (completionMutex);
            completions.push_back (std::move (completion));
            return true;
        };
        auto configureEditor = [&]
        {
            editor->setLookAndFeel (&look);
            editor->init (root);
            editor->setSize (1140, 720);
            editor->confirmStereoCollapse = [&] (const juce::String& title, const juce::String& text, std::function<void (bool)> completion)
            {
                ++prompts; heading = title; message = text; answer = std::move (completion);
            };
            editor->notifyStereoCollapse = [&] (bool error, const juce::String&, const juce::String& text)
            {
                ++notices; lastNoticeWasError = error; lastNotice = text;
            };
            editor->dispatchStereoCollapse = dispatch;
            editor->prepareStereoCollapse = [&] (const juce::File& sourceFolder, const juce::ValueTree& sourcePreset,
                                               int channel, StereoCollapse::Mode mode, StereoCollapse::Result& result)
            {
                ++preparations;
                if (failPreparation.load ()) return juce::Result::fail ("Injected worker failure before writing files");
                return StereoCollapse::prepare (sourceFolder, sourcePreset, channel, mode, result);
            };
        };
        configureEditor ();
        auto nextCompletion = [&] () -> std::function<void ()>
        {
            for (int tick { 0 }; tick < 500; ++tick)
            {
                {
                    std::lock_guard<std::mutex> lock (completionMutex);
                    if (! completions.empty ())
                    {
                        auto completion { std::move (completions.front ()) };
                        completions.erase (completions.begin ());
                        return completion;
                    }
                }
                std::this_thread::sleep_for (std::chrono::milliseconds (10));
            }
            throw std::runtime_error ("Stereo-collapse worker did not queue its completion");
        };
        const juce::String keepRight { "Keep Right" };
        auto action = [&] (int channel)
        {
            auto found { findAction (editor->createChannelToolsMenu (channel), keepRight) };
            check (found.present && found.enabled && found.invoke != nullptr, "Valid stereo pair offers an enabled Keep Right action from either channel");
            return found.invoke;
        };
        auto unchanged = [&]
        {
            juce::MemoryBlock currentWave;
            check (wave.loadFileAsData (currentWave) && currentWave == originalWave && presetFile.loadFileAsString () == savedBytes
                   && baseline.isEquivalentTo (clean), "Stereo-collapse actions never overwrite original WAVs, saved YAML or the clean baseline");
        };

        for (const int channel : { 0, 1 })
            for (const auto* name : { "Merge", "Keep Left", "Keep Right" })
            {
                const auto item { findAction (editor->createChannelToolsMenu (channel), name) };
                check (item.present && item.enabled && item.invoke != nullptr, "Both stereo tabs offer all three mono-collapse operations");
            }
        check (! findAction (editor->createChannelToolsMenu (2), keepRight).enabled,
               "Independent mono channels cannot invoke a stereo-collapse operation");
        editor->channelProperties[0].setChannelMode (ChannelProperties::stereoRight, false);
        check (! findAction (editor->createChannelToolsMenu (0), keepRight).enabled
               && ! findAction (editor->createChannelToolsMenu (1), keepRight).enabled,
               "An orphaned or chained Stereo Right mode is not presented as a valid pair");
        PresetProperties::copyTreeProperties (original, tree);

        audition.setPlayState (AudioPlayerProperties::PlayState::loop, false);
        const auto duplicateAction { action (1) };
        duplicateAction ();
        check (prompts == 1 && heading.contains ("CH 1") && heading.contains ("CH 2")
               && message.containsIgnoreCase ("all") && message.containsIgnoreCase ("zone") && message.containsIgnoreCase ("Save"),
               "Confirmation identifies the whole stereo pair, all zones and the need to Save afterward");
        duplicateAction ();
        check (prompts == 1 && ! findAction (editor->createChannelToolsMenu (0), keepRight).enabled,
               "Pending confirmation disables another collapse and rejects an already-open duplicate action");
        answer (false); answer (true);
        check (tree.isEquivalentTo (original) && preparations.load () == 0 && fileNames (folder) == originalFiles
               && audition.getPlayState () == AudioPlayerProperties::PlayState::loop,
               "Canceled confirmation is single-use and leaves preset, files and audition unchanged");
        check (! message.contains ("group relationship"), "An independent following channel does not produce a Link/Cycle group warning");

        for (const auto followingMode : { ChannelProperties::link, ChannelProperties::cycle })
        {
            editor->channelProperties[2].setChannelMode (followingMode, false);
            const auto groupedPreset { tree.createCopy () };
            action (0) ();
            check (message.contains (followingMode == ChannelProperties::link ? "Link mode" : "Cycle mode")
                   && message.contains ("Freeing CH 2") && message.contains ("group relationship"),
                   "Collapse confirmation warns when freeing the right channel may change a following Link/Cycle group");
            answer (false);
            check (tree.isEquivalentTo (groupedPreset) && editor->channelProperties[2].getChannelMode () == followingMode
                   && preparations.load () == 0 && fileNames (folder) == originalFiles,
                   "Canceling a group-impact warning preserves CH 3, the complete preset and all original files");
            PresetProperties::copyTreeProperties (original, tree);
        }

        const auto staleMenu { action (0) };
        editor->channelProperties[0].setPitch (3.0, false);
        const auto promptsBeforeStaleMenu { prompts };
        staleMenu ();
        check (prompts == promptsBeforeStaleMenu && preparations.load () == 0, "An action opened before a preset edit cannot request a stale collapse");
        PresetProperties::copyTreeProperties (original, tree);
        action (0) ();
        editor->channelProperties[0].setPitch (4.0, false);
        const auto changedBeforeApproval { tree.createCopy () };
        answer (true);
        check (preparations.load () == 0 && tree.isEquivalentTo (changedBeforeApproval), "Editing during confirmation invalidates its approval without rewriting the new edits");
        PresetProperties::copyTreeProperties (original, tree);
        action (0) ();
        const auto alternate { folder.getChildFile ("alternate") };
        check (alternate.createDirectory ().wasOk (), "Create owned folder-navigation fixture");
        preferences.setMostRecentFolder (alternate.getFullPathName ());
        answer (true);
        check (preparations.load () == 0 && tree.isEquivalentTo (original), "Changing folders before approval cannot start a worker against the old preset");
        preferences.setMostRecentFolder (folder.getFullPathName ());
        preferences.addRecentlyUsedFile (presetFile.getFullPathName ());

        failPreparation = true;
        action (0) ();
        answer (true);
        const auto failedCompletion { nextCompletion () };
        const auto beforeDuplicateWorker { preparations.load () };
        const auto promptsBeforeDuplicateWorker { prompts };
        duplicateAction ();
        check (! findAction (editor->createChannelToolsMenu (1), keepRight).enabled && preparations.load () == beforeDuplicateWorker
               && prompts == promptsBeforeDuplicateWorker, "A queued worker completion remains busy and cannot launch a second collapse");
        failedCompletion ();
        check (notices > 0 && lastNoticeWasError && lastNotice.contains ("Injected worker failure")
               && tree.isEquivalentTo (original) && fileNames (folder) == originalFiles,
               "Failed preparation reports the error and preserves the live preset and every original file");
        failPreparation = false;
        unchanged ();

        editor->dispatchStereoCollapse = [] (std::function<void ()>) { return false; };
        const auto beforeRejectedDispatch { preparations.load () };
        const auto noticesBeforeRejectedDispatch { notices };
        action (0) ();
        answer (true);
        check (editor->stereoCollapseThread.joinable (), "A rejected-dispatch fixture starts the real preparation worker");
        editor->stereoCollapseThread.join ();
        check (preparations.load () == beforeRejectedDispatch + 1 && fileNames (folder) == originalFiles && tree.isEquivalentTo (original),
               "Rejected completion delivery cleans generated files on the worker without applying the preset");
        editor->timerCallback ();
        check (notices == noticesBeforeRejectedDispatch + 1 && lastNoticeWasError && lastNotice.contains ("deliver")
               && ! editor->stereoCollapseJob && findAction (editor->createChannelToolsMenu (0), keepRight).enabled,
               "Message-thread timer reports rejected delivery and releases busy state for a safe retry");
        editor->dispatchStereoCollapse = dispatch;
        unchanged ();

        action (0) ();
        answer (true);
        const auto staleCompletion { nextCompletion () };
        editor->channelProperties[0].setPitch (4.5, false);
        const auto changedDuringWorker { tree.createCopy () };
        staleCompletion ();
        check (tree.isEquivalentTo (changedDuringWorker) && fileNames (folder) == originalFiles,
               "Stale completion preserves newer edits and removes only its unapplied generated files");
        unchanged ();
        PresetProperties::copyTreeProperties (original, tree);

        editor->channelTabs.setCurrentTabIndex (1);
        editor->channelEditors[0].setSelectedZoneFromPartner (6);
        editor->channelEditors[1].setSelectedZoneFromPartner (6);
        const auto leftIdentity { tree.getChild (0) }, rightIdentity { tree.getChild (1) };
        action (1) ();
        answer (true);
        nextCompletion () ();
        editor->timerCallback ();
        check (tree.getChild (0) == leftIdentity && tree.getChild (1) == rightIdentity
               && editor->channelTabs.getCurrentTabIndex () == 0 && editor->channelEditors[0].getSelectedZoneIndex () == 6,
               "Keep Right retains live tree identities and selects the retained left channel and current zone");
        check (editor->channelProperties[0].getChannelMode () == ChannelProperties::master
               && editor->channelProperties[1].getChannelMode () == ChannelProperties::master,
               "Stereo collapse leaves two independent Master channels");
        for (int zoneIndex { 0 }; zoneIndex < 8; ++zoneIndex)
        {
            ZoneProperties retained (tree.getChild (0).getChild (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            ZoneProperties freed (tree.getChild (1).getChild (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            SampleProperties retainedSample (samples.getSampleProperties (0, zoneIndex), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
            SampleProperties freedSample (samples.getSampleProperties (1, zoneIndex), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
            check (freed.getSample ().isEmpty () && freedSample.getAudioBufferPtr () == nullptr && freedSample.getName ().isEmpty (),
                   "All freed right-channel zones remain empty despite real stereo callbacks and their sample caches unload");
            const bool populated { zoneIndex == 0 || zoneIndex == 1 || zoneIndex == 6 };
            if (! populated) { check (retained.getSample ().isEmpty (), "Collapse preserves empty zones on the retained channel"); continue; }
            check (retained.getSample ().isNotEmpty () && retained.getSample () != wave.getFileName () && retained.getSide () == 0
                   && retained.getSampleStart ().value_or (-1) == 8 + zoneIndex && retained.getSampleEnd ().value_or (-1) == 240 - zoneIndex
                   && retained.getLoopStart ().value_or (-1) == 32 + zoneIndex && retained.getLoopLength ().value_or (-1.0) == 128.0
                   && retained.getPitchOffset () == 2.5, "Every populated left zone references mono audio while retaining its original marker and pitch settings");
            const auto* mono { retainedSample.getAudioBufferPtr () };
            check (retainedSample.getName () == retained.getSample () && retainedSample.getStatus () == SampleStatus::exists
                   && retainedSample.getNumChannels () == 1 && retainedSample.getLengthInSamples () == 256 && mono != nullptr
                   && std::abs (mono->getSample (0, 50) + 0.5f) < 1.0e-6f,
                   "SampleManager immediately reloads right-side PCM into each retained mono zone");
        }
        auto rightDefaults { defaults.getChild (0).createCopy () };
        rightDefaults.setProperty (ChannelProperties::IdPropertyId, 2, nullptr);
        check (PresetHelpers::areChannelsEqual (tree.getChild (1), rightDefaults), "The freed right channel resets every channel setting, not only its samples");
        for (int channel { 2 }; channel < 8; ++channel)
            check (tree.getChild (channel).isEquivalentTo (original.getChild (channel)), "Collapse preserves every unrelated channel");
        check (editor->channelActionSession.isDirty () && editor->savePendingLabel.isVisible () && editor->saveButton.isEnabled (),
               "The collapsed pair is visibly unsaved and requires an explicit Save");
        check (audition.getPlayState () == AudioPlayerProperties::PlayState::stop, "Accepted stereo collapse stops sample audition before altering playback sources");
        unchanged ();

        // Exercise the real CV safety callbacks: clearing R's sample references
        // must precede copying its default Mix settings, or those callbacks keep
        // the newly empty channel incorrectly muted.
        PresetProperties::copyTreeProperties (original, tree);
        const auto cvWave { folder.getChildFile ("stereo-cv.wav") };
        {
            const auto tags { CvSampleSafety::exportMetadata (true) };
            std::unordered_map<juce::String, juce::String> metadata;
            for (int index { 0 }; index < tags.size (); ++index) metadata.emplace (tags.getAllKeys ()[index], tags.getAllValues ()[index]);
            std::unique_ptr<juce::OutputStream> stream { cvWave.createOutputStream () };
            auto writer { juce::WavAudioFormat ().createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (48000)
                .withNumChannels (2).withBitsPerSample (24).withMetadataValues (metadata)) };
            juce::AudioBuffer<float> data (2, 256);
            for (int frame { 0 }; frame < 256; ++frame) { data.setSample (0, frame, 0.1f); data.setSample (1, frame, 0.2f); }
            check (writer && writer->writeFromAudioSampleBuffer (data, 0, 256) && writer->flush (), "Create purpose-tagged stereo CV fixture");
        }
        for (int side { 0 }; side < 2; ++side)
            for (const auto zoneIndex : { 0, 1, 6 })
                ZoneProperties (tree.getChild (side).getChild (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no)
                    .setSample (cvWave.getFileName (), false);
        check (editor->channelProperties[0].getMixLevel () == -90.0 && editor->channelProperties[1].getMixLevel () == -90.0,
               "Actual CV sample loading mutes both members of the stereo fixture before collapse");
        action (1) ();
        answer (true);
        nextCompletion () ();
        ChannelProperties defaultChannel (defaults.getChild (0), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        check (editor->channelProperties[0].getMixLevel () == -90.0
               && editor->channelProperties[1].getMixLevel () == defaultChannel.getMixLevel (),
               "CV mono output remains Mix Off while the genuinely empty freed channel restores its default Mix level");
        for (const auto zoneIndex : { 0, 1, 6 })
        {
            SampleProperties retainedCv (samples.getSampleProperties (0, zoneIndex), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
            SampleProperties freedCv (samples.getSampleProperties (1, zoneIndex), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
            check (retainedCv.getIsCv () && retainedCv.getNumChannels () == 1 && retainedCv.getAudioBufferPtr () != nullptr
                   && std::abs (retainedCv.getAudioBufferPtr ()->getSample (0, 50) - 0.2f) < 1.0e-6f
                   && freedCv.getAudioBufferPtr () == nullptr && freedCv.getName ().isEmpty (),
                   "CV collapse preserves purpose-tagged right-side PCM in L and fully unloads R's CV sample cache");
        }
        unchanged ();

        PresetProperties::copyTreeProperties (original, tree);
        action (0) ();
        const auto disposedApproval { answer };
        const auto beforeDestroyedEditor { tree.createCopy () };
        const auto beforeDestroy { preparations.load () };
        editor->setLookAndFeel (nullptr);
        editor.reset ();
        disposedApproval (true);
        staleMenu ();
        check (preparations.load () == beforeDestroy && tree.isEquivalentTo (beforeDestroyedEditor), "Late confirmation and menu callbacks safely ignore a destroyed editor");
        unchanged ();

        editor = std::make_unique<Assimil8orEditorComponent> ();
        configureEditor ();
        const auto beforeQueuedDestroy { tree.createCopy () };
        const auto filesBeforeQueuedDestroy { fileNames (folder) };
        const auto noticesBeforeQueuedDestroy { notices };
        action (0) ();
        answer (true);
        const auto queuedAfterDestruction { nextCompletion () };
        check (fileNames (folder) != filesBeforeQueuedDestroy && tree.isEquivalentTo (beforeQueuedDestroy),
               "The destroyed-worker fixture has generated real mono files but has not committed its queued UI result");
        editor->setLookAndFeel (nullptr);
        editor.reset ();
        check (fileNames (folder) == filesBeforeQueuedDestroy && tree.isEquivalentTo (beforeQueuedDestroy),
               "Editor destruction immediately cleans uncommitted mono files even while a queued callback retains the job");
        queuedAfterDestruction ();
        check (notices == noticesBeforeQueuedDestroy && fileNames (folder) == filesBeforeQueuedDestroy
               && tree.isEquivalentTo (beforeQueuedDestroy), "A worker completion delivered after editor destruction cannot notify, assign or recreate files");
        unchanged ();
        std::cout << "PASS: actual stereo-collapse menus, confirmation/cancel, async failures/staleness/rejected dispatch/destruction, Keep Right PCM/cache routing, freed-channel/CV callbacks and unsaved state\n";
    }
};

void testStereoCollapseUi () { StereoCollapseUiTestAccess::run (); }
