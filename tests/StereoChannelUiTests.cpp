#include "GUI/Assimil8or/Editor/Assimil8orEditorComponent.h"
#include "Assimil8or/Preset/StereoChannelTools.h"
#include "Assimil8or/Preset/PairedZoneEdits.h"
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

    void checkSettingsOnlyLoopPermission ()
    {
        const auto defaults { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ()
            .getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType) };
        auto tree { defaults.createCopy () };
        ChannelProperties left (tree.getChild (0), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::yes);
        ChannelProperties right (tree.getChild (1), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::yes);
        left.setAllowLoopOutsideSample (true, false);
        right.setAllowLoopOutsideSample (true, false);
        for (int index { 0 }; index < 2; ++index)
        {
            ZoneProperties zone (tree.getChild (index).getChild (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            zone.setSample ("external-loop.wav", false);
            zone.setSampleStart (8, false); zone.setSampleEnd (24, false);
            zone.setLoopStart (40, false); zone.setLoopLength (8.5, false);
        }
        int permissionChanges { 0 };
        left.onAllowLoopOutsideSampleChange = [&] (bool) { ++permissionChanges; };
        right.onAllowLoopOutsideSampleChange = [&] (bool) { ++permissionChanges; };
        const auto untouched { tree.createCopy () };
        check (StereoChannelTools::copySettingsPreservingLoopPermission (left.getValueTree (), defaults.getChild (0)), "Copy settings-only fixture");
        check (left.getAllowLoopOutsideSample () && permissionChanges == 0,
               "Settings-only copies retain external-loop permission without a transient false notification");
        checkZonesUnchanged (left.getValueTree (), untouched.getChild (0));
        for (const int origin : { 0, 1 })
        {
            right.setChannelMode (ChannelProperties::stereoRight, false);
            check (StereoChannelTools::resetSettings (tree.getChild (origin), defaults.getChild (0)), "Reset pair settings from either side");
            check (left.getAllowLoopOutsideSample () && right.getAllowLoopOutsideSample () && permissionChanges == 0,
                   "Pair Default retains each side's permission for retained external loop zones");
            for (int index { 0 }; index < 2; ++index) checkZonesUnchanged (tree.getChild (index), untouched.getChild (index));
        }
        right.setChannelMode (ChannelProperties::stereoRight, false);
        const auto restore { tree.createCopy () };
        check (StereoChannelTools::purge (left.getValueTree (), defaults.getChild (0)), "Full pair purge fixture");
        check (! left.getAllowLoopOutsideSample () && ! right.getAllowLoopOutsideSample ()
               && ZoneProperties (left.getZoneVT (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no).getSample ().isEmpty (),
               "Full-content purge resets permission along with the removed zone content");
        PresetProperties::copyTreeProperties (restore, tree);
        check (left.getAllowLoopOutsideSample () && right.getAllowLoopOutsideSample (),
               "Full-preset restore transfers the saved permission along with its zone content");
        for (int index { 0 }; index < 2; ++index) checkZonesUnchanged (tree.getChild (index), restore.getChild (index));
        // An enabled source must not enable an unrelated destination when its
        // zones are not copied (the inverse settings-only direction).
        ChannelProperties independent (tree.getChild (2), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        check (StereoChannelTools::copySettingsPreservingLoopPermission (independent.getValueTree (), left.getValueTree ())
               && ! independent.getAllowLoopOutsideSample (), "Settings-only copies do not inherit another channel's editing permission");
    }
}

struct StereoChannelUiTestAccess
{
    static void checkColumnLayout (ChannelEditor& channel)
    {
        const auto before { channel.channelProperties.getValueTree ().createCopy () };
        const auto originalBounds { channel.getBounds () };
        const auto originalMode { channel.loopModeComboBox.getSelectedId () };
        const auto originalAppearance { Theme::isLight () };
        channel.loopModeComboBox.setSelectedId (3, juce::dontSendNotification);
        bool inlineModeSeen { false }, fullWidthModeSeen { false };
        for (const auto width : { 760, 820, 1000, 1140, 1600 })
        {
            channel.setSize (width, 720);
            const std::array<juce::Label*, 4> headings { &channel.pitchLabel, &channel.phaseSourceSectionLabel,
                                                        &channel.mutateLabel, &channel.channelModeLabel };
            const auto columnWidth { headings.front ()->getWidth () };
            check (columnWidth > 100, "Parameter columns are wider than the old fixed 100-point columns, even in compact layouts");
            for (size_t index {}; index < headings.size (); ++index)
            {
                check (headings[index]->getWidth () == columnWidth, "All four parameter columns share the same width");
                check (headings[index]->getRight () + 3 < channel.zoneTabs.getX (), "Columns never overlap the unchanged Zones panel");
                if (index > 0)
                    check (headings[index]->getX () - headings[index - 1]->getRight () == 20, "Column gutters stay equal at every window width");
            }
            check (channel.pitchCVTextEditor.getRight () <= channel.pitchLabel.getRight () + 3 &&
                   channel.releaseModTextEditor.getRight () <= channel.phaseSourceSectionLabel.getRight () + 3 &&
                   channel.mixModTextEditor.getRight () <= channel.mutateLabel.getRight () + 3 &&
                   channel.loopLengthModTextEditor.getRight () <= channel.channelModeLabel.getRight () + 3,
                   "Parameter inputs stay within their own equal-width columns");
            const auto modeFont { channel.loopModeComboBox.getLookAndFeel ().getComboBoxFont (channel.loopModeComboBox) };
            check (channel.loopModeComboBox.getWidth () >= juce::GlyphArrangement::getStringWidth (modeFont, "Loop/Release") + 10.0f,
                   "Loop/Release fits unabridged at the normal font size, including text padding");
            check (! channel.loopModeLabel.getBounds ().intersects (channel.loopModeComboBox.getBounds ()) &&
                   channel.loopStartModLabel.getY () >= channel.loopModeComboBox.getBottom () &&
                   channel.sampleWaveformDisplay.getY () >= channel.allowLoopOutsideSampleButton.getBottom () + 12,
                   "Compact full-width loop mode moves following controls and waveform down without overlaps");
            const auto inlineMode { channel.loopModeLabel.getY () > channel.loopModeComboBox.getY () };
            inlineModeSeen = inlineModeSeen || inlineMode;
            fullWidthModeSeen = fullWidthModeSeen || ! inlineMode;
            check (channel.zoneTabs.getWidth () == 236, "Wider parameter columns do not shrink Zones");
            const auto artifacts { juce::SystemStats::getEnvironmentVariable ("A8MANAGER_TEST_ARTIFACTS", {}) };
            if (artifacts.isNotEmpty () && (width == 760 || width == 1140))
            {
                const juce::File directory { artifacts };
                check (directory.createDirectory ().wasOk (), "Create column layout snapshots");
                for (bool light : { false, true })
                {
                    Theme::setAppearance (light);
                    Theme::refreshComponentTree (channel);
                    const auto content { channel.createComponentSnapshot (channel.getLocalBounds (), true, 1.5f) };
                    juce::Image rendered (juce::Image::RGB, content.getWidth (), content.getHeight (), true);
                    juce::Graphics g (rendered);
                    g.fillAll (Theme::panel);
                    g.drawImageAt (content, 0, 0);
                    auto stream { directory.getChildFile (juce::String ("channel-columns-") + juce::String (width)
                        + (light ? "-light.png" : "-dark.png")).createOutputStream () };
                    check (stream && stream->setPosition (0) && juce::PNGImageFormat ().writeImageToStream (rendered, *stream)
                           && stream->truncate ().wasOk (), "Render real parameter columns and the complete Loop/Release text");
                }
            }
        }
        check (inlineModeSeen && fullWidthModeSeen, "Both roomy inline and compact full-width loop-mode layouts are exercised");
        channel.loopModeComboBox.setSelectedId (originalMode, juce::dontSendNotification);
        channel.setBounds (originalBounds);
        Theme::setAppearance (originalAppearance);
        Theme::refreshComponentTree (channel);
        check (channel.channelProperties.getValueTree ().isEquivalentTo (before), "Resizing and appearance do not alter channel or zone settings");
    }

    static void run ()
    {
        checkSettingsOnlyLoopPermission ();
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
        checkColumnLayout (left);

        auto checkTools = [&] (juce::Component& component, const char* id, const char* caption, int minimumWidth)
        {
            auto* button { dynamic_cast<juce::TextButton*> (component.findChildWithID (id)) };
            check (button != nullptr && button->getButtonText () == caption, "Tool buttons identify their preset/channel scope");
            check (button->getWidth () >= minimumWidth && component.getLocalBounds ().contains (button->getBounds ()),
                   "Renamed tool buttons remain readable and inside their editor");
        };
        checkTools (*editor, "presetTools", "Preset tools", 100);
        checkTools (left, "channelTools", "Channel tools", 108);

        check (! left.allowLoopOutsideSampleButton.getToggleState () && ! right.allowLoopOutsideSampleButton.isEnabled () &&
               left.allowLoopOutsideSampleButton.getY () > left.xfadeGroupComboBox.getBottom (),
               "Independent-loop checkbox defaults off below XFADE and remains read-only on the stereo right");
        const auto beforeIndependent { tree.createCopy () };
        const auto originalConfirmation { left.confirmOutsideLoopReset };
        left.allowLoopOutsideSampleUiChanged (true);
        check (leftProperties.getAllowLoopOutsideSample () && rightProperties.getAllowLoopOutsideSample () &&
               left.allowLoopOutsideSampleButton.getToggleState () && right.allowLoopOutsideSampleButton.getToggleState (),
               "Enabling independent loops synchronizes both stereo flags and checkbox displays");
        right.allowLoopOutsideSampleUiChanged (false);
        check (rightProperties.getAllowLoopOutsideSample (), "Read-only stereo right cannot disable the shared option independently");
        for (const auto channelIndex : { 0, 1 })
        {
            ZoneProperties outside (tree.getChild (channelIndex).getChild (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            outside.setLoopStart (950, false);
            outside.setLoopLength (40.5, false);
        }
        std::function<void (bool)> outsideAnswer;
        int outsidePrompts { 0 };
        left.confirmOutsideLoopReset = [&] (juce::String message, std::function<void (bool)> answer)
        {
            ++outsidePrompts;
            check (message.contains ("2 zone(s)") && message.contains ("stereo"), "Confirmation identifies external loops on both stereo sides");
            outsideAnswer = std::move (answer);
        };
        const auto externalBefore { tree.createCopy () };
        if (const auto path { juce::SystemStats::getEnvironmentVariable ("A8MANAGER_TEST_ARTIFACTS", {}) }; path.isNotEmpty ())
        {
            juce::File directory { path };
            check (directory.createDirectory ().wasOk (), "Create independent-loop UI artifact directory");
            auto stream { directory.getChildFile ("channel-independent-loop.png").createOutputStream () };
            check (stream != nullptr && stream->setPosition (0) &&
                   juce::PNGImageFormat ().writeImageToStream (left.createComponentSnapshot (left.getLocalBounds (), true, 1.5f), *stream) &&
                   stream->truncate ().wasOk (), "Render checkbox placement and unstriped external loop in the actual channel editor");
        }
        left.allowLoopOutsideSampleUiChanged (false);
        check (outsidePrompts == 1 && tree.isEquivalentTo (externalBefore) && left.allowLoopOutsideSampleButton.getToggleState (),
               "Turning independent loops off waits for approval while displaying the still-enabled option");
        outsideAnswer (false);
        check (tree.isEquivalentTo (externalBefore), "Cancel preserves both independent-loop flags and exact boundaries");
        left.allowLoopOutsideSampleUiChanged (false);
        outsideAnswer (true);
        check (! leftProperties.getAllowLoopOutsideSample () && ! rightProperties.getAllowLoopOutsideSample (),
               "Approval turns off independent editing for both stereo companions");
        for (const auto channelIndex : { 0, 1 })
        {
            ZoneProperties reset (tree.getChild (channelIndex).getChild (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            check (! reset.getLoopStart () && ! reset.getLoopLength () && reset.getSampleStart () == 100 && reset.getSampleEnd () == 900,
                   "Approved reset removes only the external loop overrides and retains the selected SAMPLE");
        }
        left.confirmOutsideLoopReset = originalConfirmation;
        PresetProperties::copyTreeProperties (beforeIndependent, tree);
        for (const auto permissionSide : { 0, 1 })
        {
            right.channelModeUiChanged (ChannelProperties::ChannelMode::master);
            leftProperties.setAllowLoopOutsideSample (false, true);
            rightProperties.setAllowLoopOutsideSample (false, true);
            editor->channelProperties[permissionSide].setAllowLoopOutsideSample (true, true);
            right.channelModeUiChanged (ChannelProperties::ChannelMode::stereoRight);
            check (leftProperties.getAllowLoopOutsideSample () && rightProperties.getAllowLoopOutsideSample (),
                   "Manually forming a stereo pair union-enables independent looping from either existing channel");
            right.channelModeUiChanged (ChannelProperties::ChannelMode::master);
            check (leftProperties.getAllowLoopOutsideSample () && rightProperties.getAllowLoopOutsideSample (),
                   "Unpairing retains each channel's loop permission");
        }
        left.channelModeUiChanged (ChannelProperties::ChannelMode::stereoRight);
        leftProperties.setAllowLoopOutsideSample (false, true);
        right.channelModeUiChanged (ChannelProperties::ChannelMode::stereoRight);
        check (! leftProperties.getAllowLoopOutsideSample () && rightProperties.getAllowLoopOutsideSample (),
               "Invalid consecutive stereo-right channels do not propagate loop permission");
        PresetProperties::copyTreeProperties (beforeIndependent, tree);

        editor->channelTabs.setCurrentTabIndex (0);
        check (std::abs (editor->getSelectedDuration (0).value_or (-1.0) - 1024.0 / 48000.0) < 1.0e-9,
               "Designer gets the loaded file duration through the real editor");
        check (std::abs (editor->getSelectedDuration (1).value_or (-1.0) - 800.0 / 48000.0) < 1.0e-9,
               "Designer gets the selected sample region, not the whole file");
        editor->channelTabs.setCurrentTabIndex (1);
        check (std::abs (editor->getSelectedDuration (2).value_or (-1.0) - 300.5 / 48000.0) < 1.0e-9,
               "Designer gets master loop timing when the stereo right channel is selected");
        leftProperties.setPitch (12.0, false);
        check (std::abs (editor->getSelectedDuration (2).value_or (-1.0) - 300.5 / 96000.0) < 1.0e-9,
               "Designer duration matching includes master channel pitch when viewing stereo right");
        leftProperties.setPitch (0.0, false);
        editor->channelTabs.setCurrentTabIndex (0);

        ZoneProperties firstZone (leftProperties.getZoneVT (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        firstZone.setSample ("", false);
        editor->updateChannelTabName (0);
        check (editor->isChannelActive (0) && editor->channelTabs.getTabNames ()[0] == "CH 1-L",
               "A channel with an empty first zone retains its active stereo label when later zones have content");
        firstZone.setSample ("stereo-ui-fixture.wav", false);

        // Copy/Continue select the new zone without retaining the old slice's
        // zoom. Keep the expanded viewer itself open across either operation.
        const auto beforeCopyNext { tree.createCopy () };
        for (const auto continueSlice : { false, true })
        {
            PresetProperties::copyTreeProperties (beforeCopyNext, tree);
            left.zoneTabs.setCurrentTabIndex (0);
            ZoneProperties source (leftProperties.getZoneVT (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            source.setSampleEnd (400, false);
            source.setLoopLength (100.5, false);
            for (auto channelIndex : { 0, 1 })
                ZoneProperties (tree.getChild (channelIndex).getChild (1), ZoneProperties::WrapperType::client,
                                ZoneProperties::EnableCallbacks::no).setSample ("", false);
            if (! left.waveformExpanded) left.sampleWaveformDisplay.onExpandRequested ();
            left.sampleWaveformDisplay.waveform.setVisibleRange (150, 80);
            left.sampleWaveformDisplay.waveform.setVerticalZoom (4.0f);
            left.copyToNextZone (0, continueSlice);
            auto& waveform { left.sampleWaveformDisplay.waveform };
            check (left.getSelectedZoneIndex () == 1 && right.getSelectedZoneIndex () == 1 && left.waveformExpanded,
                   "Copy and Continue select both stereo companions while preserving the expanded viewer");
            check (waveform.getVerticalZoom () == 1.0f && std::abs (waveform.xToSample (0)) < 0.01 &&
                   std::abs (waveform.getSamplesPerPixel () * waveform.getWidth () - audio.getNumSamples ()) < 0.01,
                   "Copy and Continue both reset the new zone to full-file Fit and 100 percent vertical zoom");
        }
        PresetProperties::copyTreeProperties (beforeCopyNext, tree);
        if (left.waveformExpanded) left.sampleWaveformDisplay.onExpandRequested ();
        left.zoneTabs.setCurrentTabIndex (0);

        {
            const auto beforePaste { tree.createCopy () };
            const auto oldFolder { preferences.getMostRecentFolder () };
            const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-zone-paste-ranges", "", false) };
            check (folder.createDirectory ().wasOk (), "Create owned clipboard range fixture");
            struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { folder };
            juce::AudioBuffer<float> shortAudio (2, 800);
            shortAudio.clear ();
            check (WaveformDesign::ExportSupport::writeWave (folder.getChildFile ("stereo-ui-fixture.wav"), audio, 48000.0, false).wasOk () &&
                   WaveformDesign::ExportSupport::writeWave (folder.getChildFile ("short-right.wav"), shortAudio, 48000.0, false).wasOk () &&
                   WaveformDesign::ExportSupport::writeWave (folder.getChildFile ("cv-paste.wav"), audio, 48000.0, true).wasOk (),
                   "Write genuine audio and tagged CV files for clipboard validation");
            preferences.setMostRecentFolder (folder.getFullPathName ());
            ZoneProperties rightTarget (rightProperties.getZoneVT (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            rightTarget.setSample ("short-right.wav", false);
            SampleProperties rightLoaded (samples.getSamplePropertiesVT (1, 0), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
            rightLoaded.setLengthInSamples (800, false);
            auto longSource { firstZone.getValueTree ().createCopy () };
            ZoneProperties copied (longSource, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            copied.setSampleStart (500, false);
            copied.setSampleEnd (2000, false);
            copied.setLoopStart (800, false);
            copied.setLoopLength (600, false);
            auto clipboard { ZoneProperties::create (1) };
            PairedZoneEdits::capture (clipboard, longSource, longSource, true);
            juce::ValueTree prepared;
            juce::StringArray repaired;
            const auto liveBefore { tree.createCopy () };
            const auto playBefore { audition.getValueTree ().createCopy () };
            check (left.prepareZonePaste (0, clipboard, prepared, repaired).wasOk () && repaired.size () == 2,
                   "Settings-only paste identifies repairs independently for both stereo target WAV lengths");
            for (const auto side : { 0, 1 })
            {
                ZoneProperties staged (prepared.getChild (side).getChild (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                const auto range { ZoneSampleRanges::resolve (ZoneSampleRanges::read (staged), side == 0 ? 1024 : 800) };
                check (range.loopValid && range.implicitLoop && range.sampleStart == 500 && range.sampleEnd == (side == 0 ? 1024 : 800),
                       "Pasted invalid loops reset to the repaired SAMPLE on each side instead of retaining out-of-file markers");
            }
            check (tree.isEquivalentTo (liveBefore) && audition.getValueTree ().isEquivalentTo (playBefore),
                   "Staged clipboard repair does not mutate either live stereo channel or playback");
            copied.setSample ("missing-paste.wav", false);
            copied.setSampleStart (64, false);
            copied.setSampleEnd (128, false);
            copied.setLoopStart (200, false);
            copied.setLoopLength (24.25, false);
            PairedZoneEdits::capture (clipboard, longSource, longSource, false);
            check (left.prepareZonePaste (0, clipboard, prepared, repaired).failed () && repaired.isEmpty () &&
                   tree.isEquivalentTo (liveBefore) && audition.getValueTree ().isEquivalentTo (playBefore),
                   "Unavailable WAV rejects paste without guessing EOF, resetting loop markers, or mutating playback/data");
            for (const auto side : { 0, 1 })
            {
                ZoneProperties staged (prepared.getChild (side).getChild (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                check (staged.getSampleStart () == 64 && staged.getSampleEnd () == 128 && staged.getLoopStart () == 200 && staged.getLoopLength () == 24.25,
                       "Missing WAV staging preserves external-loop coordinates; Sample End is not treated as file EOF");
            }
            copied.setSample ("cv-paste.wav", false);
            copied.setSampleStart (500, false);
            copied.setSampleEnd (2000, false);
            copied.setLoopStart (800, false);
            copied.setLoopLength (600, false);
            PairedZoneEdits::capture (clipboard, longSource, longSource, false);
            check (left.prepareZonePaste (0, clipboard, prepared, repaired).failed () && tree.isEquivalentTo (liveBefore) &&
                   audition.getValueTree ().isEquivalentTo (playBefore),
                   "A rejected CV-to-audio paste leaves data and playback unchanged even when range repair was needed");
            copied.setSampleStart (200, false);
            copied.setSampleEnd (700, false);
            copied.setLoopStart (300, false);
            copied.setLoopLength (100.5, false);
            PairedZoneEdits::capture (clipboard, longSource, longSource, true);
            left.copyBufferZoneProperties.getValueTree ().copyPropertiesAndChildrenFrom (clipboard, nullptr);
            left.pasteZone (0);
            for (const auto side : { 0, 1 })
            {
                ZoneProperties pasted (tree.getChild (side).getChild (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                check (pasted.getSampleStart () == 200 && pasted.getSampleEnd () == 700 && pasted.getLoopLength () == 100.5,
                       "Validated settings-only paste applies the staged ranges to both stereo sides");
            }
            check (rightTarget.getSample () == "short-right.wav", "Settings-only paste retains each target sample assignment");
            PresetProperties::copyTreeProperties (beforePaste, tree);
            rightLoaded.setLengthInSamples (1024, false);
            preferences.setMostRecentFolder (oldFolder);
        }

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
        int actionCount { 0 }, pairSubmenuCount { 0 };
        while (menuItems.next ())
        {
            const auto& item { menuItems.getItem () };
            if (item.subMenu != nullptr)
            {
                check (item.text == "Collapse stereo to mono", "Right Tools must not expose independent clone/edit/explode operations");
                ++pairSubmenuCount;
                juce::PopupMenu::MenuItemIterator collapseItems (*item.subMenu);
                int collapseCount { 0 };
                while (collapseItems.next ())
                {
                    const auto& collapse { collapseItems.getItem () };
                    check (collapse.subMenu == nullptr && collapse.action != nullptr
                           && (collapse.text == "Merge L/R..." || collapse.text == "Keep left..." || collapse.text == "Keep right..."),
                           "Stereo-right collapse submenu contains only the three pair-aware conversion operations");
                    ++collapseCount;
                }
                check (collapseCount == 3, "Stereo-right collapse offers exactly Merge, Keep left and Keep right");
            }
            if (item.action)
            {
                check (item.text == "Default (both channels)" || item.text == "Purge this channel...", "Direct stereo-right actions stay limited to pair-aware Default and Purge");
                ++actionCount;
            }
        }
        check (actionCount == 2 && pairSubmenuCount == 1, "Right Tools exposes pair-aware Default, Purge and the stereo-collapse submenu only");
        for (const int origin : { 1, 0 })
        {
            rightProperties.setChannelMode (ChannelProperties::ChannelMode::stereoRight, true);
            leftProperties.setPitch (7.5, true);
            rightProperties.setPan (-0.8, true);
            leftProperties.setAllowLoopOutsideSample (true, true);
            rightProperties.setAllowLoopOutsideSample (true, true);
            const auto before { tree.createCopy () };
            menuAction (editor->createChannelToolsMenu (origin), "Default (both channels)") ();
            check (leftProperties.getChannelMode () == ChannelProperties::ChannelMode::master && rightProperties.getChannelMode () == ChannelProperties::ChannelMode::master,
                   "Default from either stereo side resets both modes without orphaning R");
            check (leftProperties.getPitch () == editor->defaultChannelProperties.getPitch () && rightProperties.getPan () == editor->defaultChannelProperties.getPan (), "Default resets both channel parameter sets");
            check (leftProperties.getAllowLoopOutsideSample () && rightProperties.getAllowLoopOutsideSample (), "Actual pair Default retains the zone-associated loop permission");
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
        independent.setAllowLoopOutsideSample (true, true);
        const auto beforeRevert { independent.getValueTree ().createCopy () };
        menuAction (editor->createChannelToolsMenu (2), "Revert") ();
        check (independent.getPan () == savedChannel.getPan () && independent.getPitch () == savedChannel.getPitch (), "Channel Revert reads the matching saved CHANNEL, not the whole preset");
        check (independent.getAllowLoopOutsideSample (), "Actual settings-only Revert preserves current zone-associated loop permission");
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
