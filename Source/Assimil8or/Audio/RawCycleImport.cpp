#include "RawCycleImport.h"
#include "CvSampleSafety.h"
#include "WaveformDesignRecall.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>

namespace RawCycleImport
{
    namespace
    {
        constexpr int maximumBytes { 1024 * 1024 }, maximumMetadata { 64 * 1024 };

        std::filesystem::file_type type (const juce::File& file)
        {
            std::error_code error;
            return std::filesystem::symlink_status (
                std::filesystem::path (reinterpret_cast<const char8_t*> (file.getFullPathName ().toRawUTF8 ())), error).type ();
        }

        juce::Result provenance (const juce::File& file)
        {
            // Existing recipes are authoritative, including legacy untagged CV.
            // A broken/linked recipe must not turn into an audio-import fallback.
            const auto recipe { WaveformDesignRecall::adjacentRecipe (file) };
            if (recipe != juce::File ())
                return juce::Result::fail ("This WAV belongs to a recognized designer filename family or has an adjacent recipe. Restore/use its original recipe; it cannot be reinterpreted as raw audio.");
            if (type (file.getSiblingFile ("test-manifest.json")) != std::filesystem::file_type::not_found)
                return juce::Result::fail ("This folder contains hardware-test metadata. Import the original audio cycle, not a diagnostic test output.");
            return juce::Result::ok ();
        }

        bool contains (const unsigned char* data, size_t size, const char* text)
        {
            const auto length { std::strlen (text) };
            return size >= length && std::search (data, data + size, text, text + length) != data + size;
        }

        struct Decoded
        {
            Info info;
            std::array<std::vector<double>, 2> channels;
        };

        juce::Result decode (const juce::File& file, Decoded& result)
        {
            if (! file.hasFileExtension ("wav") || type (file) != std::filesystem::file_type::regular
                || type (file.getParentDirectory ()) != std::filesystem::file_type::directory)
                return juce::Result::fail ("Choose a regular, non-linked WAV file containing one audio cycle.");
            if (const auto safety { provenance (file) }; safety.failed ()) return safety;
            const auto size { file.getSize () };
            const auto identity { file.getFileIdentifier () };
            const auto modified { file.getLastModificationTime () };
            if (size < 44 || size > maximumBytes)
                return juce::Result::fail ("Raw cycle import accepts small WAVs up to 1 MiB, containing 4..8192 frames. Long recordings are not cycle tables.");
            juce::FileInputStream input (file);
            juce::MemoryBlock bytes;
            input.readIntoMemoryBlock (bytes, maximumBytes + 1);
            if (input.getStatus ().failed () || static_cast<juce::int64> (bytes.getSize ()) != size
                || file.getSize () != size || file.getFileIdentifier () != identity || file.getLastModificationTime () != modified
                || type (file) != std::filesystem::file_type::regular)
                return juce::Result::fail ("The WAV changed or could not be read completely. Retry the import.");
            const auto* data { static_cast<const unsigned char*> (bytes.getData ()) };
            if (std::memcmp (data, "RIFF", 4) != 0 || std::memcmp (data + 8, "WAVE", 4) != 0
                || static_cast<juce::int64> (juce::ByteOrder::littleEndianInt (data + 4)) + 8 != size)
                return juce::Result::fail ("Expected a complete little-endian RIFF/WAVE file. RF64, compressed and corrupt files are not supported for raw cycles.");

            size_t formatOffset {}, formatBytes {}, audioOffset {}, audioBytes {};
            size_t metadataBytes { 12 }, position { 12 };
            int chunks { 0 };
            while (position < bytes.getSize ())
            {
                if (++chunks > 256 || bytes.getSize () - position < 8)
                    return juce::Result::fail ("The WAV has a malformed or excessive RIFF chunk list.");
                const auto* header { data + position };
                const auto length { static_cast<size_t> (juce::ByteOrder::littleEndianInt (header + 4)) };
                position += 8;
                const auto padded { length + (length & 1) };
                if (padded > bytes.getSize () - position)
                    return juce::Result::fail ("A WAV chunk extends beyond the file. The source was not changed.");
                if (std::memcmp (header, "data", 4) == 0)
                {
                    if (audioOffset != 0) return juce::Result::fail ("Raw cycle import requires one complete PCM data chunk.");
                    audioOffset = position;
                    audioBytes = length;
                }
                else
                {
                    metadataBytes += padded + 8;
                    if (metadataBytes > maximumMetadata)
                        return juce::Result::fail ("This WAV has more than 64 KiB of metadata; export a simple PCM cycle WAV first.");
                    // Search only metadata, never PCM. Conflicting AUDIO/CV tags
                    // are rejected too: a CV declaration has safety precedence.
                    if (contains (data + position, length, CvSampleSafety::cvMarker)
                        || contains (data + position, length, "A8Manager.HardwareTestOutput")
                        || contains (data + position, length, "A8Manager Hardware Test Output"))
                        return juce::Result::fail ("This WAV is marked as CV or hardware-test output and cannot become an audio oscillator through raw import.");
                    if (std::memcmp (header, "fmt ", 4) == 0)
                    {
                        if (formatOffset != 0) return juce::Result::fail ("The WAV contains more than one format declaration.");
                        formatOffset = position;
                        formatBytes = length;
                    }
                }
                position += padded;
            }
            if (formatOffset == 0 || audioOffset == 0 || formatBytes < 16)
                return juce::Result::fail ("The WAV is missing its format or audio data.");
            const auto* format { data + formatOffset };
            auto encoding { static_cast<int> (juce::ByteOrder::littleEndianShort (format)) };
            const auto channelCount { static_cast<int> (juce::ByteOrder::littleEndianShort (format + 2)) };
            const auto rate { juce::ByteOrder::littleEndianInt (format + 4) };
            const auto byteRate { juce::ByteOrder::littleEndianInt (format + 8) };
            const auto alignment { static_cast<int> (juce::ByteOrder::littleEndianShort (format + 12)) };
            const auto bits { static_cast<int> (juce::ByteOrder::littleEndianShort (format + 14)) };
            if (encoding == 0xfffe)
            {
                static constexpr unsigned char guidTail[] { 0, 0, 16, 0, 128, 0, 0, 170, 0, 56, 155, 113 };
                if (formatBytes < 40 || juce::ByteOrder::littleEndianShort (format + 16) < 22
                    || std::memcmp (format + 28, guidTail, sizeof (guidTail)) != 0)
                    return juce::Result::fail ("The WAV extensible format is unsupported or malformed.");
                const auto validBits { static_cast<int> (juce::ByteOrder::littleEndianShort (format + 18)) };
                const auto subformat { juce::ByteOrder::littleEndianInt (format + 24) };
                if (subformat != 1 && subformat != 3) return juce::Result::fail ("Only integer PCM or IEEE floating-point WAV cycles can be imported.");
                encoding = static_cast<int> (subformat);
                if (validBits <= 0 || validBits > bits || (encoding == 3 && validBits != bits))
                    return juce::Result::fail ("The WAV declares an invalid significant-bit depth.");
            }
            const auto floating { encoding == 3 };
            if ((encoding != 1 && ! floating) || (floating ? bits != 32 && bits != 64 : bits != 8 && bits != 16 && bits != 24 && bits != 32))
                return juce::Result::fail ("Use integer PCM (8/16/24/32-bit) or IEEE float (32/64-bit) WAV, not compressed audio.");
            if (channelCount < 1 || channelCount > 2)
                return juce::Result::fail ("Raw cycle import accepts mono or stereo WAVs; choose a mono/stereo export first.");
            if (rate == 0 || rate > 384000 || alignment != channelCount * (bits / 8)
                || static_cast<juce::uint64> (byteRate) != static_cast<juce::uint64> (rate) * static_cast<unsigned int> (alignment)
                || audioBytes % static_cast<size_t> (alignment) != 0)
                return juce::Result::fail ("The WAV has an invalid sample rate, frame alignment or data length.");
            const auto frames { audioBytes / static_cast<size_t> (alignment) };
            if (frames < 4 || frames > 8192)
                return juce::Result::fail ("Choose a WAV containing one complete audio cycle of 4..8192 frames, not a long sample or recording.");
            Decoded decoded;
            decoded.info = { file, file.getFileNameWithoutExtension (), static_cast<double> (rate), static_cast<int> (frames), channelCount, bits, floating, {} };
            for (int channel { 0 }; channel < channelCount; ++channel)
            {
                auto& values { decoded.channels[static_cast<size_t> (channel)] };
                values.reserve (frames);
                for (size_t frame { 0 }; frame < frames; ++frame)
                {
                    const auto* sample { data + audioOffset + frame * static_cast<size_t> (alignment) + static_cast<size_t> (channel * (bits / 8)) };
                    double value {};
                    if (floating)
                        value = bits == 32 ? static_cast<double> (std::bit_cast<float> (static_cast<std::uint32_t> (juce::ByteOrder::littleEndianInt (sample))))
                                           : std::bit_cast<double> (static_cast<std::uint64_t> (juce::ByteOrder::littleEndianInt64 (sample)));
                    else if (bits == 8) value = (static_cast<int> (*sample) - 128) / 128.0;
                    else if (bits == 16) value = std::bit_cast<std::int16_t> (static_cast<std::uint16_t> (juce::ByteOrder::littleEndianShort (sample))) / 32768.0;
                    else if (bits == 24)
                    {
                        const auto integer { static_cast<int> (sample[0]) | (static_cast<int> (sample[1]) << 8) | (static_cast<int> (sample[2]) << 16) };
                        value = (integer >= 0x800000 ? integer - 0x1000000 : integer) / 8388608.0;
                    }
                    else value = std::bit_cast<std::int32_t> (static_cast<std::uint32_t> (juce::ByteOrder::littleEndianInt (sample))) / 2147483648.0;
                    if (! std::isfinite (value) || value < -1.0 || value > 1.0)
                        return juce::Result::fail ("The WAV contains nonfinite or above-full-scale samples. Repair its audio data before importing; no automatic normalization was applied.");
                    values.push_back (value);
                }
            }
            int outputFrames { 64 };
            while (outputFrames < static_cast<int> (frames)) outputFrames *= 2;
            decoded.info.warnings.add ("Source: " + juce::String (frames) + " frames at " + juce::String (rate) + " Hz. Designer: "
                + juce::String (outputFrames) + " frames at " + (rate == 96000 ? "96000" : "48000") + " Hz; the whole file is treated as one periodic cycle.");
            decoded.info.warnings.add ("Audio import removes DC, periodically resamples and filters to the selected harmonics (maximum 1024). Original level is retained; amplitude is a gain, not peak normalization.");
            if (channelCount == 2) decoded.info.warnings.add ("Choose Left, Right or Average explicitly. Averaging opposite-polarity stereo channels can cancel the cycle.");
            if (const auto safety { provenance (file) }; safety.failed ()) return safety;
            result = std::move (decoded);
            return juce::Result::ok ();
        }
    }

    juce::Result inspect (const juce::File& file, Info& info)
    {
        Decoded decoded;
        if (const auto result { decode (file, decoded) }; result.failed ()) return result;
        info = std::move (decoded.info);
        return juce::Result::ok ();
    }

    juce::Result load (const juce::File& file, StereoChannel channel, WaveformDesign::Settings& settings, Info* info)
    {
        if (channel != StereoChannel::unspecified && channel != StereoChannel::left && channel != StereoChannel::right && channel != StereoChannel::average)
            return juce::Result::fail ("Choose Left, Right or Average for a stereo import.");
        Decoded decoded;
        if (const auto result { decode (file, decoded) }; result.failed ()) return result;
        if (decoded.info.channels == 2 && channel == StereoChannel::unspecified)
            return juce::Result::fail ("This WAV is stereo. Choose Left, Right or Average; no channel is selected automatically.");
        if (decoded.info.channels == 1 && channel == StereoChannel::right)
            return juce::Result::fail ("A mono WAV has no right channel. Import its mono cycle instead.");
        auto imported { WaveformDesign::startingPoint (WaveformDesign::Mode::oscillator, WaveformDesign::Shape::imported) };
        imported.importedCycleName = decoded.info.name.substring (0, 256).replaceCharacters ("\r\n", "  ");
        imported.importedCycle = std::move (decoded.channels[channel == StereoChannel::right ? 1 : 0]);
        if (decoded.info.channels == 2 && channel == StereoChannel::average)
            for (size_t frame { 0 }; frame < imported.importedCycle.size (); ++frame)
                imported.importedCycle[frame] = (imported.importedCycle[frame] + decoded.channels[1][frame]) * 0.5;
        imported.sampleRate = decoded.info.sampleRate == 96000.0 ? 96000.0 : 48000.0;
        imported.cycleFrames = 64;
        while (imported.cycleFrames < decoded.info.frames) imported.cycleFrames *= 2;
        imported.amplitude = 1.0;
        imported.harmonics = 1024;
        imported.brightness = 1.0;
        if (const auto valid { WaveformDesign::validate (imported) }; valid.failed ()) return valid;
        settings = std::move (imported);
        if (info != nullptr) *info = std::move (decoded.info);
        return juce::Result::ok ();
    }
}
