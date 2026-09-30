#include "Assimil8or/PresetBankExport.h"
#include "Assimil8or/PresetFileOperations.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "Assimil8or/Audio/WaveformDesignExport.h"
#include "Assimil8or/Audio/WaveformDesignRecall.h"
#include "Assimil8or/MidiSetup/MidiSetupFile.h"
#include <juce_cryptography/juce_cryptography.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>

namespace
{
    void require (bool condition, const char* message) { if (! condition) throw std::runtime_error (message); }
    void success (const juce::Result& result) { if (result.failed ()) throw std::runtime_error (result.getErrorMessage ().toStdString ()); }
    juce::ValueTree defaults ()
    {
        return ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ()
            .getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType).createCopy ();
    }
    ZoneProperties zone (juce::ValueTree tree, int channel = 0, int index = 0)
    {
        return ZoneProperties (tree.getChild (channel).getChild (index), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
    }
    ChannelProperties channel (juce::ValueTree tree, int index = 0)
    {
        return ChannelProperties (tree.getChild (index), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
    }
    juce::ValueTree read (juce::File file)
    {
        juce::ValueTree tree;
        success (PresetFileOperations::read (file, tree));
        return tree;
    }
    juce::File makeFolder (juce::File parent, const juce::String& name)
    {
        const auto folder { parent.getChildFile (name) };
        success (folder.createDirectory ());
        return folder;
    }
    void wave (juce::File file, float value = 0.1f, bool cv = false)
    {
        juce::AudioBuffer<float> samples (1, 64);
        for (int frame { 0 }; frame < 64; ++frame) samples.setSample (0, frame, value);
        success (WaveformDesign::ExportSupport::writeWave (file, samples, 48000, cv));
    }
    juce::File preset (juce::File folder, const juce::String& sample = "sample.wav", int slot = 1, int midi = 0)
    {
        auto tree { defaults () };
        tree.setProperty (PresetProperties::IdPropertyId, slot, nullptr);
        tree.setProperty (PresetProperties::NamePropertyId, "Bank test " + juce::String (slot), nullptr);
        tree.setProperty (PresetProperties::MidiSetpPropertyId, midi, nullptr);
        zone (tree).setSample (sample, false);
        const auto file { folder.getChildFile ("prst" + juce::String (slot).paddedLeft ('0', 3) + ".yml") };
        Assimil8orPreset writer;
        success (writer.write (file, tree));
        return file;
    }
    std::map<juce::String, juce::String> hashes (juce::File folder)
    {
        std::map<juce::String, juce::String> result;
        for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFiles))
            result[entry.getFile ().getFileName ()] = juce::SHA256 (entry.getFile ()).toHexString ();
        return result;
    }
    void noStages (juce::File root)
    {
        for (const auto& item : juce::RangedDirectoryIterator (root, false, "*", juce::File::findDirectories))
            require (! item.getFile ().getFileName ().startsWith (".a8-bank-stage-"), "Private staging directories are cleaned after failure/cancellation");
    }
}

void testPresetBankExport ()
{
    using namespace PresetBankExport;
    const auto root { juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("a8-bank-export-test-" + juce::Uuid ().toString ()) };
    success (root.createDirectory ());
    struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { root };
    const auto first { makeFolder (root, "first") }, second { makeFolder (root, "second") }, identical { makeFolder (root, "identical") };
    wave (first.getChildFile ("sample.wav"), 0.1f);
    wave (second.getChildFile ("sample.wav"), 0.2f);
    require (second.getChildFile ("sample.wav").copyFileTo (identical.getChildFile ("sample.wav")), "Copy byte-identical collision fixture");
    const auto firstPreset { preset (first) }, secondPreset { preset (second) }, identicalPreset { preset (identical) };
    const auto nested { makeFolder (first, "nested") };
    preset (nested, {}, 2);
    require (first.getChildFile ("unrelated.txt").replaceWithText ("Not part of a bank"), "Write unrelated fixture");
    const auto firstBefore { hashes (first) }, secondBefore { hashes (second) }, identicalBefore { hashes (identical) };
    std::vector<Candidate> candidates;
    success (discover (first, candidates));
    require (candidates.size () == 1 && candidates[0].presetFile == firstPreset && candidates[0].slot == 1
             && candidates[0].name == "Bank test 1", "Discovery only reads immediate saved presets from the explicitly selected folder");
    require (discover (first, candidates, [] { return true; }).failed () && candidates.empty (), "Discovery cancellation never returns a partial candidate list");

    Report report;
    const auto output { root.getChildFile ("Combined bank") };
    success (exportBank ({ { firstPreset, 2 }, { secondPreset, 9 }, { identicalPreset, 18 } }, output, report));
    require (report.folder == output && report.presetCount == 3 && report.waveCount == 2 && report.renamedWaves == 1
             && report.ramBytes == 2 * 64 * 4 && report.midiCount == 0, "Flat bank deduplicates same-named identical content, safely renames different content and counts unique WAV RAM");
    require (isBankFolder (output) && isManifest (output.getChildFile (".a8-preset-bank.json")), "Recognize a bounded bank ownership record");
    const auto loadedFirst { read (output.getChildFile ("prst002.yml")) }, loadedSecond { read (output.getChildFile ("prst009.yml")) }, loadedThird { read (output.getChildFile ("prst018.yml")) };
    const auto secondName { zone (loadedSecond).getSample () };
    require (zone (loadedFirst).getSample () == "sample.wav" && secondName != "sample.wav" && secondName.length () <= 47
             && secondName == zone (loadedThird).getSample (), "Explicit target slots and rewritten collision references survive YAML round-trip");
    require (juce::SHA256 (output.getChildFile (secondName)) == juce::SHA256 (second.getChildFile ("sample.wav")), "Renamed WAV contents stay byte-identical");
    require (! output.getChildFile ("unrelated.txt").exists () && ! output.getChildFile ("nested").exists (), "No unrelated or nested source content enters the bank");
    require (hashes (first) == firstBefore && hashes (second) == secondBefore && hashes (identical) == identicalBefore, "All source files stay byte-identical");
    const auto outputBefore { hashes (output) };
    require (exportBank ({ { firstPreset, 1 } }, output, report).failed () && report.folder == juce::File ()
             && hashes (output) == outputBefore, "Existing destinations are never overwritten, even owned banks");
    for (const auto& entries : std::vector<std::vector<Entry>> { {}, { { firstPreset, 0 } }, { { firstPreset, 200 } }, { { firstPreset, 1 }, { secondPreset, 1 } } })
    {
        const auto destination { root.getChildFile ("Invalid selection") };
        require (exportBank (entries, destination, report).failed () && ! destination.exists (), "Invalid or duplicate target slots fail without publishing anything");
    }
    require (validateDestination (root.getChildFile (juce::String::repeatedString ("a", 32))).failed (), "Hardware folder-name limit is enforced");
    require (validateDestination (root.getChildFile (".hidden")).failed (), "Hidden bank names are refused");

    const auto missing { makeFolder (root, "missing") };
    const auto missingPreset { preset (missing) };
    require (exportBank ({ { missingPreset, 1 } }, root.getChildFile ("Missing WAV"), report).failed (), "Missing WAVs never produce a partial bank");
    const auto missingMidi { preset (first, "sample.wav", 3, 2) };
    require (exportBank ({ { missingMidi, 1 } }, root.getChildFile ("Missing MIDI"), report).failed (), "Missing explicitly selected nondefault MIDI setup is refused");
    const auto unsafeReference { makeFolder (root, "unsafe-reference") };
    const auto unsafePreset { unsafeReference.getChildFile ("prst001.yml") };
    require (unsafePreset.replaceWithText ("Preset 1 :\n  Channel 1 :\n    Zone 1 :\n      Sample : ../first/sample.wav\n"), "Write traversal fixture");
    require (exportBank ({ { unsafePreset, 1 } }, root.getChildFile ("Unsafe reference"), report).failed (), "Sample references cannot escape their selected source folder");
    const auto linked { makeFolder (root, "linked") };
    const auto linkedPreset { preset (linked) };
    std::error_code linkError;
    std::filesystem::create_symlink (std::filesystem::path (first.getChildFile ("sample.wav").getFullPathName ().toStdString ()),
                                     std::filesystem::path (linked.getChildFile ("sample.wav").getFullPathName ().toStdString ()), linkError);
    if (! linkError)
        require (exportBank ({ { linkedPreset, 1 } }, root.getChildFile ("Linked sample"), report).failed (), "Linked WAV dependencies are never followed");
    const auto cancelOutput { root.getChildFile ("Cancelled") };
    bool cancel { false };
    require (exportBank ({ { firstPreset, 1 } }, cancelOutput, report, [&] { return cancel; },
                        [&] (double, const juce::String& message) { if (message.startsWith ("Copying")) cancel = true; }).failed ()
             && ! cancelOutput.exists (), "Cancellation while copying discards only private staged data");
    const auto raceOutput { root.getChildFile ("Destination race") };
    bool raced { false };
    require (exportBank ({ { firstPreset, 1 } }, raceOutput, report, {}, [&] (double, const juce::String& message)
    {
        if (! raced && message == "Verifying source snapshot")
        {
            raced = true;
            success (raceOutput.createDirectory ());
            require (raceOutput.getChildFile ("keep.txt").replaceWithText ("Foreign folder"), "Create raced destination");
        }
    }).failed () && raceOutput.getChildFile ("keep.txt").loadFileAsString () == "Foreign folder", "A destination created during export is never replaced");
    const auto changed { makeFolder (root, "changed") };
    wave (changed.getChildFile ("sample.wav"));
    const auto changedPreset { preset (changed) };
    bool edited { false };
    require (exportBank ({ { changedPreset, 1 } }, root.getChildFile ("Changed source"), report, {}, [&] (double, const juce::String& message)
    {
        if (! edited && message.startsWith ("Copying"))
        {
            edited = true;
            require (changedPreset.appendText ("\r\n# external edit"), "Simulate external source edit");
        }
    }).failed () && changedPreset.loadFileAsString ().contains ("external edit"), "Changed source snapshots abort and preserve the external edit");

    const auto midiA { makeFolder (root, "midi-a") }, midiB { makeFolder (root, "midi-b") };
    require (midiA.getChildFile ("midi1.yml").replaceWithText ("mode : 0\nassign : 0\nbasicchannel : 3\n")
             && midiB.getChildFile ("midi1.yml").replaceWithText ("mode : 0\nassign : 0\nbasicchannel : 7\n"), "Create conflicting MIDI setups");
    const auto midiPresetA { preset (midiA, {}) }, midiPresetB { preset (midiB, {}) };
    const auto midiOut { root.getChildFile ("MIDI remap") };
    success (exportBank ({ { firstPreset, 1 }, { midiPresetA, 2 }, { midiPresetB, 3 } }, midiOut, report));
    require (report.midiCount == 2 && ! midiOut.getChildFile ("midi1.yml").exists (), "Missing default setup retains its absent slot without borrowing another source's MIDI");
    const auto midiTreeA { read (midiOut.getChildFile ("prst002.yml")) }, midiTreeB { read (midiOut.getChildFile ("prst003.yml")) };
    const auto slotA { static_cast<int> (midiTreeA.getProperty (PresetProperties::MidiSetpPropertyId)) }, slotB { static_cast<int> (midiTreeB.getProperty (PresetProperties::MidiSetpPropertyId)) };
    require (slotA == 1 && slotB == 2 && midiOut.getChildFile ("midi2.yml").loadFileAsString () == midiA.getChildFile ("midi1.yml").loadFileAsString (), "Conflicting MIDI files are copied byte-for-byte to free slots and preset references follow");
    std::vector<Entry> tooManyMidi;
    for (int index { 0 }; index < 10; ++index)
    {
        const auto folder { makeFolder (root, "midi-conflict-" + juce::String (index)) };
        require (folder.getChildFile ("midi1.yml").replaceWithText ("mode : 0\nassign : 0\nbasicchannel : " + juce::String (index) + "\n"), "Create distinct MIDI fixture");
        tooManyMidi.push_back ({ preset (folder, {}), index + 1 });
    }
    const auto tooManyOut { root.getChildFile ("Too many MIDI") };
    require (exportBank (tooManyMidi, tooManyOut, report).failed () && ! tooManyOut.exists (), "More than nine conflicting MIDI setups fails safely");

    auto layers { WaveformDesign::startingPoint (WaveformDesign::Mode::layers, WaveformDesign::Shape::saw) };
    layers.cycleFrames = 64;
    WaveformDesign::spreadVoices (layers, 2, 10, 90, 0.5);
    WaveformDesign::ExportResult layerPackage;
    success (WaveformDesign::exportDesign (layers, root, "Layer source", layerPackage));
    const auto layerOut { root.getChildFile ("Layer bank") };
    success (exportBank ({ { layerPackage.preset, 4 } }, layerOut, report));
    require (report.waveCount == 2 && report.renamedWaves == 0, "Multiple generated voices keep their names when no actual output files collide");
    for (const auto& original : layerPackage.waves)
    {
        WaveformDesignRecall::RecalledDesign recalled;
        success (WaveformDesignRecall::recallWave (layerOut.getChildFile (original.getFileName ()), recalled));
        require (recalled.settings.voiceCount == 2, "Every selected voice recalls the complete original layer design");
    }
    auto cv { WaveformDesign::startingPoint (WaveformDesign::Mode::modulation, WaveformDesign::Shape::sine) };
    cv.durationSeconds = 0.01;
    WaveformDesign::ExportResult cvPackage;
    success (WaveformDesign::exportDesign (cv, root, "CV source", cvPackage));
    const auto combinedOut { root.getChildFile ("Generated collisions") };
    success (exportBank ({ { layerPackage.preset, 1 }, { cvPackage.preset, 2 } }, combinedOut, report));
    const auto cvTree { read (combinedOut.getChildFile ("prst002.yml")) };
    const auto cvName { zone (cvTree).getSample () };
    require (cvName != cvPackage.waves[0].getFileName () && cvName.endsWith ("-01.wav") && cvName.length () <= 47,
             "Generated collisions retain automatic voice digits and short safe names");
    WaveformDesignRecall::RecalledDesign recalled;
    success (WaveformDesignRecall::recallWave (combinedOut.getChildFile (cvName), recalled));
    require (recalled.settings.mode == WaveformDesign::Mode::modulation && channel (cvTree).getMixLevel () == -90.0,
             "Renamed generated CV retains recall identity, purpose and Mix Off");
    AudioManager audio;
    auto cvReader { audio.getReaderFor (combinedOut.getChildFile (cvName)) };
    require (cvReader && CvSampleSafety::hasCvMetadata (cvReader->metadataValues), "Embedded CV safety metadata survives bank copying");

    // Older CV packages relied on design.json instead of an embedded WAV tag.
    // The bank copy must acquire that tag before rebinding the renamed recipe.
    const auto legacy { makeFolder (root, "legacy-cv") };
    auto legacySettings { cv };
    legacySettings.durationSeconds = 64.0 / 48000.0;
    juce::AudioBuffer<float> legacySamples (1, 64);
    legacySamples.clear ();
    {
        std::unique_ptr<juce::OutputStream> stream { legacy.getChildFile ("voice-01.wav").createOutputStream () };
        juce::WavAudioFormat format;
        auto legacyWriter { format.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (48000).withNumChannels (1).withBitsPerSample (24)) };
        require (legacyWriter && legacyWriter->writeFromAudioSampleBuffer (legacySamples, 0, 64), "Create legacy untagged CV fixture");
    }
    success (WaveformDesign::ExportSupport::writeText (legacy.getChildFile ("design.json"), juce::JSON::toString (WaveformDesign::ExportSupport::namedRecipe (legacySettings, "Legacy CV"))));
    const auto legacyPreset { preset (legacy, "voice-01.wav") };
    auto legacyTree { read (legacyPreset) };
    channel (legacyTree).setMixLevel (-90, false);
    channel (legacyTree).setMixMod ("Off", 0, false);
    Assimil8orPreset legacyPresetWriter;
    success (legacyPresetWriter.write (legacyPreset, legacyTree));
    const auto legacyOut { root.getChildFile ("Legacy bank") };
    success (exportBank ({ { legacyPreset, 1 } }, legacyOut, report));
    success (WaveformDesignRecall::recallWave (legacyOut.getChildFile ("voice-01.wav"), recalled));
    auto legacyReader { audio.getReaderFor (legacyOut.getChildFile ("voice-01.wav")) };
    require (legacyReader && CvSampleSafety::hasCvMetadata (legacyReader->metadataValues)
             && recalled.settings.mode == WaveformDesign::Mode::modulation, "Legacy recipe-only CV remains protected and recallable after flat bank export");

    auto unsafe { read (cvPackage.preset) };
    channel (unsafe).setMixLevel (0, false);
    Assimil8orPreset writer;
    success (writer.write (cvPackage.preset, unsafe));
    const auto unsafeOut { root.getChildFile ("Unsafe CV") };
    require (exportBank ({ { cvPackage.preset, 1 } }, unsafeOut, report).failed () && ! unsafeOut.exists (), "Unsafe CV routing is rejected rather than silently exported");

    const auto malformed { makeFolder (root, "malformed-bank") };
    require (malformed.getChildFile (".a8-preset-bank.json").replaceWithText ("{\"format\":\"A8ManagerPresetBank\",\"version\":1,\"presets\":[{\"slot\":200,\"name\":\"bad\"}]}"), "Create invalid ownership fixture");
    require (! isBankFolder (malformed), "Ownership recognition validates slot bounds, not only a magic string");
    noStages (root);
    std::cout << "PASS: preset bank export (flat selection, explicit slots, collisions/dedup, recall/CV, MIDI, capacity accounting, source preservation, cancellation/races)\n";
}
