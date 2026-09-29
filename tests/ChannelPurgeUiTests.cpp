#include "GUI/Assimil8or/Editor/Assimil8orEditorComponent.h"
#include "GUI/Assimil8or/Editor/SampleManager/SampleManager.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "Assimil8or/PresetFileOperations.h"
#include "Assimil8or/PresetManagerProperties.h"
#include "SystemServices.h"
#include "oolib/Properties/PersistentRootProperties.h"
#include <iostream>
#include <stdexcept>

struct ChannelPurgeUiTestAccess
{
    static void run ()
    {
        auto check = [] (bool ok, const char* message) { if (! ok) throw std::runtime_error (message); };
        const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-channel-purge", "", false) };
        check (folder.createDirectory ().wasOk (), "Create owned channel purge fixtures");
        struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { folder };
        const auto file { folder.getChildFile ("stereo.wav") };
        {
            std::unique_ptr<juce::OutputStream> stream { file.createOutputStream () };
            juce::WavAudioFormat format;
            auto writer { format.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (48000).withNumChannels (2).withBitsPerSample (24)) };
            juce::AudioBuffer<float> buffer (2, 128);
            for (int i { 0 }; i < 128; ++i) { buffer.setSample (0, i, 0.25f); buffer.setSample (1, i, -0.5f); }
            check (writer && writer->writeFromAudioSampleBuffer (buffer, 0, 128) && writer->flush (), "Write real stereo fixture");
        }
        const auto recipe { folder.getChildFile ("keep.design.json") };
        check (recipe.replaceWithText ("Keep this file"), "Write owned companion-file preservation fixture");
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
        SampleManager samples;
        samples.init (root);
        EditManager edits;
        services.setEditManager (&edits);
        edits.init (root, tree);
        for (int c { 0 }; c < 8; ++c)
        {
            ChannelProperties channel (tree.getChild (c), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            channel.setChannelMode (c == 1 ? ChannelProperties::stereoRight : ChannelProperties::master, false);
            channel.setPitch (3.25, false);
            for (int z { 0 }; z < 8; ++z)
            {
                ZoneProperties zone (channel.getZoneVT (z), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                zone.setSample (file.getFileName (), false);
                zone.setSide (c == 1 ? 1 : 0, false);
                zone.setSampleStart (8, false); zone.setSampleEnd (120, false);
                zone.setLoopStart (16, false); zone.setLoopLength (64, false);
                zone.setMinVoltage (5.0 - 1.25 * (z + 1), false);
            }
        }
        check (PresetFileOperations::save (presetFile, tree, baseline).wasOk (), "Save initial purge fixture preset");
        const auto original { tree.createCopy () }, clean { baseline.createCopy () };
        const auto savedBytes { presetFile.loadFileAsString () };
        ModernLookAndFeel look;
        auto editor { std::make_unique<Assimil8orEditorComponent> () };
        editor->setLookAndFeel (&look);
        editor->init (root);
        editor->setSize (1140, 720);
        std::function<void (bool)> answer;
        juce::String title, message;
        int prompts { 0 };
        editor->confirmChannelPurge = [&] (const juce::String& heading, const juce::String& text, std::function<void (bool)> completion)
        {
            title = heading; message = text; answer = std::move (completion); ++prompts;
        };
        auto action = [&] (int channel)
        {
            const auto menu { editor->createChannelToolsMenu (channel) };
            juce::PopupMenu::MenuItemIterator items (menu);
            while (items.next ())
                if (items.getItem ().text == "Purge this channel...")
                {
                    check (items.getItem ().isEnabled && items.getItem ().action != nullptr, "Bound channel offers purge action");
                    return items.getItem ().action;
                }
            throw std::runtime_error ("Missing channel purge action");
        };
        check (editor->channelTabs.onTabPopup != nullptr, "Channel tabs expose their tools through right-click");
        audition.setPlayState (AudioPlayerProperties::PlayState::loop, false);
        action (1) ();
        check (prompts == 1 && title.contains ("CH 1") && title.contains ("CH 2") && message.contains ("eight zones") && message.contains ("NOT deleted"),
               "Stereo-right purge confirms both channel identities, full scope and file preservation");
        answer (false); answer (true);
        check (tree.isEquivalentTo (original) && audition.getPlayState () == AudioPlayerProperties::PlayState::loop,
               "Canceled confirmation is single-use and preserves preset and audition");
        const auto staleAction { action (0) };
        editor->channelProperties[0].setPitch (7.0, true);
        const auto changed { tree.createCopy () };
        staleAction ();
        check (prompts == 1 && tree.isEquivalentTo (changed), "A menu opened before edits cannot request destructive confirmation for a changed document");
        PresetProperties::copyTreeProperties (original, tree);
        action (0) ();
        editor->channelProperties[1].setPan (0.3, true);
        editor->channelProperties[1].setPan (static_cast<double> (original.getChild (1).getProperty (ChannelProperties::PanPropertyId)), true);
        answer (true);
        check (tree.isEquivalentTo (original), "Changing and reverting a stereo partner invalidates pending purge approval");
        action (0) ();
        const auto other { folder.getChildFile ("other") };
        check (other.createDirectory ().wasOk (), "Create owned alternate folder");
        preferences.setMostRecentFolder (other.getFullPathName ());
        preferences.setMostRecentFolder (folder.getFullPathName ());
        answer (true);
        check (tree.isEquivalentTo (original), "Navigating away and back invalidates an old purge confirmation");
        action (0) ();
        preset.setId (24, false); preset.setId (23, false);
        answer (true);
        check (tree.isEquivalentTo (original), "Switching preset identity and back cannot reuse old purge approval");

        for (const int origin : { 1, 0, 2 })
        {
            PresetProperties::copyTreeProperties (original, tree);
            const auto first { origin < 2 ? 0 : 2 }, last { origin < 2 ? 2 : 3 };
            const auto channelIdentity { tree.getChild (origin) }, zoneIdentity { channelIdentity.getChild (7) };
            audition.setPlayState (AudioPlayerProperties::PlayState::loop, false);
            action (origin) ();
            answer (true);
            check (audition.getPlayState () == AudioPlayerProperties::PlayState::stop && editor->channelActionSession.isDirty (),
                   "Confirmed purge stops audition and leaves the edited preset unsaved");
            check (tree.getChild (origin) == channelIdentity && channelIdentity.getChild (7) == zoneIdentity,
                   "Purge retains existing channel and zone identities for editors and sample observers");
            for (int c { 0 }; c < 8; ++c)
            {
                if (c < first || c >= last)
                { check (tree.getChild (c).isEquivalentTo (original.getChild (c)), "Purge never changes unrelated channels"); continue; }
                auto expected { defaults.getChild (0).createCopy () };
                expected.setProperty (ChannelProperties::IdPropertyId, c + 1, nullptr);
                // XML defaults store numbers as strings; setters normalize their
                // types and CV decimal formatting. Compare parameter values.
                check (PresetHelpers::areChannelsEqual (tree.getChild (c), expected)
                       && tree.getChild (c).getProperty (ChannelProperties::IdPropertyId) == expected.getProperty (ChannelProperties::IdPropertyId),
                       "Every selected channel setting resets to defaults with its ID retained");
                check (editor->channelEditors[c].getSelectedZoneIndex () == 0, "Purged channel selects its first empty zone");
                for (int z { 0 }; z < 8; ++z)
                {
                    check (PresetHelpers::areZonesEqual (tree.getChild (c).getChild (z), expected.getChild (z))
                           && static_cast<int> (tree.getChild (c).getChild (z).getProperty (ZoneProperties::IdPropertyId)) == z + 1,
                           "All eight zone settings reset to defaults with their IDs retained");
                    SampleProperties sample (samples.getSampleProperties (c, z), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
                    check (sample.getName ().isEmpty () && sample.getAudioBufferPtr () == nullptr && sample.getStatus () != SampleStatus::exists,
                           "SampleManager unloads every purged zone immediately");
                }
            }
            check (baseline.isEquivalentTo (clean) && presetFile.loadFileAsString () == savedBytes && file.existsAsFile () && recipe.existsAsFile (),
                   "Purge leaves clean baseline, saved preset and sample/recipe files untouched until Save");
            const auto after { tree.createCopy () };
            answer (true);
            check (tree.isEquivalentTo (after), "Successful purge approval cannot be reused");
        }
        PresetProperties::copyTreeProperties (original, tree);
        const auto disposedAction { action (0) };
        disposedAction ();
        editor->setLookAndFeel (nullptr);
        editor.reset ();
        answer (true); disposedAction ();
        check (tree.isEquivalentTo (original), "Late popup and confirmation callbacks cannot purge a destroyed editor's document");
        std::cout << "PASS: confirmed channel purge, stereo pairing, sample-cache unload, file preservation and stale/lifetime safety\n";
    }
};

void testChannelPurgeUi () { ChannelPurgeUiTestAccess::run (); }
