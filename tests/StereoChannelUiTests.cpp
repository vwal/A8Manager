#include "GUI/Assimil8or/Editor/Assimil8orEditorComponent.h"
#include "Assimil8or/Preset/StereoChannelTools.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "Assimil8or/PresetManagerProperties.h"
#include "Assimil8or/Audio/WaveformDesignAssignment.h"
#include "Assimil8or/Audio/WaveformDesignExport.h"
#include "SystemServices.h"
#include "oolib/Properties/PersistentRootProperties.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void check (bool passed, const char* message) { if (! passed) throw std::runtime_error (message); }

    std::function<void ()> menuAction (const juce::PopupMenu& menu, const juce::String& name)
    {
        juce::PopupMenu::MenuItemIterator items (menu);
        while (items.next ())
            if (items.getItem ().text == name) return items.getItem ().action;
        throw std::runtime_error (("Missing menu action: " + name).toStdString ());
    }

    bool menuEnabled (const juce::PopupMenu& menu, const juce::String& name)
    {
        juce::PopupMenu::MenuItemIterator items (menu);
        while (items.next ())
            if (items.getItem ().text == name) return items.getItem ().isEnabled;
        throw std::runtime_error (("Missing menu item: " + name).toStdString ());
    }

    void checkZonesUnchanged (juce::ValueTree actual, juce::ValueTree before)
    {
        check (actual.getProperty (ChannelProperties::IdPropertyId) == before.getProperty (ChannelProperties::IdPropertyId), "Channel reset retains its ID");
        check (actual.getNumChildren () == before.getNumChildren (), "Channel reset retains every zone");
        for (int zone { 0 }; zone < actual.getNumChildren (); ++zone)
            check (actual.getChild (zone).isEquivalentTo (before.getChild (zone)), "Channel reset never changes sample/zone data or IDs");
    }
}

struct StereoChannelUiTestAccess
{
    static void run ()
    {
        // Use the real, fully initialized parent and all 64 zone editors, but
        // in-memory properties/audio plus owned recall files: no device,
        // scanner or persisted user preferences.
        juce::ValueTree root { "Root" };
        PersistentRootProperties persistent (root, PersistentRootProperties::WrapperType::owner, PersistentRootProperties::EnableCallbacks::no);
        RuntimeRootProperties runtime (root, RuntimeRootProperties::WrapperType::owner, RuntimeRootProperties::EnableCallbacks::no);
        AppProperties preferences;
        preferences.wrap (persistent.getValueTree (), AppProperties::WrapperType::owner, AppProperties::EnableCallbacks::no);
        AudioPlayerProperties audition (runtime.getValueTree (), AudioPlayerProperties::WrapperType::owner, AudioPlayerProperties::EnableCallbacks::no);
        GuiControlProperties gui (runtime.getValueTree (), GuiControlProperties::WrapperType::owner, GuiControlProperties::EnableCallbacks::no);
        SampleManagerProperties samples (runtime.getValueTree (), SampleManagerProperties::WrapperType::owner, SampleManagerProperties::EnableCallbacks::no);
        PresetManagerProperties presets (runtime.getValueTree (), PresetManagerProperties::WrapperType::owner, PresetManagerProperties::EnableCallbacks::no);
        SystemServices services (runtime.getValueTree (), SystemServices::WrapperType::owner, SystemServices::EnableCallbacks::no);
        AudioManager audioManager;
        EditManager edits;
        services.setAudioManager (&audioManager);
        services.setEditManager (&edits);
        const auto defaults { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType) };
        auto tree { defaults.createCopy () };
        juce::AudioBuffer<float> audio (2, 1024);
        for (int sample { 0 }; sample < audio.getNumSamples (); ++sample)
        {
            audio.setSample (0, sample, 0.5f * std::sin (sample * 0.07f));
            audio.setSample (1, sample, 0.35f * std::sin (sample * 0.05f));
        }
        for (int channelIndex { 0 }; channelIndex < 8; ++channelIndex)
        {
            ChannelProperties channel (tree.getChild (channelIndex), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            channel.setChannelMode (channelIndex == 1 || channelIndex == 7 ? ChannelProperties::ChannelMode::stereoRight : ChannelProperties::ChannelMode::master, false);
            for (int zoneIndex { 0 }; zoneIndex < 8; ++zoneIndex)
            {
                ZoneProperties zone (channel.getZoneVT (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                zone.setSample ("stereo-ui-fixture.wav", false);
                zone.setSampleStart (100 + zoneIndex, false);
                zone.setSampleEnd (900, false);
                zone.setLoopStart (200, false);
                zone.setLoopLength (300.5, false);
                zone.setSide (channelIndex % 2, false);
                zone.setMinVoltage (5.0 - 1.25 * (zoneIndex + 1), false);
                SampleProperties sample (samples.getSamplePropertiesVT (channelIndex, zoneIndex), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
                sample.setAudioBufferPtr (&audio, false);
                sample.setLengthInSamples (audio.getNumSamples (), false);
                sample.setNumChannels (2, false);
                sample.setSampleRate (48000.0, false);
                sample.setStatus (SampleStatus::exists, false);
            }
        }
        auto saved { tree.createCopy () };
        ChannelProperties savedChannel (saved.getChild (2), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        savedChannel.setPan (-0.37, false);
        savedChannel.setPitch (4.5, false);
        presets.addPreset ("edit", tree);
        presets.addPreset ("unedited", saved);
        edits.init (root, tree);

        ModernLookAndFeel look;
        auto editor { std::make_unique<Assimil8orEditorComponent> () };
        editor->setLookAndFeel (&look);
        editor->init (root);
        editor->setSize (1140, 720);
        auto& left { editor->channelEditors[0] };
        auto& right { editor->channelEditors[1] };
        auto& leftProperties { editor->channelProperties[0] };
        auto& rightProperties { editor->channelProperties[1] };

        auto checkTools = [&] (juce::Component& component, const char* id, const char* caption, int minimumWidth)
        {
            auto* button { dynamic_cast<juce::TextButton*> (component.findChildWithID (id)) };
            check (button != nullptr && button->getButtonText () == caption, "Tool buttons identify their preset/channel scope");
            check (button->getWidth () >= minimumWidth && component.getLocalBounds ().contains (button->getBounds ()),
                   "Renamed tool buttons remain readable and inside their editor");
        };
        checkTools (*editor, "presetTools", "Preset tools", 100);
        checkTools (left, "channelTools", "Channel tools", 108);

        editor->channelTabs.setCurrentTabIndex (0);
        check (std::abs (editor->getSelectedDuration (0).value_or (-1.0) - 1024.0 / 48000.0) < 1.0e-9,
               "Designer gets the loaded file duration through the real editor");
        check (std::abs (editor->getSelectedDuration (1).value_or (-1.0) - 800.0 / 48000.0) < 1.0e-9,
               "Designer gets the selected sample region, not the whole file");
        editor->channelTabs.setCurrentTabIndex (1);
        check (std::abs (editor->getSelectedDuration (2).value_or (-1.0) - 300.5 / 48000.0) < 1.0e-9,
               "Designer gets master loop timing when the stereo right channel is selected");
        editor->channelTabs.setCurrentTabIndex (0);

        std::function<void ()> disposedRecall;
        {
            const auto beforeRecall { tree.createCopy () };
            const auto beforeFolder { preferences.getMostRecentFolder () };
            const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-recall-menu", "", false) };
            check (folder.createDirectory ().wasOk (), "Create owned recall-menu fixture folder");
            struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { folder };
            auto cycle { WaveformDesign::startingPoint (WaveformDesign::Mode::oscillator, WaveformDesign::Shape::sine) };
            cycle.cycleFrames = 64;
            WaveformDesign::ExportResult package;
            check (WaveformDesign::exportDesign (cycle, folder, "UI recall", package).wasOk (), "Create actual exported audio package for Samples menu recognition");
            auto bank { WaveformDesign::startingPoint (WaveformDesign::Mode::layers, WaveformDesign::Shape::saw) };
            bank.cycleFrames = 64;
            WaveformDesign::AssignmentResult assignment;
            check (WaveformDesign::prepareAssignment (bank, package.folder, "UI bank", defaults.createCopy (), 0, 0, assignment).wasOk (),
                   "Create actual uniquely named assigned bank for Samples menu recognition");
            const auto ordinary { package.folder.getChildFile ("ordinary-sample.wav") };
            check (package.waves[0].copyFileTo (ordinary), "Create ordinary WAV without a generated recipe naming association");
            preferences.setMostRecentFolder (package.folder.getFullPathName ());
            int recalledChannel { -1 }, recalledZone { -1 }, recallCount { 0 };
            editor->onRecallWaveform = [&] (int channel, int zone)
            {
                recalledChannel = channel;
                recalledZone = zone;
                ++recallCount;
            };
            const juce::String actionName { "Edit selected waveform in designer..." };
            auto setSample = [&] (int channel, int zone, const juce::String& filename)
            {
                ZoneProperties properties (tree.getChild (channel).getChild (zone), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                properties.setSample (filename, false);
            };
            auto expectEnabled = [&] (bool enabled, const char* message)
            {
                check (editor->canRecallSelectedWaveform () == enabled, message);
                check (menuEnabled (editor->createPresetToolsMenu (), actionName) == enabled,
                       "The actual Samples TOOLS item has the same enabled/greyed-out state as validated waveform recognition");
            };
            setSample (0, 0, package.waves[0].getFileName ());
            expectEnabled (true, "An exported audio cycle can be recalled from Samples");
            const auto enabledMenu { editor->createPresetToolsMenu () };
            auto enabledAction { menuAction (enabledMenu, actionName) };
            disposedRecall = enabledAction;
            const auto beforeDispatch { tree.createCopy () };
            enabledAction ();
            check (recalledChannel == 0 && recalledZone == 0 && recallCount == 1,
                   "Enabled Samples TOOLS recall action dispatches the current zero-based channel and zone");
            check (tree.isEquivalentTo (beforeDispatch), "Opening a design never edits the preset contents");
            editor->channelTabs.setCurrentTabIndex (2);
            editor->channelEditors[2].zoneTabs.setCurrentTabIndex (4);
            setSample (2, 4, assignment.waves[2].getFileName ());
            expectEnabled (true, "An assigned bank follower resolves its shared saved recipe");
            editor->recallSelectedWaveform ();
            check (recalledChannel == 2 && recalledZone == 4 && recallCount == 2,
                   "Recall follows the newly selected independent channel and zone rather than a cached selection");
            editor->channelTabs.setCurrentTabIndex (1);
            right.zoneTabs.setCurrentTabIndex (3);
            setSample (1, 3, package.waves[0].getFileName ());
            expectEnabled (true, "A recognized waveform on the selected stereo side is recallable");
            editor->recallSelectedWaveform ();
            check (recalledChannel == 1 && recalledZone == 3 && recallCount == 3,
                   "Recall reports the exact selected stereo side instead of substituting its master channel");

            editor->channelTabs.setCurrentTabIndex (0);
            left.zoneTabs.setCurrentTabIndex (0);
            for (const auto& filename : { ordinary.getFileName (), juce::String (), juce::String ("missing.wav"), juce::String ("../voice-01.wav") })
            {
                setSample (0, 0, filename);
                expectEnabled (false, "Ordinary, empty, missing and nonflat sample references cannot be recalled as saved designs");
                editor->recallSelectedWaveform ();
                enabledAction ();
                menuAction (editor->createPresetToolsMenu (), actionName) ();
                check (recallCount == 3, "Disabled or previously enabled recall actions recheck the current sample instead of dispatching ordinary files");
            }
            setSample (0, 0, package.waves[0].getFileName ());
            const auto recipeContents { package.recipe.loadFileAsString () };
            check (package.recipe.deleteFile (), "Remove only the owned recipe fixture");
            expectEnabled (false, "A generated filename with a missing recipe is greyed out");
            enabledAction ();
            check (recallCount == 3, "A stale menu cannot recall after its recipe disappears");
            check (package.recipe.replaceWithText ("{invalid JSON"), "Write malformed owned recipe fixture");
            expectEnabled (false, "A generated filename with a malformed recipe is greyed out");
            enabledAction ();
            check (recallCount == 3, "A stale menu cannot recall a malformed recipe");
            check (package.recipe.replaceWithText (recipeContents), "Restore genuine exported recipe fixture");
            expectEnabled (true, "Restoring the valid saved recipe restores menu availability");
            preferences.setMostRecentFolder (folder.getFullPathName ());
            expectEnabled (false, "Recall uses the current preset folder rather than a cached source folder");
            enabledAction ();
            check (recallCount == 3, "A previously enabled menu does not dispatch after switching to a folder without the design");
            preferences.setMostRecentFolder (package.folder.getFullPathName ());
            editor->onRecallWaveform = nullptr;
            expectEnabled (false, "Recall is greyed out when no workspace callback is installed");
            editor->recallSelectedWaveform ();
            enabledAction ();
            check (recallCount == 3, "Recall safely ignores a missing workspace callback");
            PresetProperties::copyTreeProperties (beforeRecall, tree);
            preferences.setMostRecentFolder (beforeFolder);
            check (tree.isEquivalentTo (beforeRecall), "Recall eligibility fixtures restore all original preset data");
            editor->channelEditors[2].zoneTabs.setCurrentTabIndex (0);
            left.zoneTabs.setCurrentTabIndex (0);
            editor->channelTabs.setCurrentTabIndex (0);
        }

        int leftCallbacks { 0 }, rightCallbacks { 0 };
        const auto leftCallback { left.onSelectedZoneChanged }, rightCallback { right.onSelectedZoneChanged };
        left.onSelectedZoneChanged = [&] (int zone) { ++leftCallbacks; leftCallback (zone); };
        right.onSelectedZoneChanged = [&] (int zone) { ++rightCallbacks; rightCallback (zone); };
        left.zoneTabs.setCurrentTabIndex (3);
        check (right.getSelectedZoneIndex () == 3 && leftCallbacks == 1 && rightCallbacks == 0, "Actual left-tab callback synchronizes right without recursion");
        right.zoneTabs.setCurrentTabIndex (6);
        check (left.getSelectedZoneIndex () == 6 && leftCallbacks == 1 && rightCallbacks == 1, "Actual right-tab callback synchronizes left without recursion");
        left.zoneEditors[6].selectLoop (true);
        right.zoneEditors[2].selectLoop (false);
        audition.setSampleSource (0, 6, false);
        audition.setSamplePointsSelector (AudioPlayerProperties::SamplePointsSelector::LoopPoints, false);
        audition.setPlayState (AudioPlayerProperties::PlayState::loop, false);
        const auto auditionBefore { audition.getValueTree ().createCopy () };
        right.setSelectedZoneFromPartner (2);
        right.setSelectedZoneFromPartner (-1);
        right.setSelectedZoneFromPartner (8);
        check (right.getSelectedZoneIndex () == 2 && left.getSelectedZoneIndex () == 6 && rightCallbacks == 1, "Quiet setter validates bounds and does not echo selection");
        check (audition.getValueTree ().isEquivalentTo (auditionBefore), "Quiet partner selection preserves play state, source, and LOOP/SAMPLE routing");
        left.zoneTabs.setCurrentTabIndex (5);
        check (right.getSelectedZoneIndex () == 5 && audition.getValueTree ().isEquivalentTo (auditionBefore), "Hidden editor changes cannot stop another channel's audition");

        rightProperties.setChannelMode (ChannelProperties::ChannelMode::master, true);
        left.zoneTabs.setCurrentTabIndex (1);
        right.zoneTabs.setCurrentTabIndex (4);
        check (left.getSelectedZoneIndex () == 1 && right.getSelectedZoneIndex () == 4, "Independent channels retain independent selections");
        editor->channelTabs.setCurrentTabIndex (1);
        right.sampleWaveformDisplay.onExpandRequested ();
        check (right.waveformExpanded, "Independent channel can expand its waveform before pairing");
        rightProperties.setChannelMode (ChannelProperties::ChannelMode::stereoRight, true);
        check (right.waveformExpanded && right.sampleWaveformDisplay.isEnabled () && right.sampleWaveformDisplay.isReadOnly (),
               "Entering Stereo Right preserves the expanded viewer and keeps its close control usable");
        right.sampleWaveformDisplay.onExpandRequested ();
        check (! right.waveformExpanded && right.panTextEditor.isVisible (), "Read-only expanded waveform can close to restore Pan controls");
        check (left.getSelectedZoneIndex () == 4 && right.getSelectedZoneIndex () == 4, "Forming a pair adopts the currently selected right channel's zone");
        leftProperties.setChannelMode (ChannelProperties::ChannelMode::stereoRight, true);
        right.zoneTabs.setCurrentTabIndex (7);
        check (left.getSelectedZoneIndex () == 4 && ! StereoChannelTools::partner (tree.getChild (0)).isValid () && ! StereoChannelTools::partner (tree.getChild (1)).isValid (), "Orphan/consecutive Stereo Right channels do not synchronize");
        leftProperties.setChannelMode (ChannelProperties::ChannelMode::master, true);
        editor->channelProperties[6].setChannelMode (ChannelProperties::ChannelMode::link, true);
        editor->channelEditors[7].zoneTabs.setCurrentTabIndex (5);
        check (editor->channelEditors[6].getSelectedZoneIndex () == 5, "Channel eight safely resolves a preceding Link-mode controller");

        check (right.panTextEditor.isEnabled () && right.panModComboBox.isEnabled () && right.panModTextEditor.isEnabled (), "Stereo Right exposes Pan and modulation controls");
        check (! right.pitchTextEditor.isEnabled () && ! right.mixLevelTextEditor.isEnabled () && right.sampleWaveformDisplay.isEnabled ()
               && right.sampleWaveformDisplay.isReadOnly (), "Stereo Right keeps inherited parameters read-only without disabling waveform navigation");
        check (right.toolsButton.isEnabled (), "Stereo Right can open its safe Default menu");
        right.panTextEditor.setValue (0.45);
        right.panModComboBox.setSelectedItemText ("1A");
        right.panModComboBox.onChange ();
        right.panModTextEditor.setValue (0.7);
        check (std::abs (rightProperties.getPan () - 0.45) < 0.0001 && FormatHelpers::getCvInput (rightProperties.getPanMod ()) == "1A"
               && std::abs (FormatHelpers::getAmount (rightProperties.getPanMod ()) - 0.7) < 0.0001, "Actual Pan editor/CV/amount callbacks save right-channel values");
        check (leftProperties.getPan () != rightProperties.getPan (), "Right pan changes do not overwrite left pan");
        const auto overlay { right.stereoRightTransparantOverly.createComponentSnapshot (right.getLocalBounds ()) };
        check (overlay.getPixelAt (right.panTextEditor.getBounds ().getCentreX (), right.panTextEditor.getBounds ().getCentreY ()).getAlpha () == 0
               && overlay.getPixelAt (right.panModComboBox.getBounds ().getCentreX (), right.panModComboBox.getBounds ().getCentreY ()).getAlpha () == 0,
               "Right Pan and CV controls are visibly excluded from the disabled overlay");
        check (overlay.getPixelAt (right.pitchTextEditor.getBounds ().getCentreX (), right.pitchTextEditor.getBounds ().getCentreY ()).getAlpha () > 0, "Inherited pitch remains visually dimmed");
        check (overlay.getPixelAt (right.sampleWaveformDisplay.getBounds ().getCentreX (), right.sampleWaveformDisplay.getBounds ().getCentreY ()).getAlpha () == 0,
               "Navigable stereo-right waveform is excluded from the disabled overlay");
        const auto artifacts { juce::SystemStats::getEnvironmentVariable ("A8MANAGER_TEST_ARTIFACTS", {}) };
        if (artifacts.isNotEmpty ())
        {
            juce::File directory { artifacts };
            check (directory.createDirectory ().wasOk (), "Create UI artifact directory");
            auto output { directory.getChildFile ("stereo-right-pan-150.png").createOutputStream () };
            check (output != nullptr && output->setPosition (0), "Open stereo UI artifact");
            const auto snapshot { right.createComponentSnapshot (right.getLocalBounds (), true, 1.5f) };
            juce::Image rendered (juce::Image::RGB, snapshot.getWidth (), snapshot.getHeight (), true);
            juce::Graphics graphics (rendered);
            graphics.fillAll (Theme::panel);
            graphics.drawImageAt (snapshot, 0, 0);
            check (juce::PNGImageFormat ().writeImageToStream (rendered, *output), "Write stereo UI artifact");
            check (output->truncate ().wasOk (), "Truncate old UI artifact tail");
        }

        auto rightMenu { editor->createChannelToolsMenu (1) };
        juce::PopupMenu::MenuItemIterator menuItems (rightMenu);
        int actionCount { 0 };
        while (menuItems.next ())
        {
            const auto& item { menuItems.getItem () };
            check (item.subMenu == nullptr, "Right Tools must not expose independent clone/edit/explode operations");
            if (item.action) ++actionCount;
        }
        check (actionCount == 2, "Right Tools exposes only pair-aware Default and Purge");
        for (const int origin : { 1, 0 })
        {
            rightProperties.setChannelMode (ChannelProperties::ChannelMode::stereoRight, true);
            leftProperties.setPitch (7.5, true);
            rightProperties.setPan (-0.8, true);
            const auto before { tree.createCopy () };
            menuAction (editor->createChannelToolsMenu (origin), "Default (both channels)") ();
            check (leftProperties.getChannelMode () == ChannelProperties::ChannelMode::master && rightProperties.getChannelMode () == ChannelProperties::ChannelMode::master,
                   "Default from either stereo side resets both modes without orphaning R");
            check (leftProperties.getPitch () == editor->defaultChannelProperties.getPitch () && rightProperties.getPan () == editor->defaultChannelProperties.getPan (), "Default resets both channel parameter sets");
            for (int c { 0 }; c < 2; ++c) checkZonesUnchanged (tree.getChild (c), before.getChild (c));
            for (int c { 2 }; c < 8; ++c) check (tree.getChild (c).isEquivalentTo (before.getChild (c)), "Pair Default does not change unrelated channels");
        }
        rightProperties.setChannelMode (ChannelProperties::ChannelMode::stereoRight, true);
        const auto staleDefault { menuAction (editor->createChannelToolsMenu (1), "Default (both channels)") };
        rightProperties.setPan (0.2, true);
        const auto afterChange { tree.createCopy () };
        staleDefault ();
        check (tree.isEquivalentTo (afterChange), "Stale Default popup cannot overwrite subsequent edits");
        auto& independent { editor->channelProperties[2] };
        independent.setPan (0.9, true);
        independent.setPitch (-7.0, true);
        const auto beforeRevert { independent.getValueTree ().createCopy () };
        menuAction (editor->createChannelToolsMenu (2), "Revert") ();
        check (independent.getPan () == savedChannel.getPan () && independent.getPitch () == savedChannel.getPitch (), "Channel Revert reads the matching saved CHANNEL, not the whole preset");
        checkZonesUnchanged (independent.getValueTree (), beforeRevert);

        auto& cvEditor { editor->channelEditors[2] };
        SampleProperties cvState (samples.getSamplePropertiesVT (2, 0), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
        cvState.setName ("stereo-ui-fixture.wav", true);
        independent.setMixLevel (0.0, true);
        independent.setMixMod ("1A", 0.5, true);
        cvState.setIsCv (true, true);
        check (independent.getMixLevel () == -90.0 && FormatHelpers::getCvInput (independent.getMixMod ()) == "Off",
               "Loaded CV metadata forces the entire channel out of the stereo mix");
        check (! cvEditor.mixLevelTextEditor.isEnabled () && ! cvEditor.mixModComboBox.isEnabled () &&
               ! cvEditor.mixModTextEditor.isEnabled () && ! cvEditor.mixModIsFaderComboBox.isEnabled (),
               "CV channels visibly lock mix level and every mix modulation control");
        check (cvEditor.mixLevelTextEditor.getText () == "Off" && cvEditor.mixLevelTextEditor.getTooltip ().contains ("CV channel"),
               "CV mix displays Off and explains the individual-output routing");
        independent.setMixLevel (3.0, true);
        independent.setMixMod ("2B", 1.0, true);
        check (independent.getMixLevel () == -90.0 && FormatHelpers::getCvInput (independent.getMixMod ()) == "Off",
               "Programmatic edits or an older menu cannot restore CV stereo-mix level or modulation");
        cvState.setName ("old-file.wav", true);
        check (cvEditor.mixLevelTextEditor.isEnabled () && cvEditor.mixModComboBox.isEnabled (),
               "Stale CV metadata for a different filename does not lock the current zone's channel");
        cvState.setName ("stereo-ui-fixture.wav", true);
        check (! cvEditor.mixLevelTextEditor.isEnabled (), "Matching CV metadata restores the mix safety lock");
        cvState.setIsCv (false, true);
        check (cvEditor.mixLevelTextEditor.isEnabled () && cvEditor.mixModTextEditor.isEnabled () && cvEditor.mixModIsFaderComboBox.isEnabled (),
               "Removing the final matching CV sample restores ordinary independent-channel mix editing");
        cvEditor.mixLevelTextEditor.setValue (0.0);
        check (independent.getMixLevel () == 0.0, "Ordinary audio mix remains editable after CV safety lock clears");
        rightProperties.setChannelMode (ChannelProperties::stereoRight, true);
        check (! right.mixLevelTextEditor.isEnabled () && ! right.mixModComboBox.isEnabled (),
               "Clearing another channel's CV state never enables inherited Stereo Right mix controls");
        const auto disposedDefault { menuAction (editor->createChannelToolsMenu (1), "Default (both channels)") };
        editor->setLookAndFeel (nullptr);
        editor.reset ();
        disposedDefault ();
        disposedRecall ();
        std::cout << "PASS: actual stereo UI tab synchronization, audition isolation, right-pan editing/contrast, restricted tools, pair Default/channel Revert and validated Samples recall availability\n";
    }
};

void testStereoChannelUi () { StereoChannelUiTestAccess::run (); }
