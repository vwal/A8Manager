#include "Assimil8or/PresetFolderCopy.h"
#include "Assimil8or/PresetFileOperations.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "Assimil8or/Audio/WaveformDesignAssignment.h"
#include "Assimil8or/Audio/WaveformDesignRecall.h"
#include <juce_cryptography/juce_cryptography.h>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

namespace
{
    void require (bool okay, const char* message) { if (! okay) throw std::runtime_error (message); }

    void succeeded (const juce::Result& result)
    {
        if (result.failed ()) throw std::runtime_error (result.getErrorMessage ().toStdString ());
    }

    juce::ValueTree defaults ()
    {
        return ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ()
            .getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType).createCopy ();
    }

    ChannelProperties channel (juce::ValueTree tree, int index)
    {
        return ChannelProperties (tree.getChild (index), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
    }

    ZoneProperties zone (juce::ValueTree tree, int index, int zoneIndex = 0)
    {
        return ZoneProperties (tree.getChild (index).getChild (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
    }

    void stereo (juce::File file)
    {
        juce::AudioBuffer<float> samples (2, 128);
        for (int frame { 0 }; frame < 128; ++frame)
        {
            samples.setSample (0, frame, static_cast<float> (frame) / 256.0f);
            samples.setSample (1, frame, -static_cast<float> (frame) / 256.0f);
        }
        std::unique_ptr<juce::OutputStream> stream { file.createOutputStream () };
        juce::WavAudioFormat format;
        auto writer { format.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (48000)
                                              .withNumChannels (2).withBitsPerSample (24)) };
        require (writer && writer->writeFromAudioSampleBuffer (samples, 0, 128), "Write stereo copy fixture");
    }

    std::map<juce::String, juce::String> fingerprints (juce::File folder)
    {
        std::map<juce::String, juce::String> files;
        for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFiles))
            files[entry.getFile ().getFileName ()] = juce::SHA256 (entry.getFile ()).toHexString ();
        return files;
    }
}

void testPresetFolderCopy ()
{
    using namespace WaveformDesign;
    for (int slot { 1 }; slot <= 199; ++slot)
    {
        const auto prefix { "PR" + juce::String (slot).paddedLeft ('0', 2) + " - " };
        require (PresetFolderCopy::folderName (slot, "koe-01") == prefix + "koe-01", "Folder slots have at least two digits, not a hard-coded three digits");
        const auto longName { PresetFolderCopy::folderName (slot, "A very long preset with /slashes: emoji and punctuation") };
        require (longName.startsWith (prefix) && longName.length () <= 31 && ! longName.containsAnyOf ("/\\:"), "Folder name is truncated/sanitized within A8's 31-character limit");
        require (PresetFolderCopy::folderName (slot, juce::String::repeatedString ("x", 40)).length () == 31,
                 "Names use all remaining characters after the shortened prefix");
        auto numbered { defaults () };
        numbered.setProperty (PresetProperties::IdPropertyId, slot, nullptr);
        const auto base { juce::File::getSpecialLocation (juce::File::tempDirectory) };
        require (PresetFolderCopy::isNamedPresetFolder (base.getChildFile (prefix + "First Wave"), numbered), "Recognize every short-prefix preset slot");
        require (PresetFolderCopy::isNamedPresetFolder (base.getChildFile ("A8 Preset " + juce::String (slot).paddedLeft ('0', 2) + " - First Wave"), numbered),
                 "Recognize every legacy-prefix preset slot without renaming folders");
    }
    require (PresetFolderCopy::folderName (1, "First Wave") == "PR01 - First Wave", "User-facing short preset folder example");
    require (PresetFolderCopy::folderName (0, "invalid").isEmpty () && PresetFolderCopy::folderName (200, "invalid").isEmpty (), "Reject invalid preset numbers");
    require (PresetFolderCopy::folderName (1, juce::String::fromUTF8 ("\xe6\xb3\xa2\xe5\xbd\xa2")) == "PR01 - Untitled", "Non-ASCII-only names have a safe nonempty folder fallback");
    auto firstSlot { defaults () };
    firstSlot.setProperty (PresetProperties::IdPropertyId, 1, nullptr);
    for (const auto* invalidName : { "PR1 - Name", "PR001 - Name", "PR00 - Name", "PR200 - Name", "PR01 Name", "PR02 - Other", "A8 Preset 1 - Name", "A8 Preset 02 - Other" })
        require (! PresetFolderCopy::isNamedPresetFolder (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (invalidName), firstSlot),
                 "Malformed or different-slot folder names are not treated as the current named preset");

    const auto root { juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("a8-folder-copy-test-" + juce::Uuid ().toString ()) };
    succeeded (root.createDirectory ());
    struct Cleanup { juce::File file; ~Cleanup () { file.deleteRecursively (); } } cleanup { root };
    const auto source { root.getChildFile ("working") };
    succeeded (source.createDirectory ());
    auto initial { defaults () };
    PresetProperties preset (initial, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
    preset.setId (9, false);
    preset.setName ("koe-01", false);
    auto bank { startingPoint (Mode::layers, Shape::saw) };
    bank.cycleFrames = 128;
    spreadVoices (bank, 3, 19, 137, 0.6);
    AssignmentResult assigned;
    succeeded (prepareAssignment (bank, source, "copy-bank", initial, 0, 0, assigned));
    auto tree { assigned.editedPreset };
    zone (tree, 1).setSample ("", false); // Recall does not require all original bank voices.
    auto cv { startingPoint (Mode::modulation, Shape::sine) };
    cv.durationSeconds = 0.01;
    AssignmentResult assignedCv;
    succeeded (prepareAssignment (cv, source, "copy-cv", tree, 5, 0, assignedCv));
    tree = assignedCv.editedPreset;
    stereo (source.getChildFile ("stereo.wav"));
    for (int index : { 3, 4 })
    {
        zone (tree, index).setSample ("stereo.wav", false);
        zone (tree, index).setSide (index - 3, false);
    }
    channel (tree, 4).setChannelMode (ChannelProperties::stereoRight, false);
    channel (tree, 7).setPitch (12.25, false); // Empty channels retain settings.
    zone (tree, 0).setPitchOffset (-3.0, false);
    require (source.getChildFile ("midi1.yml").replaceWithText ("mode : 0\nassign : 0\nbasicchannel : 3\n"), "Create selected MIDI fixture");
    require (source.getChildFile ("prst009.yml").replaceWithText ("original saved preset is left untouched"), "Create original preset preservation fixture");
    require (source.getChildFile ("unrelated.txt").replaceWithText ("Keep me"), "Create unrelated source fixture");
    const auto originals { fingerprints (source) };

    juce::File output;
    succeeded (PresetFolderCopy::createOrUpdate (source, tree, output));
    require (output == source.getChildFile ("PR09 - koe-01"), "Publish short named child folder");
    require (originals == fingerprints (source), "All original/source files remain byte-identical");
    require (! output.getChildFile ("unrelated.txt").exists () && ! output.getChildFile (assigned.waves[1].getFileName ()).exists (), "Copy only dependencies, not unrelated or unreferenced bank voices");
    require (output.getChildFile (assigned.recipe.getFileName ()).existsAsFile () && output.getChildFile (assignedCv.recipe.getFileName ()).existsAsFile (), "Copy matching assignment recipes for audio and CV");
    require (output.getChildFile ("midi1.yml").loadFileAsString () == source.getChildFile ("midi1.yml").loadFileAsString (), "Copy selected MIDI setup byte-for-byte");
    require (juce::SHA256 (output.getChildFile ("stereo.wav")) == juce::SHA256 (source.getChildFile ("stereo.wav")), "Stereo shared sample copied without conversion");
    require (PresetFolderCopy::isManifest (output.getChildFile (".a8-preset-copy.json")), "Ownership metadata is recognized for desktop-only validation");
    WaveformDesignRecall::RecalledDesign recalled;
    succeeded (WaveformDesignRecall::recallWave (output.getChildFile (assigned.waves[2].getFileName ()), recalled));
    require (recalled.settings.voiceCount == 3, "One assigned bank voice plus recipe recalls the complete bank");
    juce::ValueTree loaded;
    succeeded (PresetFileOperations::read (output.getChildFile ("prst009.yml"), loaded));
    require (channel (loaded, 7).getPitch () == 12.25 && zone (loaded, 0).getPitchOffset () == -3.0, "Persist unsaved channel and zone pitches including empty channels");
    require (channel (loaded, 4).getChannelMode () == ChannelProperties::stereoRight && zone (loaded, 4).getSide () == 1, "Preserve stereo channel/zone assignments");
    require (channel (loaded, 5).getMixLevel () == -90 && std::get<0> (channel (loaded, 5).getMixMod ()) == "Off", "CV routing stays Mix Off");
    require (PresetFolderCopy::isNamedPresetFolder (output, tree), "Recognize already-open copy to avoid nesting");
    auto renamed { tree.createCopy () };
    renamed.setProperty (PresetProperties::NamePropertyId, "Renamed", nullptr);
    require (PresetFolderCopy::isNamedPresetFolder (output, renamed), "A rename while editing the named folder does not wrap it again");
    juce::File invalidOutput;
    require (PresetFolderCopy::createOrUpdate (output, tree, invalidOutput).failed () && invalidOutput == juce::File (), "Backend never silently rewrites an already-open source package");

    require (output.getChildFile (".DS_Store").replaceWithText ("Finder cache"), "Create normal Finder folder metadata");
    channel (tree, 7).setPitch (17.0, false);
    juce::File repeated;
    succeeded (PresetFolderCopy::createOrUpdate (source, tree, repeated));
    require (repeated == output && output.getChildFile (".DS_Store").loadFileAsString () == "Finder cache", "Repeat save updates owned folder and preserves Finder metadata");
    succeeded (PresetFileOperations::read (output.getChildFile ("prst009.yml"), loaded));
    require (channel (loaded, 7).getPitch () == 17.0 && originals == fingerprints (source), "Repeat save stores fresh snapshot and leaves originals unchanged");

    const auto beforeFailures { fingerprints (output) };
    auto missing { tree.createCopy () };
    zone (missing, 0).setSample ("missing.wav", false);
    require (PresetFolderCopy::createOrUpdate (source, missing, invalidOutput).failed () && fingerprints (output) == beforeFailures, "Missing sample fails without replacing existing output");
    auto unsafe { tree.createCopy () };
    channel (unsafe, 5).setMixLevel (0, false);
    require (PresetFolderCopy::createOrUpdate (source, unsafe, invalidOutput).failed () && fingerprints (output) == beforeFailures, "Unsafe CV mix fails before replacing output");
    zone (missing, 0).setSample ("../escape.wav", false);
    require (PresetFolderCopy::createOrUpdate (source, missing, invalidOutput).failed (), "Reject traversing references");
    auto invalid { tree.createCopy () };
    // The existing parser validates finite numeric syntax, not all hardware
    // parameter ranges. A nonfinite value tests its actual save contract.
    channel (invalid, 7).setPitch (std::numeric_limits<double>::quiet_NaN (), false);
    require (PresetFolderCopy::createOrUpdate (source, invalid, invalidOutput).failed () && fingerprints (output) == beforeFailures, "Invalid serialized values cannot replace good output");

    require (output.getChildFile ("private-notes.txt").replaceWithText ("external user content"), "Create unrelated output fixture");
    require (PresetFolderCopy::createOrUpdate (source, tree, invalidOutput).failed () && output.getChildFile ("private-notes.txt").loadFileAsString () == "external user content", "Foreign added files are not deleted on update");
    require (output.getChildFile ("private-notes.txt").deleteFile (), "Remove owned test note");
    require (output.getChildFile ("prst009.yml").appendText ("\n# Hardware edit\n"), "Simulate external/hardware preset edit");
    const auto external { fingerprints (output) };
    require (PresetFolderCopy::createOrUpdate (source, tree, invalidOutput).failed () && external == fingerprints (output), "Externally edited copied presets are not overwritten");

    auto other { tree.createCopy () };
    other.setProperty (PresetProperties::NamePropertyId, "Foreign", nullptr);
    const auto foreign { source.getChildFile (PresetFolderCopy::folderName (9, "Foreign")) };
    succeeded (foreign.createDirectory ());
    require (foreign.getChildFile ("private.txt").replaceWithText ("not ours"), "Create foreign collision fixture");
    require (PresetFolderCopy::createOrUpdate (source, other, invalidOutput).failed () && foreign.getChildFile ("private.txt").loadFileAsString () == "not ours", "Refuse an unowned folder with the desired name");

    const juce::String firstName { "A very long same prefix shared FIRST" }, secondName { "A very long same prefix shared SECOND" };
    other.setProperty (PresetProperties::NamePropertyId, firstName, nullptr);
    juce::File truncated;
    succeeded (PresetFolderCopy::createOrUpdate (source, other, truncated));
    other.setProperty (PresetProperties::NamePropertyId, secondName, nullptr);
    require (PresetFolderCopy::folderName (9, firstName) == PresetFolderCopy::folderName (9, secondName), "Truncation collision fixture");
    const auto truncatedBefore { fingerprints (truncated) };
    require (PresetFolderCopy::createOrUpdate (source, other, invalidOutput).failed () && truncatedBefore == fingerprints (truncated), "Full untruncated name in manifest prevents rename collisions");

    // Package-export naming (voice-NN.wav + design.json) also recalls after copy.
    const auto packageSource { root.getChildFile ("package-source") };
    succeeded (packageSource.createDirectory ());
    auto packageTree { defaults () };
    packageTree.setProperty (PresetProperties::NamePropertyId, "Package", nullptr);
    require (assigned.waves[0].copyFileTo (packageSource.getChildFile ("voice-01.wav"))
        && assigned.recipe.copyFileTo (packageSource.getChildFile ("design.json")), "Create package recipe fixture");
    zone (packageTree, 0).setSample ("voice-01.wav", false);
    juce::File packageCopy;
    succeeded (PresetFolderCopy::createOrUpdate (packageSource, packageTree, packageCopy));
    succeeded (WaveformDesignRecall::recallWave (packageCopy.getChildFile ("voice-01.wav"), recalled));
    require (packageCopy.getChildFile ("design.json").existsAsFile (), "Standalone recipe copied with package-named sample");
    auto secondSlot { packageTree.createCopy () };
    secondSlot.setProperty (PresetProperties::IdPropertyId, 2, nullptr);
    secondSlot.setProperty (PresetProperties::NamePropertyId, "Second", nullptr);
    secondSlot.setProperty (PresetProperties::MidiSetpPropertyId, 8, nullptr);
    require (packageCopy.getChildFile ("midi9.yml").replaceWithText ("mode : 0\nassign : 0\nbasicchannel : 7\n"), "Create highest MIDI setup fixture");
    const auto firstSlotFiles { fingerprints (packageCopy) };
    juce::File sibling;
    succeeded (PresetFolderCopy::createOrUpdate (packageCopy, secondSlot, sibling));
    require (sibling == packageCopy.getSiblingFile ("PR02 - Second") && fingerprints (packageCopy) == firstSlotFiles,
             "Saving another slot while inside a named preset creates a sibling, not an invisible nested folder");
    succeeded (PresetFileOperations::read (sibling.getChildFile ("prst002.yml"), loaded));
    require (static_cast<int> (loaded.getProperty (PresetProperties::IdPropertyId)) == 2, "New sibling slot uses its own preset filename and ID");
    require (sibling.getChildFile ("midi9.yml").existsAsFile () && static_cast<int> (loaded.getProperty (PresetProperties::MidiSetpPropertyId)) == 8,
             "Zero-based preset MIDI index eight copies one-based file midi9.yml");

    const auto legacyCopy { root.getChildFile ("A8 Preset 01 - Legacy") };
    require (packageCopy.copyDirectoryTo (legacyCopy), "Create legacy named-folder fixture");
    const auto legacyFiles { fingerprints (legacyCopy) };
    require (PresetFolderCopy::isNamedPresetFolder (legacyCopy, packageTree)
        && PresetFolderCopy::createOrUpdate (legacyCopy, packageTree, invalidOutput).failed (),
        "Already-open legacy folders remain in-place saves without nesting or renaming");
    juce::File legacySibling;
    succeeded (PresetFolderCopy::createOrUpdate (legacyCopy, secondSlot, legacySibling));
    require (legacySibling == legacyCopy.getSiblingFile ("PR02 - Second") && fingerprints (legacyCopy) == legacyFiles,
             "Other slots opened inside a legacy folder create short-prefix siblings and preserve legacy files");

#if ! JUCE_WINDOWS
    const auto link { packageSource.getChildFile ("linked.wav") };
    std::error_code error;
    std::filesystem::create_symlink (std::filesystem::path (reinterpret_cast<const char8_t*> (assigned.waves[0].getFullPathName ().toRawUTF8 ())),
                                     std::filesystem::path (reinterpret_cast<const char8_t*> (link.getFullPathName ().toRawUTF8 ())), error);
    require (! error, "Create sample symlink test fixture");
    zone (packageTree, 0).setSample ("linked.wav", false);
    packageTree.setProperty (PresetProperties::NamePropertyId, "Link", nullptr);
    require (PresetFolderCopy::createOrUpdate (packageSource, packageTree, invalidOutput).failed (), "Reject symlink dependencies before reading them");
#endif
    require (originals == fingerprints (source), "All original files survive success and failure cases unchanged");
    std::cout << "Preset folder copy regression passed\n";
}
