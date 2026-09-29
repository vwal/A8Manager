#include "Assimil8or/Audio/WaveformDesignRecall.h"
#include "Assimil8or/Audio/WaveformDesignAssignment.h"
#include "Assimil8or/Audio/WaveformDesignExport.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace
{
    void requireRecall (bool ok, const char* message) { if (! ok) throw std::runtime_error (message); }

    void writeRecallWave (const juce::File& file, int frames, double rate, int channels, int bits, const juce::String& purpose)
    {
        juce::AudioBuffer<float> audio (channels, frames);
        audio.clear ();
        std::unique_ptr<juce::OutputStream> stream { file.createOutputStream () };
        requireRecall (stream != nullptr && stream->setPosition (0), "Create owned WAV recall fixture");
        std::unordered_map<juce::String, juce::String> metadata;
        if (purpose.isNotEmpty ()) metadata.emplace (juce::WavAudioFormat::riffInfoComment2, purpose);
        juce::WavAudioFormat format;
        auto writer { format.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (rate)
                                              .withNumChannels (channels).withBitsPerSample (bits).withMetadataValues (metadata)) };
        requireRecall (writer && writer->writeFromAudioSampleBuffer (audio, 0, frames) && writer->flush (), "Write WAV recall fixture");
    }
}

void testWaveformDesignRecall ()
{
    using namespace WaveformDesign;
    using namespace WaveformDesignRecall;
    const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-waveform-recall", "", false) };
    requireRecall (folder.createDirectory ().wasOk (), "Create owned waveform recall directory");
    struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { folder };
    const auto defaults { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType) };
    auto sameSettings = [] (const Settings& first, const Settings& second)
    {
        return juce::JSON::toString (toJson (first)) == juce::JSON::toString (toJson (second));
    };
    auto expectFailure = [&] (const juce::Result& status, const RecalledDesign& result)
    {
        requireRecall (status.failed () && status.getErrorMessage ().isNotEmpty (), "Invalid/unassociated recall fails with an explanation");
        requireRecall (result.recipe == juce::File () && result.displayName.isEmpty () && result.voiceIndex == 0
                       && sameSettings (result.settings, Settings {}), "A failed recall does not leak previously loaded settings or recipe identity");
    };
    auto seedResult = [] ()
    {
        RecalledDesign result;
        result.settings.amplitude = 0.123;
        result.recipe = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("old-recipe.json");
        result.displayName = "Old design";
        result.voiceIndex = 7;
        return result;
    };

    for (const auto mode : { Mode::oscillator, Mode::modulation, Mode::layers })
    {
        auto settings { startingPoint (mode, mode == Mode::modulation ? Shape::drawn : Shape::pulse) };
        settings.cycleFrames = 64;
        settings.durationSeconds = 51.0 / 48000.0; // Odd PCM24 data length exercises RIFF padding.
        settings.phaseDegrees = 17.0;
        settings.pulseWidth = 0.38;
        settings.amplitude = 0.71;
        settings.offset = 0.1;
        settings.playback = Playback::loop;
        settings.steps[3] = 0.456;
        settings.drawn[12] = -0.37;
        settings.measuredFullScaleVolts = 4.96;
        spreadVoices (settings, 8, 12.0, 150.0, 0.73);
        settings.voices[7].level = 0.42;
        const auto count { mode == Mode::layers ? 8 : 1 };

        ExportResult package;
        requireRecall (exportDesign (settings, folder, "Recall-package", package, 76).wasOk (), "Create a real package for each designer mode");
        for (int index { 0 }; index < count; ++index)
        {
            auto recalled { seedResult () };
            requireRecall (recallWave (package.waves[index], recalled).wasOk (), "Recall any audio, CV or bank voice from an actual exported package");
            requireRecall (sameSettings (settings, recalled.settings) && recalled.recipe == package.recipe && recalled.voiceIndex == index,
                           "Recall returns the complete original recipe and exact selected bank voice index");
            requireRecall (recalled.displayName == package.folder.getFileName ().substring (3), "Package recall uses its meaningful folder name without an extra A8 prefix");
        }
        auto loaded { seedResult () };
        requireRecall (loadRecipe (package.recipe, loaded).wasOk () && sameSettings (settings, loaded.settings) && loaded.voiceIndex == 0,
                       "Explicit loading restores the same complete recipe and resets the bank voice index");

        AssignmentResult assignment;
        requireRecall (prepareAssignment (settings, folder, "Assigned-waveform", defaults.createCopy (), 0, 0, assignment).wasOk (),
                       "Create a real assignment for each designer mode");
        for (int index { 0 }; index < count; ++index)
        {
            auto recalled { seedResult () };
            requireRecall (recallWave (assignment.waves[index], recalled).wasOk (), "Recall any voice from a uniquely named assignment");
            requireRecall (sameSettings (settings, recalled.settings) && recalled.recipe == assignment.recipe && recalled.voiceIndex == index,
                           "Assigned bank followers resolve to their own shared recipe without losing voice controls");
            requireRecall (recalled.displayName == "Assigned-waveform", "Assignment recall removes generated filename prefix, token and voice suffix");
        }
    }

    auto settings { startingPoint (Mode::oscillator, Shape::sine) };
    settings.cycleFrames = 64;
    const auto fixture { folder.getChildFile ("fixtures") };
    requireRecall (fixture.createDirectory ().wasOk (), "Create isolated malformed/association fixtures");
    const auto validJson { juce::JSON::toString (toJson (settings)) };
    const auto recipe { fixture.getChildFile ("design.json") };
    const auto wave { fixture.getChildFile ("voice-01.wav") };
    requireRecall (recipe.replaceWithText (validJson), "Write genuine recipe schema for association fixtures");
    auto checkBadWave = [&] (const juce::File& file)
    {
        auto recalled { seedResult () };
        const auto status { recallWave (file, recalled) };
        expectFailure (status, recalled);
    };
    auto writeWave = [&] (int frames, double rate, int channels, int bits, const juce::String& purpose)
    {
        // FileOutputStream appends by default, so remove only this owned fixture
        // before rewriting it with a possibly shorter header/data layout.
        requireRecall (! wave.exists () || wave.deleteFile (), "Replace only the owned waveform test fixture");
        writeRecallWave (wave, frames, rate, channels, bits, purpose);
    };
    writeWave (64, 48000, 1, 24, CvSampleSafety::audioMarker);
    RecalledDesign recalled;
    requireRecall (recallWave (wave, recalled).wasOk (), "Complete known generated association fixture is accepted");
    for (const auto* name : { "unrelated.wav", "voice-00.wav", "voice-09.wav", "voice-1.wav", "voice-01.aiff", "Other-0123456789ab-01.wav", "A8-Test-not-a-token-01.wav" })
    {
        const auto unrelated { fixture.getChildFile (name) };
        requireRecall (wave.copyFileTo (unrelated), "Create unrelated WAV filename fixture");
        checkBadWave (unrelated);
    }
    const auto extraVoice { fixture.getChildFile ("voice-02.wav") };
    requireRecall (wave.copyFileTo (extraVoice), "Create out-of-range recipe voice fixture");
    checkBadWave (extraVoice);
    const auto missingAssignment { fixture.getChildFile ("A8-Test-0123456789ab-01.wav") };
    requireRecall (wave.copyFileTo (missingAssignment), "Create assigned WAV without its exact recipe");
    checkBadWave (missingAssignment); // Must not fall back to the unrelated design.json.
    checkBadWave (fixture.getChildFile ("missing.wav"));
    checkBadWave (fixture);

    writeWave (64, 48000, 1, 24, {});
    checkBadWave (wave);
    requireRecall (loadRecipe (recipe, recalled).wasOk (), "Older untagged exports can still load their recipe explicitly");
    writeWave (64, 48000, 1, 24, CvSampleSafety::cvMarker);
    checkBadWave (wave);
    writeWave (64, 48000, 1, 24, juce::String (CvSampleSafety::audioMarker) + "\n" + CvSampleSafety::cvMarker);
    checkBadWave (wave);
    writeWave (64, 96000, 1, 24, CvSampleSafety::audioMarker);
    checkBadWave (wave);
    writeWave (128, 48000, 1, 24, CvSampleSafety::audioMarker);
    checkBadWave (wave);
    writeWave (32, 48000, 1, 24, CvSampleSafety::audioMarker);
    checkBadWave (wave);
    writeWave (64, 48000, 2, 24, CvSampleSafety::audioMarker);
    checkBadWave (wave);
    writeWave (64, 48000, 1, 16, CvSampleSafety::audioMarker);
    checkBadWave (wave);
    writeWave (64, 48000, 1, 24, CvSampleSafety::audioMarker);
    juce::MemoryBlock intact;
    requireRecall (wave.loadFileAsData (intact) && intact.getSize () > 20, "Read bounded owned WAV bytes for malformed header fixtures");
    auto invalidRiff { intact };
    auto* bytes { static_cast<unsigned char*> (invalidRiff.getData ()) };
    bytes[16] = 0xff; bytes[17] = 0xff; bytes[18] = 0xff; bytes[19] = 0x7f;
    requireRecall (wave.replaceWithData (invalidRiff.getData (), invalidRiff.getSize ()), "Write corrupt RIFF metadata length fixture");
    checkBadWave (wave);
    auto invalidInfo { intact };
    auto* infoBytes { static_cast<unsigned char*> (invalidInfo.getData ()) };
    auto infoOffset { size_t { 0 } };
    for (size_t index { 0 }; index + 8 <= invalidInfo.getSize (); ++index)
        if (std::memcmp (infoBytes + index, "ICMT", 4) == 0) { infoOffset = index; break; }
    requireRecall (infoOffset != 0, "Find real generated INFO comment for nested metadata extent regression");
    infoBytes[infoOffset + 4] = 0xff; infoBytes[infoOffset + 5] = 0xff; infoBytes[infoOffset + 6] = 0xff; infoBytes[infoOffset + 7] = 0x7f;
    requireRecall (wave.replaceWithData (invalidInfo.getData (), invalidInfo.getSize ()), "Write corrupt nested INFO extent fixture");
    checkBadWave (wave);
    auto invalidSampler { intact };
    auto* samplerBytes { static_cast<unsigned char*> (invalidSampler.getData ()) };
    auto samplerOffset { size_t { 0 } };
    for (size_t index { 0 }; index + 44 <= invalidSampler.getSize (); ++index)
        if (std::memcmp (samplerBytes + index, "smpl", 4) == 0) { samplerOffset = index; break; }
    requireRecall (samplerOffset != 0 && juce::ByteOrder::littleEndianInt (samplerBytes + samplerOffset + 36) == 0,
                   "JUCE emits a real default smpl chunk even though the generated WAV declares no sample loops");
    samplerBytes[samplerOffset + 36] = 0xff; samplerBytes[samplerOffset + 37] = 0xff;
    samplerBytes[samplerOffset + 38] = 0xff; samplerBytes[samplerOffset + 39] = 0x7f;
    requireRecall (wave.replaceWithData (invalidSampler.getData (), invalidSampler.getSize ()), "Write corrupt smpl loop count fixture");
    checkBadWave (wave);
    invalidSampler = intact;
    samplerBytes = static_cast<unsigned char*> (invalidSampler.getData ());
    samplerBytes[samplerOffset + 40] = 0xff; samplerBytes[samplerOffset + 41] = 0xff;
    samplerBytes[samplerOffset + 42] = 0xff; samplerBytes[samplerOffset + 43] = 0x7f;
    requireRecall (wave.replaceWithData (invalidSampler.getData (), invalidSampler.getSize ()), "Write corrupt smpl extra data size fixture");
    checkBadWave (wave);
    requireRecall (wave.replaceWithData (intact.getData (), intact.getSize () - 1), "Write truncated RIFF fixture");
    checkBadWave (wave);
    requireRecall (wave.replaceWithData (intact.getData (), intact.getSize ()), "Restore intact generated WAV fixture");

    auto badRecipe = [&] (const juce::String& contents)
    {
        requireRecall (recipe.replaceWithText (contents), "Write malformed recipe fixture");
        auto result { seedResult () };
        const auto status { loadRecipe (recipe, result) };
        expectFailure (status, result);
        checkBadWave (wave);
    };
    badRecipe ({});
    badRecipe ("{}");
    badRecipe ("{invalid JSON");
    badRecipe (validJson + "\nAnother object follows");
    auto future { toJson (settings) };
    future.getDynamicObject ()->setProperty ("version", 99);
    badRecipe (juce::JSON::toString (future));
    auto invalid { toJson (settings) };
    invalid["settings"].getDynamicObject ()->setProperty ("amplitude", 1.5);
    badRecipe (juce::JSON::toString (invalid));
    badRecipe (validJson.trimEnd ().dropLastCharacters (1) + ",\"extra\":" + juce::String::repeatedString ("[", 64) + "0" + juce::String::repeatedString ("]", 64) + "}");
    badRecipe (validJson + juce::String::repeatedString (" ", 1024 * 1024));
    for (const auto contents : { juce::MemoryBlock ("{}\0", 3), juce::MemoryBlock ("\xff", 1) })
    {
        requireRecall (recipe.replaceWithData (contents.getData (), contents.getSize ()), "Write invalid recipe encoding fixture");
        auto result { seedResult () };
        const auto status { loadRecipe (recipe, result) };
        expectFailure (status, result);
        checkBadWave (wave);
    }
    auto result { seedResult () };
    const auto status { loadRecipe (fixture.getChildFile ("missing.json"), result) };
    expectFailure (status, result);
    const auto named { fixture.getChildFile ("My-design.json") };
    requireRecall (named.replaceWithText (validJson.replace ("\n", "\r\n")), "Write an explicitly saved named CRLF recipe");
    requireRecall (loadRecipe (named, recalled).wasOk () && recalled.displayName == "My-design" && sameSettings (settings, recalled.settings),
                   "Explicit loading accepts a complete named saved recipe and portable CRLF line endings");
    std::cout << "PASS: generated waveform recipe recall, all bank voices, naming/purpose/dimension association, malformed RIFF and bounded JSON rejection\n";
}
