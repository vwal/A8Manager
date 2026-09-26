#include "Assimil8or/PresetFileOperations.h"
#include "Assimil8or/PresetArchive.h"
#include "Assimil8or/MidiSetup/MidiSetupFile.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void require (bool value, const char* message) { if (! value) throw std::runtime_error (message); }
    juce::ValueTree preset (juce::String name)
    {
        const auto defaults { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType) };
        auto tree { defaults.createCopy () };
        tree.setProperty (PresetProperties::NamePropertyId, name, nullptr);
        return tree;
    }
    void writeAudio (juce::File file, float value)
    {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream { file.createOutputStream () };
        auto writer { format.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (48000).withNumChannels (1).withBitsPerSample (16)) };
        juce::AudioBuffer<float> data (1, 64);
        for (auto i { 0 }; i < 64; ++i) data.setSample (0, i, value);
        require (writer != nullptr && writer->writeFromAudioSampleBuffer (data, 0, 64), "Write isolated WAV fixture");
    }
    void archive (juce::File output, const std::vector<std::pair<juce::File, juce::String>>& files)
    {
        juce::ZipFile::Builder builder;
        for (const auto& [file, name] : files) builder.addFile (file, 5, name);
        auto stream { output.createOutputStream () };
        require (stream != nullptr && builder.writeToStream (*stream, nullptr), "Write isolated ZIP fixture");
    }
    void testSave (juce::File folder)
    {
        auto baseline { preset ("Before") }, edited { preset ("After") };
        const auto blocked { folder.getChildFile ("prst001.yml") };
        require (blocked.createDirectory ().wasOk (), "Create directory save-failure fixture");
        require (PresetFileOperations::save (blocked, edited, baseline).failed (), "Preset save detects directory destination");
        require (baseline.getProperty (PresetProperties::NamePropertyId) == juce::var ("Before"), "Failed save preserves dirty baseline");
        const auto destination { folder.getChildFile ("prst002.yml") };
        require (PresetFileOperations::save (destination, edited, baseline).wasOk (), "Retry save succeeds");
        require (baseline.isEquivalentTo (edited), "Successful save updates baseline");
        require (static_cast<int> (edited.getProperty (PresetProperties::IdPropertyId)) == 1, "Export/save to a different filename does not mutate live ID");
        juce::ValueTree loaded;
        require (PresetFileOperations::read (destination, loaded).wasOk () && static_cast<int> (loaded.getProperty (PresetProperties::IdPropertyId)) == 2, "Written slot ID follows destination");
        MidiSetupProperties midi;
        MidiSetupFile midiFile;
        const auto midiBlocked { folder.getChildFile ("midi1.yml") };
        require (midiBlocked.createDirectory ().wasOk (), "Create MIDI failure fixture");
        require (midiFile.write (midiBlocked, midi.getValueTree ()).failed (), "MIDI save detects failure instead of closing as success");
        require (midiFile.write (folder.getChildFile ("midi2.yml"), midi.getValueTree ()).wasOk (), "MIDI save succeeds to a writable file");
    }
    void testSlots (juce::File folder)
    {
        for (auto name : { "prst000.yml", "prst200.yml", "prst999.yml", "prst-01.yml", "prst1.yml", "prst001.txt" })
            require (! FileTypeHelpers::isPresetFile (folder.getChildFile (name)), "Reject invalid preset filename/slot before indexing");
        require (FileTypeHelpers::isPresetFile (folder.getChildFile ("prst001.yml")) && FileTypeHelpers::isPresetFile (folder.getChildFile ("prst199.yml")), "Accept endpoint slots");
        PresetFileOperations::PresetInfoList rows {};
        rows[0] = {10, true, "Ten"}; rows[1] = {190, true, "Ninety"};
        require (PresetFileOperations::rowForSlot (rows, 2, 10) == 0 && PresetFileOperations::rowForSlot (rows, 2, 190) == 1, "Filtered rows map by slot, not slot-minus-one");
        require (PresetFileOperations::rowForSlot (rows, 2, 1) == -1, "Hidden slot has no visible row");
        require (PresetFileOperations::slotAfterSwap (10, 10, 9) == 9 && PresetFileOperations::slotAfterSwap (9, 10, 9) == 10 && PresetFileOperations::slotAfterSwap (8, 10, 9) == 8, "Move save binding follows the selected preset in either direction");
        const auto occupied { folder.getChildFile ("prst010.yml") }, empty { folder.getChildFile ("prst011.yml") };
        Assimil8orPreset writer;
        require (writer.write (occupied, preset ("Ten")).wasOk (), "Write paste target fixture");
        require (PresetFileOperations::needsOverwriteConfirmation (occupied) && ! PresetFileOperations::needsOverwriteConfirmation (empty), "Paste confirmation is based on destination existence, not selected row");
    }
    void testMoves (juce::File folder)
    {
        const auto from { folder.getChildFile ("prst030.yml") }, to { folder.getChildFile ("prst031.yml") };
        Assimil8orPreset writer;
        require (writer.write (from, preset ("Source")).wasOk () && writer.write (to, preset ("Target")).wasOk (), "Create move fixtures");
        const auto beforeFrom { from.loadFileAsString () }, beforeTo { to.loadFileAsString () };
        auto calls { 0 };
        auto result { PresetFileOperations::swap (folder, 30, 31, [&] (juce::File source, juce::File target)
        {
            if (++calls == 2) return false;
            return source.replaceFileIn (target);
        }) };
        require (result.failed () && from.loadFileAsString () == beforeFrom && to.loadFileAsString () == beforeTo, "Second-install failure rolls both original files back");
        require (PresetFileOperations::swap (folder, 30, 31).wasOk (), "Swap succeeds");
        juce::ValueTree tree;
        require (PresetFileOperations::read (to, tree).wasOk () && tree.getProperty (PresetProperties::NamePropertyId) == juce::var ("Source") && static_cast<int> (tree.getProperty (PresetProperties::IdPropertyId)) == 31, "Swapped source carries destination ID");
        require (PresetFileOperations::swap (folder, 31, 32).wasOk () && ! to.exists () && folder.getChildFile ("prst032.yml").existsAsFile (), "Move into empty slot succeeds");
        require (PresetFileOperations::swap (folder, 0, 1).failed () && PresetFileOperations::swap (folder, 199, 200).failed (), "Invalid move slots fail safely");
    }
    void testDiscardGuard ()
    {
        auto baseline { preset ("Clean") }, edited { preset ("Dirty") };
        auto asked { false }, applied { false }, cancelled { false };
        PresetFileOperations::guardedChange (edited, baseline,
            [&] (std::function<void ()>, std::function<void ()> cancel) { asked = true; cancel (); },
            [&] () { applied = true; }, [&] () { cancelled = true; });
        require (asked && cancelled && ! applied, "Cancelling dirty guard does not import or move");
        PresetFileOperations::guardedChange (edited, baseline,
            [&] (std::function<void ()> proceed, std::function<void ()>) { proceed (); },
            [&] () { applied = true; }, [] () {});
        require (applied, "Confirmed dirty guard applies operation");
        asked = false; applied = false;
        PresetFileOperations::guardedChange (baseline, baseline,
            [&] (std::function<void ()>, std::function<void ()>) { asked = true; },
            [&] () { applied = true; }, [] () {});
        require (! asked && applied, "Clean preset does not need confirmation");
    }
    void testArchives (juce::File folder)
    {
        const auto sourceFolder { folder.getChildFile ("zip-source") }, target { folder.getChildFile ("zip-target") };
        require (sourceFolder.createDirectory ().wasOk () && target.createDirectory ().wasOk (), "Create isolated archive directories");
        const auto wave { sourceFolder.getChildFile ("sample.wav") }, presetFile { sourceFolder.getChildFile ("sound.yml") };
        writeAudio (wave, .25f);
        auto tree { preset ("Archived") };
        tree.getChild (0).getChild (0).setProperty (ZoneProperties::SamplePropertyId, "sample.wav", nullptr);
        Assimil8orPreset writer;
        require (writer.write (presetFile, tree).wasOk (), "Write archived preset");
        const auto validZip { folder.getChildFile ("valid.zip") };
        archive (validZip, {{presetFile, "sound.yml"}, {wave, "sample.wav"}});
        juce::ValueTree imported;
        require (PresetArchive::importPreset (validZip, target, imported).wasOk () && imported.isValid (), "Import valid archive");
        require (PresetArchive::sameContents (wave, target.getChildFile ("sample.wav")), "Imported sample preserves bytes");
        require (PresetArchive::importPreset (validZip, target, imported).wasOk (), "Reuse identical existing sample");
        const auto collisionTarget { folder.getChildFile ("collision-target") };
        require (collisionTarget.createDirectory ().wasOk (), "Create collision fixture");
        writeAudio (collisionTarget.getChildFile ("sample.wav"), -.5f);
        const auto oldSize { collisionTarget.getChildFile ("sample.wav").getSize () };
        require (PresetArchive::importPreset (validZip, collisionTarget, imported).failed () && ! imported.isValid (), "Reject differing collision before changing editor");
        require (collisionTarget.getChildFile ("sample.wav").getSize () == oldSize && ! PresetArchive::sameContents (wave, collisionTarget.getChildFile ("sample.wav")), "Collision bytes remain unchanged");
        require (PresetArchive::copyNew (wave, collisionTarget.getChildFile ("sample.wav")).failed () &&
                 ! PresetArchive::sameContents (wave, collisionTarget.getChildFile ("sample.wav")), "Final no-overwrite copy also rejects a target that appears after preflight");
        const auto duplicateZip { folder.getChildFile ("duplicate.zip") };
        archive (duplicateZip, {{presetFile, "one.yml"}, {presetFile, "two.yml"}, {wave, "sample.wav"}});
        require (PresetArchive::importPreset (duplicateZip, target, imported).failed (), "Reject ambiguous multi-preset archive");
        const auto unsafeZip { folder.getChildFile ("unsafe.zip") };
        archive (unsafeZip, {{presetFile, "sound.yml"}, {wave, "../escape.wav"}});
        require (PresetArchive::importPreset (unsafeZip, target, imported).failed () && ! folder.getChildFile ("escape.wav").exists (), "Reject traversal before extracting into destination");
        const auto malformed { sourceFolder.getChildFile ("invalid.yml") };
        require (malformed.replaceWithText ("not a preset"), "Write malformed preset fixture");
        const auto invalidZip { folder.getChildFile ("invalid.zip") };
        archive (invalidZip, {{malformed, "invalid.yml"}, {wave, "sample.wav"}});
        require (PresetArchive::importPreset (invalidZip, target, imported).failed (), "Reject malformed preset");
    }
}

int runPresetWorkflowAuditTests ()
{
    try
    {
        const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-preset-workflow-regression", "", false) };
        require (folder.createDirectory ().wasOk (), "Create test sandbox");
        struct Cleanup { juce::File directory; ~Cleanup () { directory.deleteRecursively (); } } cleanup { folder };
        testSave (folder); testSlots (folder); testMoves (folder); testDiscardGuard (); testArchives (folder);
        std::cout << "Preset workflow audit regression passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Preset workflow audit regression FAILED: " << error.what () << '\n';
        return 1;
    }
}
