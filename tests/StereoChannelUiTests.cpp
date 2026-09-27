#include "GUI/Assimil8or/Editor/Assimil8orEditorComponent.h"
#include "Assimil8or/Preset/StereoChannelTools.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "Assimil8or/PresetManagerProperties.h"
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
        // only in-memory properties/audio: no device, scanner or preferences.
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

        editor->channelTabs.setCurrentTabIndex (0);
        check (std::abs (editor->getSelectedDuration (0).value_or (-1.0) - 1024.0 / 48000.0) < 1.0e-9,
               "Designer gets the loaded file duration through the real editor");
        check (std::abs (editor->getSelectedDuration (1).value_or (-1.0) - 800.0 / 48000.0) < 1.0e-9,
               "Designer gets the selected sample region, not the whole file");
        editor->channelTabs.setCurrentTabIndex (1);
        check (std::abs (editor->getSelectedDuration (2).value_or (-1.0) - 300.5 / 48000.0) < 1.0e-9,
               "Designer gets master loop timing when the stereo right channel is selected");
        editor->channelTabs.setCurrentTabIndex (0);

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
        check (! right.waveformExpanded && right.panTextEditor.isVisible (), "Entering Stereo Right restores access to Pan instead of trapping an expanded waveform");
        check (left.getSelectedZoneIndex () == 4 && right.getSelectedZoneIndex () == 4, "Forming a pair adopts the currently selected right channel's zone");
        leftProperties.setChannelMode (ChannelProperties::ChannelMode::stereoRight, true);
        right.zoneTabs.setCurrentTabIndex (7);
        check (left.getSelectedZoneIndex () == 4 && ! StereoChannelTools::partner (tree.getChild (0)).isValid () && ! StereoChannelTools::partner (tree.getChild (1)).isValid (), "Orphan/consecutive Stereo Right channels do not synchronize");
        leftProperties.setChannelMode (ChannelProperties::ChannelMode::master, true);
        editor->channelProperties[6].setChannelMode (ChannelProperties::ChannelMode::link, true);
        editor->channelEditors[7].zoneTabs.setCurrentTabIndex (5);
        check (editor->channelEditors[6].getSelectedZoneIndex () == 5, "Channel eight safely resolves a preceding Link-mode controller");

        check (right.panTextEditor.isEnabled () && right.panModComboBox.isEnabled () && right.panModTextEditor.isEnabled (), "Stereo Right exposes Pan and modulation controls");
        check (! right.pitchTextEditor.isEnabled () && ! right.mixLevelTextEditor.isEnabled () && ! right.sampleWaveformDisplay.isEnabled (), "Stereo Right keeps inherited parameters read-only");
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
        check (actionCount == 1, "Right Tools exposes only pair-aware Default");
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
        const auto disposedDefault { menuAction (editor->createChannelToolsMenu (1), "Default (both channels)") };
        editor->setLookAndFeel (nullptr);
        editor.reset ();
        disposedDefault ();
        std::cout << "PASS: actual stereo UI tab synchronization, audition isolation, right-pan editing/contrast, restricted tools, pair Default and channel Revert\n";
    }
};

void testStereoChannelUi () { StereoChannelUiTestAccess::run (); }
