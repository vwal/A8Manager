#include "GUI/PresetEditSession.h"
#include "Assimil8or/PresetFileOperations.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void check (bool ok, const char* message) { if (! ok) throw std::runtime_error (message); }
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
    std::cout << "PASS: shared preset state, numbered save, unsaved changes, identity-preserving assignment and stale-generation protection\n";
}
