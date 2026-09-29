#include "Assimil8or/Audio/ChannelCvSafety.h"
#include "Assimil8or/PresetFileOperations.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "Assimil8or/PresetManagerProperties.h"
#include "GUI/Assimil8or/Editor/EditManager.h"
#include "GUI/Assimil8or/Editor/SampleManager/SampleManagerProperties.h"
#include "SystemServices.h"
#include "oolib/Properties/PersistentRootProperties.h"
#include "oolib/Properties/RuntimeRootProperties.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void check (bool ok, const char* message) { if (! ok) throw std::runtime_error (message); }
    void wave (const juce::File& file, bool cv, int channels = 1)
    {
        const auto tags { CvSampleSafety::exportMetadata (cv) };
        std::unordered_map<juce::String, juce::String> metadata;
        for (int i { 0 }; i < tags.size (); ++i) metadata.emplace (tags.getAllKeys ()[i], tags.getAllValues ()[i]);
        std::unique_ptr<juce::OutputStream> output { file.createOutputStream () };
        juce::WavAudioFormat format;
        auto writer { format.createWriterFor (output, juce::AudioFormatWriterOptions {}.withSampleRate (48000).withNumChannels (channels).withBitsPerSample (24).withMetadataValues (metadata)) };
        check (writer != nullptr, "Create typed fixture");
        juce::AudioBuffer<float> data (channels, 128);
        for (int side { 0 }; side < channels; ++side)
            for (int frame { 0 }; frame < 128; ++frame) data.setSample (side, frame, 0.2f * std::sin (frame * 0.2f));
        check (writer->writeFromAudioSampleBuffer (data, 0, 128), "Write typed fixture");
    }
}

void testChannelCvSafety ()
{
    const auto rootFolder { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-cv-channel", "", false) };
    check (rootFolder.createDirectory ().wasOk (), "Create isolated safety fixtures");
    struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { rootFolder };
    const auto folder { rootFolder.getChildFile ("preset") };
    check (folder.createDirectory ().wasOk (), "Create preset destination");
    const auto cvFile { rootFolder.getChildFile ("modulation.wav") }, audioFile { rootFolder.getChildFile ("oscillator.wav") };
    const auto cvStereo { rootFolder.getChildFile ("stereo-cv.wav") };
    wave (cvFile, true); wave (audioFile, false); wave (cvStereo, true, 2);

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
    services.setAudioManager (&audio);
    presets.addPreset ("edit", ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType).createCopy ());
    const auto tree { presets.getPreset ("edit") };
    PresetProperties::copyTreeProperties (ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType), tree);
    EditManager edits;
    services.setEditManager (&edits);
    edits.init (root, tree);
    auto channel = [&] (int i) { return ChannelProperties (tree.getChild (i), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no); };
    auto zone = [&] (int c, int z) { return ZoneProperties (tree.getChild (c).getChild (z), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no); };

    check (edits.assignSamples (0, 0, { cvFile.getFullPathName () }), "Assign generated CV to empty channel");
    check (channel (0).getMixLevel () == -90.0 && std::get<0> (channel (0).getMixMod ()) == "Off", "CV assignment forces Mix and Mix modulation Off");
    auto before { tree.createCopy () };
    check (! edits.assignSamples (0, 1, { audioFile.getFullPathName () }) && tree.isEquivalentTo (before), "Audio cannot append to CV channel");
    check (! folder.getChildFile (audioFile.getFileName ()).exists (), "Rejected import rolls back newly copied audio");
    check (! edits.assignSamples (0, 0, { audioFile.getFullPathName () }), "Replacing the only CV zone with audio requires explicit purge first");
    check (edits.assignSamples (0, 1, { cvFile.getFullPathName () }), "Multiple CV zones may share channel");
    check (edits.assignSamples (1, 0, { audioFile.getFullPathName () }), "Audio on separate channel remains valid");
    before = tree.createCopy ();
    check (! edits.assignSamples (1, 1, { cvFile.getFullPathName () }) && tree.isEquivalentTo (before), "CV cannot append to audio channel");
    check (! edits.assignSamples (2, 0, { audioFile.getFullPathName (), cvFile.getFullPathName () }), "Mixed-purpose batch cannot populate empty channel");
    check (zone (2, 0).getSample ().isEmpty () && zone (2, 1).getSample ().isEmpty (), "Mixed batch changes no zone");

    // SampleManager publishes this metadata after reading the file. The model
    // guard must also reject programmatic Mix changes (clone/revert/paste).
    SampleProperties loaded (samples.getSamplePropertiesVT (0, 0), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
    loaded.setIsCv (true, false);
    loaded.setName (zone (0, 0).getSample (), false);
    check (edits.channelContainsCv (0), "Cached classification belongs to the assigned sample");
    channel (0).setMixLevel (0.0, false);
    channel (0).setMixMod ("1A", 1.0, false);
    check (channel (0).getMixLevel () == -90.0 && std::get<0> (channel (0).getMixMod ()) == "Off", "Model guard defeats cloned or manually reenabled mix");
    loaded.setName ("old-sample.wav", false);
    check (! edits.channelContainsCv (0), "Stale cached purpose does not identify a different assignment");
    loaded.setName (zone (0, 0).getSample (), false);

    check (edits.assignSamples (2, 0, { cvStereo.getFullPathName () }), "Stereo CV assignment creates safe pair");
    check (channel (2).getMixLevel () == -90.0 && channel (3).getMixLevel () == -90.0, "Both CV stereo channels have Mix Off");
    check (! edits.assignSamples (3, 0, { audioFile.getFullPathName () }), "Right-side drop cannot bypass CV isolation");
    zone (3, 1).setSample (audioFile.getFileName (), false);
    before = tree.createCopy ();
    check (! edits.assignSamples (2, 1, { cvFile.getFullPathName () }) && tree.isEquivalentTo (before), "Invalid existing right-channel content blocks CV pair assignment");
    zone (3, 1).setSample ("", false);

    auto proposed { tree.createCopy () };
    ZoneProperties proposedZone (proposed.getChild (1).getChild (1), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
    proposedZone.setSample (cvFile.getFileName (), false);
    check (edits.validateContentChange (proposed).failed (), "Cross-channel CV zone paste into audio is rejected");
    proposedZone.setSample (audioFile.getFileName (), false);
    check (edits.validateContentChange (proposed).wasOk (), "Audio-only paste remains allowed");
    zone (4, 0).setSample ("missing.wav", false);
    check (! edits.assignSamples (4, 0, { cvFile.getFullPathName () }), "Unverifiable existing purpose fails closed");
    zone (4, 0).setSample ("", false);

    auto baseline { tree.createCopy () };
    check (PresetFileOperations::save (folder.getChildFile ("prst001.yml"), tree, baseline).wasOk (), "Separate CV and audio channels save normally");
    zone (0, 2).setSample (audioFile.getFileName (), false);
    check (PresetFileOperations::save (folder.getChildFile ("invalid.yml"), tree, baseline).failed (), "Save rejects a mixed legacy or externally changed channel");
    check (! folder.getChildFile ("invalid.yml").exists (), "Unsafe preset is not written");
    zone (0, 2).setSample ("", false);
    loaded.setIsCv (false, false); // Save independently verifies files, not just cache.
    channel (0).setMixLevel (0.0, false);
    check (PresetFileOperations::save (folder.getChildFile ("invalid.yml"), tree, baseline).failed (), "Save independently blocks CV with Mix enabled");
    std::cout << "PASS: CV/audio channel isolation, import rollback, stereo and paste safety, Mix lock and guarded Save\n";
}
