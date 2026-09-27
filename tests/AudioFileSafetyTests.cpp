#include "Assimil8or/Audio/SafeAudioImport.h"
#include "Assimil8or/Audio/WaveformDesign.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "GUI/Assimil8or/Editor/EditManager.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void check (bool passed, const char* message) { if (! passed) throw std::runtime_error (message); }

    juce::MemoryBlock bytes (const juce::File& file)
    {
        juce::MemoryBlock data;
        check (file.loadFileAsData (data), "Read fixture bytes");
        return data;
    }

    void makeAudio (const juce::File& file, bool aiff = false, double rate = 48000.0, int channels = 1,
                    const std::unordered_map<juce::String, juce::String>& metadata = {})
    {
        std::unique_ptr<juce::OutputStream> output { file.createOutputStream () };
        check (output != nullptr, "Create generated audio output");
        juce::WavAudioFormat wav;
        juce::AiffAudioFormat aif;
        auto& format { aiff ? static_cast<juce::AudioFormat&> (aif) : static_cast<juce::AudioFormat&> (wav) };
        auto writer { format.createWriterFor (output, juce::AudioFormatWriterOptions {}.withSampleRate (rate).withNumChannels (channels)
                                              .withBitsPerSample (24).withMetadataValues (metadata)) };
        check (writer != nullptr, "Create generated audio writer");
        juce::AudioBuffer<float> buffer (channels, 128);
        for (int side { 0 }; side < channels; ++side)
            for (int i { 0 }; i < 128; ++i) buffer.setSample (side, i, side == 0 ? 0.25f : -0.5f);
        check (writer->writeFromAudioSampleBuffer (buffer, 0, 128), "Write generated audio");
        // AIFF's writer finalises its header on destruction; unlike WAV, it
        // does not override the base flush() implementation (which returns false).
        writer.reset ();
    }

    void makeFloatWave (const juce::File& file)
    {
        juce::MemoryOutputStream output;
        output.write ("RIFF", 4); output.writeInt (36 + 128 * 4); output.write ("WAVEfmt ", 8);
        output.writeInt (16); output.writeShort (3); output.writeShort (1);
        output.writeInt (48000); output.writeInt (48000 * 4); output.writeShort (4); output.writeShort (32);
        output.write ("data", 4); output.writeInt (128 * 4);
        for (int i { 0 }; i < 128; ++i) output.writeFloat (0.25f);
        check (file.replaceWithData (output.getData (), output.getDataSize ()), "Write floating-point WAV fixture");
    }

    void checkAudio (AudioManager& manager, const juce::File& file)
    {
        auto reader { manager.getReaderFor (file) };
        check (reader && reader->lengthInSamples == 128 && ! reader->usesFloatingPointData && manager.isAssimil8orSupportedAudioFile (file), "Imported WAV is valid integer audio with correct length");
        juce::AudioBuffer<float> buffer (1, 128);
        check (reader->read (&buffer, 0, 128, 0, true, false), "Read converted channel");
        check (std::abs (buffer.getSample (0, 64) - 0.25f) < 0.00001f, "Converted channel contents preserved");
    }

    void testCvImportSafety (AudioManager& manager, const juce::File& external, const juce::File& preset)
    {
        auto tagged = [] (const juce::File& file, int channels, bool floating)
        {
            std::unique_ptr<juce::OutputStream> stream { file.createOutputStream () };
            auto values { CvSampleSafety::exportMetadata (true) };
            std::unordered_map<juce::String, juce::String> metadata;
            for (int index { 0 }; index < values.size (); ++index)
                metadata.emplace (values.getAllKeys ()[index], values.getAllValues ()[index]);
            juce::WavAudioFormat format;
            auto writer { format.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (48000)
                .withNumChannels (channels).withBitsPerSample (floating ? 32 : 24)
                .withSampleFormat (floating ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                          : juce::AudioFormatWriterOptions::SampleFormat::integral)
                .withMetadataValues (metadata)) };
            check (writer != nullptr, "Create tagged CV fixture");
            juce::AudioBuffer<float> data (channels, 128);
            for (int side { 0 }; side < channels; ++side)
                for (int frame { 0 }; frame < 128; ++frame) data.setSample (side, frame, side == 0 ? 0.25f : -0.5f);
            check (writer->writeFromAudioSampleBuffer (data, 0, 128), "Write tagged CV fixture");
        };
        auto verifyTag = [&manager] (const juce::File& file)
        {
            auto reader { manager.getReaderFor (file) };
            check (reader && CvSampleSafety::hasCvMetadata (reader->metadataValues) && CvSampleSafety::isCv (file, *reader),
                   "CV purpose survives independently of filename and recipe");
        };
        const auto marked { external.getChildFile ("control.wav") };
        tagged (marked, 1, false);
        const auto original { bytes (marked) };
        juce::File imported;
        check (SafeAudioImport::importFile (manager, marked, preset, imported).wasOk (), "Import tagged CV");
        check (bytes (imported) == original && bytes (marked) == original, "Compatible tagged CV copy remains byte-identical");
        verifyTag (imported);
        checkAudio (manager, imported);

        const auto floating { external.getChildFile ("control-float.wav") };
        tagged (floating, 1, true);
        const auto floatOriginal { bytes (floating) };
        check (SafeAudioImport::importFile (manager, floating, preset, imported).wasOk () && bytes (floating) == floatOriginal,
               "Floating CV conversion preserves the source");
        verifyTag (imported);
        checkAudio (manager, imported);

        const auto oldFolder { external.getChildFile ("old-cv-export") };
        check (oldFolder.createDirectory ().wasOk (), "Create legacy CV export fixture");
        const auto oldWave { oldFolder.getChildFile ("voice-01.wav") };
        makeAudio (oldWave);
        auto recipe { WaveformDesign::startingPoint (WaveformDesign::Mode::modulation, WaveformDesign::Shape::sine) };
        recipe.durationSeconds = 128.0 / 48000.0;
        check (oldFolder.getChildFile ("design.json").replaceWithText (juce::JSON::toString (WaveformDesign::toJson (recipe))), "Write legacy CV recipe");
        const auto legacyOriginal { bytes (oldWave) };
        auto oldReader { manager.getReaderFor (oldWave) };
        check (oldReader && ! CvSampleSafety::hasCvMetadata (oldReader->metadataValues) && CvSampleSafety::isCv (oldWave, *oldReader),
               "Legacy classification comes from the matching recipe, not an embedded tag");
        oldReader.reset ();
        check (SafeAudioImport::importFile (manager, oldWave, preset, imported).wasOk () && bytes (oldWave) == legacyOriginal,
               "Import embeds legacy CV purpose in a new copy without modifying the original");
        verifyTag (imported);
        checkAudio (manager, imported);

        const auto stereo { external.getChildFile ("cv-stereo.wav") };
        tagged (stereo, 2, false);
        const auto stereoOriginal { bytes (stereo) };
        manager.splitStereoIntoTwoMono (stereo);
        verifyTag (external.getChildFile ("cv-stereo-L.wav"));
        verifyTag (external.getChildFile ("cv-stereo-R.wav"));
        manager.mixStereoToMono (stereo);
        verifyTag (external.getChildFile ("cv-stereo-mono.wav"));
        check (bytes (stereo) == stereoOriginal, "Splitting/mixing preserves original CV WAV bytes");

        const auto aiff { external.getChildFile ("instrument.aiff") };
        makeAudio (aiff, true, 48000, 2, { { "MidiUnityNote", "60" }, { "Loop0Type", "1" },
                                         { "Loop0StartIdentifier", "10" }, { "Loop0EndIdentifier", "20" } });
        const auto aiffOriginal { bytes (aiff) };
        auto aiffReader { manager.getReaderFor (aiff) };
        check (aiffReader && aiffReader->metadataValues["NumSampleLoops"] == "2"
               && aiffReader->metadataValues["Loop0StartIdentifier"] == "10", "AIFF fixture exposes its incompatible instrument-loop metadata");
        aiffReader.reset ();
        manager.splitStereoIntoTwoMono (aiff);
        manager.mixStereoToMono (aiff);
        for (const auto* suffix : { "-L.wav", "-R.wav", "-mono.wav" })
        {
            auto reader { manager.getReaderFor (external.getChildFile (juce::String ("instrument") + suffix)) };
            check (reader && reader->lengthInSamples == 128 && reader->metadataValues["NumSampleLoops"].getIntValue () == 0,
                   "AIFF split/mix does not manufacture WAV loops from incompatible metadata");
        }
        check (bytes (aiff) == aiffOriginal, "AIFF split/mix preserves original file bytes");
        std::cout << "PASS: CV provenance survives copy, conversion, legacy import and stereo split/mix\n";
    }

    // Write RIFF bytes directly: a normal WAV writer would repair the missing
    // final pad byte, masking the JUCE 9.0.1 compatibility regression.
    void makePcmWave (const juce::File& file, int bits, const std::vector<juce::uint8>& data,
                      int declaredDataBytes, bool finalPad, bool cueLabel = false)
    {
        juce::MemoryOutputStream body;
        body.write ("WAVEfmt ", 8);
        body.writeInt (16);
        body.writeShort (1); // PCM
        body.writeShort (1); // mono
        body.writeInt (48000);
        body.writeInt (48000 * bits / 8);
        body.writeShort (static_cast<short> (bits / 8));
        body.writeShort (static_cast<short> (bits));
        body.write ("data", 4);
        body.writeInt (declaredDataBytes);
        body.write (data.data (), data.size ());
        if (! cueLabel && finalPad && (data.size () & 1) != 0) body.writeByte (0);
        if (cueLabel)
        {
            check ((data.size () & 1) == 0, "Metadata fixture has an aligned audio chunk");
            body.write ("cue ", 4);
            body.writeInt (28);
            body.writeInt (1); // cue count
            body.writeInt (7); // cue identifier
            body.writeInt (2); // play order/position
            body.write ("data", 4);
            body.writeInt (0); // chunk start
            body.writeInt (0); // block start
            body.writeInt (2); // sample offset
            body.write ("LIST", 4);
            body.writeInt (19); // adtl + labl header + cue ID + three text bytes
            body.write ("adtllabl", 8);
            body.writeInt (7);
            body.writeInt (7); // associated cue identifier
            body.write ("LS\0", 3);
            if (finalPad) body.writeByte (0);
        }
        juce::MemoryOutputStream output;
        output.write ("RIFF", 4);
        output.writeInt (static_cast<int> (body.getDataSize ()));
        output.write (body.getData (), body.getDataSize ());
        check (file.replaceWithData (output.getData (), output.getDataSize ()), "Write generated RIFF padding fixture");
    }

    void checkPcmSamples (AudioManager& manager, const juce::File& file, int bits, const std::vector<float>& expected)
    {
        auto reader { manager.getReaderFor (file) };
        check (reader && reader->lengthInSamples == static_cast<juce::int64> (expected.size ())
               && reader->numChannels == 1 && reader->sampleRate == 48000.0
               && reader->bitsPerSample == static_cast<unsigned int> (bits), "Unpadded WAV retains exact audio format and sample count");
        check (manager.isAssimil8orSupportedAudioFile (file), "Production format validation accepts readable unpadded PCM WAV");
        juce::AudioBuffer<float> buffer (1, static_cast<int> (expected.size ()));
        check (reader->read (&buffer, 0, buffer.getNumSamples (), 0, true, false), "Read unpadded PCM samples through AudioManager");
        for (int index { 0 }; index < buffer.getNumSamples (); ++index)
            check (std::abs (buffer.getSample (0, index) - expected[static_cast<size_t> (index)]) < 0.00001f,
                   "Unpadded WAV sample values are not truncated or shifted");
    }

    void testWavPadding (AudioManager& manager, const juce::File& external, const juce::File& preset)
    {
        const std::vector<juce::uint8> pcm8 { 0x80, 0xc0, 0x40, 0xa0, 0x60 };
        const std::vector<float> expected { 0.0f, 0.5f, -0.5f, 0.25f, -0.25f };
        const auto padded { external.getChildFile ("padded-control.wav") };
        makePcmWave (padded, 8, pcm8, 5, true);
        checkPcmSamples (manager, padded, 8, expected);

        const auto unpadded8 { external.getChildFile ("unpadded-8-bit.wav") };
        makePcmWave (unpadded8, 8, pcm8, 5, false);
        checkPcmSamples (manager, unpadded8, 8, expected);
        juce::File imported;
        check (SafeAudioImport::importFile (manager, unpadded8, preset, imported).wasOk (), "Unpadded PCM8 WAV can be imported");
        check (bytes (imported) == bytes (unpadded8), "Compatible unpadded WAV import preserves source bytes");
        checkPcmSamples (manager, imported, 8, expected);

        // Odd-frame mono 24-bit audio also has an odd byte count; this is not
        // limited to the 8-bit fixture used in JUCE's own regression test.
        const auto unpadded24 { external.getChildFile ("unpadded-24-bit.wav") };
        makePcmWave (unpadded24, 24, { 0, 0, 0, 0, 0, 0x40, 0, 0, 0xc0, 0, 0, 0x20, 0, 0, 0xe0 }, 15, false);
        checkPcmSamples (manager, unpadded24, 24, expected);

        const auto metadata { external.getChildFile ("unpadded-cue-label.wav") };
        makePcmWave (metadata, 8, { 0x80, 0xc0, 0x40, 0xa0, 0x60, 0x80 }, 6, false, true);
        const std::vector<float> metadataSamples { 0.0f, 0.5f, -0.5f, 0.25f, -0.25f, 0.0f };
        checkPcmSamples (manager, metadata, 8, metadataSamples);
        auto checkCue = [&manager] (const juce::File& file)
        {
            auto reader { manager.getReaderFor (file) };
            check (reader && reader->metadataValues["NumCuePoints"] == "1" && reader->metadataValues["Cue0Identifier"] == "7"
                   && reader->metadataValues["Cue0Offset"] == "2" && reader->metadataValues["NumCueLabels"] == "1"
                   && reader->metadataValues["CueLabel0Identifier"] == "7" && reader->metadataValues["CueLabel0Text"] == "LS",
                   "Final unpadded LIST preserves cue ID, sample offset and associated label");
        };
        checkCue (metadata);
        check (SafeAudioImport::importFile (manager, metadata, preset, imported).wasOk (), "WAV with final unpadded cue-label metadata can be imported");
        checkCue (imported);
        check (bytes (imported) == bytes (metadata), "Import preserves cue metadata byte-for-byte");

        // Missing a data byte must not be mistaken for the permitted missing
        // alignment byte, whether the declared payload length is odd or even.
        for (const auto declared : { 5, 6, 1000 })
        {
            const auto truncated { external.getChildFile ("truncated-" + juce::String (declared) + ".wav") };
            const std::vector<juce::uint8> actual { declared == 5 ? std::vector<juce::uint8> { 0x80, 0xc0, 0x40, 0xa0 } : pcm8 };
            makePcmWave (truncated, 8, actual, declared, false);
            const auto original { bytes (truncated) };
            auto reader { manager.getReaderFor (truncated) };
            check (! reader || reader->lengthInSamples == 0, "Genuinely truncated WAV data is not exposed as usable audio");
            check (! manager.isAssimil8orSupportedAudioFile (truncated), "Production validation rejects genuinely truncated WAV data");
            const auto fileCount { preset.findChildFiles (juce::File::findFiles, false).size () };
            check (SafeAudioImport::importFile (manager, truncated, preset, imported).failed () && imported == juce::File ()
                   && bytes (truncated) == original && preset.findChildFiles (juce::File::findFiles, false).size () == fileCount,
                   "Truncated WAV import fails without modifying source or publishing output");
        }
        std::cout << "PASS: final WAV padding compatibility, PCM8/PCM24 sample contents, cue-label preservation and truncated-data rejection\n";
    }
}

struct AudioFileSafetyTestAccess
{
    static juce::ValueTree bind (EditManager& editor, AudioManager& audio, const juce::File& folder)
    {
        auto tree { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType).createCopy () };
        editor.audioManager = &audio;
        editor.appProperties.wrap (juce::ValueTree ("Root"), AppProperties::WrapperType::owner, AppProperties::EnableCallbacks::no);
        editor.appProperties.setMostRecentFolder (folder.getFullPathName ());
        editor.presetProperties.wrap (tree, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        for (int channel { 0 }; channel < 8; ++channel)
        {
            editor.channelPropertiesList[channel].wrap (tree.getChild (channel), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            for (int zone { 0 }; zone < 8; ++zone)
                editor.zoneAndSamplePropertiesList[channel][zone].zoneProperties.wrap (tree.getChild (channel).getChild (zone), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        }
        return tree;
    }
};

void testAudioFileSafety ()
{
    const auto root { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-audio-safety", "", false) };
    check (root.createDirectory ().wasOk (), "Create isolated fixture folder");
    struct Cleanup { juce::File directory; ~Cleanup () { directory.deleteRecursively (); } } cleanup { root };
    const auto external { root.getChildFile ("external") }, preset { root.getChildFile ("preset") };
    check (external.createDirectory ().wasOk () && preset.createDirectory ().wasOk (), "Create source and preset folders");
    AudioManager audio;
    EditManager editor;
    auto tree { AudioFileSafetyTestAccess::bind (editor, audio, preset) };
    ZoneProperties zone (tree.getChild (0).getChild (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);

    const auto aiff { external.getChildFile ("external.aiff") };
    makeAudio (aiff, true);
    const auto aiffBytes { bytes (aiff) };
    check (editor.assignSamples (0, 0, { aiff.getFullPathName () }), "External AIFF assignment succeeds");
    check (bytes (aiff) == aiffBytes, "External AIFF original is never deleted or changed");
    checkAudio (audio, preset.getChildFile (zone.getSample ()));

    const auto floatWave { preset.getChildFile ("floating.wav") };
    makeFloatWave (floatWave);
    const auto floatBytes { bytes (floatWave) };
    check (editor.assignSamples (0, 0, { floatWave.getFullPathName () }), "Internal floating WAV assignment succeeds");
    check (bytes (floatWave) == floatBytes && zone.getSample () != floatWave.getFileName (), "Internal unsupported WAV preserved with separate converted filename");
    checkAudio (audio, preset.getChildFile (zone.getSample ()));
    const auto externalFloat { external.getChildFile ("float-outside.wav") };
    makeFloatWave (externalFloat);
    const auto externalFloatBytes { bytes (externalFloat) };
    check (editor.assignSamples (0, 0, { externalFloat.getFullPathName () }) && bytes (externalFloat) == externalFloatBytes, "External floating WAV source is preserved");

    const auto kick { external.getChildFile ("kick.wav") }, occupied { preset.getChildFile ("kick.wav") };
    makeAudio (kick);
    check (occupied.replaceWithText ("Existing sample must survive"), "Create colliding destination");
    check (editor.assignSamples (0, 0, { kick.getFullPathName () }), "Collision gets a new filename");
    check (zone.getSample () == "kick-1.wav" && occupied.loadFileAsString () == "Existing sample must survive", "Compatible import does not overwrite occupied destination");
    checkAudio (audio, preset.getChildFile (zone.getSample ()));
    check (SafeAudioImport::copyNew (kick, occupied).failed () && occupied.loadFileAsString () == "Existing sample must survive", "Low-level publication refuses overwrite");
    const auto failedReadOutput { preset.getChildFile ("failed-read.wav") };
    check (SafeAudioImport::copyNew (external, failedReadOutput).failed () && ! failedReadOutput.exists (), "Failed read leaves no partial imported output");

    const auto snapshot { tree.createCopy () };
    const auto initialFiles { preset.findChildFiles (juce::File::findFiles, false).size () };
    check (! editor.assignSamples (0, 0, { aiff.getFullPathName (), external.getChildFile ("missing.wav").getFullPathName () }), "Batch import propagates later read failure");
    check (tree.isEquivalentTo (snapshot) && preset.findChildFiles (juce::File::findFiles, false).size () == initialFiles, "Failed batch leaves every zone unchanged and removes its unused new outputs");
    check (editor.getLastAssignmentError ().isNotEmpty (), "Assignment failure supplies a user-visible reason");
    check (! editor.assignSamples (-1, 0, { kick.getFullPathName () }) && ! editor.assignSamples (0, 8, { kick.getFullPathName () }), "Invalid assignment indexes fail safely");

    ChannelProperties rightChannel (tree.getChild (1), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
    ZoneProperties rightZone (rightChannel.getZoneVT (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
    rightZone.setSample ("independent.wav", false);
    const auto stereo { external.getChildFile ("stereo.wav") };
    makeAudio (stereo, false, 48000, 2);
    check (editor.assignSamples (0, 1, { stereo.getFullPathName () }), "Stereo assignment beside occupied independent channel");
    check (rightChannel.getChannelMode () == ChannelProperties::ChannelMode::master && rightZone.getSample () == "independent.wav",
           "Empty same-index zone does not permit repurposing an occupied next channel");
    rightZone.setSample ("", false);
    zone.setSampleStart (4, false);
    zone.setSampleEnd (100, false);
    zone.setLoopStart (8, false);
    zone.setLoopLength (24.0, false);
    check (editor.assignSamples (0, 1, { stereo.getFullPathName () }) && rightChannel.getChannelMode () == ChannelProperties::ChannelMode::stereoRight,
           "Later stereo zone can pair a wholly empty next channel");
    ZoneProperties secondLeft (tree.getChild (0).getChild (1), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
    ZoneProperties secondRight (rightChannel.getZoneVT (1), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
    check (rightZone.getSample () == zone.getSample () && rightZone.getSide () == 0
           && rightZone.getSampleStart () == zone.getSampleStart () && rightZone.getSampleEnd () == zone.getSampleEnd ()
           && rightZone.getLoopStart () == zone.getLoopStart () && rightZone.getLoopLength () == zone.getLoopLength ()
           && secondRight.getSample () == secondLeft.getSample () && secondRight.getSide () == 1,
           "New pair initializes earlier mono zones and stereo zones with matching ranges and correct sides");
    check (editor.assignSamples (0, 0, { stereo.getFullPathName () }) && rightChannel.getChannelMode () == ChannelProperties::ChannelMode::stereoRight
           && rightZone.getSample () == zone.getSample () && rightZone.getSide () == 1, "Empty next master can become a matching stereo pair");
    check (editor.assignSamples (0, 0, { kick.getFullPathName () }) && rightChannel.getChannelMode () == ChannelProperties::ChannelMode::stereoRight
           && rightZone.getSample () == zone.getSample () && rightZone.getSide () == 0, "Mono assignment to existing pair replaces both sides with mono");
    check (rightZone.getMinVoltage () == zone.getMinVoltage (), "Paired voltage boundary follows assignment distribution");

    juce::File converted, backup;
    const auto tooFast { external.getChildFile ("384k.wav") };
    makeAudio (tooFast, false, 384000);
    const auto fastBytes { bytes (tooFast) };
    const auto unsupported { SafeAudioImport::convertInPlace (audio, tooFast, converted, backup) };
    check (unsupported.failed () && unsupported.getErrorMessage ().contains ("resampling") && converted == juce::File () && backup == juce::File () && bytes (tooFast) == fastBytes,
           "Unsupported sample rate is rejected without false success or changing original");
    check (SafeAudioImport::importFile (audio, kick, occupied, converted).failed (), "Non-directory destination fails safely");
    const auto corrupt { external.getChildFile ("corrupt.wav") };
    check (corrupt.replaceWithText ("not audio"), "Create corrupt fixture");
    check (SafeAudioImport::importFile (audio, corrupt, preset, converted).failed () && corrupt.loadFileAsString () == "not audio", "Corrupt source remains untouched");

    const auto repaired { external.getChildFile ("repair.wav") };
    makeFloatWave (repaired);
    const auto repairBytes { bytes (repaired) };
    check (SafeAudioImport::convertInPlace (audio, repaired, converted, backup).wasOk () && converted == repaired, "Validator conversion replaces only after staging");
    check (backup.getFileName ().startsWith (".a8-original-") && bytes (backup) == repairBytes, "Validator conversion retains recoverable original backup");
    checkAudio (audio, repaired);
    const auto failed { external.getChildFile ("failed-install.wav") };
    makeFloatWave (failed);
    const auto failedBytes { bytes (failed) };
    check (SafeAudioImport::convertInPlace (audio, failed, converted, backup,
        [] (const juce::File&, const juce::File&) { return juce::Result::fail ("Injected installation failure"); }).failed (), "Installation failure is propagated");
    check (bytes (failed) == failedBytes && bytes (backup) == failedBytes && converted == juce::File (), "Failed validator installation restores original and retains backup");
    const auto other { aiff.withFileExtension ("wav") };
    check (other.replaceWithText ("Preserve collision"), "Create validator collision");
    check (SafeAudioImport::convertInPlace (audio, aiff, converted, backup).failed () && bytes (aiff) == aiffBytes && other.loadFileAsString () == "Preserve collision", "Validator never overwrites unrelated WAV destination");
    const auto retainedAiff { external.getChildFile ("retained-original.aiff") };
    makeAudio (retainedAiff, true);
    const auto retainedBytes { bytes (retainedAiff) };
    check (SafeAudioImport::convertInPlace (audio, retainedAiff, converted, backup).wasOk ()
           && converted == retainedAiff.withFileExtension ("wav") && backup == retainedAiff
           && bytes (retainedAiff) == retainedBytes, "Validator keeps non-WAV originals at their original paths");
    checkAudio (audio, converted);

    const auto longFolder { root.getChildFile (juce::String::repeatedString ("a", 52) + ".folder") };
    check (longFolder.createDirectory ().wasOk (), "Create overlong folder name");
    const auto collision { root.getChildFile (juce::String::repeatedString ("a", 31)) };
    check (collision.createDirectory ().wasOk (), "Create folder-name collision");
    juce::File renamed;
    check (SafeRename::automaticDestination (longFolder, renamed).wasOk () && renamed.getFileName ().length () <= 31 && renamed != collision, "Auto-rename enforces folder limit including collision suffix");
    check (SafeRename::apply (longFolder, renamed.getFileName ()).wasOk (), "Automatic folder target passes shared validation");
    const auto longFile { root.getChildFile (juce::String::repeatedString ("b", 60) + ".aiff") };
    check (longFile.replaceWithText ("preserve"), "Create overlong file name");
    check (SafeRename::automaticDestination (longFile, renamed).wasOk () && renamed.getFileName ().length () <= 47 && renamed.getFileExtension () == ".aiff", "Auto-rename file limit includes actual extension length");
    testWavPadding (audio, external, preset);
    testCvImportSafety (audio, external, preset);
    std::cout << "PASS: source-preserving imports, conversion staging/backup/recovery, collisions, failed assignment and type-aware name limits\n";
}
