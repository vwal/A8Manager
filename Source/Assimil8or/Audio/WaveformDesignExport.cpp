#include "WaveformDesignExport.h"
#include "AudioManager.h"
#include "CvSampleSafety.h"
#include "../Assimil8orPreset.h"
#include "../Preset/ParameterPresetsSingleton.h"
#include <cmath>
#include <filesystem>
#include <cerrno>
#include <cstring>
#include <unordered_map>
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#elif JUCE_MAC
 #include <sys/stdio.h>
#elif JUCE_LINUX
 #include <fcntl.h>
 #include <linux/fs.h>
 #include <sys/syscall.h>
 #include <unistd.h>
#endif

namespace WaveformDesign
{
    namespace
    {
        // Ordinary POSIX rename can replace an existing empty directory. Use
        // the platform's exclusive rename instead; never fall back to overwrite.
        juce::Result publishFolder (const juce::File& stage, const juce::File& destination)
        {
#if JUCE_WINDOWS
            if (::MoveFileExW (stage.getFullPathName ().toWideCharPointer (), destination.getFullPathName ().toWideCharPointer (), 0))
                return juce::Result::ok ();
            return juce::Result::fail ("Unable to publish the export folder (Windows error " + juce::String (::GetLastError ()) + ").");
#elif JUCE_MAC
            if (::renamex_np (stage.getFullPathName ().toRawUTF8 (), destination.getFullPathName ().toRawUTF8 (), RENAME_EXCL) == 0)
                return juce::Result::ok ();
            return juce::Result::fail ("Unable to publish the export folder: " + juce::String (std::strerror (errno)));
#elif JUCE_LINUX && defined (SYS_renameat2)
            if (::syscall (SYS_renameat2, AT_FDCWD, stage.getFullPathName ().toRawUTF8 (), AT_FDCWD,
                           destination.getFullPathName ().toRawUTF8 (), RENAME_NOREPLACE) == 0)
                return juce::Result::ok ();
            return juce::Result::fail ("Unable to publish the export folder: " + juce::String (std::strerror (errno)));
#else
            juce::ignoreUnused (stage, destination);
            return juce::Result::fail ("This platform does not provide the required non-overwriting folder rename.");
#endif
        }

        juce::String safeStem (const juce::String& name)
        {
            auto stem { name.retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-").trim ().replaceCharacter (' ', '-') };
            if (stem.isEmpty ()) stem = "waveform";
            // The prefix also avoids hidden names and Windows device names.
            return ("A8-" + stem).substring (0, 31);
        }

        double mixHeadroomDb (int count)
        {
            // MixLevel is a dB offset (-90..+6), not a linear gain. Round down
            // to the hardware UI's tenth-dB step so summed layers have headroom.
            return std::floor (-20.0 * std::log10 (static_cast<double> (count)) * 10.0) / 10.0;
        }

        juce::Result writeText (const juce::File& file, const juce::String& text)
        {
            const auto contents { text.replace ("\r\n", "\n").replace ("\r", "\n") };
            auto stream { file.createOutputStream () };
            if (! stream || stream->getStatus ().failed ()) return juce::Result::fail ("Unable to create " + file.getFileName ());
            const auto written { stream->writeText (contents, false, false, "\n") };
            stream->flush ();
            const auto status { stream->getStatus () };
            stream.reset ();
            if (! written || status.failed () || file.loadFileAsString () != contents)
                return juce::Result::fail ("Unable to complete " + file.getFileName ());
            return juce::Result::ok ();
        }

        juce::Result writeWave (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate, bool cv)
        {
            auto fileStream { file.createOutputStream () };
            if (! fileStream || fileStream->getStatus ().failed ()) return juce::Result::fail ("Unable to create " + file.getFileName ());
            auto* output { fileStream.get () };
            std::unique_ptr<juce::OutputStream> stream { std::move (fileStream) };
            juce::WavAudioFormat format;
            const auto purpose { CvSampleSafety::exportMetadata (cv) };
            std::unordered_map<juce::String, juce::String> metadata;
            for (int index { 0 }; index < purpose.size (); ++index)
                metadata.emplace (purpose.getAllKeys ()[index], purpose.getAllValues ()[index]);
            auto writer { format.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (sampleRate)
                                                  .withNumChannels (1).withBitsPerSample (24).withMetadataValues (metadata)) };
            if (! writer || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples ()) || ! writer->flush ())
                return juce::Result::fail ("Writing " + file.getFileName () + " failed.");
            output->flush ();
            const auto status { output->getStatus () };
            writer.reset ();
            if (status.failed ()) return status;
            AudioManager manager;
            auto reader { manager.getReaderFor (file) };
            if (! reader || reader->sampleRate != sampleRate || reader->numChannels != 1 || reader->usesFloatingPointData
                || reader->bitsPerSample != 24 || reader->lengthInSamples != audio.getNumSamples ()
                || reader->metadataValues.getValue (juce::WavAudioFormat::riffInfoComment2, {}) != purpose[juce::WavAudioFormat::riffInfoComment2]
                || CvSampleSafety::isCv (file, *reader) != cv)
                return juce::Result::fail ("WAV verification failed for " + file.getFileName ());
            juce::AudioBuffer<float> check (1, 4096);
            for (int offset { 0 }; offset < audio.getNumSamples (); offset += check.getNumSamples ())
            {
                const auto count { std::min (check.getNumSamples (), audio.getNumSamples () - offset) };
                if (! reader->read (&check, 0, count, offset, true, false)) return juce::Result::fail ("Cannot read back " + file.getFileName ());
                for (int index { 0 }; index < count; ++index)
                    if (! std::isfinite (check.getSample (0, index)) || std::abs (check.getSample (0, index) - audio.getSample (0, offset + index)) > 3.0 / 8388608.0)
                        return juce::Result::fail ("Audio read-back differs from the rendered design in " + file.getFileName ());
            }
            return juce::Result::ok ();
        }

        juce::String instructions (const Settings& settings, const Render& rendered, const juce::StringArray& waves)
        {
            juce::String text { "A8Manager Waveform Design\n\n" };
            text << "Copy this entire new folder to the root of your Assimil8or SD card, safely eject it, then load the folder and preset 001.\n"
                 << "Keep prst001.yml and its WAV files together. Do not merge into another preset folder without checking names and references.\n"
                 << "design.json is the editable design recipe. This README and recipe are not hardware configuration files.\n\n"
                 << "TRIGGERING AND PLAYBACK\n"
                 << "Automatic triggering is OFF: loading this preset does not start playback. Trigger/gate channel 1; linked layers follow it.\n";
            if (settings.playback == Playback::oneShot)
                text << "One Shot: a trigger plays the full sample once, independently of trigger length; looping is disabled.\n";
            else if (settings.playback == Playback::loop)
                text << "Continuous Loop: one trigger starts repeated playback. Gate release does not stop it. Use the module's manual stop/play controls to stop it.\n";
            else
                text << "Gated Loop: hold channel 1's gate high to loop; gate release ends the gated playback.\n";
            text << "Attack and release are set to zero; the hardware adds no programmed fade. Abrupt boundaries may click or cause CV steps.\n"
                 << "All voices use zone 1; other zones/channels are empty defaults. Sample and loop ranges cover the whole file [0, frame count).\n"
                 << "Layers use Master/Link, NOT Stereo/Right: detune and pan remain independent. No external pitch/CV routing or MIDI setup is assigned.\n"
                 << "Channel LEVEL and zone level offset are 0 dB. Per-voice gain is already baked into the WAV, once.\n";
            if (settings.mode == Mode::modulation)
                text << "Intended CV routing: Mix Output Level Off; use the individual channel output only.\n"
                     << "The preset writes numeric MixLevel -90 (the application's minimum). Confirm that the module displays Off before patching; this hardware mapping needs module verification.\n";
            else
                text << "Per-channel mix offset: " << juce::String (mixHeadroomDb (waves.size ()), 1)
                     << " dB (approximately 1/voice-count gain), reserving summed mix headroom without attenuating individual outputs.\n"
                     << "Mixing many loud layers may overload the mix output; reduce levels before monitoring.\n";
            text << "\nFILES AND TIMING\n"
                 << "Each WAV is mono 24-bit integer PCM at " << juce::String (rendered.sampleRate, 0) << " Hz, " << juce::String (rendered.frames) << " frames.\n"
                 << "CV files are never normalized or DC-filtered. Audio cycles are harmonic-filtered and normalized before applying amplitude/offset.\n"
                 << "The exporter applies no additional gain or DC filtering. Phase and voice level are baked in; detune is preset ChannelPitch.\n"
                 << "Each WAV carries a RIFF INFO/ICMT purpose tag: " << (settings.mode == Mode::modulation ? CvSampleSafety::cvMarker : CvSampleSafety::audioMarker) << ".\n"
                 << "This tag changes metadata only, never PCM values. It survives ordinary file renaming/copying.\n";
            const auto baseRate { settings.mode == Mode::modulation ? settings.cycles * rendered.sampleRate / static_cast<double> (rendered.frames)
                                                                  : rendered.sampleRate / static_cast<double> (rendered.frames) };
            for (int index { 0 }; index < waves.size (); ++index)
            {
                const auto voice { settings.mode == Mode::layers ? settings.voices[static_cast<size_t> (index)] : Voice {} };
                const auto ratio { std::pow (2.0, voice.detuneCents / 1200.0) };
                text << "CH " << juce::String (index + 1) << ": " << waves[index] << "; pitch " << juce::String (voice.detuneCents / 100.0, 4)
                     << " semitones; pan " << juce::String (voice.pan, 3) << "; playback duration "
                     << juce::String (static_cast<double> (rendered.frames) / rendered.sampleRate / ratio, 6) << " s; "
                     << (settings.mode == Mode::modulation ? "nominal cycle rate " : "fundamental ") << juce::String (baseRate * ratio, 6) << " Hz.\n";
            }
            text << "Rates assume unchanged hardware pitch and no modulation. Non-periodic shapes/random curves need not sound at the nominal cycle rate.\n\n"
                 << "CV SAFETY AND CALIBRATION\n"
                 << "Amplitude and offset are fractions of digital full scale, not volts. There is no assumed +/-5 V mapping.\n"
                 << "For CV, use the individual channel output, not the panned/summed stereo mix. Verify polarity and range with a meter/scope before patching.\n"
                 << "DC/slow CV is not usefully auditioned through normal speakers or AC-coupled interfaces. Keep monitor levels low or disconnected.\n"
                 << "A8Manager blocks known CV-tagged files from speaker audition; this is not protection in other players or on the hardware.\n"
                 << "Editors/converters may strip metadata. Untagged external files are not automatically recognized as CV from their name, amplitude or DC content.\n"
                 << "For older untagged A8Manager CV exports, keep the original voice-01.wav beside its matching design.json; renaming or separating that old file loses this fallback.\n"
                 << "Actual voltage depends on hardware calibration, output selection, gain settings and connected load; an estimate is not a calibration guarantee.\n";
            if (settings.measuredFullScaleVolts > 0.0)
                text << "User-entered positive full-scale measurement: " << juce::String (settings.measuredFullScaleVolts, 4)
                     << " V. Peak estimate from rendered digital amplitude: " << juce::String (rendered.peak * settings.measuredFullScaleVolts, 4) << " V magnitude.\n";
            else text << "Output-voltage calibration is UNKNOWN. No voltage estimate is supplied.\n";
            text << "Digital peak: " << juce::String (rendered.peak * 100.0, 4) << "% FS; DC: " << juce::String (rendered.dc * 100.0, 4)
                 << "% FS; boundary jump: " << juce::String (rendered.boundaryJump * 100.0, 4) << "% FS; clipped samples: " << juce::String (rendered.clippedSamples) << ".\n";
            for (const auto& warning : rendered.warnings) text << "Warning: " << warning << "\n";
            text << "\nHardware operation reference: https://www.rossum-electro.com/pages/downloads (Assimil8or User Manual).\n";
            return text;
        }
    }

    juce::Result exportDesign (const Settings& settings, const juce::File& parentFolder, const juce::String& name, ExportResult& result)
    {
        result = ExportResult {};
        if (! parentFolder.isDirectory ()) return juce::Result::fail ("Choose an existing parent folder for the new export.");
        Render rendered;
        if (const auto valid { render (settings, rendered) }; valid.failed ()) return valid;
        const auto count { settings.mode == Mode::layers ? settings.voiceCount : 1 };
        if (count < 1 || count > 8 || rendered.voices.size () != static_cast<size_t> (count) || rendered.frames < 4
            || ! std::isfinite (rendered.sampleRate) || rendered.sampleRate <= 0.0 || rendered.sampleRate > 192000.0)
            return juce::Result::fail ("The rendered design does not contain valid mono voices.");
        for (const auto& voice : rendered.voices)
        {
            if (voice.getNumChannels () != 1 || voice.getNumSamples () != rendered.frames)
                return juce::Result::fail ("Rendered voice dimensions are inconsistent.");
            for (int index { 0 }; index < voice.getNumSamples (); ++index)
                if (! std::isfinite (voice.getSample (0, index)) || std::abs (voice.getSample (0, index)) > 1.0f)
                    return juce::Result::fail ("The rendered design contains invalid PCM values.");
        }

        const auto stage { parentFolder.getChildFile (".a8-design-" + juce::Uuid ().toString ()) };
        std::error_code error;
        if (! std::filesystem::create_directory (std::filesystem::u8path (stage.getFullPathName ().toStdString ()), error) || error)
            return juce::Result::fail ("Unable to create a private export staging folder: " + juce::String (error.message ()));
        struct Cleanup { juce::File folder; ~Cleanup () { if (folder.exists ()) folder.deleteRecursively (); } } cleanup { stage };
        auto fail = [&stage] (const juce::String& message)
        {
            const auto removed { stage.deleteRecursively () };
            return juce::Result::fail (message + (removed ? juce::String () : " Incomplete temporary export remains at " + stage.getFullPathName ()));
        };
        const auto defaults { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType) };
        auto tree { defaults.createCopy () };
        PresetProperties preset (tree, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        preset.setId (1, false);
        preset.setName (safeStem (name).substring (0, 12), false);
        juce::StringArray waves;
        for (int index { 0 }; index < count; ++index)
        {
            const auto filename { "voice-" + juce::String (index + 1).paddedLeft ('0', 2) + ".wav" };
            if (const auto written { writeWave (stage.getChildFile (filename), rendered.voices[static_cast<size_t> (index)], rendered.sampleRate,
                                               settings.mode == Mode::modulation) }; written.failed ())
                return fail (written.getErrorMessage ());
            waves.add (filename);
            const auto voice { settings.mode == Mode::layers ? settings.voices[static_cast<size_t> (index)] : Voice {} };
            ChannelProperties channel (preset.getChannelVT (index), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            channel.setChannelMode (index == 0 ? ChannelProperties::master : ChannelProperties::link, false);
            channel.setPitch (voice.detuneCents / 100.0, false);
            channel.setPan (voice.pan, false);
            channel.setLevel (0.0, false);
            // The app's numeric minimum represents the intended Off setting;
            // textual "Off" is not accepted by its numeric preset parser.
            // Confirm this sentinel on the module before hardware CV use.
            channel.setMixLevel (settings.mode == Mode::modulation ? -90.0 : mixHeadroomDb (count), false);
            channel.setAttack (0.0, false);
            channel.setRelease (0.0, false);
            channel.setAutoTrigger (false, false);
            channel.setPlayMode (settings.playback == Playback::gatedLoop ? 0 : 1, false);
            channel.setLoopMode (settings.playback == Playback::oneShot ? 0 : settings.playback == Playback::loop ? 1 : 2, false);
            channel.setLoopLengthIsEnd (false, false);
            ZoneProperties zone (channel.getZoneVT (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            zone.setSample (filename, false);
            zone.setSide (0, false);
            zone.setSampleStart (0, false);
            zone.setSampleEnd (rendered.frames, false);
            zone.setLoopStart (0, false);
            zone.setLoopLength (static_cast<double> (rendered.frames), false);
            zone.setMinVoltage (-5.0, false);
            zone.setPitchOffset (0.0, false);
            zone.setLevelOffset (0.0, false);
        }
        const auto presetFile { stage.getChildFile ("prst001.yml") };
        Assimil8orPreset writer;
        if (const auto written { writer.write (presetFile, tree) }; written.failed ()) return fail (written.getErrorMessage ());
        juce::StringArray lines;
        presetFile.readLines (lines);
        Assimil8orPreset parser;
        parser.parse (lines);
        if (lines.isEmpty () || parser.getParseErrorsVT ().getNumChildren () != 0)
            return fail ("The generated preset did not pass read-back validation.");
        if (const auto written { writeText (stage.getChildFile ("design.json"), juce::JSON::toString (toJson (settings)) + "\n") }; written.failed ())
            return fail (written.getErrorMessage ());
        if (const auto written { writeText (stage.getChildFile ("README.txt"), instructions (settings, rendered, waves)) }; written.failed ())
            return fail (written.getErrorMessage ());

        const auto stem { safeStem (name) };
        for (int suffix { 0 }; suffix < 10000; ++suffix)
        {
            const auto tail { suffix == 0 ? juce::String () : "-" + juce::String (suffix + 1) };
            const auto destination { parentFolder.getChildFile (stem.substring (0, 31 - tail.length ()) + tail) };
            if (destination.exists ()) continue;
            const auto published { publishFolder (stage, destination) };
            if (published.failed ())
            {
                if (destination.exists ()) continue; // A concurrent export took the name; try the next one.
                return fail (published.getErrorMessage ());
            }
            result.folder = destination;
            result.preset = destination.getChildFile ("prst001.yml");
            result.recipe = destination.getChildFile ("design.json");
            for (const auto& filename : waves) result.waves.add (destination.getChildFile (filename));
            return juce::Result::ok ();
        }
        return fail ("No unused compatible export-folder name was available.");
    }
}
