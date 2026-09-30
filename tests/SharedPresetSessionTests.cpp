#include "GUI/PresetEditSession.h"
#include "GUI/DesignerPresetSaveStatus.h"
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
    check (! session.matches ({}), "Uninitialized session cannot approve a background-copy completion");
    session.init (root);
    auto source { session.snapshot () };
    check (source && ! session.isDirty () && source->preset != edit, "Snapshot is detached and binds the shared selected slot");
    DesignerPresetSaveStatus copyStatus;
    const auto copiedFolder { folder.getChildFile ("P47 - Copy") };
    check (copiedFolder.createDirectory ().wasOk (), "Create owned copy-status destination");
    check (! copyStatus.copyWasRefreshed (source)
           && DesignerPresetSaveStatus::text (false, false, true, true, false) == "Saved (remember to refresh copy)",
           "A clean working preset alone must not claim its portable copy was refreshed");
    copyStatus.recordSuccess (source, copiedFolder);
    check (copyStatus.copyWasRefreshed (source)
           && DesignerPresetSaveStatus::text (false, false, true, true, true) == "Saved / refreshed copy",
           "Confirmed copy and original-save completion permits a truthful refreshed-copy status");
    check (DesignerPresetSaveStatus::text (false, false, true, false, false) == "Saved / unchanged"
           && DesignerPresetSaveStatus::text (false, true, true, true, true) == "SAVE IS PENDING"
           && DesignerPresetSaveStatus::text (true, false, true, true, true) == "Saving A8 folder..."
           && DesignerPresetSaveStatus::text (false, false, false, false, true) == "Select a preset slot",
           "Named folders, dirty edits, active saves and unbound slots do not misreport copy completion");
    const juce::Font saveStateFont { juce::FontOptions (14.0f, juce::Font::bold) };
    for (const bool refreshed : { false, true })
        check (juce::GlyphArrangement::getStringWidth (saveStateFont, DesignerPresetSaveStatus::text (false, false, true, true, refreshed)) <= 284.0f,
               "Both saved-copy messages fit a compact 300-pixel header area including its 16-pixel border inset");
    const auto originalRevision { session.getRevision () };
    check (session.matches (*source) && session.getRevision () == originalRevision && ! session.isDirty (),
           "An unchanged snapshot approves completion without editing the preset, baseline or revision");
    auto wrongRevision { *source };
    ++wrongRevision.revision;
    check (! session.matches (wrongRevision), "Copy completion requires the captured document revision");
    check (! copyStatus.copyWasRefreshed (wrongRevision), "Copy freshness belongs to the exact saved session revision");
    auto wrongFolder { *source };
    wrongFolder.folder = folder.getChildFile ("different-destination");
    check (! session.matches (wrongFolder), "Copy completion cannot save an equivalent preset in another working folder");
    check (! copyStatus.copyWasRefreshed (wrongFolder), "A refreshed copy in another working folder cannot satisfy the current one");
    auto wrongContent { *source };
    wrongContent.preset = source->preset.createCopy ();
    wrongContent.preset.setProperty (PresetProperties::NamePropertyId, "Different", nullptr);
    check (! session.matches (wrongContent) && session.matches (*source),
           "Copy completion checks captured contents as well as revision without mutating the original snapshot");
    check (! copyStatus.copyWasRefreshed (wrongContent), "Changed preset contents invalidate the refreshed-copy indication");
    copyStatus.reset ();
    check (! copyStatus.copyWasRefreshed (source), "Starting another copy drops the old success claim until the operation succeeds");
    copyStatus.recordSuccess (source, folder.getChildFile ("missing-copy"));
    check (! copyStatus.copyWasRefreshed (source), "Missing copy destinations cannot claim a completed refresh");
    copyStatus.recordSuccess (source, copiedFolder);
    current.setName ("Edited", false);
    check (session.isDirty () && source->preset.getProperty (PresetProperties::NamePropertyId) != "Edited", "Unsaved edits and snapshots stay separate");
    check (! copyStatus.copyWasRefreshed (session.snapshot ()), "Editing the working preset invalidates its previous copy confirmation");
    check (! session.matches (*source), "Edits made while copying cannot be saved by the stale completion");
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
    source = session.snapshot ();
    check (session.matches (*source) && session.isDirty (), "A dirty but unchanged document remains eligible for copy-then-save completion");
    check (PresetFileOperations::save (folder.getChildFile ("prst047.yml"), edit, baseline).wasOk (), "Shared Save writes selected slot");
    check (! session.isDirty () && ! folder.getChildFile ("prst001.yml").exists (), "Save never falls back to preset001");
    check (! copyStatus.copyWasRefreshed (session.snapshot ()), "Saving only the working preset cannot silently mark its separate copy refreshed");
    check (! session.matches (*source) && session.matches (*session.snapshot ()),
           "A completed Save changes the baseline revision so an earlier copy cannot save again");
    juce::ValueTree readBack;
    check (PresetFileOperations::read (folder.getChildFile ("prst047.yml"), readBack).wasOk (), "Read back shared saved preset");
    check (readBack.getProperty (PresetProperties::IdPropertyId) == juce::var (47), "Saved preset header uses chosen number");

    source = session.snapshot ();
    current.setName ("Temporary", false); current.setName ("Edited", false);
    check (! session.matches (*source), "Changing and reverting during a background copy still rejects its pending save");
    check (session.apply (*source, source->preset).failed (), "Changed-and-reverted document still invalidates generation");
    source = session.snapshot ();
    auto wrongSlot { source->preset.createCopy () };
    wrongSlot.setProperty (PresetProperties::IdPropertyId, 1, nullptr);
    check (session.apply (*source, wrongSlot).failed (), "Cannot apply a generated tree for another slot");
    const auto other { folder.getChildFile ("other") };
    check (other.createDirectory ().wasOk (), "Create second preset folder");
    app.setMostRecentFolder (other.getFullPathName ());
    check (! session.snapshot (), "Folder change with old MRU binding cannot assign");
    check (! session.matches (*source), "Unbound folder navigation also rejects a pending copy-save completion");
    app.addRecentlyUsedFile (other.getChildFile ("prst047.yml").getFullPathName ());
    check (! session.matches (*source), "The same preset number in a new working folder cannot receive the previous copy's save");
    check (session.apply (*source, source->preset).failed (), "Same preset number in new folder cannot receive old generation");
    app.setMostRecentFolder (folder.getFullPathName ());
    app.addRecentlyUsedFile (folder.getChildFile ("prst047.yml").getFullPathName ());
    check (! session.matches (*source), "Navigating away and back does not revive a copy's stale original-save approval");
    check (session.apply (*source, source->preset).failed (), "Navigating away and back still invalidates pending generation");
    current.setId (48, false);
    check (! session.snapshot (), "A slot ID alone does not change the Save destination");
    check (! session.matches (*source), "An incompletely rebound preset slot cannot be saved by a background completion");
    app.addRecentlyUsedFile (folder.getChildFile ("prst048.yml").getFullPathName ());
    check (session.snapshot ()->preset.getProperty (PresetProperties::IdPropertyId) == juce::var (48), "Slot selection follows shared MRU binding");
    check (! session.matches (*source) && session.matches (*session.snapshot ()), "Only the newly selected slot's snapshot may approve its save");
    checkStereoNeighbourAssignment (folder.getChildFile ("stereo-session"), defaults);
    std::cout << "PASS: shared preset state, numbered save, unsaved changes, identity-preserving assignment and stale-generation protection\n";
}
