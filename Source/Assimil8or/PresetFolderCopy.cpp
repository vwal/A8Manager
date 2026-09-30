#include "PresetFolderCopy.h"
#include "Assimil8orPreset.h"
#include "PresetFileOperations.h"
#include "Audio/ChannelCvSafety.h"
#include "Audio/SafeAudioImport.h"
#include "Audio/WaveformDesignExport.h"
#include "Audio/WaveformDesignRecall.h"
#include "MidiSetup/MidiSetupFile.h"
#include <juce_cryptography/juce_cryptography.h>
#include <filesystem>
#include <map>
#include <set>

namespace PresetFolderCopy
{
    namespace
    {
        const juce::String manifestName { ".a8-preset-copy.json" };
        constexpr int maximumManifestBytes { 128 * 1024 }, maximumFiles { 130 };
        const juce::String safeCharacters { " !#$%&'()+,-.0123456789;=@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]_`{}~abcdefghijklmnopqrstuvwxyz" };

        struct Fingerprint
        {
            juce::String name, hash;
            juce::int64 size {};
        };

        std::filesystem::path path (const juce::File& file)
        {
            return std::filesystem::path (reinterpret_cast<const char8_t*> (file.getFullPathName ().toRawUTF8 ()));
        }

        bool regular (const juce::File& file)
        {
            std::error_code error;
            return std::filesystem::symlink_status (path (file), error).type () == std::filesystem::file_type::regular && ! error;
        }

        bool directory (const juce::File& file)
        {
            std::error_code error;
            return std::filesystem::symlink_status (path (file), error).type () == std::filesystem::file_type::directory && ! error;
        }

        bool exists (const juce::File& file)
        {
            // Unlike File::exists(), dangling symbolic links count as occupied.
            std::error_code error;
            const auto status { std::filesystem::symlink_status (path (file), error) };
            return status.type () != std::filesystem::file_type::not_found;
        }

        bool safeLeaf (const juce::String& name)
        {
            return name.isNotEmpty () && name.length () <= 47 && ! name.startsWithChar ('.') && ! name.endsWithChar ('.')
                && ! name.endsWithChar (' ') && name.containsOnly (safeCharacters);
        }

        int namedFolderSlot (const juce::File& folder)
        {
            const auto name { folder.getFileName () };
            // Keep existing copies recognized without renaming user folders.
            const juce::String prefix { name.startsWith ("PR") ? "PR" : "A8 Preset " };
            if (! name.startsWith (prefix)) return 0;
            const auto number { name.substring (prefix.length ()).upToFirstOccurrenceOf (" - ", false, false) };
            const auto slot { number.getIntValue () };
            return slot >= 1 && slot <= 199 && number == juce::String (slot).paddedLeft ('0', 2)
                && name.startsWith (prefix + number + " - ") ? slot : 0;
        }

        juce::Result fingerprint (const juce::File& file, Fingerprint& result)
        {
            if (! regular (file)) return juce::Result::fail ("Not a regular file (or a symbolic link): " + file.getFullPathName ());
            const auto identity { file.getFileIdentifier () };
            const auto size { file.getSize () };
            const auto modified { file.getLastModificationTime () };
            juce::FileInputStream stream (file);
            if (stream.getStatus ().failed ()) return stream.getStatus ();
            const auto hash { juce::SHA256 (stream).toHexString () };
            if (stream.getStatus ().failed () || stream.getPosition () != size || ! regular (file)
                || file.getFileIdentifier () != identity || file.getSize () != size || file.getLastModificationTime () != modified)
                return juce::Result::fail ("The file changed or could not be read completely: " + file.getFullPathName ());
            result = { file.getFileName (), hash, size };
            return juce::Result::ok ();
        }

        bool equal (const Fingerprint& first, const Fingerprint& second)
        {
            return first.name == second.name && first.size == second.size && first.hash == second.hash;
        }

        juce::Result readManifest (const juce::File& folder, juce::String& text, juce::var& parsed)
        {
            const auto file { folder.getChildFile (manifestName) };
            if (! regular (file) || file.getSize () < 1 || file.getSize () > maximumManifestBytes)
                return juce::Result::fail ("This folder is not an unchanged A8Manager preset copy: " + folder.getFullPathName ());
            juce::FileInputStream stream (file);
            juce::MemoryBlock data;
            stream.readIntoMemoryBlock (data, maximumManifestBytes + 1);
            const auto bytes { static_cast<int> (data.getSize ()) };
            if (stream.getStatus ().failed () || bytes != file.getSize () || bytes > maximumManifestBytes
                || ! CvSampleSafety::detail::boundedJsonObject (static_cast<const char*> (data.getData ()), bytes))
                return juce::Result::fail ("Cannot read a valid preset-copy ownership record; nothing was replaced.");
            text = juce::String::fromUTF8 (static_cast<const char*> (data.getData ()), bytes);
            return juce::JSON::parse (text, parsed);
        }

        juce::Result verifyOwned (const juce::File& folder, const juce::File& source, int slot,
                                  const juce::String& name, juce::String& previousManifest)
        {
            if (! directory (folder)) return juce::Result::fail ("The preset-copy destination is not a regular folder: " + folder.getFullPathName ());
            if (! isManifest (folder.getChildFile (manifestName)))
                return juce::Result::fail ("This folder has no valid A8Manager preset-copy ownership record; nothing was replaced: " + folder.getFullPathName ());
            juce::String text;
            juce::var parsed;
            if (const auto read { readManifest (folder, text, parsed) }; read.failed ()) return read;
            auto* object { parsed.getDynamicObject () };
            if (object == nullptr || object->getProperty ("format").toString () != "A8ManagerPresetFolderCopy"
                || static_cast<int> (object->getProperty ("version")) != 1
                || object->getProperty ("source").toString () != source.getFullPathName ()
                || static_cast<int> (object->getProperty ("slot")) != slot || object->getProperty ("name").toString () != name)
                return juce::Result::fail ("The destination belongs to another preset/source or its name was truncated to the same folder name. Nothing was replaced: " + folder.getFullPathName ());
            if (previousManifest.isNotEmpty () && text != previousManifest)
                return juce::Result::fail ("The preset copy changed during saving; nothing was replaced.");
            const auto files { object->getProperty ("files") };
            const auto* entries { files.getArray () };
            if (entries == nullptr || entries->isEmpty () || entries->size () > maximumFiles)
                return juce::Result::fail ("The preset-copy file inventory is invalid; nothing was replaced.");
            std::set<juce::String> names;
            for (const auto& entry : *entries)
            {
                auto* item { entry.getDynamicObject () };
                if (item == nullptr) return juce::Result::fail ("Invalid preset-copy inventory.");
                Fingerprint expected { item->getProperty ("name").toString (), item->getProperty ("sha256").toString (),
                                       static_cast<juce::int64> (item->getProperty ("size")) };
                if (! safeLeaf (expected.name) || expected.size < 0 || expected.hash.length () != 64
                    || ! expected.hash.containsOnly ("0123456789abcdef") || ! names.insert (expected.name.toLowerCase ()).second)
                    return juce::Result::fail ("Invalid preset-copy inventory; nothing was replaced.");
                Fingerprint current;
                if (const auto checked { fingerprint (folder.getChildFile (expected.name), current) }; checked.failed ()) return checked;
                if (! equal (current, expected))
                    return juce::Result::fail ("The saved copy was edited outside this save operation. Its changes were preserved: " + folder.getChildFile (expected.name).getFullPathName ());
            }
            int count { 0 };
            for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFilesAndDirectories))
            {
                const auto file { entry.getFile () };
                // Finder's directory-view cache is not an owned asset. Keep a
                // small regular cache when updating, without treating a normal
                // Finder visit as an external preset/sample edit.
                if (file.getFileName () == ".DS_Store" && regular (file) && file.getSize () <= 1024 * 1024)
                    continue;
                if (++count > maximumFiles + 1) return juce::Result::fail ("The preset-copy folder contains additional files; nothing was replaced.");
                if (file.getFileName () == manifestName) continue;
                if (! regular (file) || names.count (file.getFileName ().toLowerCase ()) == 0)
                    return juce::Result::fail ("The preset-copy folder contains additional content, which was preserved: " + file.getFullPathName ());
            }
            if (count != static_cast<int> (names.size ()) + 1)
                return juce::Result::fail ("The preset-copy folder inventory changed; nothing was replaced.");
            previousManifest = text;
            return juce::Result::ok ();
        }

        juce::Result createPrivateDirectory (const juce::File& file)
        {
            std::error_code error;
            return std::filesystem::create_directory (path (file), error) && ! error ? juce::Result::ok ()
                : juce::Result::fail ("Cannot create the private preset-copy staging folder: " + file.getFullPathName ());
        }
    }

    juce::String folderName (int slot, const juce::String& presetName)
    {
        if (slot < 1 || slot > 199) return {};
        const auto prefix { "PR" + juce::String (slot).paddedLeft ('0', 2) + " - " };
        auto name { presetName.retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-").trim () };
        if (name.isEmpty ()) name = "Untitled";
        name = name.substring (0, 31 - prefix.length ()).trimEnd ();
        return prefix + name;
    }

    bool isNamedPresetFolder (const juce::File& folder, const juce::ValueTree& tree)
    {
        if (! tree.hasType (PresetProperties::PresetTypeId)) return false;
        const auto slot { static_cast<int> (tree.getProperty (PresetProperties::IdPropertyId)) };
        if (slot < 1 || slot > 199) return false;
        return namedFolderSlot (folder) == slot;
    }

    bool isManifest (const juce::File& file)
    {
        if (file.getFileName () != manifestName) return false;
        juce::String text;
        juce::var parsed;
        if (readManifest (file.getParentDirectory (), text, parsed).failed ()) return false;
        const auto* object { parsed.getDynamicObject () };
        if (object == nullptr || object->getProperty ("format").toString () != "A8ManagerPresetFolderCopy"
            || ! object->getProperty ("version").isInt () || static_cast<int> (object->getProperty ("version")) != 1
            || ! object->getProperty ("slot").isInt () || static_cast<int> (object->getProperty ("slot")) < 1
            || static_cast<int> (object->getProperty ("slot")) > 199
            || ! object->getProperty ("source").isString () || object->getProperty ("source").toString ().length () > 16384
            || ! juce::File::isAbsolutePath (object->getProperty ("source").toString ())
            || ! object->getProperty ("name").isString () || object->getProperty ("name").toString ().length () > 1024) return false;
        const auto files { object->getProperty ("files") };
        const auto* entries { files.getArray () };
        if (entries == nullptr || entries->isEmpty () || entries->size () > maximumFiles) return false;
        std::set<juce::String> names;
        for (const auto& item : *entries)
        {
            const auto* entry { item.getDynamicObject () };
            if (entry == nullptr) return false;
            const auto name { entry->getProperty ("name").toString () };
            const auto hash { entry->getProperty ("sha256").toString () };
            const auto size { entry->getProperty ("size") };
            if (! entry->getProperty ("name").isString () || ! safeLeaf (name) || ! names.insert (name.toLowerCase ()).second
                || ! entry->getProperty ("sha256").isString () || hash.length () != 64 || ! hash.containsOnly ("0123456789abcdef")
                || (! size.isInt () && ! size.isInt64 ()) || static_cast<juce::int64> (size) < 0) return false;
        }
        return true;
    }

    juce::Result createOrUpdate (const juce::File& source, const juce::ValueTree& tree, juce::File& resultFolder)
    {
        resultFolder = juce::File ();
        if (! directory (source)) return juce::Result::fail ("The current preset folder is unavailable or is a symbolic link.");
        if (! tree.hasType (PresetProperties::PresetTypeId) || tree.getNumChildren () != kNumChannels)
            return juce::Result::fail ("The preset snapshot is invalid.");
        const auto slot { static_cast<int> (tree.getProperty (PresetProperties::IdPropertyId)) };
        const auto name { tree.getProperty (PresetProperties::NamePropertyId).toString () };
        const auto leaf { folderName (slot, name) };
        if (leaf.isEmpty ()) return juce::Result::fail ("Choose a preset slot from 1 through 199.");
        if (name.length () > 1024 || source.getFullPathName ().length () > 16384)
            return juce::Result::fail ("The preset name or source path is too long for a bounded ownership record.");
        if (isNamedPresetFolder (source, tree))
            return juce::Result::fail ("This preset is already in its named folder; use the normal preset Save without creating another copy.");

        // New slots created while inspecting an existing named preset belong
        // beside it, not nested inside a folder the hardware cannot browse.
        const auto parent { namedFolderSlot (source) != 0 ? source.getParentDirectory () : source };
        const auto destination { parent.getChildFile (leaf) };
        const auto replacing { exists (destination) };
        juce::String previousManifest;
        if (replacing)
            if (const auto checked { verifyOwned (destination, source, slot, name, previousManifest) }; checked.failed ()) return checked;

        std::map<juce::String, juce::File> files;
        auto add = [&] (const juce::File& file) -> juce::Result
        {
            if (! safeLeaf (file.getFileName ()) || ! regular (file))
                return juce::Result::fail ("Missing, unsafe, linked or non-regular preset dependency: " + file.getFullPathName ());
            const auto key { file.getFileName ().toLowerCase () };
            if (const auto found { files.find (key) }; found != files.end () && found->second.getFileName () != file.getFileName ())
                return juce::Result::fail ("Two sample names differ only by letter case. Rename them before copying to an SD card.");
            files[key] = file;
            return juce::Result::ok ();
        };
        AudioManager audio;
        for (int channel { 0 }; channel < kNumChannels; ++channel)
        {
            const auto channelTree { tree.getChild (channel) };
            if (! channelTree.hasType (ChannelProperties::ChannelTypeId) || channelTree.getNumChildren () != kNumZones)
                return juce::Result::fail ("The preset has an invalid channel or zone structure.");
            for (int zone { 0 }; zone < kNumZones; ++zone)
            {
                const auto filename { channelTree.getChild (zone).getProperty (ZoneProperties::SamplePropertyId).toString () };
                if (filename.isEmpty ()) continue;
                if (! safeLeaf (filename) || ! filename.endsWithIgnoreCase (".wav"))
                    return juce::Result::fail ("Samples must be flat WAV filenames of at most 47 supported characters: " + filename);
                const auto file { source.getChildFile (filename) };
                if (const auto added { add (file) }; added.failed ()) return added;
                if (! audio.isAssimil8orSupportedAudioFile (file)) return juce::Result::fail ("The sample is unreadable or not an Assimil8or-compatible WAV: " + filename);
                const auto stem { file.getFileNameWithoutExtension () };
                if (stem.length () < 4 || stem[stem.length () - 3] != '-' || ! stem.getLastCharacters (2).containsOnly ("0123456789")) continue;
                const auto family { stem.dropLastCharacters (3) };
                if (family != "voice" && ! WaveformDesignRecall::detail::hasAssignmentToken (family)) continue;
                const auto recipe { source.getChildFile (family == "voice" ? "design.json" : family + ".design.json") };
                if (! exists (recipe)) continue;
                if (const auto added { add (recipe) }; added.failed ()) return added;
                WaveformDesignRecall::RecalledDesign recalled;
                if (const auto loaded { WaveformDesignRecall::loadRecipe (recipe, recalled) }; loaded.failed ()) return loaded;
            }
        }
        const auto midi { static_cast<int> (tree.getProperty (PresetProperties::MidiSetpPropertyId)) };
        if (midi < 0 || midi > 8) return juce::Result::fail ("The preset references an invalid MIDI setup.");
        // Preset MidiSetup is zero-based; midi1.yml through midi9.yml are not.
        const auto midiFile { source.getChildFile ("midi" + juce::String (midi + 1) + ".yml") };
        if (exists (midiFile))
        {
            if (const auto added { add (midiFile) }; added.failed ()) return added;
            MidiSetupFile document;
            if (const auto checked { document.read (midiFile) }; checked.failed ()) return checked;
        }

        const auto stage { parent.getChildFile (".a8-preset-copy-" + juce::Uuid ().toString ()) };
        if (const auto made { createPrivateDirectory (stage) }; made.failed ()) return made;
        struct Cleanup { juce::File directory; ~Cleanup () { if (directory.exists ()) directory.deleteRecursively (); } } cleanup { stage };
        std::vector<Fingerprint> inventory;
        for (const auto& item : files)
        {
            const auto& file { item.second };
            Fingerprint before, copied, after;
            if (const auto checked { fingerprint (file, before) }; checked.failed ()) return checked;
            const auto output { stage.getChildFile (file.getFileName ()) };
            if (const auto copiedFile { SafeAudioImport::copyNew (file, output) }; copiedFile.failed ()) return copiedFile;
            if (const auto checked { fingerprint (output, copied) }; checked.failed ()) return checked;
            if (const auto checked { fingerprint (file, after) }; checked.failed ()) return checked;
            if (! equal (before, copied) || ! equal (before, after)) return juce::Result::fail ("A source file changed while copying; the original files and previous saved copy were preserved.");
            inventory.push_back (copied);
        }
        if (const auto safe { ChannelCvSafety::validatePreset (tree, stage) }; safe.failed ()) return safe;
        const auto presetFile { stage.getChildFile ("prst" + juce::String (slot).paddedLeft ('0', 3) + ".yml") };
        Assimil8orPreset writer;
        if (const auto written { writer.write (presetFile, tree) }; written.failed ()) return written;
        juce::ValueTree readback;
        if (const auto checked { PresetFileOperations::read (presetFile, readback) }; checked.failed ()) return checked;
        Fingerprint saved;
        if (const auto checked { fingerprint (presetFile, saved) }; checked.failed ()) return checked;
        inventory.push_back (saved);
        juce::DynamicObject::Ptr manifest { new juce::DynamicObject };
        manifest->setProperty ("format", "A8ManagerPresetFolderCopy");
        manifest->setProperty ("version", 1);
        manifest->setProperty ("source", source.getFullPathName ());
        manifest->setProperty ("slot", slot);
        manifest->setProperty ("name", name);
        juce::Array<juce::var> entries;
        for (const auto& item : inventory)
        {
            juce::DynamicObject::Ptr entry { new juce::DynamicObject };
            entry->setProperty ("name", item.name);
            entry->setProperty ("size", item.size);
            entry->setProperty ("sha256", item.hash);
            entries.add (juce::var (entry.get ()));
        }
        manifest->setProperty ("files", entries);
        const auto manifestText { juce::JSON::toString (juce::var (manifest.get ())) + "\n" };
        if (manifestText.getNumBytesAsUTF8 () > maximumManifestBytes) return juce::Result::fail ("The preset-copy inventory is too large.");
        if (const auto written { WaveformDesign::ExportSupport::writeText (stage.getChildFile (manifestName), manifestText) }; written.failed ()) return written;

        juce::File backup;
        if (replacing)
        {
            if (const auto checked { verifyOwned (destination, source, slot, name, previousManifest) }; checked.failed ()) return checked;
            const auto finderCache { destination.getChildFile (".DS_Store") };
            const auto hasFinderCache { exists (finderCache) };
            Fingerprint finderBefore;
            if (hasFinderCache)
            {
                if (const auto checked { fingerprint (finderCache, finderBefore) }; checked.failed ()) return checked;
                if (const auto copied { SafeAudioImport::copyNew (finderCache, stage.getChildFile (".DS_Store")) }; copied.failed ()) return copied;
                Fingerprint copied;
                if (const auto checked { fingerprint (stage.getChildFile (".DS_Store"), copied) }; checked.failed ()) return checked;
                if (! equal (copied, finderBefore)) return juce::Result::fail ("Finder's folder metadata changed while saving; please retry.");
            }
            backup = parent.getChildFile (".a8-preset-backup-" + juce::Uuid ().toString ());
            if (const auto moved { WaveformDesign::ExportSupport::publishExclusive (destination, backup) }; moved.failed ()) return moved;
            // Check again after taking possession; a changed folder is never
            // deleted just because it passed an earlier ownership check.
            auto checked { verifyOwned (backup, source, slot, name, previousManifest) };
            if (checked.wasOk ())
            {
                Fingerprint finderAfter;
                if (hasFinderCache != exists (backup.getChildFile (".DS_Store"))
                    || (hasFinderCache && (fingerprint (backup.getChildFile (".DS_Store"), finderAfter).failed () || ! equal (finderBefore, finderAfter))))
                    checked = juce::Result::fail ("Finder's folder metadata changed while saving; please retry.");
            }
            if (checked.failed ())
            {
                const auto restored { WaveformDesign::ExportSupport::publishExclusive (backup, destination) };
                return juce::Result::fail (checked.getErrorMessage () + (restored.wasOk () ? " The previous folder was restored."
                    : " The previous folder is preserved at " + backup.getFullPathName ()));
            }
        }
        if (const auto published { WaveformDesign::ExportSupport::publishExclusive (stage, destination) }; published.failed ())
        {
            auto message { published.getErrorMessage () };
            if (backup != juce::File ())
            {
                const auto restored { WaveformDesign::ExportSupport::publishExclusive (backup, destination) };
                message += restored.wasOk () ? " The previous folder was restored." : " The previous folder is preserved at " + backup.getFullPathName ();
            }
            return juce::Result::fail (message);
        }
        if (backup != juce::File ())
        {
            // A verified backup contains only files this helper created. If it
            // changed or removal fails, retain it for recovery, never touch it.
            if (verifyOwned (backup, source, slot, name, previousManifest).wasOk ()) backup.deleteRecursively ();
        }
        resultFolder = destination;
        return juce::Result::ok ();
    }
}
