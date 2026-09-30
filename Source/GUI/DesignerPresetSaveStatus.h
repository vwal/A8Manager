#pragma once

#include "PresetEditSession.h"

// A successful copy is known only for the saved session snapshot. This is not
// an ongoing filesystem audit: an external file edit may require another Save.
class DesignerPresetSaveStatus
{
public:
    void reset () { saved.reset (); copyFolder = juce::File {}; }

    void recordSuccess (const std::optional<PresetEditSession::Snapshot>& current, const juce::File& destination)
    {
        reset ();
        if (! current || ! destination.isDirectory ()) return;
        saved = *current;
        saved->preset = current->preset.createCopy ();
        copyFolder = destination;
    }

    bool copyWasRefreshed (const std::optional<PresetEditSession::Snapshot>& current) const
    {
        return saved && current && current->revision == saved->revision && current->folder == saved->folder
            && current->preset.isEquivalentTo (saved->preset) && copyFolder.isDirectory ();
    }

    static juce::String text (bool busy, bool dirty, bool bound, bool separateCopy, bool copyRefreshed)
    {
        if (busy) return dirty ? "SAVE IS PENDING (saving...)" : "Saving A8 folder...";
        if (dirty) return "SAVE IS PENDING";
        if (! bound) return "Select a preset slot";
        if (! separateCopy) return "Saved / unchanged";
        return copyRefreshed ? "Saved / refreshed copy" : "Saved (remember to refresh copy)";
    }

private:
    std::optional<PresetEditSession::Snapshot> saved;
    juce::File copyFolder;
};
