#include "HardwareTestOutput.h"
#include "WaveformDesignExport.h"
#include "ChannelCvSafety.h"
#include "../Assimil8orPreset.h"
#include "../Preset/ParameterPresetsSingleton.h"
#include <cmath>
#include <filesystem>

namespace HardwareTestOutput
{
    namespace
    {
        constexpr double referenceLevel { 0.03162277660168379 }; // -30 dBFS peak.

        juce::String signalName (Signal signal)
        {
            switch (signal)
            {
                case Signal::currentDesign: return "currentDesign";
                case Signal::audioTone: return "audioTone";
                case Signal::cvLevels: return "cvLevels";
                case Signal::cvSine: return "cvSine";
                case Signal::cvRamp: return "cvRamp";
            }
            return {};
        }

        juce::var object () { return juce::var (new juce::DynamicObject ()); }

        double edgeGain (int frame, int frames, int fade)
        {
            const auto distance { std::min (frame, frames - 1 - frame) };
            if (distance >= fade) return 1.0;
            return 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * static_cast<double> (distance) / fade);
        }

        WaveformDesign::Settings effectiveDesign (const Settings& settings)
        {
            if (settings.signal == Signal::currentDesign) return settings.design;
            auto design { WaveformDesign::startingPoint (settings.signal == Signal::audioTone ? WaveformDesign::Mode::oscillator
                                                                                             : WaveformDesign::Mode::modulation,
                                                         WaveformDesign::Shape::sine) };
            design.sampleRate = settings.design.sampleRate;
            design.playback = WaveformDesign::Playback::oneShot;
            return design;
        }

        juce::Result renderSource (const Settings& settings, WaveformDesign::Render& rendered)
        {
            if (settings.signal == Signal::currentDesign) return WaveformDesign::render (settings.design, rendered);
            rendered.sampleRate = settings.design.sampleRate;
            rendered.frames = std::llround (settings.durationSeconds * rendered.sampleRate);
            auto& voice { rendered.voices.emplace_back (1, static_cast<int> (rendered.frames)) };
            const auto frames { voice.getNumSamples () };
            const auto fade { std::max (1, static_cast<int> (std::llround (0.005 * rendered.sampleRate))) };
            for (int frame { 0 }; frame < frames; ++frame)
            {
                const auto phase { static_cast<double> (frame) / (frames - 1) };
                double value { 0.0 };
                switch (settings.signal)
                {
                    case Signal::audioTone:
                        value = std::sin (juce::MathConstants<double>::twoPi * 440.0 * frame / rendered.sampleRate)
                              * edgeGain (frame, frames, fade);
                        break;
                    case Signal::cvLevels:
                    {
                        // Boundaries floor(N*k/5) are recorded in the manifest;
                        // there is deliberately no fade or DC filtering here.
                        const auto first { frames / 5 }, second { frames * 2 / 5 }, third { frames * 3 / 5 }, fourth { frames * 4 / 5 };
                        value = frame >= first && frame < second ? 1.0 : frame >= third && frame < fourth ? -1.0 : 0.0;
                        break;
                    }
                    case Signal::cvSine: value = std::sin (juce::MathConstants<double>::twoPi * phase); break;
                    case Signal::cvRamp: value = 1.0 - std::abs (2.0 * phase - 1.0); break;
                    case Signal::currentDesign: break;
                }
                // Exact zero endpoints for one-shot diagnostics, without
                // altering the held DC steps or smoothing their transitions.
                if (frame == 0 || frame == frames - 1) value = 0.0;
                voice.setSample (0, frame, static_cast<float> (value * settings.level));
            }
            return juce::Result::ok ();
        }

        juce::AudioBuffer<float> renderReference (double rate, juce::int64 windowFrames, juce::Array<juce::var>& bursts)
        {
            const auto pulseFrames { static_cast<int> (std::llround (rate * 0.02)) };
            const auto spacingFrames { static_cast<int> (std::llround (rate * 0.04)) };
            const auto tailFrames { static_cast<int> (std::llround (rate * 0.12)) };
            const auto fadeFrames { std::max (1, static_cast<int> (std::llround (rate * 0.002))) };
            juce::AudioBuffer<float> reference (1, static_cast<int> (windowFrames) + tailFrames);
            reference.clear ();
            auto addBurst = [&] (const juce::String& event, juce::int64 start, double frequency)
            {
                auto item { object () };
                auto* info { item.getDynamicObject () };
                info->setProperty ("event", event);
                info->setProperty ("startFrame", start);
                info->setProperty ("endFrameExclusive", start + pulseFrames);
                info->setProperty ("frequencyHz", frequency);
                info->setProperty ("peakFs", referenceLevel);
                info->setProperty ("fadeFrames", fadeFrames);
                bursts.add (item);
                for (int frame { 0 }; frame < pulseFrames; ++frame)
                    reference.setSample (0, static_cast<int> (start) + frame,
                                         static_cast<float> (referenceLevel * edgeGain (frame, pulseFrames, fadeFrames)
                                           * std::sin (juce::MathConstants<double>::twoPi * frequency * frame / rate)));
            };
            addBurst ("start", 0, 1000.0);
            addBurst ("start", spacingFrames, 1000.0);
            addBurst ("end", windowFrames, 2000.0);
            addBurst ("end", windowFrames + spacingFrames, 2000.0);
            addBurst ("end", windowFrames + spacingFrames * 2, 2000.0);
            return reference;
        }

        // Check the serialized routing/markers, not just parser acceptance.
        bool sameConfiguration (PresetProperties& expected, PresetProperties& actual, int count)
        {
            if (expected.getId () != actual.getId () || expected.getName () != actual.getName () || actual.getMidiSetup () != 0)
                return false;
            for (int index { 0 }; index < 8; ++index)
            {
                ChannelProperties first (expected.getChannelVT (index), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
                ChannelProperties second (actual.getChannelVT (index), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
                if (index < count && (second.getChannelMode () != first.getChannelMode () || second.getPlayMode () != first.getPlayMode ()
                    || second.getLoopMode () != first.getLoopMode () || second.getLoopLengthIsEnd () || second.getAutoTrigger ()
                    || second.getMixLevel () != -90.0 || std::get<0> (second.getMixMod ()) != "Off" || std::get<1> (second.getMixMod ()) != 0.0
                    // Match the existing hardware-preset writer's decimal
                    // precision, not the higher-precision editable recipe.
                    || second.getPitch () != juce::String (first.getPitch ()).getDoubleValue ()
                    || second.getPan () != juce::String (first.getPan ()).getDoubleValue ()
                    || second.getLevel () != 0.0 || second.getAttack () != 0.0 || second.getRelease () != 0.0
                    || std::get<0> (second.getPitchCV ()) != "Off" || second.getZonesCV () != "Off")) return false;
                for (int zoneIndex { 0 }; zoneIndex < 8; ++zoneIndex)
                {
                    ZoneProperties a (first.getZoneVT (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                    ZoneProperties b (second.getZoneVT (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                    if (a.getSample () != b.getSample ()) return false;
                    if (index < count && zoneIndex == 0 && (a.getSampleStart () != b.getSampleStart () || a.getSampleEnd () != b.getSampleEnd ()
                        || a.getLoopStart () != b.getLoopStart () || a.getLoopLength () != b.getLoopLength () || b.getSide () != 0
                        || b.getMinVoltage () != -5.0 || b.getPitchOffset () != 0.0 || b.getLevelOffset () != 0.0)) return false;
                }
            }
            return true;
        }
    }

    juce::Result validate (const Settings& settings)
    {
        if (signalName (settings.signal).isEmpty ()) return juce::Result::fail ("Choose a supported hardware test signal.");
        if (! std::isfinite (settings.durationSeconds) || settings.durationSeconds < 1.0 || settings.durationSeconds > 60.0)
            return juce::Result::fail ("Choose a test window of 1 to 60 seconds.");
        if (! std::isfinite (settings.level) || settings.level < 0.01 || settings.level > 0.25)
            return juce::Result::fail ("Choose a built-in test level of 1 to 25% digital full scale.");
        if (settings.presetNumber < 1 || settings.presetNumber > 199) return juce::Result::fail ("Choose a preset number from 1 to 199.");
        if (settings.design.sampleRate != 48000.0 && settings.design.sampleRate != 96000.0)
            return juce::Result::fail ("Choose a sample rate of 48 or 96 kHz.");
        if (settings.signal == Signal::currentDesign)
        {
            if (const auto valid { WaveformDesign::validate (settings.design) }; valid.failed ()) return valid;
            if (settings.design.mode == WaveformDesign::Mode::layers && settings.design.voiceCount > 7)
                return juce::Result::fail ("Hardware testing needs one free reference channel. Use at most seven design voices; no voice will be replaced.");
        }
        return juce::Result::ok ();
    }

    juce::Result exportPackage (const Settings& settings, const juce::File& parentFolder, const juce::String& name, ExportResult& result)
    {
        using namespace WaveformDesign::ExportSupport;
        result = {};
        if (const auto valid { validate (settings) }; valid.failed ()) return valid;
        if (! parentFolder.isDirectory ()) return juce::Result::fail ("Choose an existing parent folder for the hardware test package.");
        WaveformDesign::Render rendered;
        if (const auto valid { renderSource (settings, rendered) }; valid.failed ()) return valid;
        const auto design { effectiveDesign (settings) };
        const auto sourceCount { static_cast<int> (rendered.voices.size ()) };
        const auto cv { design.mode == WaveformDesign::Mode::modulation };
        const auto windowFrames { static_cast<juce::int64> (std::llround (settings.durationSeconds * rendered.sampleRate)) };
        juce::Array<juce::var> bursts;
        const auto reference { renderReference (rendered.sampleRate, windowFrames, bursts) };
        const auto stage { parentFolder.getChildFile (".a8-hardware-test-" + juce::Uuid ().toString ()) };
        std::error_code error;
        if (! std::filesystem::create_directory (std::filesystem::u8path (stage.getFullPathName ().toStdString ()), error) || error)
            return juce::Result::fail ("Unable to create private test-export folder: " + juce::String (error.message ()));
        struct Cleanup { juce::File folder; ~Cleanup () { if (folder.exists ()) folder.deleteRecursively (); } } cleanup { stage };
        auto fail = [&stage] (const juce::String& message)
        {
            const auto removed { stage.deleteRecursively () };
            return juce::Result::fail (message + (removed ? juce::String () : " Incomplete temporary export remains at " + stage.getFullPathName ()));
        };
        const auto defaults { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType) };
        auto tree { defaults.createCopy () };
        PresetProperties preset (tree, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        preset.setId (settings.presetNumber, false);
        preset.setName (safeStem (name).substring (0, 12), false);
        preset.setMidiSetup (0, false);
        preset.forEachChannel ([] (juce::ValueTree channelTree, int)
        {
            ChannelProperties channel (channelTree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            ChannelCvSafety::mute (channel);
            channel.setAutoTrigger (false, false);
            return true;
        });
        juce::StringArray filenames;
        juce::Array<juce::var> channels;
        for (int index { 0 }; index <= sourceCount; ++index)
        {
            const auto isReference { index == sourceCount };
            const auto filename { isReference ? juce::String ("test-reference.wav") : "voice-" + juce::String (index + 1).paddedLeft ('0', 2) + ".wav" };
            const auto& audio { isReference ? reference : rendered.voices[static_cast<size_t> (index)] };
            if (const auto written { writeWave (stage.getChildFile (filename), audio, rendered.sampleRate, ! isReference && cv) }; written.failed ())
                return fail (written.getErrorMessage ());
            filenames.add (filename);
            ChannelProperties channel (preset.getChannelVT (index), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            if (isReference)
            {
                auto referenceDesign { WaveformDesign::startingPoint (WaveformDesign::Mode::oscillator, WaveformDesign::Shape::sine) };
                referenceDesign.playback = WaveformDesign::Playback::oneShot;
                configureChannel (channel.getValueTree (), referenceDesign, index, sourceCount + 1);
            }
            else configureChannel (channel.getValueTree (), design, index, sourceCount);
            ChannelCvSafety::mute (channel); // Deliberate even for AUDIO diagnostics.
            channel.setAutoTrigger (false, false);
            ZoneProperties zone (channel.getZoneVT (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            configureZone (zone.getValueTree (), filename, audio.getNumSamples ());
            zone.setMinVoltage (-5.0, false);
            auto route { object () };
            auto* info { route.getDynamicObject () };
            info->setProperty ("channel", index + 1);
            info->setProperty ("file", filename);
            info->setProperty ("purpose", ! isReference && cv ? "CV" : "AUDIO");
            info->setProperty ("role", isReference ? "reference" : "signal");
            info->setProperty ("frames", audio.getNumSamples ());
            info->setProperty ("sampleRate", rendered.sampleRate);
            info->setProperty ("pitchSemitones", channel.getPitch ());
            info->setProperty ("requestedPitchSemitones", channel.getPitch ());
            info->setProperty ("pan", channel.getPan ());
            info->setProperty ("requestedPan", channel.getPan ());
            info->setProperty ("nominalPlaybackSeconds", audio.getNumSamples () / rendered.sampleRate / std::pow (2.0, channel.getPitch () / 12.0));
            info->setProperty ("channelMode", channel.getChannelMode ());
            info->setProperty ("playMode", channel.getPlayMode ());
            info->setProperty ("loopMode", channel.getLoopMode ());
            info->setProperty ("mixLevel", -90.0);
            info->setProperty ("mixMod", "Off");
            info->setProperty ("autoTrigger", false);
            info->setProperty ("sampleStart", 0);
            info->setProperty ("sampleEndExclusive", audio.getNumSamples ());
            info->setProperty ("loopStart", 0);
            info->setProperty ("loopEndExclusive", audio.getNumSamples ());
            channels.add (route);
        }
        const auto presetFilename { "prst" + juce::String (settings.presetNumber).paddedLeft ('0', 3) + ".yml" };
        const auto presetFile { stage.getChildFile (presetFilename) };
        Assimil8orPreset writer;
        if (const auto written { writer.write (presetFile, tree) }; written.failed ()) return fail (written.getErrorMessage ());
        juce::StringArray lines;
        presetFile.readLines (lines);
        Assimil8orPreset parser;
        parser.parse (lines);
        PresetProperties readback (parser.getPresetVT (), PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        if (lines.isEmpty () || parser.getParseErrorsVT ().getNumChildren () != 0 || ! sameConfiguration (preset, readback, sourceCount + 1))
            return fail ("The test preset did not preserve its routing, playback or markers on read-back.");
        if (const auto safe { ChannelCvSafety::validatePreset (readback.getValueTree (), stage) }; safe.failed ()) return fail (safe.getErrorMessage ());
        // The analysis manifest describes the emitted preset, while the recipe
        // and requested fields retain the unrounded designer values.
        for (int index { 0 }; index <= sourceCount; ++index)
        {
            ChannelProperties actual (readback.getChannelVT (index), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            auto* route { channels.getReference (index).getDynamicObject () };
            route->setProperty ("pitchSemitones", actual.getPitch ());
            route->setProperty ("pan", actual.getPan ());
            route->setProperty ("nominalPlaybackSeconds", static_cast<double> (route->getProperty ("frames")) / rendered.sampleRate
                                                         / std::pow (2.0, actual.getPitch () / 12.0));
        }
        auto manifest { object () };
        auto* info { manifest.getDynamicObject () };
        info->setProperty ("type", "A8Manager.HardwareTestOutput");
        info->setProperty ("schemaVersion", 1);
        info->setProperty ("signal", signalName (settings.signal));
        info->setProperty ("sampleRate", rendered.sampleRate);
        info->setProperty ("durationSeconds", settings.durationSeconds);
        info->setProperty ("windowFrames", windowFrames);
        info->setProperty ("level", settings.level);
        info->setProperty ("levelAppliesToCurrentDesign", false);
        info->setProperty ("presetNumber", settings.presetNumber);
        info->setProperty ("presetFile", presetFilename);
        info->setProperty ("referenceChannel", sourceCount + 1);
        info->setProperty ("referenceBursts", juce::var (bursts));
        info->setProperty ("channels", juce::var (channels));
        info->setProperty ("frameConvention", "Zero-based, end-exclusive; frame positions describe WAV data, not guaranteed hardware onset alignment.");
        info->setProperty ("referenceMeaning", "Scheduled test-window end, not proof of test-channel stop. Capture the actual gate/stop event for loops.");
        info->setProperty ("voltageCalibration", "Unknown: digital full-scale fractions are not volts. Measure individual outputs, polarity and Mix isolation.");
        info->setProperty ("alignment", "Linked triggering does not guarantee sample-locked output onset; account for trigger latency, detune, resampling and recording clock drift.");
        info->setProperty ("presetPrecision", "Channel pitch and pan use the existing preset writer's decimal precision. Manifest pitchSemitones/pan are the read-back values; requested fields and design.json retain the original precision.");
        if (settings.signal == Signal::currentDesign) info->setProperty ("design", WaveformDesign::toJson (settings.design));
        if (settings.signal == Signal::cvLevels)
        {
            juce::Array<juce::var> sections;
            for (int section { 0 }; section < 5; ++section)
            {
                auto item { object () };
                item.getDynamicObject ()->setProperty ("startFrame", rendered.frames * section / 5);
                item.getDynamicObject ()->setProperty ("endFrameExclusive", rendered.frames * (section + 1) / 5);
                item.getDynamicObject ()->setProperty ("expectedDigitalSample", section == 1 ? settings.level : section == 3 ? -settings.level : 0.0);
                sections.add (item);
            }
            info->setProperty ("cvSections", juce::var (sections));
        }
        info->setProperty ("pcm24ToleranceFs", 3.0 / 8388608.0);
        info->setProperty ("sampleFormula", settings.signal == Signal::audioTone ? "level*sin(2*pi*440*n/sampleRate), with 5 ms raised-cosine endpoint fades"
                            : settings.signal == Signal::cvSine ? "level*sin(2*pi*n/(N-1)); first and last sample exactly zero"
                            : settings.signal == Signal::cvRamp ? "level*(1-abs(2*n/(N-1)-1)); first and last sample exactly zero"
                            : settings.signal == Signal::cvLevels ? "Held values in cvSections, with no smoothing, normalization or DC filtering"
                            : "Unmodified WaveformDesign render from design recipe; per-voice gain remains baked in once");
        if (const auto written { writeText (stage.getChildFile ("test-manifest.json"), juce::JSON::toString (manifest) + "\n") }; written.failed ())
            return fail (written.getErrorMessage ());
        if (settings.signal == Signal::currentDesign)
            if (const auto written { writeText (stage.getChildFile ("design.json"), juce::JSON::toString (WaveformDesign::toJson (settings.design)) + "\n") }; written.failed ())
                return fail (written.getErrorMessage ());
        juce::String instructions { "A8Manager Hardware Test Output\n\n" };
        instructions << "Copy this isolated folder to the Assimil8or SD card; keep " << presetFilename << " and all WAVs together. Load preset " << settings.presetNumber << ".\n"
                     << "This export does not replace or assign any live preset. test-manifest.json is the exact-frame analysis record; it is not an A8 configuration file.\n\n"
                     << "ROUTING AND SAFETY\n"
                     << "Disconnect speakers/headphones from test and CV outputs. No generated test is guaranteed safe for an arbitrary connected speaker, interface or module.\n"
                     << "All used channels have Mix Off (-90) and Mix modulation Off, including the AUDIO reference. Confirm Off on the hardware and verify BOTH Mix L/R remain free of CV.\n"
                     << "Source CH 1" << (sourceCount > 1 ? " through " + juce::String (sourceCount) : juce::String ()) << "; reference CH " << sourceCount + 1 << ". Record individual outputs separately.\n"
                     << "Trigger/gate CH 1; the remaining channels are Link. Auto Trigger is OFF and no external CV or MIDI setup is assigned. Keep trigger/gate inputs low until ready; the preset does not request automatic playback.\n"
                     << "Known CV WAVs are tagged and blocked from A8Manager speaker audition. Other players, metadata-stripping converters and physical patching can bypass these protections.\n"
                     << "Levels are digital full-scale fractions, NOT volts. Use DC-coupled 1 Mohm scope inputs and verify actual voltage, polarity and sustained DC before patching.\n\n"
                     << "REFERENCE MARKERS\n"
                     << "The separate mono AUDIO reference has two 20 ms 1 kHz start bursts (40 ms spacing), then three 20 ms 2 kHz end bursts (40 ms spacing). Each has 2 ms edge fades and -30 dBFS peak.\n"
                     << "First start burst begins at WAV frame 0; first end burst begins at frame " << windowFrames << ". The manifest lists every end-exclusive burst interval.\n"
                     << "The end code marks the scheduled " << juce::String (settings.durationSeconds, 6) << " s test window, NOT proof of source playback stopping. Capture the actual gate/stop event for gated or continuous loops.\n"
                     << "Linked triggers do not establish guaranteed sample-lock. Account for trigger latency, pitch, resampling and recorder clock drift; analog recordings need not match PCM bit-for-bit.\n\n"
                     << "FILES AND EXPECTED VALUES\n"
                     << "Mono 24-bit PCM at " << juce::String (rendered.sampleRate, 0) << " Hz. Signal: " << signalName (settings.signal) << ".\n"
                     << "Built-in diagnostics are one-shot, exact-duration tests returning to zero. The level control applies only to built-ins.\n";
        if (settings.signal == Signal::currentDesign)
            instructions << "Current design PCM, gain, detune and selected playback/loop modes are preserved; only MIX routing is intentionally muted for this diagnostic package. design.json remains editable.\n"
                         << "Pitch and pan are serialized at the existing preset writer's decimal precision. The manifest records both requested and actual read-back values, and derives timing from the emitted pitch. The recipe retains the original precision.\n"
                         << "Its file length/playback duration can differ from the marker window. A continuous design does not stop at the end code; stop it using the hardware controls.\n";
        for (int index { 0 }; index < filenames.size (); ++index)
            instructions << "CH " << index + 1 << ": " << filenames[index] << " (" << (index < sourceCount && cv ? "CV" : "AUDIO") << ").\n";
        instructions << "Keep normal sample/CV files unmodified: reference bursts are never inserted into the signal under test.\n";
        if (const auto written { writeText (stage.getChildFile ("README.txt"), instructions) }; written.failed ()) return fail (written.getErrorMessage ());
        const auto stem { safeStem (name) };
        for (int suffix { 0 }; suffix < 10000; ++suffix)
        {
            const auto tail { suffix == 0 ? juce::String () : "-" + juce::String (suffix + 1) };
            const auto destination { parentFolder.getChildFile (stem.substring (0, 31 - tail.length ()) + tail) };
            if (destination.exists ()) continue;
            if (const auto published { publishExclusive (stage, destination) }; published.failed ())
            {
                if (destination.exists ()) continue;
                return fail (published.getErrorMessage ());
            }
            result.folder = destination;
            result.preset = destination.getChildFile (presetFilename);
            result.manifest = destination.getChildFile ("test-manifest.json");
            for (const auto& filename : filenames) result.waves.add (destination.getChildFile (filename));
            return juce::Result::ok ();
        }
        return fail ("No unused compatible test-package folder name was available.");
    }
}
