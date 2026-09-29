#include "Assimil8or/Preset/PairedZoneEdits.h"
#include "Assimil8or/Preset/ZoneContinuation.h"
#include "Assimil8or/Preset/StereoChannelTools.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "Assimil8or/Preset/PresetHelpers.h"
#include "Assimil8or/Audio/WaveformDesignExport.h"
#include "Assimil8or/PresetManagerProperties.h"
#include "GUI/Assimil8or/Editor/EditManager.h"
#include "GUI/Assimil8or/Editor/SampleManager/SampleManagerProperties.h"
#include "SystemServices.h"
#include "oolib/Properties/PersistentRootProperties.h"
#include "oolib/Properties/RuntimeRootProperties.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    void check (bool condition, const char* message) { if (! condition) throw std::runtime_error (message); }

    juce::ValueTree makePair ()
    {
        juce::ValueTree preset { PresetProperties::PresetTypeId };
        for (auto side { 0 }; side < 2; ++side)
        {
            auto tree { ChannelProperties::create (side + 1) };
            preset.addChild (tree, -1, nullptr);
            ChannelProperties channel (tree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            channel.setChannelMode (side == 0 ? ChannelProperties::ChannelMode::master : ChannelProperties::ChannelMode::stereoRight, false);
            for (auto index { 0 }; index < 8; ++index)
            {
                const auto value { ZoneProperties::create (index + 1) };
                tree.addChild (value, -1, nullptr);
                ZoneProperties zone (value, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                zone.setMinVoltage (-5.0, false);
                zone.setSide (side, false);
                if (index == 0)
                {
                    zone.setSample (side == 0 ? "left.wav" : "right.wav", false);
                    zone.setSampleStart (100, false);
                    zone.setSampleEnd (200, false);
                    zone.setLoopStart (120, false);
                    zone.setLoopLength (40.5, false);
                    zone.setPitchOffset (3.0, false);
                }
            }
        }
        return preset;
    }

    juce::ValueTree zoneTree (juce::ValueTree preset, int side, int index)
    {
        return ChannelProperties (preset.getChild (side), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no).getZoneVT (index);
    }

    ZoneProperties zone (juce::ValueTree preset, int side, int index)
    {
        return { zoneTree (preset, side, index), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no };
    }

    void checkChannelPurge ()
    {
        const auto defaults { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType) };
        const auto defaultChannel { defaults.getChild (0) };
        auto populatedPreset = [&] ()
        {
            auto tree { defaults.createCopy () };
            for (int index { 0 }; index < 8; ++index)
            {
                ChannelProperties channel (tree.getChild (index), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
                channel.setChannelMode (index == 1 || index == 7 ? ChannelProperties::stereoRight : ChannelProperties::link, false);
                channel.setPitch (3.5 + index, false);
                // Exact binary fractions avoid a near-zero epsilon residual
                // that JUCE ValueTree deliberately considers equal to zero.
                channel.setPan (-0.875 + index * 0.0625, false);
                channel.setMixLevel (-12.0, false);
                for (int position { 0 }; position < 8; ++position)
                {
                    auto entry { zone (tree, index, position) };
                    // Include hidden/sparse assignments rather than relying on
                    // the visible consecutive-used-zone count.
                    entry.setSample (position % 3 == 0 || position == 7 ? "keep-on-disk.wav" : "", false);
                    entry.setSampleStart (100 + position, false);
                    entry.setSampleEnd (800 + position, false);
                    entry.setLoopStart (200 + position, false);
                    entry.setLoopLength (300.5, false);
                    entry.setPitchOffset (4.0, false);
                    entry.setLevelOffset (-6.0, false);
                    entry.setMinVoltage (5.0 - 1.25 * (position + 1), false);
                    entry.setSide (index % 2, false);
                }
            }
            return tree;
        };
        struct Changes : juce::ValueTree::Listener
        {
            std::vector<int> modeOrder;
            bool clearedWhilePaired { false };
            void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override
            {
                if (property == ChannelProperties::ChannelModePropertyId)
                    modeOrder.push_back (static_cast<int> (tree.getProperty (ChannelProperties::IdPropertyId)));
                if (tree.hasType (ZoneProperties::ZoneTypeId) && property == ZoneProperties::SamplePropertyId)
                    clearedWhilePaired = clearedWhilePaired || StereoChannelTools::partner (tree.getParent ()).isValid ();
            }
        };
        for (const auto selected : { 0, 1, 3, 6, 7 })
        {
            auto tree { populatedPreset () };
            const auto before { tree.createCopy () };
            const auto paired { StereoChannelTools::partner (tree.getChild (selected)) };
            const auto pairedIndex { paired.isValid () ? static_cast<int> (paired.getProperty (ChannelProperties::IdPropertyId)) - 1 : -1 };
            std::array<juce::ValueTree, 8> channels;
            std::array<std::array<juce::ValueTree, 8>, 8> zones;
            for (int channel { 0 }; channel < 8; ++channel)
            {
                channels[channel] = tree.getChild (channel);
                for (int position { 0 }; position < 8; ++position) zones[channel][position] = zoneTree (tree, channel, position);
            }
            Changes changes;
            tree.addListener (&changes);
            check (StereoChannelTools::purge (tree.getChild (selected), defaultChannel), "Purge independent channel or stereo pair from either side");
            tree.removeListener (&changes);
            check (! changes.clearedWhilePaired, "Purge detaches pair before any zone callbacks can propagate stale content");
            if (pairedIndex >= 0)
                check (changes.modeOrder.size () == 2 && changes.modeOrder[0] == std::max (selected, pairedIndex) + 1,
                       "Purge unpairs right before resetting left, including a Channel 7/8 pair");
            for (int channel { 0 }; channel < 8; ++channel)
            {
                check (tree.getChild (channel) == channels[channel], "Purge retains each live channel tree identity");
                for (int position { 0 }; position < 8; ++position)
                    check (zoneTree (tree, channel, position) == zones[channel][position], "Purge retains each live zone tree identity");
                if (channel == selected || channel == pairedIndex)
                {
                    // XML defaults store numbers as text, while setters use
                    // typed values and normalize e.g. "Off 1.0" to "Off 1.0000".
                    check (PresetHelpers::areChannelsEqual (tree.getChild (channel), defaultChannel)
                           && static_cast<int> (tree.getChild (channel).getProperty (ChannelProperties::IdPropertyId)) == channel + 1,
                           "Purge restores every channel setting while retaining its ID");
                    for (int position { 0 }; position < 8; ++position)
                        check (PresetHelpers::areZonesEqual (zones[channel][position], defaultChannel.getChild (position))
                               && static_cast<int> (zones[channel][position].getProperty (ZoneProperties::IdPropertyId)) == position + 1,
                               "Purge restores all eight zone parameter values while retaining slot IDs");
                }
                else check (tree.getChild (channel).isEquivalentTo (before.getChild (channel)), "Purge never modifies unrelated channels or zones");
            }
        }

        auto tree { populatedPreset () };
        for (const auto invalid : { juce::ValueTree {}, juce::ValueTree { "NotAChannel" } })
        {
            const auto before { tree.createCopy () };
            check (! StereoChannelTools::purge (invalid, defaultChannel) && tree.isEquivalentTo (before), "Invalid purge target is a no-op");
        }
        for (int scenario { 0 }; scenario < 6; ++scenario)
        {
            tree = populatedPreset ();
            auto config { defaultChannel.createCopy () };
            if (scenario == 0) tree.getChild (1).removeChild (7, nullptr);
            if (scenario == 1) tree.getChild (0).getChild (7).setProperty (ZoneProperties::IdPropertyId, 7, nullptr);
            if (scenario == 2) config.removeChild (7, nullptr);
            if (scenario == 3) config.getChild (0).setProperty (ZoneProperties::SamplePropertyId, "not-empty.wav", nullptr);
            if (scenario == 4) config.setProperty (ChannelProperties::ChannelModePropertyId, ChannelProperties::stereoRight, nullptr);
            if (scenario == 5) config.getChild (0).setProperty (ZoneProperties::IdPropertyId, 8, nullptr);
            const auto before { tree.createCopy () };
            check (! StereoChannelTools::purge (tree.getChild (0), config) && tree.isEquivalentTo (before),
                   "Malformed target, partner or nonempty/invalid defaults reject before any partial purge");
        }
        // Do not turn another orphan Stereo Right into a new partner merely by
        // resetting the preceding channel to Master.
        for (const auto selected : { 0, 1 })
        {
            tree = populatedPreset ();
            tree.getChild (0).setProperty (ChannelProperties::ChannelModePropertyId, ChannelProperties::stereoRight, nullptr);
            const auto before { tree.createCopy () };
            check (! StereoChannelTools::partner (tree.getChild (selected)).isValid () && ! StereoChannelTools::purge (tree.getChild (selected), defaultChannel)
                   && tree.isEquivalentTo (before), "Orphan or consecutive Stereo Right purge fails without changing any channel");
        }
        for (const auto selected : { 0, 1 })
        {
            tree = populatedPreset ();
            tree.getChild (2).setProperty (ChannelProperties::ChannelModePropertyId, ChannelProperties::stereoRight, nullptr);
            const auto before { tree.createCopy () };
            check (! StereoChannelTools::purge (tree.getChild (selected), defaultChannel) && tree.isEquivalentTo (before),
                   "Purging a pair cannot accidentally adopt a third Stereo Right outside the requested pair");
        }
    }

    void checkCvChannelPurge ()
    {
        const auto directory { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-channel-purge", "", false) };
        const auto folder { directory.getChildFile ("preset") };
        check (folder.createDirectory ().wasOk (), "Create owned channel-purge sample fixture folder");
        struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { directory };
        const auto cvFile { directory.getChildFile ("control.wav") }, audioFile { directory.getChildFile ("audio.wav") };
        juce::AudioBuffer<float> data (1, 128);
        for (int frame { 0 }; frame < 128; ++frame) data.setSample (0, frame, 0.3f * std::sin (frame * 0.1f));
        check (WaveformDesign::ExportSupport::writeWave (cvFile, data, 48000.0, true).wasOk ()
               && WaveformDesign::ExportSupport::writeWave (audioFile, data, 48000.0, false).wasOk (), "Write genuine tagged CV/audio purge fixtures");
        juce::MemoryBlock originalCv;
        check (cvFile.loadFileAsData (originalCv), "Keep original CV bytes for nondestructive-purge verification");

        juce::ValueTree root { "Root" };
        PersistentRootProperties persistent (root, PersistentRootProperties::WrapperType::owner, PersistentRootProperties::EnableCallbacks::no);
        RuntimeRootProperties runtime (root, RuntimeRootProperties::WrapperType::owner, RuntimeRootProperties::EnableCallbacks::no);
        AppProperties app;
        app.wrap (persistent.getValueTree (), AppProperties::WrapperType::owner, AppProperties::EnableCallbacks::no);
        app.setMostRecentFolder (folder.getFullPathName ());
        PresetManagerProperties presets (runtime.getValueTree (), PresetManagerProperties::WrapperType::owner, PresetManagerProperties::EnableCallbacks::no);
        SampleManagerProperties samples (runtime.getValueTree (), SampleManagerProperties::WrapperType::owner, SampleManagerProperties::EnableCallbacks::no);
        SystemServices services (runtime.getValueTree (), SystemServices::WrapperType::owner, SystemServices::EnableCallbacks::no);
        AudioManager audio;
        EditManager edits;
        services.setAudioManager (&audio);
        services.setEditManager (&edits);
        const auto defaults { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType) };
        presets.addPreset ("edit", defaults.createCopy ());
        const auto tree { presets.getPreset ("edit") };
        edits.init (root, tree);
        juce::Array<juce::File> importedCvFiles;
        for (int index { 0 }; index < 2; ++index)
        {
            check (edits.assignSamples (index, 0, { cvFile.getFullPathName () }), "Assign CV before channel purge");
            importedCvFiles.add (folder.getChildFile (zone (tree, index, 0).getSample ()));
            SampleProperties loaded (samples.getSamplePropertiesVT (index, 0), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
            loaded.setIsCv (true, false);
            loaded.setName (zone (tree, index, 0).getSample (), false);
            ChannelProperties channel (tree.getChild (index), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            channel.setMixLevel (0.0, false);
            channel.setMixMod ("1A", 0.5, false);
            check (edits.channelContainsCv (index) && channel.getMixLevel () == -90.0 && std::get<0> (channel.getMixMod ()) == "Off",
                   "Real model callbacks protect CV mix before purging");
        }
        tree.getChild (1).setProperty (ChannelProperties::ChannelModePropertyId, ChannelProperties::stereoRight, nullptr);
        check (StereoChannelTools::purge (tree.getChild (1), defaults.getChild (0)), "Purge a CV stereo pair from its right side");
        for (int index { 0 }; index < 2; ++index)
        {
            ChannelProperties channel (tree.getChild (index), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            ChannelProperties defaultChannel (defaults.getChild (0), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            check (! edits.channelContainsCv (index) && channel.getChannelMode () == ChannelProperties::master
                   && channel.getMixLevel () == defaultChannel.getMixLevel () && channel.getMixMod () == defaultChannel.getMixMod (),
                   "Empty purged channels restore default Mix despite retained old CV metadata and active safety callbacks");
            check (edits.assignSamples (index, 0, { audioFile.getFullPathName () }), "A purged CV channel can immediately receive ordinary audio");
            const auto assignedName { zone (tree, index, 0).getSample () };
            check (assignedName.isNotEmpty () && folder.getChildFile (assignedName).hasIdenticalContentTo (audioFile),
                   "Post-purge audio assignment reaches the selected channel, including collision-safe renamed imports");
        }
        juce::MemoryBlock sourceCv;
        check (cvFile.loadFileAsData (sourceCv) && sourceCv == originalCv, "Purge never deletes or changes source WAV bytes");
        for (const auto& imported : importedCvFiles)
        {
            juce::MemoryBlock importedCv;
            check (imported.loadFileAsData (importedCv) && importedCv == originalCv, "Purge never deletes or changes any imported WAV bytes");
        }
    }
}

void testPairedZoneEdits ()
{
    auto preset { makePair () };
    const auto left { preset.getChild (0) };
    auto next { ZoneContinuation::makeNext (zoneTree (preset, 0, 0), 1000, true) };
    check (PairedZoneEdits::copyNext (left, 0, next).wasOk (), "Continue copies both sides");
    check (zone (preset, 0, 1).getSample () == "left.wav" && zone (preset, 1, 1).getSample () == "right.wav", "Continue retains distinct paired source files");
    check (zone (preset, 1, 1).getSide () == 1 && zone (preset, 1, 1).getSampleStart () == 200 && zone (preset, 1, 1).getSampleEnd () == 300, "Continue mirrors new slice and preserves R selector");
    check (zone (preset, 0, 0).getMinVoltage () == 0.0 && zone (preset, 1, 0).getMinVoltage () == 0.0 && zone (preset, 1, 1).getMinVoltage () == -5.0, "Continue splits and synchronizes voltage bounds");
    for (auto side { 0 }; side < 2; ++side) check (zone (preset, side, 1).getId () == 2, "Copy preserves destination slot identity");
    zone (preset, 0, 1).setMinVoltage (-2.0, false);
    check (PairedZoneEdits::copyNext (left, 0, zoneTree (preset, 0, 0).createCopy ()).wasOk () && zone (preset, 1, 1).getMinVoltage () == -2.0, "Replacing occupied target retains voltage");
    zone (preset, 1, 0).setSample ("", false);
    const auto before { preset.createCopy () };
    check (PairedZoneEdits::copyNext (left, 0, next).failed () && preset.isEquivalentTo (before), "Missing paired source fails before modifying left");
    check (! PairedZoneEdits::explode (left, 0, 3, 1001) && preset.isEquivalentTo (before), "Incomplete paired source cannot be exploded");
    auto incompleteClipboard { ZoneProperties::create (1) };
    PairedZoneEdits::capture (incompleteClipboard, zoneTree (preset, 0, 0), zoneTree (preset, 1, 0), false);
    check (! PairedZoneEdits::pasteContent (left, 4, incompleteClipboard) && preset.isEquivalentTo (before), "Incomplete copied pair cannot replace a target");
    zone (preset, 1, 0).setSample ("right.wav", false);

    zone (preset, 0, 1).setSample ("second-left.wav", false);
    zone (preset, 1, 1).setSample ("second-right.wav", false);
    check (PairedZoneEdits::insert (left, 0), "Insert duplicates and shifts pair");
    check (zone (preset, 0, 2).getSample () == "second-left.wav" && zone (preset, 1, 2).getSample () == "second-right.wav", "Insert moves both later zones without mismatching files");
    zone (preset, 1, 7).setSample ("do-not-drop.wav", false);
    const auto full { preset.createCopy () };
    check (! PairedZoneEdits::insert (left, 0) && preset.isEquivalentTo (full), "Insert refuses occupied final slot on either side");
    zone (preset, 1, 7).setSample ("", false);
    check (PairedZoneEdits::flip (left, 0, 3), "Flip paired zones");
    check (zone (preset, 0, 0).getSample () == "second-left.wav" && zone (preset, 1, 0).getSample () == "second-right.wav", "Flip preserves the pairing");
    check (zone (preset, 0, 0).getId () == 1 && zone (preset, 1, 2).getId () == 3, "Flip retains slot IDs");

    auto clipboard { ZoneProperties::create (1) };
    PairedZoneEdits::capture (clipboard, zoneTree (preset, 0, 0), zoneTree (preset, 1, 0), false);
    check (PairedZoneEdits::pasteContent (left, 4, clipboard), "Paste full pair");
    check (zone (preset, 0, 4).getSample () == "second-left.wav" && zone (preset, 1, 4).getSample () == "second-right.wav", "Clipboard carries both files");
    zone (preset, 0, 0).setPitchOffset (7.0, false);
    zone (preset, 1, 0).setPitchOffset (7.0, false);
    PairedZoneEdits::capture (clipboard, zoneTree (preset, 0, 0), zoneTree (preset, 1, 0), true);
    check (PairedZoneEdits::pasteContent (left, 1, clipboard), "Settings-only paste");
    check (zone (preset, 0, 1).getSample () == "left.wav" && zone (preset, 1, 1).getSample () == "right.wav" && zone (preset, 1, 1).getSide () == 1, "Settings paste retains each target file and side");
    check (zone (preset, 0, 1).getPitchOffset () == 7.0 && zone (preset, 1, 1).getPitchOffset () == 7.0, "Settings paste updates both sides");
    PairedZoneEdits::capture (clipboard, zoneTree (preset, 0, 1), {}, false);
    check (PairedZoneEdits::pasteContent (left, 5, clipboard) && zone (preset, 1, 5).getSample () == "left.wav" && zone (preset, 1, 5).getSide () == 0, "Unpaired clipboard intentionally duplicates chosen side in existing pair");

    check (PairedZoneEdits::explode (left, 1, 3, 1001), "Explode paired source");
    check (zone (preset, 0, 3).getSampleEnd () == 1001 && zone (preset, 1, 3).getSampleEnd () == 1001, "Explode covers final remainder on both sides");
    check (zone (preset, 1, 2).getSample () == "right.wav" && zone (preset, 1, 2).getSide () == 1, "Explode retains distinct right source");
    check (zone (preset, 0, 2).getSampleStart () == zone (preset, 1, 2).getSampleStart (), "Explode slice boundaries match");
    const auto valid { preset.createCopy () };
    check (! PairedZoneEdits::explode (left, 6, 3, 1001) && ! PairedZoneEdits::explode (left, 0, 8, 10) && preset.isEquivalentTo (valid), "Invalid explode is a no-op");
    check (! PairedZoneEdits::clearAll (preset.getChild (1), ZoneProperties::create (1)), "Cannot independently clear read-only stereo-right");
    const auto defaults { ZoneProperties::create (1) };
    check (PairedZoneEdits::clearAll (left, defaults), "Clear paired channel");
    for (auto side { 0 }; side < 2; ++side)
        for (auto index { 0 }; index < 8; ++index)
            check (zone (preset, side, index).getSample ().isEmpty () && zone (preset, side, index).getId () == index + 1, "Clear all eight slots including hidden orphan R zones");
    checkChannelPurge ();
    checkCvChannelPurge ();
    std::cout << "Paired zone copy/continue/insert/paste/flip/explode/clear passed\n";
}
