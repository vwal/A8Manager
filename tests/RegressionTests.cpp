#include "Assimil8or/Assimil8orPreset.h"
#include "Assimil8or/Preset/ZoneContinuation.h"
#include "Assimil8or/Preset/ZoneSampleRanges.h"
#include "Assimil8or/Audio/AudioManager.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "oolib/Debug/DebugLog.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// The probes do not need the application's file logger or GUI startup.
void DebugLog (juce::String, juce::String) {}
void FlushDebugLog () {}

namespace
{
    void require (bool condition, const juce::String& message)
    {
        // Unlike jassert, these checks also run in Release builds.
        if (! condition)
            throw std::runtime_error (message.toStdString ());
    }

    void testParserAndCv ()
    {
        Assimil8orPreset parser;
        for (auto repetition { 0 }; repetition < 100; ++repetition)
        {
            parser.parse ({ "UnknownGlobal: 1", "Preset 1:", "UnknownPreset: 2", "Name: ScopeTest",
                            "Channel 1:", "UnknownChannel: 3", "Level: -6", "Zone 1:",
                            "UnknownZone: 4", "Sample: example.wav", "Channel 2:", "Pitch: 2" });
            const auto errors { parser.getParseErrorsVT () };
            require (errors.getNumChildren () == 4, "Expected four unknown-parameter diagnostics");
            for (const auto error : errors)
                require (error.getProperty ("type").toString () == "UnknownParameterError", "Unexpected diagnostic type");

            PresetProperties preset (parser.getPresetVT (), PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
            ChannelProperties first (preset.getChannelVT (0), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            ChannelProperties second (preset.getChannelVT (1), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            ZoneProperties zone (first.getZoneVT (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            require (preset.getName () == "ScopeTest", "Preset scope lost after unknown parameter");
            require (std::abs (first.getLevel () + 6.0) < 1e-9, "Channel scope lost after unknown parameter");
            require (zone.getSample () == "example.wav", "Zone scope lost after unknown parameter");
            require (std::abs (second.getPitch () - 2.0) < 1e-9, "Failed to transition from zone to next channel");

            first.setZonesCV ("CV A", false);
            require (first.getZonesCV () == "0A", "Zones CV normalization failed");
            require (ChannelProperties::getCvInputAndValueString ("CV B", 0.5, 2) == "0B 0.50", "CV amount normalization failed");
        }

        parser.parse ({ "Preset 2:", "Channel 1:", "PitchCV: 1A" });
        require (parser.getParseErrorsVT ().getNumChildren () == 1, "Missing CV delimiter was not reported");
        require (parser.getParseErrorsVT ().getChild (0).getProperty ("type").toString () == "ParameterFormatError", "Expected a CV format diagnostic");
        parser.parse ({ "Preset 3:", "Name: Clean" });
        require (parser.getParseErrorsVT ().getNumChildren () == 0, "Parse errors did not reset");
        PresetProperties cleanPreset (parser.getPresetVT (), PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        require (cleanPreset.getId () == 3 && cleanPreset.getName () == "Clean", "Parsing did not recover after malformed CV");

        // Exact reported card contents, including CRLF, negative zero, a numeric-leading
        // filename and a settings-only channel. This checks our parser, not A8 firmware.
        parser.parse (juce::StringArray::fromLines (
            "Preset 2 :\r\n"
            "  Name : koe-3\r\n"
            "  Channel 1 :\r\n"
            "    LoopMode : 2\r\n"
            "    MixLevel : -0\r\n"
            "    Release : 0\r\n"
            "    Zone 1 :\r\n"
            "      LoopLength : 512\r\n"
            "      LoopStart : 0\r\n"
            "      MinVoltage : -5\r\n"
            "      Sample : 1234567890abcdefg-a4e6352772f0-01.wav\r\n"
            "      SampleStart : 0\r\n"
            "      SampleEnd : 512\r\n"
            "  Channel 7 :\r\n"
            "    MixLevel : -90\r\n"
            "    MixMod : Off 0.0000"));
        require (parser.getParseErrorsVT ().getNumChildren () == 0, "Reported card preset must parse without diagnostics");
        PresetProperties cardPreset (parser.getPresetVT (), PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        ChannelProperties populated (cardPreset.getChannelVT (0), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        ChannelProperties settingsOnly (cardPreset.getChannelVT (6), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        ZoneProperties populatedZone (populated.getZoneVT (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        require (cardPreset.getId () == 2 && cardPreset.getName () == "koe-3", "Reported card preset identity must survive parsing");
        require (populatedZone.getSample () == "1234567890abcdefg-a4e6352772f0-01.wav",
                 "Settings-only channel must not discard the populated channel or alter its filename");
        require (populatedZone.getSampleStart () == 0 && populatedZone.getSampleEnd () == 512
                 && populatedZone.getLoopStart () == 0 && populatedZone.getLoopLength () == 512.0
                 && populatedZone.getMinVoltage () == -5.0, "Reported card zone boundaries must survive parsing");
        require (populated.getLoopMode () == 2 && populated.getMixLevel () == 0.0 && populated.getRelease () == 0.0,
                 "Reported card loop mode, negative-zero mix level and zero release must survive parsing");
        require (settingsOnly.getMixLevel () == -90.0 && std::get<0> (settingsOnly.getMixMod ()) == "Off"
                 && std::get<1> (settingsOnly.getMixMod ()) == 0.0, "Settings-only channel must retain its muted mix and modulation");
        for (auto zoneIndex { 0 }; zoneIndex < 8; ++zoneIndex)
        {
            ZoneProperties emptyZone (settingsOnly.getZoneVT (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            require (emptyZone.getSample ().isEmpty (), "Settings-only channel must not acquire a sample while parsing");
        }
        std::cout << "PASS: parser/CV (all scopes, 100 repeated parses, malformed CV, normalization, reported card preset)\n";
    }

    struct StereoFixture
    {
        // JUCE generates a unique path in the system temporary directory.
        juce::TemporaryFile input { ".wav" };
        juce::File left { input.getFile ().getSiblingFile (input.getFile ().getFileNameWithoutExtension () + "-L.wav") };
        juce::File right { input.getFile ().getSiblingFile (input.getFile ().getFileNameWithoutExtension () + "-R.wav") };

        ~StereoFixture ()
        {
            // Only generated outputs are removed; TemporaryFile removes the input.
            left.deleteFile ();
            right.deleteFile ();
        }
    };

    void testZoneContinuation ()
    {
        ZoneProperties source;
        source.setId (1, false);
        source.setSample ("slices.wav", false);
        source.setSampleStart (100, false);
        source.setSampleEnd (300, false);
        source.setLoopStart (100, false);
        source.setLoopLength (200, false);
        source.setPitchOffset (2.0, false);
        source.setLevelOffset (-3.0, false);
        source.setSide (1, false);
        const auto before { source.getValueTree ().createCopy () };
        const auto copied { ZoneContinuation::makeNext (source.getValueTree (), 1000, false) };
        require (copied.isEquivalentTo (before) && copied != source.getValueTree (), "Copy must be detached and preserve all settings");
        ZoneProperties next (ZoneContinuation::makeNext (source.getValueTree (), 1000, true), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        require (next.getSampleStart () == 300 && next.getSampleEnd () == 500, "Continuation must start at the previous end with the same duration");
        const auto nextRange { ZoneSampleRanges::resolve (ZoneSampleRanges::read (next), 1000) };
        require (! next.getLoopStart () && ! next.getLoopLength () && nextRange.loopStart == 300 && nextRange.loopLength == 200.0,
                 "Continued slice starts with an automatic loop following its sample range");
        require (next.getSample () == "slices.wav" && next.getSide () == 1 && std::abs (next.getPitchOffset () - 2.0) < 1e-9, "Continuation lost sample settings");
        ZoneProperties tail (ZoneContinuation::makeNext (source.getValueTree (), 350, true), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        require (tail.getSampleEnd () == 350 && tail.getSampleStart () == 300, "Tail must stop at the file end");
        require (! ZoneContinuation::makeNext (source.getValueTree (), 300, true).isValid (), "End-of-file continuation should be refused");
        require (! ZoneContinuation::makeNext (source.getValueTree (), 303, true).isValid (), "Sub-four-sample tail should be refused");
        require (! ZoneContinuation::makeNext (source.getValueTree (), 0, true).isValid (), "Unavailable sample should be refused");
        require (source.getValueTree ().isEquivalentTo (before), "Preparing the next slice must not mutate the source");
        ZoneProperties empty;
        require (! ZoneContinuation::makeNext (empty.getValueTree (), 1000, false).isValid (), "Empty zone copy should be refused");
        std::cout << "PASS: zone copy/continuation (settings, boundaries, tail, invalid inputs, source preservation)\n";
    }

    void testStereoSplit ()
    {
        AudioManager audio;
        constexpr auto sampleCount { 8193 };
        for (const auto bits : { 16, 24 })
        {
            StereoFixture fixture;
            const auto input { fixture.input.getFile () };
            juce::AudioBuffer<float> source (2, sampleCount);
            for (auto channel { 0 }; channel < 2; ++channel)
                for (auto sample { 0 }; sample < sampleCount; ++sample)
                    source.setSample (channel, sample, 0.7f * std::sin (static_cast<float> (sample + channel * 37) * 0.07f));

            {
                std::unique_ptr<juce::OutputStream> stream { input.createOutputStream () };
                require (stream != nullptr, "Failed to open stereo fixture");
                juce::WavAudioFormat format;
                auto writer { format.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (48000)
                                                                                                .withNumChannels (2)
                                                                                                .withBitsPerSample (bits)) };
                require (writer != nullptr && writer->writeFromAudioSampleBuffer (source, 0, sampleCount), "Failed to write stereo fixture");
            }

            // Compare against the quantized WAV data, not the pre-encoding floats.
            auto original { audio.getReaderFor (input) };
            require (original != nullptr && original->read (&source, 0, sampleCount, 0, true, true), "Failed to read stereo fixture");
            audio.splitStereoIntoTwoMono (input);
            for (auto channel { 0 }; channel < 2; ++channel)
            {
                auto reader { audio.getReaderFor (channel == 0 ? fixture.left : fixture.right) };
                require (reader != nullptr, "Mono output missing or unreadable");
                require (reader->numChannels == 1, "Output is not mono");
                require (reader->lengthInSamples == sampleCount, "Mono output length mismatch");
                require (reader->bitsPerSample == static_cast<unsigned int> (bits), "Mono output bit depth mismatch");
                require (std::abs (reader->sampleRate - 48000.0) < 1e-9, "Mono output sample rate mismatch");
                juce::AudioBuffer<float> mono (1, sampleCount);
                require (reader->read (&mono, 0, sampleCount, 0, true, false), "Failed to read mono output");

                // Float decoding/re-encoding may round by one PCM quantization step.
                const auto tolerance { 1.1f / static_cast<float> (1 << (bits - 1)) };
                auto maxError { 0.0f };
                for (auto sample { 0 }; sample < sampleCount; ++sample)
                {
                    const auto actual { mono.getSample (0, sample) };
                    const auto error { std::abs (source.getSample (channel, sample) - actual) };
                    require (std::isfinite (actual) && error <= tolerance,
                             juce::String (bits) + "-bit channel " + juce::String (channel) + ", sample " + juce::String (sample) + ": content mismatch");
                    maxError = std::max (maxError, error);
                }
                std::cout << bits << "-bit channel " << channel << ": max error " << maxError << '\n';
            }
            std::cout << "PASS: " << bits << "-bit stereo split (both channels, all " << sampleCount << " samples, lengths and format)\n";
        }
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialise;
    auto result { 0 };
    try
    {
        require (argc == 2, "Usage: A8ManagerRegressionTests --parser-cv | --stereo-split | --zone-continuation");
        const juce::String selection { argv[1] };
        if (selection == "--parser-cv")
            testParserAndCv ();
        else if (selection == "--stereo-split")
            testStereoSplit ();
        else if (selection == "--zone-continuation")
            testZoneContinuation ();
        else
            require (false, "Unknown test selection: " + selection);
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what () << '\n';
        result = 1;
    }
    ParameterPresetsSingleton::deleteInstance ();
    return result;
}
