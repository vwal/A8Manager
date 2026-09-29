#include "GUI/PresetEditSession.h"
#include "Assimil8or/PresetFileOperations.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "Assimil8or/Audio/WaveformDesignAssignment.h"
#include "Assimil8or/Audio/WaveformDesignExport.h"
#include "GUI/Assimil8or/Editor/EditManager.h"
#include "GUI/Assimil8or/Editor/SampleManager/SampleManager.h"
#include "SystemServices.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void check (bool ok, const char* message) { if (! ok) throw std::runtime_error (message); }

    void checkStereoNeighbourAssignment (const juce::File& folder, juce::ValueTree defaults)
    {
        check (folder.createDirectory ().wasOk (), "Create stereo/designer session fixture folder");
        const auto stereoFile { folder.getChildFile ("stereo.wav") };
        {
            std::unique_ptr<juce::OutputStream> output { stereoFile.createOutputStream () };
            juce::WavAudioFormat format;
            auto writer { format.createWriterFor (output, juce::AudioFormatWriterOptions {}.withSampleRate (48000).withNumChannels (2).withBitsPerSample (24)) };
            juce::AudioBuffer<float> data (2, 128);
            for (int frame { 0 }; frame < 128; ++frame)
            {
                data.setSample (0, frame, 0.25f);
                data.setSample (1, frame, -0.5f);
            }
            check (writer && writer->writeFromAudioSampleBuffer (data, 0, 128) && writer->flush (), "Write occupied stereo-pair audio");
        }
        juce::ValueTree root { "Root" };
        PersistentRootProperties persistent (root, PersistentRootProperties::WrapperType::owner, PersistentRootProperties::EnableCallbacks::no);
        RuntimeRootProperties runtime (root, RuntimeRootProperties::WrapperType::owner, RuntimeRootProperties::EnableCallbacks::no);
        AppProperties app;
        app.wrap (persistent.getValueTree (), AppProperties::WrapperType::owner, AppProperties::EnableCallbacks::no);
        app.setMostRecentFolder (folder.getFullPathName ());
        const auto presetFile { folder.getChildFile ("prst073.yml") };
        app.addRecentlyUsedFile (presetFile.getFullPathName ());
        DirectoryDataProperties directory (runtime.getValueTree (), DirectoryDataProperties::WrapperType::owner, DirectoryDataProperties::EnableCallbacks::no);
        PresetManagerProperties presets (runtime.getValueTree (), PresetManagerProperties::WrapperType::owner, PresetManagerProperties::EnableCallbacks::no);
        presets.addPreset ("edit", defaults.createCopy ());
        presets.addPreset ("unedited", defaults.createCopy ());
        const auto edit { presets.getPreset ("edit") }, baseline { presets.getPreset ("unedited") };
        PresetProperties current (edit, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        current.setId (73, false); current.setName ("Stereo+Synth", false);
        AudioManager audio;
        SystemServices services (runtime.getValueTree (), SystemServices::WrapperType::owner, SystemServices::EnableCallbacks::no);
        services.setAudioManager (&audio);
        SampleManager samples;
        samples.init (root);
        EditManager edits;
        services.setEditManager (&edits);
        edits.init (root, edit);
        ChannelProperties right (current.getChannelVT (1), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        right.setChannelMode (ChannelProperties::stereoRight, false);
        for (int side { 0 }; side < 2; ++side)
        {
            ZoneProperties zone (current.getChannelVT (side).getChild (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            zone.setSample (stereoFile.getFileName (), false);
            zone.setSide (side, false);
            zone.setSampleStart (8, false); zone.setSampleEnd (120, false);
            zone.setLoopStart (16, false); zone.setLoopLength (64.0, false);
            zone.setMinVoltage (-5.0, false);
        }
        check (PresetFileOperations::save (presetFile, edit, baseline).wasOk (), "Save initial preset containing occupied stereo CH 1/2");
        const auto leftBefore { edit.getChild (0).createCopy () }, rightBefore { edit.getChild (1).createCopy () };
        const auto cleanBefore { baseline.createCopy () };
        const auto liveChannelThree { edit.getChild (2) }, liveZone { liveChannelThree.getChild (0) };
        ZoneProperties observedZone (liveZone, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::yes);
        juce::String notifiedSample;
        observedZone.onSampleChange = [&] (juce::String name) { notifiedSample = name; };
        PresetEditSession session;
        session.init (root);
        const auto source { session.snapshot () };
        check (source && ! session.isDirty () && observedZone.getSample ().isEmpty (), "Stereo preset binds clean session with empty CH 3");
        auto settings { WaveformDesign::startingPoint (WaveformDesign::Mode::oscillator, WaveformDesign::Shape::saw) };
        settings.cycleFrames = 128;
        WaveformDesign::AssignmentResult generated;
        check (WaveformDesign::prepareAssignment (settings, folder, "Third channel", source->preset, 2, 0, generated).wasOk (),
               "Generate design for suggested CH 3 after stereo pair using real backend");
        check (observedZone.getSample ().isEmpty () && session.getRevision () == source->revision,
               "Writing generated files does not change/invalidate the live preset before apply");
        check (session.apply (*source, generated.editedPreset).wasOk (), "Generated CH 3 assignment applies to the real shared session");
        SampleProperties loaded (samples.getSampleProperties (2, 0), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
        check (observedZone.getSample () == generated.waves[0].getFileName () && notifiedSample == observedZone.getSample () &&
               edit.getChild (2) == liveChannelThree && liveChannelThree.getChild (0) == liveZone,
               "CH 3 is populated and existing editor observers receive the assignment without rebinding");
        check (loaded.getName () == observedZone.getSample () && loaded.getStatus () == SampleStatus::exists &&
               loaded.getAudioBufferPtr () != nullptr && loaded.getNumChannels () == 1 && loaded.getLengthInSamples () == settings.cycleFrames,
               "Actual SampleManager loads assigned CH 3 audio immediately");
        check (edit.getChild (0).isEquivalentTo (leftBefore) && edit.getChild (1).isEquivalentTo (rightBefore) &&
               baseline.isEquivalentTo (cleanBefore) && session.isDirty (),
               "Stereo pair stays unchanged and generated assignment is marked unsaved");
        check (PresetFileOperations::save (presetFile, edit, baseline).wasOk () && ! session.isDirty (), "Shared Save persists generated CH 3 to selected preset");
        juce::ValueTree saved;
        check (PresetFileOperations::read (presetFile, saved).wasOk (), "Read back stereo-plus-generated preset");
        ZoneProperties savedThird (saved.getChild (2).getChild (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        check (savedThird.getSample () == observedZone.getSample () && saved.getProperty (PresetProperties::IdPropertyId) == juce::var (73),
               "Selected-slot YAML references the generated CH 3 WAV after save");

        // Separate package export has a different contract: it creates its own
        // preset folder and deliberately does not apply anything to this session.
        const auto beforePackage { session.snapshot () };
        WaveformDesign::ExportResult package;
        check (WaveformDesign::exportDesign (settings, folder, "Separate package", package, 73).wasOk (), "Separate package export succeeds beside shared preset");
        check (package.folder != folder && package.preset.getParentDirectory () == package.folder &&
               session.getRevision () == beforePackage->revision && edit.isEquivalentTo (beforePackage->preset) && ! session.isDirty (),
               "Exporting a separate package cannot masquerade as assigning to the current channel");
    }
}

void testSharedPresetSession ()
{
    const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-shared-preset", "", false) };
    check (folder.createDirectory ().wasOk (), "Create isolated preset folder");
    struct Cleanup { juce::File file; ~Cleanup () { file.deleteRecursively (); } } cleanup { folder };
    juce::ValueTree root { "Root" };
    PersistentRootProperties persistent (root, PersistentRootProperties::WrapperType::owner, PersistentRootProperties::EnableCallbacks::no);
    RuntimeRootProperties runtime (root, RuntimeRootProperties::WrapperType::owner, RuntimeRootProperties::EnableCallbacks::no);
    AppProperties app;
    app.wrap (persistent.getValueTree (), AppProperties::WrapperType::owner, AppProperties::EnableCallbacks::no);
    PresetManagerProperties presets (runtime.getValueTree (), PresetManagerProperties::WrapperType::owner, PresetManagerProperties::EnableCallbacks::no);
    const auto defaults { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType) };
    presets.addPreset ("edit", defaults.createCopy ());
    presets.addPreset ("unedited", defaults.createCopy ());
    const auto edit { presets.getPreset ("edit") }, baseline { presets.getPreset ("unedited") };
    PresetProperties::copyTreeProperties (defaults, edit);
    PresetProperties current (edit, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
    current.setId (47, false);
    PresetProperties::copyTreeProperties (edit, baseline);
    app.setMostRecentFolder (folder.getFullPathName ());
    app.addRecentlyUsedFile (folder.getChildFile ("prst047.yml").getFullPathName ());
    PresetEditSession session;
    session.init (root);
    auto source { session.snapshot () };
    check (source && ! session.isDirty () && source->preset != edit, "Snapshot is detached and binds the shared selected slot");
    current.setName ("Edited", false);
    check (session.isDirty () && source->preset.getProperty (PresetProperties::NamePropertyId) != "Edited", "Unsaved edits and snapshots stay separate");
    check (session.apply (*source, source->preset).failed (), "Reject stale generation after name edit");
    source = session.snapshot ();
    const auto editedChannel { current.getChannelVT (0) };
    const auto editedZone { editedChannel.getChild (0) };
    const auto cleanBefore { baseline.createCopy () };
    auto generated { source->preset.createCopy () };
    ZoneProperties zone (generated.getChild (0).getChild (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
    zone.setSample ("generated.wav", false);
    check (session.apply (*source, generated).wasOk (), "Apply to selected slot without saving");
    check (session.isDirty () && baseline.isEquivalentTo (cleanBefore) && ! folder.getChildFile ("prst047.yml").exists (), "Assignment preserves baseline and leaves preset unsaved");
    check (current.getChannelVT (0) == editedChannel && editedChannel.getChild (0) == editedZone, "Applying retains editor/listener tree identities");
    check (current.getName () == "Edited" && current.getId () == 47, "Assignment keeps current name and slot");
    check (session.apply (*source, generated).failed (), "Assignment callback is single-use");
    check (PresetFileOperations::save (folder.getChildFile ("prst047.yml"), edit, baseline).wasOk (), "Shared Save writes selected slot");
    check (! session.isDirty () && ! folder.getChildFile ("prst001.yml").exists (), "Save never falls back to preset001");
    juce::ValueTree readBack;
    check (PresetFileOperations::read (folder.getChildFile ("prst047.yml"), readBack).wasOk (), "Read back shared saved preset");
    check (readBack.getProperty (PresetProperties::IdPropertyId) == juce::var (47), "Saved preset header uses chosen number");

    source = session.snapshot ();
    current.setName ("Temporary", false); current.setName ("Edited", false);
    check (session.apply (*source, source->preset).failed (), "Changed-and-reverted document still invalidates generation");
    source = session.snapshot ();
    auto wrongSlot { source->preset.createCopy () };
    wrongSlot.setProperty (PresetProperties::IdPropertyId, 1, nullptr);
    check (session.apply (*source, wrongSlot).failed (), "Cannot apply a generated tree for another slot");
    const auto other { folder.getChildFile ("other") };
    check (other.createDirectory ().wasOk (), "Create second preset folder");
    app.setMostRecentFolder (other.getFullPathName ());
    check (! session.snapshot (), "Folder change with old MRU binding cannot assign");
    app.addRecentlyUsedFile (other.getChildFile ("prst047.yml").getFullPathName ());
    check (session.apply (*source, source->preset).failed (), "Same preset number in new folder cannot receive old generation");
    app.setMostRecentFolder (folder.getFullPathName ());
    app.addRecentlyUsedFile (folder.getChildFile ("prst047.yml").getFullPathName ());
    check (session.apply (*source, source->preset).failed (), "Navigating away and back still invalidates pending generation");
    current.setId (48, false);
    check (! session.snapshot (), "A slot ID alone does not change the Save destination");
    app.addRecentlyUsedFile (folder.getChildFile ("prst048.yml").getFullPathName ());
    check (session.snapshot ()->preset.getProperty (PresetProperties::IdPropertyId) == juce::var (48), "Slot selection follows shared MRU binding");
    checkStereoNeighbourAssignment (folder.getChildFile ("stereo-session"), defaults);
    std::cout << "PASS: shared preset state, numbered save, unsaved changes, identity-preserving assignment and stale-generation protection\n";
}
