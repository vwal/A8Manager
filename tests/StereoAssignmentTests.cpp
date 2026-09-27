#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "GUI/Assimil8or/Editor/EditManager.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void check (bool passed, const char* message) { if (! passed) throw std::runtime_error (message); }

    void makeAudio (const juce::File& file, int channels, int length)
    {
        std::unique_ptr<juce::OutputStream> output { file.createOutputStream () };
        check (output != nullptr, "Create generated stereo-assignment fixture");
        juce::WavAudioFormat format;
        auto writer { format.createWriterFor (output, juce::AudioFormatWriterOptions {}.withSampleRate (48000.0).withNumChannels (channels).withBitsPerSample (24)) };
        check (writer != nullptr, "Create fixture writer");
        juce::AudioBuffer<float> buffer (channels, length);
        for (int side { 0 }; side < channels; ++side)
            for (int sample { 0 }; sample < length; ++sample)
                buffer.setSample (side, sample, side == 0 ? 0.25f : -0.5f);
        check (writer->writeFromAudioSampleBuffer (buffer, 0, length), "Write fixture samples");
    }

    ChannelProperties channelAt (juce::ValueTree preset, int channel)
    {
        return { preset.getChild (channel), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no };
    }

    ZoneProperties zoneAt (juce::ValueTree preset, int channel, int zone)
    {
        return { preset.getChild (channel).getChild (zone), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no };
    }

    void checkPair (juce::ValueTree preset, int leftIndex, int zoneIndex, const juce::File& sample, int rightSide)
    {
        auto left { zoneAt (preset, leftIndex, zoneIndex) };
        auto right { zoneAt (preset, leftIndex + 1, zoneIndex) };
        check (left.getSample () == sample.getFileName () && right.getSample () == left.getSample (), "Both occupied pair sides receive the replacement file");
        check (left.getSide () == 0 && right.getSide () == rightSide, "Replacement selects the correct audio sides");
        check (left.getSampleStart () == right.getSampleStart () && left.getSampleEnd () == right.getSampleEnd ()
               && left.getLoopStart () == right.getLoopStart () && left.getLoopLength () == right.getLoopLength (), "Pair range settings remain aligned");
        check (left.getMinVoltage () == right.getMinVoltage () && left.getPitchOffset () == right.getPitchOffset ()
               && left.getLevelOffset () == right.getLevelOffset (), "Pair zone settings follow the controlling left zone");
        check (channelAt (preset, leftIndex + 1).getChannelMode () == ChannelProperties::ChannelMode::stereoRight, "Replacement preserves Stereo Right mode");
    }
}

struct StereoAssignmentTestAccess
{
    static juce::ValueTree bind (EditManager& editor, AudioManager& audio, const juce::File& folder)
    {
        auto tree { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType).createCopy () };
        editor.audioManager = &audio;
        editor.appProperties.wrap (juce::ValueTree ("Root"), AppProperties::WrapperType::owner, AppProperties::EnableCallbacks::no);
        editor.appProperties.setMostRecentFolder (folder.getFullPathName ());
        editor.presetProperties.wrap (tree, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        for (int channel { 0 }; channel < 8; ++channel)
        {
            editor.channelPropertiesList [channel].wrap (tree.getChild (channel), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            for (int zone { 0 }; zone < 8; ++zone)
                editor.zoneAndSamplePropertiesList [channel][zone].zoneProperties.wrap (tree.getChild (channel).getChild (zone), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        }
        return tree;
    }
};

void testStereoAssignment ()
{
    const auto root { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-stereo-assignment", "", false) };
    check (root.createDirectory ().wasOk (), "Create isolated fixture directory");
    struct Cleanup { juce::File directory; ~Cleanup () { directory.deleteRecursively (); } } cleanup { root };
    const auto folder { root.getChildFile ("preset") };
    check (folder.createDirectory ().wasOk (), "Create isolated preset directory");
    const auto first { folder.getChildFile ("first.wav") }, replacement { folder.getChildFile ("replacement.wav") };
    const auto mono { folder.getChildFile ("mono.wav") }, external { root.getChildFile ("external.wav") };
    makeAudio (first, 2, 128);
    makeAudio (replacement, 2, 48);
    makeAudio (mono, 1, 96);
    makeAudio (external, 2, 64);
    AudioManager audio;
    EditManager editor;
    auto tree { StereoAssignmentTestAccess::bind (editor, audio, folder) };

    check (editor.assignSamples (0, 0, { first.getFullPathName () }), "Create an initial stereo pair");
    checkPair (tree, 0, 0, first, 1);
    auto left { zoneAt (tree, 0, 0) };
    auto right { zoneAt (tree, 1, 0) };
    left.setSampleStart (8, false);
    left.setSampleEnd (120, false);
    left.setLoopStart (16, false);
    left.setLoopLength (80.0, false);
    left.setPitchOffset (3.5, false);
    left.setLevelOffset (-4.0, false);
    right.setSampleStart (1, false);
    right.setPitchOffset (-7.0, false);
    const auto neighbor { tree.getChild (2).createCopy () };
    check (editor.assignSamples (1, 0, { replacement.getFullPathName () }), "Right-side drop replaces an occupied pair");
    checkPair (tree, 0, 0, replacement, 1);
    check (left.getSampleStart () == 8 && left.getSampleEnd () == 48 && left.getLoopStart () == 16
           && left.getLoopLength () == 32.0 && left.getPitchOffset () == 3.5 && left.getLevelOffset () == -4.0,
           "Right-side replacement preserves left settings and clamps ranges to a shorter file");
    check (tree.getChild (2).isEquivalentTo (neighbor), "Right-side replacement never overwrites the following channel");

    check (editor.assignSamples (1, 0, { mono.getFullPathName () }), "Right-side mono replacement succeeds");
    checkPair (tree, 0, 0, mono, 0);
    check (editor.assignSamples (0, 0, { first.getFullPathName () }), "Left-side occupied-pair replacement remains supported");
    checkPair (tree, 0, 0, first, 1);
    check (editor.assignSamples (1, 1, { replacement.getFullPathName (), mono.getFullPathName () }), "Right-side multi-file drop appends matching zones to both channels");
    checkPair (tree, 0, 1, replacement, 1);
    checkPair (tree, 0, 2, mono, 0);
    check (left.getMinVoltage () > zoneAt (tree, 0, 1).getMinVoltage () && zoneAt (tree, 0, 1).getMinVoltage () > zoneAt (tree, 0, 2).getMinVoltage ()
           && zoneAt (tree, 0, 2).getMinVoltage () == -5.0, "Right-side append redistributes the left CV boundaries in order");

    auto unchanged { tree.createCopy () };
    check (! editor.assignSamples (1, 7, { external.getFullPathName (), mono.getFullPathName () }), "Oversized right-side batch is rejected");
    check (! editor.assignSamples (1, 4, { external.getFullPathName () }), "Right-side drop cannot introduce a left-zone gap");
    check (! editor.assignSamples (1, 0, { root.getChildFile ("missing.wav").getFullPathName () }), "Failed import through the right side is rejected");
    check (tree.isEquivalentTo (unchanged) && ! folder.getChildFile (external.getFileName ()).exists (), "Failed right-side assignments leave preset and files unchanged");

    // Occupancy validation is based on the controlling channel, even if an old
    // preset has fewer zones on its right side.
    zoneAt (tree, 1, 2).setSample ("", false);
    check (editor.assignSamples (1, 3, { first.getFullPathName () }), "Right-side append uses left occupancy rather than a stale right count");
    checkPair (tree, 0, 3, first, 1);

    EditManager lastEditor;
    auto lastTree { StereoAssignmentTestAccess::bind (lastEditor, audio, folder) };
    check (lastEditor.assignSamples (6, 0, { first.getFullPathName () }), "Channels seven and eight form a pair");
    check (lastEditor.assignSamples (7, 0, { replacement.getFullPathName () }), "Channel-eight right-side replacement resolves to channel seven");
    checkPair (lastTree, 6, 0, replacement, 1);

    // Existing non-right controlling modes must not be reset by replacement.
    for (const auto mode : { ChannelProperties::ChannelMode::link, ChannelProperties::ChannelMode::cycle })
    {
        channelAt (lastTree, 6).setChannelMode (mode, false);
        check (lastEditor.assignSamples (7, 0, { mono.getFullPathName () }), "Non-right controlling mode accepts pair replacement");
        checkPair (lastTree, 6, 0, mono, 0);
        check (channelAt (lastTree, 6).getChannelMode () == mode, "Replacement preserves the controlling channel mode");
    }

    EditManager invalidEditor;
    auto invalidTree { StereoAssignmentTestAccess::bind (invalidEditor, audio, folder) };
    channelAt (invalidTree, 0).setChannelMode (ChannelProperties::ChannelMode::stereoRight, false);
    channelAt (invalidTree, 1).setChannelMode (ChannelProperties::ChannelMode::stereoRight, false);
    unchanged = invalidTree.createCopy ();
    check (! invalidEditor.assignSamples (0, 0, { external.getFullPathName () }), "Channel one cannot resolve a Stereo Right partner");
    check (! invalidEditor.assignSamples (1, 0, { external.getFullPathName () }), "Consecutive Stereo Right modes do not form a valid pair");
    check (invalidEditor.getLastAssignmentError ().contains ("left partner") && invalidTree.isEquivalentTo (unchanged)
           && ! folder.getChildFile (external.getFileName ()).exists (), "Malformed pair rejection has an explanation and no side effects");

    EditManager independentEditor;
    auto independentTree { StereoAssignmentTestAccess::bind (independentEditor, audio, folder) };
    check (independentEditor.assignSamples (1, 0, { mono.getFullPathName () }), "Populate an independent neighboring channel");
    const auto independent { independentTree.getChild (1).createCopy () };
    check (independentEditor.assignSamples (0, 0, { first.getFullPathName () }), "Stereo assignment beside an occupied independent channel still succeeds");
    check (independentTree.getChild (1).isEquivalentTo (independent), "Stereo assignment does not commandeer an occupied independent channel");
    check (independentEditor.assignSamples (7, 0, { first.getFullPathName () }), "Independent last-channel stereo assignment remains safe");
    check (zoneAt (independentTree, 7, 0).getSample () == first.getFileName () && zoneAt (independentTree, 7, 0).getSide () == 0,
           "Without an available partner the independent channel retains the selected left side");

    std::cout << "PASS: stereo replacement from either side, mono and batch pairing, range/CV preservation, neighbor safety and invalid-pair rejection\n";
}
