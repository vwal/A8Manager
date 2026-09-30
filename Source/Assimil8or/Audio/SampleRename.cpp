#include "SampleRename.h"
#include "SafeAudioImport.h"
#include "WaveformDesignExport.h"
#include "WaveformDesignRecall.h"
#include "../Preset/PresetProperties.h"
#include <juce_cryptography/juce_cryptography.h>
#include <filesystem>

namespace SampleRename
{
    namespace
    {
        const juce::String characters { " !#$%&'()+,-.0123456789;=@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]_`{}~abcdefghijklmnopqrstuvwxyz" };

        std::filesystem::path path (const juce::File& file)
        {
            return std::filesystem::path (reinterpret_cast<const char8_t*> (file.getFullPathName ().toRawUTF8 ()));
        }

        bool regular (const juce::File& file, bool directory = false)
        {
            std::error_code error;
            return std::filesystem::symlink_status (path (file), error).type ()
                == (directory ? std::filesystem::file_type::directory : std::filesystem::file_type::regular) && ! error;
        }

        juce::Result identify (const juce::File& file, Result::CreatedFile& record)
        {
            if (! regular (file)) return juce::Result::fail ("Missing, linked or non-regular file: " + file.getFullPathName ());
            const auto id { file.getFileIdentifier () };
            const auto size { file.getSize () };
            const auto modified { file.getLastModificationTime () };
            juce::FileInputStream stream (file);
            if (stream.getStatus ().failed ()) return stream.getStatus ();
            const auto hash { juce::SHA256 (stream).toHexString () };
            if (stream.getStatus ().failed () || stream.getPosition () != size || ! regular (file)
                || file.getFileIdentifier () != id || file.getSize () != size || file.getLastModificationTime () != modified)
                return juce::Result::fail ("A file changed or could not be read completely: " + file.getFullPathName ());
            record = { file, id, size, modified, hash };
            return juce::Result::ok ();
        }

        bool same (const Result::CreatedFile& first, const Result::CreatedFile& second)
        {
            return first.file == second.file && first.identity == second.identity && first.size == second.size
                && first.modified == second.modified && first.sha256 == second.sha256;
        }

        juce::Result available (const juce::File& file)
        {
            std::error_code error;
            std::filesystem::directory_iterator iterator (path (file.getParentDirectory ()), error), end;
            if (error) return juce::Result::fail ("Cannot inspect the destination folder.");
            int entries { 0 };
            for (; iterator != end && ! error; iterator.increment (error))
            {
                if (++entries > 100000) return juce::Result::fail ("Too many files in this folder to check names safely.");
                const auto name { iterator->path ().filename ().u8string () };
                if (juce::String::fromUTF8 (reinterpret_cast<const char*> (name.c_str ())).equalsIgnoreCase (file.getFileName ()))
                    return juce::Result::fail ("That name is already in use (including letter-case variants): " + file.getFileName ());
            }
            return error ? juce::Result::fail ("Cannot finish inspecting the destination folder.") : juce::Result::ok ();
        }
    }

    juce::Result validateName (const juce::String& requestedName, juce::String& finalFilename)
    {
        finalFilename.clear ();
        auto name { requestedName.trim () };
        if (name.isEmpty () || ! name.containsOnly (characters) || name.startsWithChar ('.') || name.endsWithChar ('.'))
            return juce::Result::fail ("Enter a plain sample name using Assimil8or-compatible ASCII characters, not a path or a hidden filename.");
        if (! name.endsWithIgnoreCase (".wav")) name += ".wav";
        if (name.length () > 47) return juce::Result::fail ("The sample name must be at most 47 characters including .wav.");
        const auto stem { name.dropLastCharacters (4) };
        const auto device { stem.upToFirstOccurrenceOf (".", false, false).trimEnd ().toUpperCase () };
        if (stem.isEmpty () || stem.endsWithChar ('.') || stem.endsWithChar (' ') || device == "CON" || device == "PRN"
            || device == "AUX" || device == "NUL" || device == "CLOCK$" || device == "CONIN$" || device == "CONOUT$"
            || (device.length () == 4 && (device.startsWith ("COM") || device.startsWith ("LPT")) && device[3] >= '1' && device[3] <= '9'))
            return juce::Result::fail ("Choose a non-reserved sample name without trailing spaces or dots.");
        finalFilename = name;
        return juce::Result::ok ();
    }

    juce::Result cleanup (Result& result)
    {
        std::vector<Result::CreatedFile> remaining;
        juce::StringArray errors;
        for (const auto& owned : result.ownership)
        {
            if (! owned.file.exists () && ! owned.file.isSymbolicLink ()) continue;
            Result::CreatedFile current;
            if (identify (owned.file, current).failed () || ! same (owned, current))
            {
                remaining.push_back (owned);
                errors.add ("Changed file preserved: " + owned.file.getFullPathName ());
            }
            else if (! owned.file.deleteFile ())
            {
                remaining.push_back (owned);
                errors.add ("Unable to remove unused copied file: " + owned.file.getFullPathName ());
            }
        }
        result.editedPreset = {};
        result.filename.clear ();
        result.references = 0;
        result.ownership = std::move (remaining);
        result.createdFiles.clear ();
        for (const auto& owned : result.ownership) result.createdFiles.add (owned.file);
        return errors.isEmpty () ? juce::Result::ok () : juce::Result::fail (errors.joinIntoString ("\n"));
    }

    juce::Result prepare (const juce::File& folder, const juce::ValueTree& preset, const juce::String& sourceFilename,
                           const juce::String& requestedName, Result& result, Publish publish)
    {
        result = {};
        if (! regular (folder, true)) return juce::Result::fail ("The preset folder is unavailable or linked.");
        juce::String filename;
        if (const auto valid { validateName (requestedName, filename) }; valid.failed ()) return valid;
        if (sourceFilename.isEmpty () || sourceFilename.startsWithChar ('.') || ! sourceFilename.containsOnly (characters)
            || ! sourceFilename.endsWithIgnoreCase (".wav") || sourceFilename.length () > 255)
            return juce::Result::fail ("The assigned sample must be a flat WAV filename.");
        if (! preset.hasType (PresetProperties::PresetTypeId) || preset.getNumChildren () != 8)
            return juce::Result::fail ("The preset structure is invalid.");
        auto edited { preset.createCopy () };
        int references { 0 };
        for (int channel { 0 }; channel < 8; ++channel)
        {
            auto tree { edited.getChild (channel) };
            if (! tree.hasType (ChannelProperties::ChannelTypeId) || tree.getNumChildren () != 8)
                return juce::Result::fail ("The channel structure is invalid.");
            for (int zone { 0 }; zone < 8; ++zone)
            {
                auto item { tree.getChild (zone) };
                if (! item.hasType (ZoneProperties::ZoneTypeId) || item.getNumChildren () != 0)
                    return juce::Result::fail ("The zone structure is invalid.");
                if (item.getProperty (ZoneProperties::SamplePropertyId).toString ().equalsIgnoreCase (sourceFilename))
                {
                    // A case-sensitive filesystem might contain distinct files;
                    // never quietly coalesce those references.
                    const auto old { item.getProperty (ZoneProperties::SamplePropertyId).toString () };
                    if (old != sourceFilename && folder.getChildFile (old).existsAsFile ()
                        && folder.getChildFile (old).getFileIdentifier () != folder.getChildFile (sourceFilename).getFileIdentifier ())
                        return juce::Result::fail ("Sample references differ only by case but identify different files.");
                    item.setProperty (ZoneProperties::SamplePropertyId, filename, nullptr);
                    ++references;
                }
            }
        }
        if (references == 0) return juce::Result::fail ("This sample is no longer assigned to the current preset.");
        const auto source { folder.getChildFile (sourceFilename) };
        Result::CreatedFile sourceBefore;
        if (const auto read { identify (source, sourceBefore) }; read.failed ()) return read;
        if (filename == sourceFilename)
        {
            result.editedPreset = preset.createCopy ();
            result.filename = filename;
            return juce::Result::ok ();
        }
        const auto destination { folder.getChildFile (filename) };
        if (const auto unused { available (destination) }; unused.failed ()) return unused;
        if (const auto unused { available (WaveformDesignRecall::copiedRecipe (destination)) }; unused.failed ()) return unused;
        const auto inheritedRecipe { WaveformDesignRecall::adjacentRecipe (destination) };
        if (inheritedRecipe != juce::File ())
            if (const auto unused { available (inheritedRecipe) }; unused.failed ()) return unused;
        AudioManager audio;
        auto reader { audio.getReaderFor (source) };
        if (! reader || ! audio.isAssimil8orSupportedAudioFile (source))
            return juce::Result::fail ("The assigned sample is not a readable Assimil8or-compatible WAV.");
        const auto cv { CvSampleSafety::isCv (source, *reader) };
        const auto needsCvTag { cv && ! CvSampleSafety::hasCvMetadata (reader->metadataValues) };
        WaveformDesignRecall::RecalledDesign recalled;
        juce::var document;
        Result::CreatedFile recipeBefore;
        const auto recipe { WaveformDesignRecall::adjacentRecipe (source) };
        const bool hasRecipe { recipe != juce::File () && (recipe.exists () || recipe.isSymbolicLink ()) };
        if (hasRecipe)
        {
            if (const auto read { identify (recipe, recipeBefore) }; read.failed ()) return read;
            if (const auto loaded { WaveformDesignRecall::loadRecipe (recipe, recalled, &document) }; loaded.failed ()) return loaded;
            WaveformDesignRecall::RecalledDesign verified;
            const auto recall { WaveformDesignRecall::recallWave (source, verified) };
            if (recall.failed ())
            {
                // Only the documented old untagged CV package can acquire a
                // purpose tag here. Never fabricate audio-generator provenance.
                auto input { source.createInputStream () };
                const auto frames { std::llround (recalled.settings.sampleRate * recalled.settings.durationSeconds) };
                if (! needsCvTag || sourceFilename != "voice-01.wav" || recipe.getFileName () != "design.json"
                    || recalled.settings.mode != WaveformDesign::Mode::modulation || ! input
                    || ! WaveformDesignRecall::detail::boundedWave (*input, frames)) return recall;
                recalled.voiceIndex = 0;
            }
            else recalled.voiceIndex = verified.voiceIndex;
        }
        reader.reset ();
        const auto stage { folder.getChildFile (".a8-sample-copy-" + juce::Uuid ().toString ()) };
        std::error_code error;
        if (! std::filesystem::create_directory (path (stage), error) || error)
            return juce::Result::fail ("Cannot create a private sample-copy staging folder.");
        struct RemoveStage { juce::File folder; ~RemoveStage () { folder.deleteRecursively (); } } remove { stage };
        const auto stagedWave { stage.getChildFile (filename) };
        if (const auto copied { needsCvTag ? SafeAudioImport::stageWave (audio, source, stagedWave)
                                          : SafeAudioImport::copyNew (source, stagedWave) }; copied.failed ()) return copied;
        Result::CreatedFile staged;
        if (const auto checked { identify (stagedWave, staged) }; checked.failed ()) return checked;
        if (! needsCvTag && (staged.size != sourceBefore.size || staged.sha256 != sourceBefore.sha256))
            return juce::Result::fail ("The copied WAV differs from the original.");
        std::vector<juce::File> outputs { stagedWave };
        if (hasRecipe)
        {
            juce::DynamicObject::Ptr binding { new juce::DynamicObject };
            binding->setProperty ("filename", filename);
            binding->setProperty ("voiceIndex", recalled.voiceIndex);
            binding->setProperty ("sha256", staged.sha256);
            document.getDynamicObject ()->setProperty ("copiedWave", juce::var (binding.get ()));
            const auto alias { WaveformDesignRecall::copiedRecipe (stagedWave) };
            if (const auto written { WaveformDesign::ExportSupport::writeText (alias, juce::JSON::toString (document) + "\n") }; written.failed ()) return written;
            WaveformDesignRecall::RecalledDesign verified;
            if (const auto checked { WaveformDesignRecall::recallWave (stagedWave, verified) }; checked.failed ()) return checked;
            outputs.push_back (alias);
        }
        Result::CreatedFile after;
        if (const auto checked { identify (source, after) }; checked.failed ()) return checked;
        if (! same (sourceBefore, after)) return juce::Result::fail ("The original sample changed while copying.");
        if (hasRecipe)
        {
            if (const auto checked { identify (recipe, after) }; checked.failed ()) return checked;
            if (! same (recipeBefore, after)) return juce::Result::fail ("The original waveform recipe changed while copying.");
        }
        if (! publish) publish = WaveformDesign::ExportSupport::publishExclusive;
        auto fail = [&] (const juce::String& message)
        {
            const auto undone { cleanup (result) };
            return juce::Result::fail (message + (undone.failed () ? "\n" + undone.getErrorMessage () : juce::String ()));
        };
        for (const auto& file : outputs)
        {
            const auto target { folder.getChildFile (file.getFileName ()) };
            if (const auto unused { available (target) }; unused.failed ()) return fail (unused.getErrorMessage ());
            if (const auto checked { identify (file, staged) }; checked.failed ()) return fail (checked.getErrorMessage ());
            if (const auto published { publish (file, target) }; published.failed ()) return fail (published.getErrorMessage ());
            staged.file = target;
            result.ownership.push_back (staged);
            result.createdFiles.add (target);
            if (const auto checked { identify (target, after) }; checked.failed ()) return fail (checked.getErrorMessage ());
            if (! same (staged, after)) return fail ("The new file changed before assignment and has been preserved.");
        }
        result.editedPreset = edited;
        result.filename = filename;
        result.references = references;
        return juce::Result::ok ();
    }
}
