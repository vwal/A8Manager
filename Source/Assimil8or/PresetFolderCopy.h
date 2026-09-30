#pragma once

#include <JuceHeader.h>

// Builds an SD-ready copy of one preset, without altering its working files.
// File I/O belongs on a worker thread; pass a detached preset snapshot.
namespace PresetFolderCopy
{
    // Pnn - <name>, with a hardware-safe maximum of 31 characters.
    juce::String folderName (int slot, const juce::String& presetName);

    // An already-open named folder for this slot is saved normally, never
    // wrapped in another folder. Also recognizes legacy PR and A8 Preset prefixes.
    // The editable preset name may have changed.
    bool isNamedPresetFolder (const juce::File& folder, const juce::ValueTree& preset);

    // Bounded schema recognition for the desktop-only validator (no WAV hashes).
    bool isManifest (const juce::File& file);

    // Writes all referenced WAVs, adjacent generated recipes, an available
    // selected MIDI setup, and prstNNN.yml. Only our unchanged, manifest-owned
    // copies may be updated; foreign/externally modified folders are refused.
    // resultFolder is populated only on success. Originals are never modified.
    juce::Result createOrUpdate (const juce::File& sourceFolder, const juce::ValueTree& preset,
                                 juce::File& resultFolder);
}
