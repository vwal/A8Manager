#pragma once

#include "WaveformDesign.h"
#include "CvSampleSafety.h"
#include <cmath>
#include <cstring>

// Recall restores the saved generator recipe, not settings reverse-engineered
// from PCM or the current preset's pitch/marker edits. Never call on audio thread.
namespace WaveformDesignRecall
{
    struct RecalledDesign
    {
        WaveformDesign::Settings settings;
        juce::File recipe;
        juce::String displayName;
        int voiceIndex { 0 };
    };

    namespace detail
    {
        inline bool hasAssignmentToken (const juce::String& stem)
        {
            // Assignment exporter: A8-<name>-<first 12 UUID hex characters>.
            return stem.startsWith ("A8-") && stem.length () > 16 && stem[stem.length () - 13] == '-'
                && stem.getLastCharacters (12).containsOnly ("0123456789abcdefABCDEF");
        }

        inline juce::String cleanName (juce::String name)
        {
            if (hasAssignmentToken (name)) name = name.dropLastCharacters (13);
            if (name.startsWith ("A8-")) name = name.substring (3);
            return name.isNotEmpty () ? name : "waveform";
        }

        inline bool boundedWave (juce::InputStream& stream, juce::int64 expectedFrames)
        {
            // The generated files have one mono PCM24 data chunk. Check RIFF
            // extents before JUCE parses metadata, preventing a corrupt chunk
            // length from requesting large allocations. Never read PCM here.
            constexpr juce::int64 maximumMetadata { 64 * 1024 };
            const auto size { stream.getTotalLength () };
            if (size < 44 || size > expectedFrames * 3 + maximumMetadata) return false;
            char header[12];
            if (stream.read (header, sizeof (header)) != sizeof (header) || std::memcmp (header, "RIFF", 4) != 0
                || std::memcmp (header + 8, "WAVE", 4) != 0
                || static_cast<juce::int64> (juce::ByteOrder::littleEndianInt (header + 4)) + 8 != size) return false;
            auto metadataBytes { juce::int64 { 12 } };
            bool foundData { false }, foundFormat { false };
            int chunks { 0 };
            while (stream.getPosition () < size)
            {
                char chunk[8];
                if (++chunks > 1024 || stream.read (chunk, sizeof (chunk)) != sizeof (chunk)) return false;
                const auto length { static_cast<juce::int64> (juce::ByteOrder::littleEndianInt (chunk + 4)) };
                const auto padded { length + (length & 1) };
                if (padded > size - stream.getPosition ()) return false;
                const auto chunkEnd { stream.getPosition () + padded };
                if (std::memcmp (chunk, "data", 4) == 0)
                {
                    if (foundData || length != expectedFrames * 3) return false;
                    foundData = true;
                }
                else
                {
                    metadataBytes += padded + 8;
                    if (metadataBytes > maximumMetadata) return false;
                    if (std::memcmp (chunk, "fmt ", 4) == 0)
                    {
                        if (foundFormat || (length != 16 && length != 40)) return false;
                        foundFormat = true;
                    }
                    else if (std::memcmp (chunk, "smpl", 4) == 0)
                    {
                        // JUCE emits this even when no sample loops are set:
                        // a 36-byte header plus space for one unused loop.
                        // Validate declared counts before handing it to JUCE.
                        char sampler[36];
                        if (length < static_cast<juce::int64> (sizeof (sampler)) || stream.read (sampler, sizeof (sampler)) != sizeof (sampler)) return false;
                        const auto loops { static_cast<juce::int64> (juce::ByteOrder::littleEndianInt (sampler + 28)) };
                        const auto extraBytes { static_cast<juce::int64> (juce::ByteOrder::littleEndianInt (sampler + 32)) };
                        const auto available { length - static_cast<juce::int64> (sizeof (sampler)) };
                        if (loops > 64 || loops * 24 > available || extraBytes > available - loops * 24) return false;
                    }
                    else if (std::memcmp (chunk, "LIST", 4) == 0)
                    {
                        // Exported metadata is INFO only. Validate nested sizes
                        // too: a corrupt subchunk must not read into PCM or make
                        // JUCE interpret arbitrary appended cue/XML metadata.
                        char type[4];
                        if (length < 4 || stream.read (type, sizeof (type)) != sizeof (type) || std::memcmp (type, "INFO", 4) != 0) return false;
                        const auto listEnd { chunkEnd - (length & 1) };
                        while (stream.getPosition () < listEnd)
                        {
                            char info[8];
                            if (++chunks > 1024 || listEnd - stream.getPosition () < static_cast<juce::int64> (sizeof (info))
                                || stream.read (info, sizeof (info)) != sizeof (info)) return false;
                            const auto infoBytes { static_cast<juce::int64> (juce::ByteOrder::littleEndianInt (info + 4)) };
                            const auto infoPadded { infoBytes + (infoBytes & 1) };
                            if (infoPadded > listEnd - stream.getPosition () || ! stream.setPosition (stream.getPosition () + infoPadded)) return false;
                        }
                    }
                    else if (std::memcmp (chunk, "JUNK", 4) != 0)
                        return false; // Automatic recall only accepts the generated RIFF layout.
                }
                if (! stream.setPosition (chunkEnd)) return false;
            }
            return foundData && foundFormat && stream.setPosition (0);
        }
    }

    inline juce::Result loadRecipe (const juce::File& file, RecalledDesign& result)
    {
        result = {};
        constexpr int maximumBytes { 1024 * 1024 };
        if (! file.existsAsFile ()) return juce::Result::fail ("The saved waveform recipe is missing: " + file.getFileName ());
        const auto size { file.getSize () };
        if (size <= 0 || size > maximumBytes) return juce::Result::fail ("The waveform recipe must be nonempty and no larger than 1 MB.");
        auto stream { file.createInputStream () };
        if (! stream || stream->getStatus ().failed ()) return juce::Result::fail ("Cannot read the waveform recipe: " + file.getFileName ());
        juce::MemoryBlock contents;
        stream->readIntoMemoryBlock (contents, maximumBytes + 1);
        const auto bytes { static_cast<int> (contents.getSize ()) };
        if (bytes <= 0 || bytes > maximumBytes || stream->getStatus ().failed () || std::memchr (contents.getData (), 0, contents.getSize ()) != nullptr
            || ! CvSampleSafety::detail::boundedJsonObject (static_cast<const char*> (contents.getData ()), bytes))
            return juce::Result::fail ("The waveform recipe is not a complete, bounded UTF-8 JSON object.");
        RecalledDesign recalled;
        if (const auto parsed { WaveformDesign::fromJson (juce::JSON::parse (juce::String::fromUTF8 (static_cast<const char*> (contents.getData ()), bytes)), recalled.settings) }; parsed.failed ())
            return parsed;
        recalled.recipe = file;
        auto name { file.getFileNameWithoutExtension () };
        if (file.getFileName ().equalsIgnoreCase ("design.json")) name = file.getParentDirectory ().getFileName ();
        else if (name.endsWithIgnoreCase (".design")) name = name.dropLastCharacters (7);
        recalled.displayName = detail::cleanName (name);
        result = std::move (recalled);
        return juce::Result::ok ();
    }

    inline juce::Result recallWave (const juce::File& wave, RecalledDesign& result)
    {
        result = {};
        auto noRecipe = [] () { return juce::Result::fail ("This WAV has no recognized generated waveform recipe. Use Load recipe to open a saved design explicitly."); };
        if (! wave.existsAsFile () || ! wave.hasFileExtension ("wav")) return noRecipe ();
        const auto stem { wave.getFileNameWithoutExtension () };
        if (stem.length () < 4 || stem[stem.length () - 3] != '-' || ! stem.getLastCharacters (2).containsOnly ("0123456789")) return noRecipe ();
        const auto voice { stem.getLastCharacters (2).getIntValue () - 1 };
        if (voice < 0 || voice >= 8) return noRecipe ();
        const auto family { stem.dropLastCharacters (3) };
        const auto package { family == "voice" };
        if (! package && ! detail::hasAssignmentToken (family)) return noRecipe ();
        const auto recipe { wave.getSiblingFile (package ? "design.json" : family + ".design.json") };
        RecalledDesign recalled;
        if (const auto loaded { loadRecipe (recipe, recalled) }; loaded.failed ()) return loaded;
        const auto& settings { recalled.settings };
        const auto count { settings.mode == WaveformDesign::Mode::layers ? settings.voiceCount : 1 };
        if (voice >= count) return juce::Result::fail ("This voice is not present in the adjacent waveform recipe.");
        const auto frames { settings.mode == WaveformDesign::Mode::modulation ? std::llround (settings.sampleRate * settings.durationSeconds) : settings.cycleFrames };
        auto stream { wave.createInputStream () };
        if (! stream || stream->getStatus ().failed () || ! detail::boundedWave (*stream, frames))
            return juce::Result::fail ("The WAV is not an intact generated waveform matching this recipe.");
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatReader> reader { format.createReaderFor (stream.release (), true) };
        if (! reader || reader->numChannels != 1 || reader->bitsPerSample != 24 || reader->usesFloatingPointData
            || ! juce::approximatelyEqual (reader->sampleRate, settings.sampleRate) || reader->lengthInSamples != frames)
            return juce::Result::fail ("The WAV format or length no longer matches its saved waveform recipe.");
        const auto comments { reader->metadataValues.getValue (juce::WavAudioFormat::riffInfoComment2, {}) };
        const auto cv { CvSampleSafety::detail::hasMarker (comments, CvSampleSafety::cvMarker) };
        const auto audio { CvSampleSafety::detail::hasMarker (comments, CvSampleSafety::audioMarker) };
        if (cv == audio || cv != (settings.mode == WaveformDesign::Mode::modulation))
            return juce::Result::fail ("The WAV's generated audio/CV purpose tag does not match the recipe. Use Load recipe for an older untagged design.");
        recalled.voiceIndex = voice;
        result = std::move (recalled);
        return juce::Result::ok ();
    }
}
