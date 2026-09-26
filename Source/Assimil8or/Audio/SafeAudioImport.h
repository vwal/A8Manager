#pragma once

#include "AudioManager.h"
#include "../SafeRename.h"
#include <array>
#include <cmath>
#include <cstdio>

namespace SafeAudioImport
{
    // Exclusive creation refuses collisions, including a destination created
    // after the caller chose its name. Only our own new file is removed on an
    // I/O failure, so a partial copy is never reported as a successful import.
    inline juce::Result copyNew (const juce::File& source, const juce::File& destination)
    {
#if JUCE_WINDOWS
        auto* input { _wfopen (source.getFullPathName ().toWideCharPointer (), L"rb") };
#else
        auto* input { std::fopen (source.getFullPathName ().toRawUTF8 (), "rb") };
#endif
        if (! input) return juce::Result::fail ("Could not read " + source.getFullPathName ());
#if JUCE_WINDOWS
        auto* output { _wfopen (destination.getFullPathName ().toWideCharPointer (), L"wbx") };
#else
        auto* output { std::fopen (destination.getFullPathName ().toRawUTF8 (), "wbx") };
#endif
        if (! output)
        {
            std::fclose (input);
            return juce::Result::fail ("Could not create " + destination.getFullPathName () + ". Nothing already at that path was overwritten.");
        }
        std::array<unsigned char, 65536> buffer;
        bool failed { false };
        for (;;)
        {
            const auto count { std::fread (buffer.data (), 1, buffer.size (), input) };
            if (count != 0 && std::fwrite (buffer.data (), 1, count, output) != count) { failed = true; break; }
            if (count < buffer.size ()) { failed = std::ferror (input) != 0; break; }
        }
        if (std::fflush (output) != 0) failed = true;
        if (std::fclose (output) != 0) failed = true;
        if (std::fclose (input) != 0) failed = true;
        if (! failed) return juce::Result::ok ();
        const auto removed { destination.deleteFile () };
        return juce::Result::fail ("Copying audio failed. The source has not been changed. "
                                  + (removed ? juce::String ("The incomplete output was removed.")
                                             : "Remove the incomplete output manually: " + destination.getFullPathName ()));
    }

    inline juce::File unusedWaveName (const juce::File& folder, const juce::File& source)
    {
        const auto name { SafeRename::validLeafStem (source.getFileNameWithoutExtension ()) };
        for (int suffix { 0 }; suffix < 100000; ++suffix)
        {
            const auto tail { suffix == 0 ? juce::String () : "-" + juce::String (suffix) };
            const auto file { folder.getChildFile (name.substring (0, 43 - tail.length ()) + tail + ".wav") };
            if (! file.exists ()) return file;
        }
        return {};
    }

    inline juce::Result validateReader (const juce::AudioFormatReader& reader)
    {
        if (! std::isfinite (reader.sampleRate) || reader.sampleRate <= 0.0 || reader.lengthInSamples <= 0 || reader.numChannels == 0)
            return juce::Result::fail ("The audio file has invalid or empty audio data.");
        if (reader.sampleRate > 192000.0)
            return juce::Result::fail ("Sample rates above 192 kHz require resampling, which is not supported by this converter. Convert the rate in an audio editor first; the original has not been changed.");
        if (reader.numChannels > 2)
            return juce::Result::fail ("Choose a mono or stereo file. Multichannel downmixing is not supported; the original has not been changed.");
        return juce::Result::ok ();
    }

    inline juce::Result stageWave (AudioManager& manager, const juce::File& source, const juce::File& stage)
    {
        auto reader { manager.getReaderFor (source) };
        if (! reader) return juce::Result::fail ("Unable to read " + source.getFullPathName ());
        const auto validation { validateReader (*reader) };
        if (validation.failed ()) return validation;
        const auto frames { reader->lengthInSamples };
        const auto rate { reader->sampleRate };
        const auto channels { reader->numChannels };

        if (manager.isAssimil8orSupportedAudioFile (source))
        {
            const auto copied { copyNew (source, stage) };
            if (copied.failed ()) return copied;
        }
        else
        {
            if (stage.exists ()) return juce::Result::fail ("The temporary output already exists; conversion cancelled.");
            auto fileStream { stage.createOutputStream () };
            if (! fileStream || fileStream->getStatus ().failed ()) return juce::Result::fail ("Unable to create temporary audio output.");
            auto* output { fileStream.get () };
            std::unique_ptr<juce::OutputStream> stream { std::move (fileStream) };
            juce::WavAudioFormat format;
            std::unordered_map<juce::String, juce::String> metadata;
            // WAV-to-WAV conversion keeps cue/loop metadata at unchanged frame
            // positions. AIFF loop-type values have different meanings.
            if (source.hasFileExtension ("wav"))
                for (int index { 0 }; index < reader->metadataValues.size (); ++index)
                    metadata.emplace (reader->metadataValues.getAllKeys ()[index], reader->metadataValues.getAllValues ()[index]);
            // Conversions use 24-bit PCM; existing compatible WAVs are copied byte-for-byte.
            auto writer { format.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (rate)
                                                 .withNumChannels (static_cast<int> (channels)).withBitsPerSample (24)
                                                 .withMetadataValues (metadata)) };
            if (! writer) return juce::Result::fail ("Unable to create the PCM WAV writer.");
            if (! writer->writeFromAudioReader (*reader, 0, frames) || ! writer->flush ())
                return juce::Result::fail ("Writing the converted audio failed. The original has not been changed.");
            output->flush ();
            const auto writeStatus { output->getStatus () };
            writer.reset ();
            if (writeStatus.failed ()) return writeStatus;
        }
        reader.reset ();
        auto verify { manager.getReaderFor (stage) };
        if (! verify || verify->lengthInSamples != frames || verify->sampleRate != rate || verify->numChannels != channels
            || verify->usesFloatingPointData || ! manager.isAssimil8orSupportedAudioFile (stage))
            return juce::Result::fail ("The staged WAV did not pass validation. The original has not been changed.");
        return juce::Result::ok ();
    }

    inline juce::Result importFile (AudioManager& manager, const juce::File& source, const juce::File& folder, juce::File& imported)
    {
        imported = juce::File ();
        if (! folder.isDirectory ()) return juce::Result::fail ("The preset folder is unavailable.");
        if (! source.existsAsFile ()) return juce::Result::fail ("The source audio file is unavailable.");
        auto reader { manager.getReaderFor (source) };
        if (! reader) return juce::Result::fail ("Unable to read " + source.getFullPathName ());
        const auto validation { validateReader (*reader) };
        if (validation.failed ()) return validation;
        reader.reset ();
        if (source.getParentDirectory () == folder && manager.isAssimil8orSupportedAudioFile (source))
        {
            imported = source;
            return juce::Result::ok ();
        }
        const auto destination { unusedWaveName (folder, source) };
        if (destination == juce::File ()) return juce::Result::fail ("Unable to find an unused sample name.");
        juce::TemporaryFile temporary (folder.getChildFile (".a8-import-" + juce::Uuid ().toString () + ".wav"));
        const auto staged { stageWave (manager, source, temporary.getFile ()) };
        if (staged.failed ()) return staged;
        const auto published { copyNew (temporary.getFile (), destination) };
        if (published.failed ()) return published;
        imported = destination;
        return juce::Result::ok ();
    }

    // The replacement is deliberately injectable so regression tests can exercise
    // failed installation/recovery without requiring a full or failing disk.
    using Install = std::function<juce::Result (const juce::File&, const juce::File&)>;
    inline juce::Result convertInPlace (AudioManager& manager, const juce::File& source, juce::File& converted,
                                       juce::File& backup, Install install = copyNew)
    {
        converted = juce::File ();
        backup = juce::File ();
        if (! source.existsAsFile ()) return juce::Result::fail ("The source audio file is unavailable.");
        const auto destination { source.withFileExtension ("wav") };
        if (destination != source && destination.exists ())
            return juce::Result::fail ("The WAV destination already exists. Rename it or the source first; neither file was changed.");
        juce::TemporaryFile temporary (source.getSiblingFile (".a8-convert-" + juce::Uuid ().toString () + ".wav"));
        const auto staged { stageWave (manager, source, temporary.getFile ()) };
        if (staged.failed ()) return staged;
        // AIFF and other non-WAV sources are not replaced: retain them at their
        // original paths and install the new WAV alongside them.
        if (destination != source)
        {
            const auto published { install (temporary.getFile (), destination) };
            if (published.failed ()) return published;
            converted = destination;
            backup = source;
            return juce::Result::ok ();
        }
        const auto originalBackup { source.getSiblingFile (".a8-original-" + juce::Uuid ().toString () + "-" + source.getFileName ().substring (0, 100)) };
        if (! source.moveFileTo (originalBackup)) return juce::Result::fail ("Unable to preserve the original; conversion cancelled.");
        backup = originalBackup;
        const auto published { install (temporary.getFile (), destination) };
        if (published.failed ())
        {
            const auto restored { source.exists () ? juce::Result::fail ("The original path is occupied.") : copyNew (backup, source) };
            return juce::Result::fail (published.getErrorMessage () + (restored.wasOk () ? " The original was restored." : " The original could not be restored automatically.")
                                       + " A recoverable original remains at " + backup.getFullPathName ());
        }
        converted = destination;
        return juce::Result::ok ();
    }
}
