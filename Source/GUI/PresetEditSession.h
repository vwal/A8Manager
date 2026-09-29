#pragma once

#include "../AppProperties.h"
#include "../Assimil8or/PresetManagerProperties.h"
#include "../Assimil8or/Preset/PresetHelpers.h"
#include "oolib/Properties/PersistentRootProperties.h"
#include "oolib/Properties/RuntimeRootProperties.h"
#include <cstdint>
#include <optional>

// Both workspaces edit the same document. Background generators receive a
// detached snapshot, never the live tree or an implicit "currently selected" slot.
class PresetEditSession : private juce::ValueTree::Listener
{
public:
    struct Snapshot
    {
        juce::File folder;
        juce::ValueTree preset;
        uint64_t revision { 0 };
    };

    ~PresetEditSession () override { detach (); }

    void init (juce::ValueTree root)
    {
        detach ();
        PersistentRootProperties persistent (root, PersistentRootProperties::WrapperType::client, PersistentRootProperties::EnableCallbacks::no);
        app.wrap (persistent.getValueTree (), AppProperties::WrapperType::client, AppProperties::EnableCallbacks::no);
        appTree = app.getValueTree ();
        RuntimeRootProperties runtime (root, RuntimeRootProperties::WrapperType::client, RuntimeRootProperties::EnableCallbacks::no);
        PresetManagerProperties presets (runtime.getValueTree (), PresetManagerProperties::WrapperType::client, PresetManagerProperties::EnableCallbacks::no);
        edit = presets.getPreset ("edit");
        baseline = presets.getPreset ("unedited");
        edit.addListener (this);
        baseline.addListener (this);
        appTree.addListener (this);
        ++revision;
    }

    uint64_t getRevision () const { return revision; }
    juce::ValueTree getEdit () const { return edit; }
    bool isDirty () const { return edit.isValid () && ! PresetHelpers::areEntirePresetsEqual (baseline, edit); }

    std::optional<Snapshot> snapshot ()
    {
        if (! edit.isValid () || ! app.isValid ()) return {};
        PresetProperties preset (edit, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        const auto id { preset.getId () };
        const auto folderName { app.getMostRecentFolder () };
        const auto recent { app.getMRUList () };
        if (id < 1 || id > 199 || folderName.isEmpty () || recent.isEmpty ()) return {};
        const juce::File folder { folderName };
        const auto expected { folder.getChildFile ("prst" + juce::String (id).paddedLeft ('0', 3) + ".yml") };
        // During folder scanning the old preset may still be loaded. Never bind
        // it to the new folder until the preset list has selected its real slot.
        if (! folder.isDirectory () || juce::File (recent[0]) != expected) return {};
        return Snapshot { folder, edit.createCopy (), revision };
    }

    juce::Result apply (const Snapshot& source, juce::ValueTree generated)
    {
        const auto current { snapshot () };
        if (! current || current->revision != source.revision || current->folder != source.folder
            || ! current->preset.isEquivalentTo (source.preset))
            return juce::Result::fail ("The preset or folder changed while generating. Nothing was assigned; select the destination and try again.");
        if (! generated.hasType (PresetProperties::PresetTypeId)
            || generated.getProperty (PresetProperties::IdPropertyId) != source.preset.getProperty (PresetProperties::IdPropertyId))
            return juce::Result::fail ("The generated assignment does not belong to the selected preset.");
        // Retain all ValueTree identities: existing editors and SampleManager
        // listeners must observe the edits. Only Save updates the clean baseline.
        PresetProperties::copyTreeProperties (generated, edit);
        ++revision; // Approval is single-use even if a future assignment is a no-op.
        return juce::Result::ok ();
    }

private:
    AppProperties app;
    juce::ValueTree edit, baseline, appTree;
    uint64_t revision { 0 };

    void detach ()
    {
        edit.removeListener (this);
        baseline.removeListener (this);
        appTree.removeListener (this);
    }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { ++revision; }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { ++revision; }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { ++revision; }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { ++revision; }
};
