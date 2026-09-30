#include "PresetBankExport.h"
#include "PresetFileOperations.h"
#include "Audio/SafeAudioImport.h"
#include "Audio/WaveformDesignExport.h"
#include "Audio/WaveformDesignRecall.h"
#include "MidiSetup/MidiSetupFile.h"
#include <juce_cryptography/juce_cryptography.h>
#include <array>
#include <filesystem>
#include <map>
#include <set>

namespace PresetBankExport
{
    namespace
    {
        constexpr int maximumTextBytes { 1024 * 1024 }, maximumManifestBytes { 128 * 1024 };
        constexpr juce::int64 maximumRam { 422LL * 1024 * 1024 };
        const juce::String manifestName { ".a8-preset-bank.json" };
        const juce::String safeCharacters { " !#$%&'()+,-.0123456789;=@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]_`{}~abcdefghijklmnopqrstuvwxyz" };

        std::filesystem::path path (const juce::File& file)
        {
            return std::filesystem::path (reinterpret_cast<const char8_t*> (file.getFullPathName ().toRawUTF8 ()));
        }
        std::filesystem::file_type type (const juce::File& file)
        {
            std::error_code error;
            const auto status { std::filesystem::symlink_status (path (file), error) };
            return status.type ();
        }
        bool regular (const juce::File& file) { return type (file) == std::filesystem::file_type::regular; }
        bool directory (const juce::File& file) { return type (file) == std::filesystem::file_type::directory; }
        bool occupied (const juce::File& file) { return type (file) != std::filesystem::file_type::not_found; }
        bool safeLeaf (const juce::String& name, int maximum = 47)
        {
            return name.isNotEmpty () && name.length () <= maximum && name.containsOnly (safeCharacters)
                && ! name.startsWithChar ('.') && ! name.endsWithChar ('.') && ! name.endsWithChar (' ');
        }
        juce::Result cancelled () { return juce::Result::fail ("Bank export cancelled; original files were not changed."); }
        bool stop (const Cancel& cancel) { return cancel && cancel (); }
        int presetSlot (const juce::File& file)
        {
            const auto name { file.getFileName () };
            if (name.length () != 11 || ! name.startsWithIgnoreCase ("prst") || ! name.endsWithIgnoreCase (".yml")) return 0;
            const auto digits { name.substring (4, 7) };
            const auto slot { digits.getIntValue () };
            return digits.containsOnly ("0123456789") && slot >= 1 && slot <= 199 ? slot : 0;
        }

        struct Fingerprint
        {
            juce::File file;
            juce::String hash;
            juce::int64 size {};
            juce::uint64 identity {};
            juce::Time modified;
        };
        class CancellableInput : public juce::InputStream
        {
        public:
            CancellableInput (juce::FileInputStream& input, const Cancel& shouldCancel) : source (input), cancel (shouldCancel) {}
            juce::int64 getTotalLength () override { return source.getTotalLength (); }
            juce::int64 getPosition () override { return source.getPosition (); }
            bool setPosition (juce::int64 position) override { return source.setPosition (position); }
            bool isExhausted () override { return stop (cancel) || source.isExhausted (); }
            int read (void* buffer, int count) override { return stop (cancel) ? 0 : source.read (buffer, std::min (count, 65536)); }
        private:
            juce::FileInputStream& source;
            const Cancel& cancel;
        };
        juce::Result fingerprint (const juce::File& file, Fingerprint& value, const Cancel& cancel = {})
        {
            if (! regular (file)) return juce::Result::fail ("Missing or linked dependency: " + file.getFullPathName ());
            Fingerprint result { file, {}, file.getSize (), file.getFileIdentifier (), file.getLastModificationTime () };
            juce::FileInputStream stream (file);
            if (stream.getStatus ().failed ()) return stream.getStatus ();
            CancellableInput cancellable (stream, cancel);
            result.hash = juce::SHA256 (cancellable).toHexString ();
            if (stop (cancel)) return cancelled ();
            if (stream.getStatus ().failed () || stream.getPosition () != result.size || ! regular (file)
                || file.getSize () != result.size || file.getFileIdentifier () != result.identity || file.getLastModificationTime () != result.modified)
                return juce::Result::fail ("Source changed while reading: " + file.getFullPathName ());
            value = std::move (result);
            return juce::Result::ok ();
        }
        bool sameContent (const Fingerprint& a, const Fingerprint& b) { return a.size == b.size && a.hash == b.hash; }
        juce::Result unchanged (const Fingerprint& original, const Cancel& cancel)
        {
            Fingerprint now;
            if (const auto checked { fingerprint (original.file, now, cancel) }; checked.failed ()) return checked;
            return sameContent (original, now) && original.identity == now.identity && original.modified == now.modified
                ? juce::Result::ok () : juce::Result::fail ("Source changed during export: " + original.file.getFullPathName ());
        }
        juce::Result copyNew (const juce::File& source, const juce::File& destination, const Cancel& cancel)
        {
#if JUCE_WINDOWS
            auto* input { _wfopen (source.getFullPathName ().toWideCharPointer (), L"rb") };
            auto* output { input ? _wfopen (destination.getFullPathName ().toWideCharPointer (), L"wbx") : nullptr };
#else
            auto* input { std::fopen (source.getFullPathName ().toRawUTF8 (), "rb") };
            auto* output { input ? std::fopen (destination.getFullPathName ().toRawUTF8 (), "wbx") : nullptr };
#endif
            if (! input || ! output)
            {
                if (input) std::fclose (input);
                return juce::Result::fail ("Cannot exclusively copy " + source.getFileName () + " into the private staging folder.");
            }
            std::array<unsigned char, 65536> buffer;
            bool failed { false };
            while (! stop (cancel))
            {
                const auto count { std::fread (buffer.data (), 1, buffer.size (), input) };
                if (count != 0 && std::fwrite (buffer.data (), 1, count, output) != count) { failed = true; break; }
                if (count < buffer.size ()) { failed = std::ferror (input) != 0; break; }
            }
            if (std::fflush (output) != 0) failed = true;
            if (std::fclose (output) != 0) failed = true;
            if (std::fclose (input) != 0) failed = true;
            if (stop (cancel)) return cancelled ();
            return failed ? juce::Result::fail ("Bank dependency copy failed: " + source.getFileName ()) : juce::Result::ok ();
        }
        juce::Result boundedText (const juce::File& file, juce::String& text, int maximum = maximumTextBytes)
        {
            if (! regular (file) || file.getSize () < 1 || file.getSize () > maximum)
                return juce::Result::fail ("Missing, linked or oversized text file: " + file.getFullPathName ());
            juce::FileInputStream stream (file);
            juce::MemoryBlock bytes;
            stream.readIntoMemoryBlock (bytes, maximum + 1);
            const auto size { static_cast<int> (bytes.getSize ()) };
            const auto* data { static_cast<const char*> (bytes.getData ()) };
            if (stream.getStatus ().failed () || size < 1 || size > maximum || size != file.getSize ()
                || std::memchr (data, 0, bytes.getSize ()) != nullptr || ! juce::CharPointer_UTF8::isValidString (data, size))
                return juce::Result::fail ("Cannot read bounded UTF-8 text: " + file.getFullPathName ());
            text = juce::String::createStringFromData (data, size);
            return juce::Result::ok ();
        }
        juce::Result loadPreset (const juce::File& file, juce::ValueTree& tree)
        {
            if (! directory (file.getParentDirectory ()) || presetSlot (file) == 0)
                return juce::Result::fail ("Choose an immediate saved prst001.yml through prst199.yml file.");
            juce::String text;
            if (const auto read { boundedText (file, text) }; read.failed ()) return read;
            const auto lines { juce::StringArray::fromLines (text) };
            int headers { 0 };
            for (const auto& line : lines)
            {
                const auto trimmed { line.trim () };
                if (trimmed.startsWithChar ('#')) continue;
                const auto key { trimmed.upToFirstOccurrenceOf (":", false, false).trim () };
                const auto section { key.upToFirstOccurrenceOf (" ", false, false) };
                if (section != "Preset" && section != "Channel" && section != "Zone") continue;
                const auto digits { key.fromFirstOccurrenceOf (" ", false, false).trim () };
                const auto id { digits.getIntValue () };
                if (digits.isEmpty () || ! digits.containsOnly ("0123456789") || id < 1 || id > (section == "Preset" ? 199 : 8))
                    return juce::Result::fail ("Invalid preset section in " + file.getFileName ());
                if (section == "Preset") ++headers;
            }
            if (headers != 1) return juce::Result::fail ("Expected one Preset section in " + file.getFileName ());
            Assimil8orPreset parser;
            parser.parse (lines);
            if (parser.getParseErrorsVT ().getNumChildren () != 0)
                return juce::Result::fail ("Invalid preset " + file.getFileName () + ": "
                    + parser.getParseErrorsVT ().getChild (0).getProperty ("description").toString ());
            tree = parser.getPresetVT ().createCopy ();
            return juce::Result::ok ();
        }

        struct Wave
        {
            Fingerprint input, recipe;
            juce::String outputName, recipeSignature;
            juce::StringArray absentMetadata;
            juce::var document;
            int voice {};
            bool cv {}, addCvTag {}, hasRecipe {};
            juce::int64 ram {};
        };
        struct Preset { Fingerprint input; juce::ValueTree tree; int slot {}, midi {}; };
        struct Midi { Fingerprint input; int slot {}; };

        juce::Result inspectWave (AudioManager& audio, const juce::File& file, Wave& wave, const Cancel& cancel)
        {
            if (! safeLeaf (file.getFileName ()) || ! file.hasFileExtension ("wav"))
                return juce::Result::fail ("Samples must use flat supported WAV filenames (maximum 47 characters): " + file.getFileName ());
            if (! regular (file) || file.getSize () > maximumRam + maximumTextBytes)
                return juce::Result::fail ("Missing, linked or oversized WAV: " + file.getFullPathName ());
            if (const auto checked { fingerprint (file, wave.input, cancel) }; checked.failed ()) return checked;
            auto reader { audio.getReaderFor (file) };
            if (! reader || ! audio.isAssimil8orSupportedAudioFile (file) || reader->lengthInSamples <= 0
                || reader->numChannels < 1 || reader->numChannels > 2 || reader->lengthInSamples > maximumRam / (4 * reader->numChannels))
                return juce::Result::fail ("Unreadable, unsupported or over-capacity sample: " + file.getFileName ());
            wave.ram = reader->lengthInSamples * reader->numChannels * 4;
            wave.cv = CvSampleSafety::isCv (file, *reader);
            wave.addCvTag = wave.cv && ! CvSampleSafety::hasCvMetadata (reader->metadataValues);
            const auto alias { WaveformDesignRecall::copiedRecipe (file) };
            if (! occupied (alias)) wave.absentMetadata.add (alias.getFullPathName ());
            const auto recipe { WaveformDesignRecall::adjacentRecipe (file) };
            wave.hasRecipe = recipe != juce::File () && occupied (recipe);
            if (recipe != juce::File () && ! wave.hasRecipe) wave.absentMetadata.addIfNotAlreadyThere (recipe.getFullPathName ());
            if (wave.hasRecipe)
            {
                if (! regular (recipe) || recipe.getSize () < 1 || recipe.getSize () > maximumTextBytes)
                    return juce::Result::fail ("Missing, linked or oversized waveform recipe: " + recipe.getFullPathName ());
                if (const auto checked { fingerprint (recipe, wave.recipe, cancel) }; checked.failed ()) return checked;
                WaveformDesignRecall::RecalledDesign recalled, verified;
                if (const auto loaded { WaveformDesignRecall::loadRecipe (recipe, recalled, &wave.document) }; loaded.failed ()) return loaded;
                const auto recall { WaveformDesignRecall::recallWave (file, verified) };
                if (recall.failed ())
                {
                    auto input { file.createInputStream () };
                    if (! wave.addCvTag || file.getFileName () != "voice-01.wav" || recipe.getFileName () != "design.json"
                        || recalled.settings.mode != WaveformDesign::Mode::modulation || ! input
                        || ! WaveformDesignRecall::detail::boundedWave (*input, std::llround (recalled.settings.sampleRate * recalled.settings.durationSeconds))) return recall;
                    wave.voice = 0;
                }
                else wave.voice = verified.voiceIndex;
                // Bind each copied voice independently. Original family recipes
                // need not pull unselected voices into the bank.
                wave.document.getDynamicObject ()->removeProperty ("copiedWave");
                wave.document.getDynamicObject ()->setProperty ("displayName", recalled.displayName);
                wave.recipeSignature = juce::JSON::toString (wave.document) + ":" + juce::String (wave.voice);
            }
            return juce::Result::ok ();
        }

        juce::String collisionName (const Wave& wave, int attempt)
        {
            auto prefix { wave.input.file.getFileNameWithoutExtension ().retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-").substring (0, 18) };
            if (prefix.isEmpty ()) prefix = "sample";
            const auto identity { wave.input.hash + wave.recipeSignature + juce::String (attempt) };
            return prefix + "-" + juce::SHA256 (identity.toRawUTF8 (), identity.getNumBytesAsUTF8 ()).toHexString ().substring (0, 12)
                + (wave.hasRecipe ? "-" + juce::String (wave.voice + 1).paddedLeft ('0', 2) : juce::String ()) + ".wav";
        }
    }

    juce::Result validateDestination (const juce::File& destination)
    {
        if (destination == juce::File () || ! safeLeaf (destination.getFileName (), 31))
            return juce::Result::fail ("Use a new bank folder name of 1-31 supported characters, without a leading dot or trailing dot/space.");
        if (! directory (destination.getParentDirectory ())) return juce::Result::fail ("Choose an existing, non-linked parent folder.");
        if (occupied (destination)) return juce::Result::fail ("The bank destination already exists. Choose a new folder; nothing will be overwritten.");
        return juce::Result::ok ();
    }

    juce::Result discover (const juce::File& folder, std::vector<Candidate>& candidates, Cancel cancel)
    {
        candidates.clear ();
        if (! directory (folder)) return juce::Result::fail ("Choose an existing, non-linked source folder.");
        std::vector<Candidate> found;
        std::set<int> slots;
        int visited { 0 };
        for (const auto& item : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFilesAndDirectories))
        {
            if (stop (cancel)) return cancelled ();
            if (++visited > 32768) return juce::Result::fail ("This folder contains too many entries. Select a smaller preset folder.");
            const auto file { item.getFile () };
            const auto slot { presetSlot (file) };
            if (slot == 0) continue;
            if (! slots.insert (slot).second) return juce::Result::fail ("Preset filenames differ only by case in this source folder.");
            juce::ValueTree tree;
            if (const auto loaded { loadPreset (file, tree) }; loaded.failed ()) return loaded;
            found.push_back ({ file, slot, tree.getProperty (PresetProperties::NamePropertyId).toString () });
        }
        std::sort (found.begin (), found.end (), [] (const auto& a, const auto& b) { return a.slot < b.slot; });
        candidates = std::move (found);
        return juce::Result::ok ();
    }

    bool isManifest (const juce::File& file)
    {
        if (file.getFileName () != manifestName) return false;
        juce::String text;
        if (boundedText (file, text, maximumManifestBytes).failed ()
            || ! CvSampleSafety::detail::boundedJsonObject (text.toRawUTF8 (), static_cast<int> (text.getNumBytesAsUTF8 ()))) return false;
        const auto parsed { juce::JSON::parse (text) };
        const auto* object { parsed.getDynamicObject () };
        if (object == nullptr || object->getProperty ("format").toString () != "A8ManagerPresetBank"
            || ! object->getProperty ("version").isInt () || static_cast<int> (object->getProperty ("version")) != 1) return false;
        const auto presets { object->getProperty ("presets") };
        const auto* entries { presets.getArray () };
        if (entries == nullptr || entries->isEmpty () || entries->size () > 199) return false;
        std::set<int> slots;
        for (const auto& entry : *entries)
        {
            const auto* item { entry.getDynamicObject () };
            if (item == nullptr || ! item->getProperty ("slot").isInt () || ! item->getProperty ("name").isString ()) return false;
            const auto slot { static_cast<int> (item->getProperty ("slot")) };
            if (slot < 1 || slot > 199 || ! slots.insert (slot).second || item->getProperty ("name").toString ().length () > 1024) return false;
        }
        return true;
    }
    bool isBankFolder (const juce::File& folder) { return directory (folder) && isManifest (folder.getChildFile (manifestName)); }

    juce::Result exportBank (const std::vector<Entry>& entries, const juce::File& destination, Report& report, Cancel cancel, Progress progress)
    {
        report = {};
        if (entries.empty () || entries.size () > 199) return juce::Result::fail ("Select between 1 and 199 saved presets.");
        if (const auto valid { validateDestination (destination) }; valid.failed ()) return valid;
        if (stop (cancel)) return cancelled ();
        std::set<int> targets;
        std::vector<Preset> presets;
        std::vector<Wave> waves;
        std::vector<Midi> midis;
        std::map<juce::String, size_t> sourceWaves;
        std::set<juce::String> reservedNames;
        std::array<bool, 9> reservedMidi {};
        std::vector<Fingerprint> inputs;
        std::set<juce::String> absentInputs;
        juce::int64 recipeBytes { 0 };
        Report result;
        AudioManager audio;
        for (const auto& entry : entries)
        {
            if (stop (cancel)) return cancelled ();
            if (entry.targetSlot < 1 || entry.targetSlot > 199 || ! targets.insert (entry.targetSlot).second)
                return juce::Result::fail ("Choose a distinct destination slot (1-199) for every selected preset. Slots are never silently renumbered.");
            Preset preset;
            preset.slot = entry.targetSlot;
            if (! regular (entry.presetFile) || entry.presetFile.getSize () < 1 || entry.presetFile.getSize () > maximumTextBytes)
                return juce::Result::fail ("Missing, linked or oversized preset: " + entry.presetFile.getFullPathName ());
            if (const auto checked { fingerprint (entry.presetFile, preset.input, cancel) }; checked.failed ()) return checked;
            if (const auto loaded { loadPreset (entry.presetFile, preset.tree) }; loaded.failed ()) return loaded;
            preset.tree.setProperty (PresetProperties::IdPropertyId, entry.targetSlot, nullptr);
            preset.midi = static_cast<int> (preset.tree.getProperty (PresetProperties::MidiSetpPropertyId));
            if (preset.midi < 0 || preset.midi > 8) return juce::Result::fail ("The preset has an invalid MIDI setup selection.");
            const auto midiFile { entry.presetFile.getSiblingFile ("midi" + juce::String (preset.midi + 1) + ".yml") };
            if (! occupied (midiFile))
            {
                if (preset.midi != 0) return juce::Result::fail ("Missing selected MIDI setup: " + midiFile.getFullPathName ());
                reservedMidi[0] = true;
                absentInputs.insert (midiFile.getFullPathName ());
                result.warnings.addIfNotAlreadyThere ("Some presets have no midi1.yml. Slot 1 remains absent so it uses the module's default; no other source's setup is assigned there.");
            }
            presets.push_back (std::move (preset));
        }
        for (auto& preset : presets)
        {
            if (stop (cancel)) return cancelled ();
            if (progress) progress (0.15 * static_cast<double> (&preset - presets.data ()) / presets.size (), "Checking " + preset.input.file.getFileName ());
            inputs.push_back (preset.input);
            const auto source { preset.input.file.getParentDirectory () };
            const auto midiFile { source.getChildFile ("midi" + juce::String (preset.midi + 1) + ".yml") };
            if (occupied (midiFile))
            {
                Midi midi;
                if (! regular (midiFile) || midiFile.getSize () < 1 || midiFile.getSize () > maximumTextBytes)
                    return juce::Result::fail ("Missing, linked or oversized MIDI setup: " + midiFile.getFullPathName ());
                if (const auto checked { fingerprint (midiFile, midi.input, cancel) }; checked.failed ()) return checked;
                juce::String midiText;
                if (const auto read { boundedText (midiFile, midiText) }; read.failed ()) return read;
                MidiSetupFile document;
                document.parse (juce::StringArray::fromLines (midiText));
                if (document.getParseResult ().failed ()) return document.getParseResult ();
                inputs.push_back (midi.input);
                auto found { std::find_if (midis.begin (), midis.end (), [&] (const auto& other) { return sameContent (midi.input, other.input); }) };
                if (found != midis.end ()) preset.midi = found->slot;
                else
                {
                    auto slot { preset.midi };
                    if (reservedMidi[static_cast<size_t> (slot)])
                    {
                        slot = 0;
                        while (slot < 9 && reservedMidi[static_cast<size_t> (slot)]) ++slot;
                    }
                    if (slot == 9) return juce::Result::fail ("The selected presets require more than nine distinct/default MIDI setups. Export fewer conflicting setups in this bank.");
                    if (slot != preset.midi) result.warnings.add (midiFile.getFileName () + " from " + source.getFileName () + " copied to midi" + juce::String (slot + 1) + ".yml; preset selection updated.");
                    midi.slot = slot;
                    reservedMidi[static_cast<size_t> (slot)] = true;
                    preset.midi = slot;
                    midis.push_back (std::move (midi));
                }
            }
            preset.tree.setProperty (PresetProperties::MidiSetpPropertyId, preset.midi, nullptr);
            for (auto channel : preset.tree)
                for (auto zone : channel)
                {
                    const auto filename { zone.getProperty (ZoneProperties::SamplePropertyId).toString () };
                    if (filename.isEmpty ()) continue;
                    if (stop (cancel)) return cancelled ();
                    if (! safeLeaf (filename) || ! filename.endsWithIgnoreCase (".wav")) return juce::Result::fail ("Unsafe sample reference: " + filename);
                    const auto file { source.getChildFile (filename) };
                    const auto key { file.getFullPathName () };
                    if (const auto found { sourceWaves.find (key) }; found != sourceWaves.end ())
                    {
                        zone.setProperty (ZoneProperties::SamplePropertyId, waves[found->second].outputName, nullptr);
                        continue;
                    }
                    Wave wave;
                    if (const auto checked { inspectWave (audio, file, wave, cancel) }; checked.failed ()) return checked;
                    inputs.push_back (wave.input);
                    if (wave.hasRecipe)
                    {
                        inputs.push_back (wave.recipe);
                        recipeBytes += wave.recipe.size;
                        if (recipeBytes > 16 * 1024 * 1024) return juce::Result::fail ("Selected waveform recipes exceed the 16 MiB metadata budget. Export a smaller bank.");
                    }
                    for (const auto& absent : wave.absentMetadata) absentInputs.insert (absent);
                    const auto equivalent { std::find_if (waves.begin (), waves.end (), [&] (const auto& other)
                    {
                        return other.input.file.getFileName ().equalsIgnoreCase (filename) && sameContent (wave.input, other.input)
                            && wave.cv == other.cv && wave.recipeSignature == other.recipeSignature;
                    }) };
                    if (equivalent != waves.end ())
                    {
                        sourceWaves[key] = static_cast<size_t> (equivalent - waves.begin ());
                        zone.setProperty (ZoneProperties::SamplePropertyId, equivalent->outputName, nullptr);
                        continue;
                    }
                    wave.outputName = filename;
                    juce::StringArray names;
                    for (int attempt { 0 }; ; ++attempt)
                    {
                        if (attempt > 1000) return juce::Result::fail ("Cannot reserve a unique sample/recipe name.");
                        const auto output { destination.getChildFile (wave.outputName) };
                        names = { wave.outputName.toLowerCase (), WaveformDesignRecall::copiedRecipe (output).getFileName ().toLowerCase () };
                        const auto inherited { WaveformDesignRecall::adjacentRecipe (output) };
                        // A generated copy always has its explicit per-WAV alias,
                        // so a shared family fallback cannot affect its recall.
                        // Untagged/non-generated samples reserve that fallback too
                        // to prevent accidentally acquiring another wave's recipe.
                        if (! wave.hasRecipe && inherited != juce::File ()) names.addIfNotAlreadyThere (inherited.getFileName ().toLowerCase ());
                        if (std::none_of (names.begin (), names.end (), [&] (const auto& name) { return reservedNames.count (name) != 0; })) break;
                        wave.outputName = collisionName (wave, attempt);
                    }
                    for (const auto& name : names) reservedNames.insert (name);
                    if (wave.outputName != filename) ++result.renamedWaves;
                    if (wave.ram > maximumRam - result.ramBytes) return juce::Result::fail ("Referenced bank samples exceed Assimil8or's 422 MiB RAM capacity. Export a smaller bank.");
                    result.ramBytes += wave.ram;
                    sourceWaves[key] = waves.size ();
                    zone.setProperty (ZoneProperties::SamplePropertyId, wave.outputName, nullptr);
                    waves.push_back (std::move (wave));
                }
        }

        const auto stage { destination.getSiblingFile (".a8-bank-stage-" + juce::Uuid ().toString ()) };
        std::error_code error;
        if (! std::filesystem::create_directory (path (stage), error) || error) return juce::Result::fail ("Cannot create the private bank staging folder.");
        struct Cleanup { juce::File folder; ~Cleanup () { if (folder.exists ()) folder.deleteRecursively (); } } cleanup { stage };
        for (size_t index { 0 }; index < waves.size (); ++index)
        {
            if (stop (cancel)) return cancelled ();
            const auto& wave { waves[index] };
            if (progress) progress (0.15 + 0.6 * static_cast<double> (index) / std::max (size_t { 1 }, waves.size ()), "Copying " + wave.outputName);
            const auto output { stage.getChildFile (wave.outputName) };
            if (const auto copied { wave.addCvTag ? SafeAudioImport::stageWave (audio, wave.input.file, output)
                                                : copyNew (wave.input.file, output, cancel) }; copied.failed ()) return copied;
            Fingerprint staged;
            if (const auto checked { fingerprint (output, staged, cancel) }; checked.failed ()) return checked;
            if (! wave.addCvTag && ! sameContent (wave.input, staged)) return juce::Result::fail ("Copied WAV does not match its source.");
            if (wave.hasRecipe)
            {
                auto document { wave.document.clone () };
                juce::DynamicObject::Ptr binding { new juce::DynamicObject };
                binding->setProperty ("filename", wave.outputName);
                binding->setProperty ("voiceIndex", wave.voice);
                binding->setProperty ("sha256", staged.hash);
                document.getDynamicObject ()->setProperty ("copiedWave", juce::var (binding.get ()));
                if (const auto written { WaveformDesign::ExportSupport::writeText (WaveformDesignRecall::copiedRecipe (output), juce::JSON::toString (document) + "\n") }; written.failed ()) return written;
                WaveformDesignRecall::RecalledDesign recalled;
                if (const auto checked { WaveformDesignRecall::recallWave (output, recalled) }; checked.failed ()) return checked;
            }
        }
        for (const auto& midi : midis)
        {
            if (stop (cancel)) return cancelled ();
            const auto output { stage.getChildFile ("midi" + juce::String (midi.slot + 1) + ".yml") };
            if (const auto copied { copyNew (midi.input.file, output, cancel) }; copied.failed ()) return copied;
            Fingerprint staged;
            if (const auto checked { fingerprint (output, staged, cancel) }; checked.failed ()) return checked;
            if (! sameContent (midi.input, staged)) return juce::Result::fail ("Copied MIDI setup changed during export.");
        }
        juce::Array<juce::var> inventory;
        for (auto& preset : presets)
        {
            if (stop (cancel)) return cancelled ();
            if (const auto safety { ChannelCvSafety::validatePreset (preset.tree, stage) }; safety.failed ()) return safety;
            const auto output { stage.getChildFile ("prst" + juce::String (preset.slot).paddedLeft ('0', 3) + ".yml") };
            Assimil8orPreset writer;
            if (const auto written { writer.write (output, preset.tree) }; written.failed ()) return written;
            juce::ValueTree readback;
            if (const auto checked { loadPreset (output, readback) }; checked.failed ()) return checked;
            if (! PresetHelpers::areEntirePresetsEqual (preset.tree, readback)) return juce::Result::fail ("Preset read-back did not preserve its settings.");
            juce::DynamicObject::Ptr item { new juce::DynamicObject };
            item->setProperty ("slot", preset.slot);
            item->setProperty ("name", preset.tree.getProperty (PresetProperties::NamePropertyId).toString ());
            inventory.add (juce::var (item.get ()));
        }
        juce::DynamicObject::Ptr manifest { new juce::DynamicObject };
        manifest->setProperty ("format", "A8ManagerPresetBank");
        manifest->setProperty ("version", 1);
        manifest->setProperty ("presets", inventory);
        const auto manifestText { juce::JSON::toString (juce::var (manifest.get ())) + "\n" };
        if (manifestText.getNumBytesAsUTF8 () > maximumManifestBytes) return juce::Result::fail ("Bank ownership record is too large.");
        if (const auto written { WaveformDesign::ExportSupport::writeText (stage.getChildFile (manifestName), manifestText) }; written.failed ()) return written;
        if (! isBankFolder (stage)) return juce::Result::fail ("Bank ownership record did not validate.");
        for (size_t index { 0 }; index < inputs.size (); ++index)
        {
            if (stop (cancel)) return cancelled ();
            if (progress) progress (0.75 + 0.24 * static_cast<double> (index) / std::max (size_t { 1 }, inputs.size ()), "Verifying source snapshot");
            if (const auto checked { unchanged (inputs[index], cancel) }; checked.failed ()) return checked;
        }
        if (stop (cancel)) return cancelled ();
        for (const auto& name : absentInputs)
            if (occupied (juce::File (name))) return juce::Result::fail ("A previously absent MIDI setup or recipe appeared during export. Please retry; originals were not changed.");
        if (const auto valid { validateDestination (destination) }; valid.failed ()) return valid;
        if (const auto published { WaveformDesign::ExportSupport::publishExclusive (stage, destination) }; published.failed ()) return published;
        result.folder = destination;
        result.presetCount = static_cast<int> (presets.size ());
        result.waveCount = static_cast<int> (waves.size ());
        result.midiCount = static_cast<int> (midis.size ());
        report = std::move (result);
        if (progress) progress (1.0, "Bank export complete");
        return juce::Result::ok ();
    }
}
