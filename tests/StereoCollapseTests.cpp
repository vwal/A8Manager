#include "Assimil8or/Audio/StereoCollapse.h"
#include "Assimil8or/Audio/AudioManager.h"
#include "Assimil8or/Audio/CvSampleSafety.h"
#include "Assimil8or/Audio/WaveformDesignExport.h"
#include "Assimil8or/Audio/WaveformDesignRecall.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "Assimil8or/Preset/PresetProperties.h"
#include <juce_cryptography/juce_cryptography.h>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace
{
    constexpr int frames { 2 * 8192 + 137 };
    void require (bool okay, const char* message) { if (! okay) throw std::runtime_error (message); }
    void succeeded (const juce::Result& result) { if (result.failed ()) throw std::runtime_error (result.getErrorMessage ().toStdString ()); }

    float sample (int side, int frame) { return (side == 0 ? 0.25f : -0.5f) + static_cast<float> (frame % (side == 0 ? 256 : 128)) / 2048.0f; }

    void wave (const juce::File& file, int channels = 2, int length = frames, double rate = 48000, bool cv = false, bool floating = false)
    {
        juce::AudioBuffer<float> data (channels, length);
        for (int side { 0 }; side < channels; ++side)
            for (int index { 0 }; index < length; ++index)
                data.setSample (side, index, floating && index == 0 ? std::numeric_limits<float>::quiet_NaN () : sample (side, index));
        std::unique_ptr<juce::OutputStream> stream { file.createOutputStream () };
        juce::WavAudioFormat format;
        std::unordered_map<juce::String, juce::String> metadata;
        if (cv) metadata.emplace (juce::WavAudioFormat::riffInfoComment2, CvSampleSafety::cvMarker);
        auto writer { format.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (rate).withNumChannels (channels)
                                              .withBitsPerSample (floating ? 32 : 24).withMetadataValues (metadata)
                                              .withSampleFormat (floating ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                                         : juce::AudioFormatWriterOptions::SampleFormat::integral)) };
        require (writer && writer->writeFromAudioSampleBuffer (data, 0, length) && writer->flush (), "Create owned stereo-collapse WAV fixture");
    }

    juce::ValueTree defaults ()
    {
        return ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ()
            .getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType).createCopy ();
    }

    ChannelProperties channel (juce::ValueTree tree, int index)
    {
        return { tree.getChild (index), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no };
    }

    ZoneProperties zone (juce::ValueTree tree, int index, int zoneIndex = 0)
    {
        return { tree.getChild (index).getChild (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no };
    }

    juce::ValueTree pair (juce::String name, int left = 0)
    {
        auto tree { defaults () };
        channel (tree, left + 1).setChannelMode (ChannelProperties::stereoRight, false);
        channel (tree, left).setPitch (7.25, false);
        channel (tree, left).setPan (-0.4, false);
        channel (tree, left).setLoopLengthIsEnd (true, false);
        channel (tree, left + 1).setPan (0.75, false);
        channel (tree, left + 1).setLevel (-5.0, false);
        for (int index : { 0, 2 })
            for (int side { 0 }; side < 2; ++side)
            {
                auto value { zone (tree, left + side, index) };
                value.setSample (name, false);
                value.setSide (side, false);
                value.setSampleStart (10 + index, false);
                value.setSampleEnd (100 + index, false);
                value.setLoopStart (1000 + index, false);
                value.setLoopLength (201.5 + index, false);
                value.setMinVoltage (index == 0 ? 0.0 : -5.0, false);
                value.setPitchOffset (-3.5, false);
                value.setLevelOffset (-2.0, false);
            }
        return tree;
    }

    void verifyWave (juce::File file, StereoCollapse::Mode mode, bool swapped = false, bool bothMono = false, bool cv = false)
    {
        AudioManager audio;
        auto reader { audio.getReaderFor (file) };
        require (reader && reader->numChannels == 1 && reader->bitsPerSample == 24 && ! reader->usesFloatingPointData
                 && reader->lengthInSamples == frames && reader->sampleRate == 48000, "Mono conversion preserves full sample length/rate and writes PCM24");
        require (CvSampleSafety::isCv (file, *reader) == cv, "Mono conversion preserves audio/CV purpose");
        juce::AudioBuffer<float> data (1, frames);
        require (reader->read (&data, 0, frames, 0, true, false), "Read all converted samples including the partial final processing block");
        for (int index { 0 }; index < frames; ++index)
        {
            const auto left { sample (swapped ? 1 : 0, index) };
            const auto right { sample (bothMono || swapped ? 0 : 1, index) };
            const auto expected { mode == StereoCollapse::Mode::merge ? (left + right) * 0.5f : mode == StereoCollapse::Mode::keepLeft ? left : right };
            require (std::abs (data.getSample (0, index) - expected) < 2.0 / 8388608.0, "Merge/select output has exact intended sample contents, DC offset, gain and endpoints");
        }
        WaveformDesignRecall::RecalledDesign recalled;
        require (WaveformDesignRecall::recallWave (file, recalled).failed (), "Converted PCM does not falsely claim its original waveform-designer recipe");
    }
}

void testStereoCollapse ()
{
    using namespace StereoCollapse;
    const auto root { juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("a8-stereo-collapse-test-" + juce::Uuid ().toString ()) };
    succeeded (root.createDirectory ());
    struct Cleanup { juce::File file; ~Cleanup () { file.deleteRecursively (); } } remove { root };
    const auto original { root.getChildFile ("Original stereo.wav") };
    const auto second { root.getChildFile ("Second.wav") };
    wave (original);
    wave (second);
    const auto originalHash { juce::SHA256 (original).toHexString () };
    const auto secondHash { juce::SHA256 (second).toHexString () };
    auto tree { pair (original.getFileName ()) };
    const auto before { tree.createCopy () };
    const auto saved { root.getChildFile ("prst001.yml") };
    require (saved.replaceWithText ("Saved preset is not touched until Save"), "Create saved preset preservation fixture");

    for (const auto mode : { Mode::merge, Mode::keepLeft, Mode::keepRight })
    {
        Result result;
        succeeded (prepare (root, tree, mode == Mode::keepRight ? 1 : 0, mode, result));
        require (result.leftChannel == 0 && result.createdFiles.size () == 1 && result.ownership.size () == 1,
                 "Either side resolves to left, and repeated zone source pairs share one converted WAV");
        require (result.createdFiles[0].getFileName ().startsWith ("Original-stereo-") && result.createdFiles[0].getFileName ().length () <= 47,
                 "Converted filenames start with the source name and retain safe unique suffixes");
        verifyWave (result.createdFiles[0], mode);
        auto expectedLeft { before.getChild (0).createCopy () };
        for (int index { 0 }; index < 8; ++index)
        {
            ZoneProperties expected (expectedLeft.getChild (index), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            if (expected.getSample ().isNotEmpty ()) expected.setSample (result.createdFiles[0].getFileName (), false);
            expected.setSide (0, false);
        }
        require (result.editedPreset.getChild (0).isEquivalentTo (expectedLeft), "Left settings, pan, pitch, zone voltages and all markers stay untouched except sample/side");
        require (result.editedPreset.getChild (1).isEquivalentTo (defaults ().getChild (1)), "Right channel and all eight zones become complete defaults");
        for (int index { 2 }; index < 8; ++index)
            require (result.editedPreset.getChild (index).isEquivalentTo (before.getChild (index)), "Other channels remain unchanged");
        require (tree.isEquivalentTo (before) && saved.loadFileAsString () == "Saved preset is not touched until Save", "Preparation never mutates the source preset or its saved YAML");
        const auto output { result.createdFiles[0] };
        succeeded (cleanup (result));
        require (! output.exists () && ! result.editedPreset.isValid () && result.createdFiles.isEmpty () && result.ownership.empty (), "Stale/unapplied conversion removes only owned output");
        succeeded (cleanup (result));
    }

    // Selection means the assigned side, not hard-coded WAV L/R indices.
    auto swapped { tree.createCopy () };
    for (int index : { 0, 2 }) { zone (swapped, 0, index).setSide (1, false); zone (swapped, 1, index).setSide (0, false); }
    Result selected;
    succeeded (prepare (root, swapped, 1, Mode::keepRight, selected));
    verifyWave (selected.createdFiles[0], Mode::keepRight, true);
    succeeded (cleanup (selected));

    const auto monoLeft { root.getChildFile ("Mono-left.wav") }, monoRight { root.getChildFile ("Mono-right.wav") };
    wave (monoLeft, 1); wave (monoRight, 1);
    auto separate { tree.createCopy () };
    for (int index : { 0, 2 })
    {
        zone (separate, 0, index).setSample (monoLeft.getFileName (), false);
        zone (separate, 1, index).setSample (monoRight.getFileName (), false);
        zone (separate, 1, index).setSide (0, false);
    }
    succeeded (prepare (root, separate, 0, Mode::merge, selected));
    verifyWave (selected.createdFiles[0], Mode::merge, false, true);
    succeeded (cleanup (selected));
    juce::MemoryBlock paddedMono;
    require (monoLeft.loadFileAsData (paddedMono), "Read odd-length mono fixture including final padding");
    const auto unpadded { root.getChildFile ("No-final-pad.wav") };
    require (unpadded.replaceWithData (paddedMono.getData (), paddedMono.getSize () - 1), "Omit only the last non-PCM WAV padding byte");
    auto noPad { separate.createCopy () };
    zone (noPad, 0).setSample (unpadded.getFileName (), false);
    succeeded (prepare (root, noPad, 0, Mode::keepLeft, selected));
    verifyWave (selected.createdFiles[0], Mode::keepLeft, false, true);
    succeeded (cleanup (selected));
    for (const auto controlling : { ChannelProperties::master, ChannelProperties::link, ChannelProperties::cycle })
    {
        auto last { pair (original.getFileName (), 6) };
        channel (last, 6).setChannelMode (controlling, false);
        require (pairLeftIndex (last, 7) == 6, "Last-channel right side resolves to CH 7");
        succeeded (prepare (root, last, 7, Mode::keepLeft, selected));
        require (selected.leftChannel == 6 && channel (selected.editedPreset, 6).getChannelMode () == controlling
                 && selected.editedPreset.getChild (7).isEquivalentTo (defaults ().getChild (7)), "CH 7/8 conversion preserves controlling mode and frees channel eight");
        succeeded (cleanup (selected));
    }
    const auto cv { root.getChildFile ("CV-stereo.wav") };
    wave (cv, 2, frames, 48000, true);
    auto cvPair { pair (cv.getFileName ()) };
    channel (cvPair, 0).setMixLevel (-90, false); channel (cvPair, 1).setMixLevel (-90, false);
    channel (cvPair, 0).setMixMod ("Off", 0, false); channel (cvPair, 1).setMixMod ("Off", 0, false);
    succeeded (prepare (root, cvPair, 1, Mode::merge, selected));
    verifyWave (selected.createdFiles[0], Mode::merge, false, false, true);
    require (channel (selected.editedPreset, 0).getMixLevel () == -90, "CV remains excluded from the stereo mix");
    succeeded (cleanup (selected));

    auto fails = [&] (const juce::ValueTree& invalid, int clicked = 0)
    {
        const auto snapshot { invalid.createCopy () };
        const auto count { root.getNumberOfChildFiles (juce::File::findFilesAndDirectories) };
        Result result;
        require (prepare (root, invalid, clicked, Mode::merge, result).failed () && ! result.editedPreset.isValid ()
                 && result.createdFiles.isEmpty () && root.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == count
                 && snapshot.isEquivalentTo (invalid), "Invalid conversion fails without publication, leftover stage or preset changes");
    };
    auto invalid { tree.createCopy () };
    zone (invalid, 1, 2).setSample ("", false); fails (invalid);
    invalid = tree.createCopy (); zone (invalid, 0).setSample ("missing.wav", false); fails (invalid);
    invalid = tree.createCopy (); zone (invalid, 0).setSample ("../outside.wav", false); fails (invalid);
    invalid = tree.createCopy (); zone (invalid, 0).setSide (2, false); fails (invalid);
    invalid = separate.createCopy (); zone (invalid, 1).setSide (1, false); fails (invalid);
    invalid = tree.createCopy (); channel (invalid, 0).setChannelMode (ChannelProperties::stereoRight, false); fails (invalid); fails (invalid, 1);
    invalid = tree.createCopy (); channel (invalid, 2).setChannelMode (ChannelProperties::stereoRight, false); fails (invalid);
    invalid = tree.createCopy (); invalid.getChild (0).removeChild (7, nullptr); fails (invalid);
    invalid = cvPair.createCopy (); channel (invalid, 0).setMixLevel (0, false); fails (invalid);
    invalid = cvPair.createCopy (); zone (invalid, 1).setSample (original.getFileName (), false); fails (invalid);
    invalid = cvPair.createCopy (); zone (invalid, 0, 2).setSample (original.getFileName (), false); zone (invalid, 1, 2).setSample (original.getFileName (), false); fails (invalid);
    const auto shortWave { root.getChildFile ("Short.wav") }, fast { root.getChildFile ("Fast.wav") }, nan { root.getChildFile ("Float-NaN.wav") };
    wave (shortWave, 2, frames - 1); wave (fast, 2, frames, 96000); wave (nan, 2, frames, 48000, false, true);
    for (const auto& file : { shortWave, fast, nan })
    {
        invalid = tree.createCopy (); zone (invalid, 1).setSample (file.getFileName (), false); fails (invalid);
    }
    juce::MemoryBlock bytes;
    require (original.loadFileAsData (bytes), "Read owned sample for malformed RIFF fixture");
    const auto broken { root.getChildFile ("Truncated.wav") };
    require (broken.replaceWithData (bytes.getData (), bytes.getSize () - 1), "Create physically truncated WAV fixture");
    invalid = tree.createCopy (); zone (invalid, 0).setSample (broken.getFileName (), false); fails (invalid);
#if ! JUCE_WINDOWS
    const auto link { root.getChildFile ("Linked.wav") };
    std::error_code error;
    std::filesystem::create_symlink (std::filesystem::path (reinterpret_cast<const char8_t*> (original.getFullPathName ().toRawUTF8 ())),
                                     std::filesystem::path (reinterpret_cast<const char8_t*> (link.getFullPathName ().toRawUTF8 ())), error);
    require (! error, "Create owned sample symlink fixture");
    invalid = tree.createCopy (); zone (invalid, 0).setSample (link.getFileName (), false); fails (invalid);
#endif

    // An exclusive-publish collision after one successful publication must
    // roll back only that first output, not the later foreign occupant.
    auto twoFiles { tree.createCopy () };
    zone (twoFiles, 0, 2).setSample (second.getFileName (), false); zone (twoFiles, 1, 2).setSample (second.getFileName (), false);
    int publications { 0 };
    juce::File firstOutput, foreignOutput;
    Result collision;
    const auto failed { prepare (root, twoFiles, 0, Mode::merge, collision, [&] (const juce::File& staged, const juce::File& target)
    {
        if (++publications == 1) firstOutput = target;
        else
        {
            foreignOutput = target;
            require (target.replaceWithText ("Other writer's file"), "Inject publication race fixture");
        }
        return WaveformDesign::ExportSupport::publishExclusive (staged, target);
    }) };
    require (failed.failed () && publications == 2 && ! firstOutput.exists () && foreignOutput.loadFileAsString () == "Other writer's file"
             && collision.createdFiles.isEmpty () && ! collision.editedPreset.isValid (), "Collision rollback preserves a foreign file and removes only our previously published file");

    Result firstResult, secondResult;
    succeeded (prepare (root, tree, 0, Mode::keepLeft, firstResult));
    succeeded (prepare (root, tree, 0, Mode::keepLeft, secondResult));
    require (firstResult.createdFiles[0] != secondResult.createdFiles[0], "Repeated conversions never overwrite an existing generated file");
    const auto changed { firstResult.createdFiles[0] };
    require (changed.appendText ("external modification"), "Change pending output to test conservative rollback ownership");
    require (cleanup (firstResult).failed () && changed.existsAsFile (), "Stale cleanup preserves externally changed converted files");
    succeeded (cleanup (secondResult));
    require (juce::SHA256 (original).toHexString () == originalHash && juce::SHA256 (second).toHexString () == secondHash
             && tree.isEquivalentTo (before), "Original samples and source preset remain unchanged across all conversions and failures");
    std::cout << "PASS: stereo-pair mono merge/side selection, all-zone settings and lengths, reuse, CV isolation, ownership rollback and source preservation\n";
}
