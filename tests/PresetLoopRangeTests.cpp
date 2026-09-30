#include "Assimil8or/Preset/PresetLoopRanges.h"
#include "Assimil8or/PresetFileOperations.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "Assimil8or/Audio/SampleLoopSimulation.h"
#include "GUI/Assimil8or/PresetList/PresetListComponent.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void require (bool value, const char* message) { if (! value) throw std::runtime_error (message); }
    void success (const juce::Result& result) { if (result.failed ()) throw std::runtime_error (result.getErrorMessage ().toStdString ()); }
    juce::ValueTree defaults ()
    {
        return ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ()
            .getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType).createCopy ();
    }
    void writeWave (const juce::File& file, int frames = 256)
    {
        std::unique_ptr<juce::OutputStream> stream { file.createOutputStream () };
        auto writer { juce::WavAudioFormat ().createWriterFor (stream, juce::AudioFormatWriterOptions {}
            .withSampleRate (48000).withNumChannels (1).withBitsPerSample (24)) };
        juce::AudioBuffer<float> buffer (1, frames);
        buffer.clear ();
        require (writer && writer->writeFromAudioSampleBuffer (buffer, 0, frames) && writer->flush (), "Write owned range fixture WAV");
    }
}

struct PresetLoopRangeTestAccess
{
    static void load (const juce::File& file, const juce::String& originalBytes)
    {
        PresetListComponent list;
        auto baseline { defaults () }, edited { defaults () };
        list.unEditedPresetProperties.wrap (baseline, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        list.presetProperties.wrap (edited, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        juce::StringArray notified;
        list.notifyLoopRepairs = [&] (const juce::StringArray& zones) { notified = zones; };
        require (list.loadPreset (file), "Load legacy preset through the real preset list");
        ZoneProperties fixed (edited.getChild (0).getChild (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        ZoneProperties old (baseline.getChild (0).getChild (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        require (notified.size () == 1 && notified[0].startsWith ("CH 1, zone 1:"), "Notify once with the preserved zone identity");
        ChannelProperties editedChannel (edited.getChild (0), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        ChannelProperties baselineChannel (baseline.getChild (0), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        require (fixed.getValueTree ().isEquivalentTo (old.getValueTree ()) && fixed.getLoopStart () == 220 && fixed.getLoopLength () == 20.0,
                 "Import preserves the original outside-sample loop exactly");
        require (editedChannel.getAllowLoopOutsideSample () && ! baselineChannel.getAllowLoopOutsideSample (),
                 "Only editable copy enables independent loop editing");
        require (! PresetHelpers::areEntirePresetsEqual (edited, baseline), "Imported editor permission is SAVE IS PENDING");
        require (file.loadFileAsString () == originalBytes, "Import correction never writes the original file automatically");
        success (PresetFileOperations::save (file, edited, baseline));
        require (PresetHelpers::areEntirePresetsEqual (edited, baseline), "Explicit Save clears pending changes");
        notified.clear ();
        require (list.loadPreset (file) && notified.isEmpty (), "Reload of the corrected save needs no further warning");
        require (fixed.getLoopStart () == 220 && fixed.getLoopLength () == 20.0 && editedChannel.getAllowLoopOutsideSample (),
                 "Outside loop and channel permission survive saving/reloading");
    }
};

void testPresetLoopRanges ()
{
    const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("a8-loop-ranges-" + juce::Uuid ().toString ()) };
    success (folder.createDirectory ());
    struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { folder };
    writeWave (folder.getChildFile ("range.wav"));
    auto tree { defaults () };
    tree.setProperty (PresetProperties::IdPropertyId, 19, nullptr);
    ZoneProperties zone (tree.getChild (0).getChild (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
    zone.setSample ("range.wav", false);
    zone.setSampleStart (64, false);
    zone.setSampleEnd (192, false);
    const auto presetFile { folder.getChildFile ("prst019.yml") };
    const auto original { tree.createCopy () };
    Assimil8orPreset writer;
    success (writer.write (presetFile, tree));
    require (tree.isEquivalentTo (original), "Saving concrete hardware loops does not mutate implicit editor defaults");
    const auto text { presetFile.loadFileAsString () };
    require (text.contains ("LoopStart : 64") && text.contains ("LoopLength : 128"), "Hardware receives sample-aligned loop coordinates, not full-file defaults");
    juce::ValueTree readback;
    success (PresetFileOperations::read (presetFile, readback));
    require (PresetHelpers::areEntirePresetsEqual (tree, readback), "Bound editor comment preserves default intent across round-trip");
    juce::StringArray hardwareLines;
    for (const auto& line : juce::StringArray::fromLines (text))
        if (! line.trimStart ().startsWithChar ('#')) hardwareLines.add (line);
    Assimil8orPreset hardware;
    hardware.parse (hardwareLines);
    require (hardware.getParseErrorsVT ().getNumChildren () == 0, "Hardware-facing parameter text remains ordinary preset syntax");
    ZoneProperties concrete (hardware.getPresetVT ().getChild (0).getChild (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
    require (concrete.getLoopStart () == 64 && concrete.getLoopLength () == 128.0, "Ignoring editor comments leaves complete valid hardware loop values");

    require (presetFile.replaceWithText (text.replace ("LoopLength : 128", "LoopLength : 96")), "Simulate hardware editing a saved loop");
    success (PresetFileOperations::read (presetFile, readback));
    ZoneProperties modified (readback.getChild (0).getChild (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
    require (modified.getLoopLength () == 96.0 && modified.getLoopStart () == 64, "Stale default-intent comment never overrides external marker edits");

    zone.setSampleEnd (-1, false);
    const auto exported { folder.getChildFile ("export") };
    success (exported.createDirectory ());
    success (writer.write (exported.getChildFile ("prst019.yml"), tree, folder));
    require (exported.getChildFile ("prst019.yml").loadFileAsString ().contains ("LoopLength : 192"), "EOF defaults use the actual source folder when exporting elsewhere");
    success (PresetFileOperations::read (exported.getChildFile ("prst019.yml"), readback));
    require (PresetHelpers::areEntirePresetsEqual (tree, readback), "Open-ended sample defaults remain automatic after export");

    zone.setSampleEnd (192, false);
    zone.setLoopStart (220, false);
    zone.setLoopLength (20.0, false);
    success (writer.write (presetFile, tree));
    PresetLoopRangeTestAccess::load (presetFile, presetFile.loadFileAsString ());

    auto copied { tree.createCopy () };
    ZoneProperties valid (copied.getChild (0).getChild (1), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
    valid.copyFrom (zone.getValueTree (), false);
    valid.setId (2, false);
    valid.setLoopStart (80, false);
    valid.setLoopLength (67.125, false);
    const auto validBefore { valid.getValueTree ().createCopy () };
    ZoneProperties shortSelection (copied.getChild (0).getChild (2), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
    shortSelection.setSample ("range.wav", false);
    shortSelection.setSampleStart (64, false); shortSelection.setSampleEnd (66, false);
    shortSelection.setLoopStart (64, false); shortSelection.setLoopLength (4.0, false);
    shortSelection.setPitchOffset (3.25, false);
    writeWave (folder.getChildFile ("tiny.wav"), 3);
    ZoneProperties tinyFile (copied.getChild (0).getChild (3), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
    tinyFile.setSample ("tiny.wav", false);
    tinyFile.setSampleStart (1, false); tinyFile.setSampleEnd (3, false);
    tinyFile.setLoopStart (1, false); tinyFile.setLoopLength (4.0, false);
    ZoneProperties shortAutomatic (copied.getChild (0).getChild (4), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
    shortAutomatic.setSample ("range.wav", false);
    shortAutomatic.setSampleStart (70, false); shortAutomatic.setSampleEnd (72, false);
    shortAutomatic.setLoopStart (-1, false); shortAutomatic.setLoopLength (-1.0, false);
    const auto shortAutomaticBefore { shortAutomatic.getValueTree ().createCopy () };
    juce::MemoryBlock tinyBefore;
    require (folder.getChildFile ("tiny.wav").loadFileAsData (tinyBefore), "Read tiny source before migration");
    const auto repairs { PresetLoopRanges::repair (copied, folder) };
    require (repairs.size () == 2 && valid.getValueTree ().isEquivalentTo (validBefore), "Enable outside editing and repair only file-invalid explicit loops; valid fractional settings stay intact");
    require (shortSelection.getSampleStart () == 64 && shortSelection.getSampleEnd () == 66
             && shortSelection.getPitchOffset () == 3.25 && shortSelection.getLoopStart () == 64 && shortSelection.getLoopLength () == 4.0,
             "Import preserves a short SAMPLE and its file-valid external LOOP exactly");
    require (tinyFile.getSampleStart () == 1 && tinyFile.getSampleEnd () == 3
             && ! tinyFile.getLoopStart () && ! tinyFile.getLoopLength (),
             "Repair never expands or repositions SAMPLE in a sub-four-frame file");
    require (! SampleLoopSimulation::resolve (shortSelection, 256) && ! SampleLoopSimulation::resolve (tinyFile, 3),
             "Preserved external loops cannot make short samples eligible for contained simulation");
    require (shortAutomatic.getValueTree ().isEquivalentTo (shortAutomaticBefore)
             && ! SampleLoopSimulation::resolve (shortAutomatic, 256),
             "A short already-automatic selection remains unchanged and non-loopable");
    juce::MemoryBlock tinyAfter;
    require (folder.getChildFile ("tiny.wav").loadFileAsData (tinyAfter) && tinyBefore == tinyAfter,
             "Loop migration never mutates source audio files");
    require (PresetLoopRanges::message (repairs).contains ("SAVE IS PENDING"), "Repair notice explains that explicit saving is required");

    // The channel setting is editor metadata, including on a sample-less
    // channel. Copy/reset/equality use its semantic false default.
    auto empty { defaults () };
    ChannelProperties emptyChannel (empty.getChild (7), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::yes);
    const auto emptyBefore { empty.createCopy () };
    int selfChanges {}, otherChanges {};
    emptyChannel.onAllowLoopOutsideSampleChange = [&] (bool) { ++selfChanges; };
    ChannelProperties observer (empty.getChild (7), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::yes);
    observer.onAllowLoopOutsideSampleChange = [&] (bool) { ++otherChanges; };
    require (! emptyChannel.getAllowLoopOutsideSample (), "Allow outside sample defaults off");
    emptyChannel.setAllowLoopOutsideSample (true, false);
    require (selfChanges == 0 && otherChanges == 1 && ! PresetHelpers::areEntirePresetsEqual (empty, emptyBefore),
             "Editor flag notifies other wrappers and participates in dirty equality");
    const auto emptyFile { folder.getChildFile ("prst020.yml") };
    success (writer.write (emptyFile, empty));
    const auto emptyText { emptyFile.loadFileAsString () };
    require (emptyText.contains ("Channel 8 :") && emptyText.contains ("# A8Manager allow loop outside sample v1")
             && ! emptyText.contains ("AllowLoopOutsideSample :"), "Empty channel preference persists only in YAML comment, not hardware parameter");
    success (PresetFileOperations::read (emptyFile, readback));
    ChannelProperties recalledEmpty (readback.getChild (7), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
    require (recalledEmpty.getAllowLoopOutsideSample (), "Empty channel comment round-trips");
    auto channelCopy { emptyBefore.getChild (7).createCopy () };
    ChannelProperties copyTarget (channelCopy, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
    copyTarget.copyFrom (emptyChannel.getValueTree ());
    require (copyTarget.getAllowLoopOutsideSample (), "Channel copy retains outside preference");
    emptyChannel.setAllowLoopOutsideSample (false, false);
    require (selfChanges == 0 && otherChanges == 2 && ! emptyChannel.getValueTree ().hasProperty (ChannelProperties::AllowLoopOutsideSamplePropertyId),
             "Reset removes property, excludes self callback, and notifies other wrappers");
    copyTarget.copyFrom (emptyChannel.getValueTree ());
    require (! copyTarget.getAllowLoopOutsideSample (), "Channel copy/reset clears previous outside preference");
    emptyChannel.setAllowLoopOutsideSample (true, true);
    emptyChannel.setAllowLoopOutsideSample (false, true);
    require (selfChanges == 2 && otherChanges == 4, "Explicit self callbacks fire for enabling and disabling");
    require (empty.isEquivalentTo (emptyBefore), "On/off and copy/reset restore exact default channel tree shape");

    Assimil8orPreset reused;
    reused.parse (juce::StringArray::fromLines (emptyText));
    reused.parse ({ "Preset 1 :", "  Name : Reused", "  Channel 8 :", "    # A8Manager allow loop outside sample v1 extra" });
    ChannelProperties reusedChannel (reused.getPresetVT ().getChild (7), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
    require (! reusedChannel.getAllowLoopOutsideSample (), "Parser reuse clears old metadata and ignores inexact comments");
    reused.parse ({ "Preset 1 :", "  Name : Misplaced", "  Channel 8 :", "    Zone 1 :", "      # A8Manager allow loop outside sample v1" });
    require (! reusedChannel.getAllowLoopOutsideSample (), "Zone comments cannot enable channel permission");

    auto pair { defaults () };
    ChannelProperties pairLeft (pair.getChild (0), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
    ChannelProperties pairRight (pair.getChild (1), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
    pairRight.setChannelMode (ChannelProperties::stereoRight, false);
    ZoneProperties rightZone (pair.getChild (1).getChild (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
    rightZone.setSample ("range.wav", false); rightZone.setSampleStart (64, false); rightZone.setSampleEnd (128, false);
    rightZone.setLoopStart (200, false); rightZone.setLoopLength (24.25, false);
    const auto rightBefore { rightZone.getValueTree ().createCopy () };
    require (PresetLoopRanges::repair (pair, folder).size () == 1 && pairLeft.getAllowLoopOutsideSample () && pairRight.getAllowLoopOutsideSample ()
             && rightZone.getValueTree ().isEquivalentTo (rightBefore), "Right-only external stereo loop enables both channel permissions without changing points");

    rightZone.setLoopLength (-1.0, false);
    success (writer.write (folder.getChildFile ("prst021.yml"), pair));
    const auto partialText { folder.getChildFile ("prst021.yml").loadFileAsString () };
    require (partialText.contains ("LoopLength : 56"), "Beyond-sample start with automatic length exports physical EOF extent");
    success (PresetFileOperations::read (folder.getChildFile ("prst021.yml"), readback));
    ZoneProperties partial (readback.getChild (1).getChild (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
    require (partial.getLoopStart () == 200 && ! partial.getLoopLength () && ZoneSampleRanges::resolve (ZoneSampleRanges::read (partial), 256, true).loopLength == 56,
             "Beyond-sample automatic length keeps its editor intent across hardware-safe export");

    rightZone.setSample ("missing.wav", false);
    rightZone.setLoopLength (24.25, false);
    pairLeft.setAllowLoopOutsideSample (false, false); pairRight.setAllowLoopOutsideSample (false, false);
    const auto missingBefore { rightZone.getValueTree ().createCopy () };
    require (PresetLoopRanges::repair (pair, folder).size () == 1 && rightZone.getValueTree ().isEquivalentTo (missingBefore),
             "Missing WAV never causes a guessed EOF to discard an outside loop");
    rightZone.setLoopLength (-1.0, false);
    pairLeft.setAllowLoopOutsideSample (false, false); pairRight.setAllowLoopOutsideSample (false, false);
    const auto missingPartialBefore { rightZone.getValueTree ().createCopy () };
    require (PresetLoopRanges::repair (pair, folder).size () == 1 && pairLeft.getAllowLoopOutsideSample () && pairRight.getAllowLoopOutsideSample ()
             && rightZone.getValueTree ().isEquivalentTo (missingPartialBefore),
             "Unknown EOF preserves beyond-sample LoopStart plus unset length and enables its stereo pair");
    auto malformedPair { defaults () };
    ChannelProperties firstRight (malformedPair.getChild (1), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
    ChannelProperties secondRight (malformedPair.getChild (2), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
    firstRight.setChannelMode (ChannelProperties::stereoRight, false);
    secondRight.setChannelMode (ChannelProperties::stereoRight, false);
    ZoneProperties chained (malformedPair.getChild (2).getChild (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
    chained.copyFrom (rightZone.getValueTree (), false);
    PresetLoopRanges::repair (malformedPair, folder);
    require (secondRight.getAllowLoopOutsideSample () && ! firstRight.getAllowLoopOutsideSample (),
             "Malformed consecutive stereo-right channels do not propagate outside permission through an invalid pair");
    std::cout << "Preset loop range persistence/import regression tests passed\n";
}
