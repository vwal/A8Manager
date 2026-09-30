#include "StereoCollapse.h"
#include "AudioManager.h"
#include "CvSampleSafety.h"
#include "WaveformDesignExport.h"
#include "../Preset/ParameterPresetsSingleton.h"
#include "../Preset/PresetProperties.h"
#include <juce_cryptography/juce_cryptography.h>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <tuple>
#include <unordered_map>

namespace StereoCollapse
{
    namespace
    {
        constexpr int blockFrames { 8192 };

        std::filesystem::path path (const juce::File& file)
        {
            return std::filesystem::path (reinterpret_cast<const char8_t*> (file.getFullPathName ().toRawUTF8 ()));
        }

        bool regular (const juce::File& file, bool folder = false)
        {
            std::error_code error;
            const auto type { std::filesystem::symlink_status (path (file), error).type () };
            return ! error && type == (folder ? std::filesystem::file_type::directory : std::filesystem::file_type::regular);
        }

        bool safeName (const juce::String& name)
        {
            return name.isNotEmpty () && name.length () <= 47 && ! name.startsWithChar ('.')
                && name.endsWithIgnoreCase (".wav")
                && name.containsOnly (" !#$%&'()+,-.0123456789;=@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]_`{}~abcdefghijklmnopqrstuvwxyz");
        }

        juce::Result identify (const juce::File& file, Result::CreatedFile& record)
        {
            if (! regular (file)) return juce::Result::fail ("The sample is missing, linked or not a regular file: " + file.getFullPathName ());
            const auto identity { file.getFileIdentifier () };
            const auto size { file.getSize () };
            const auto modified { file.getLastModificationTime () };
            juce::FileInputStream stream (file);
            if (stream.getStatus ().failed ()) return stream.getStatus ();
            const auto hash { juce::SHA256 (stream).toHexString () };
            if (stream.getStatus ().failed () || stream.getPosition () != size || ! regular (file)
                || file.getFileIdentifier () != identity || file.getSize () != size || file.getLastModificationTime () != modified)
                return juce::Result::fail ("A sample changed or could not be read completely: " + file.getFullPathName ());
            record = { file, identity, size, modified, hash };
            return juce::Result::ok ();
        }

        bool same (const Result::CreatedFile& before, const Result::CreatedFile& after)
        {
            return before.file == after.file && before.identity == after.identity && before.size == after.size
                && before.modified == after.modified && before.sha256 == after.sha256;
        }

        juce::Result validateWaveContainer (const juce::File& file)
        {
            // Some readers pad truncated PCM with zeros. Validate RIFF extents
            // before opening the audio reader so a damaged file is never
            // silently shortened or extended by the conversion.
            juce::FileInputStream stream (file);
            char header[12];
            const auto size { stream.getTotalLength () };
            auto bad = [&] { return juce::Result::fail ("The WAV is incomplete or not a supported integer PCM RIFF file: " + file.getFileName ()); };
            if (stream.getStatus ().failed () || stream.read (header, 12) != 12 || std::memcmp (header, "RIFF", 4) != 0
                || std::memcmp (header + 8, "WAVE", 4) != 0) return bad ();
            const auto declaredEnd { static_cast<juce::int64> (juce::ByteOrder::littleEndianInt (header + 4)) + 8 };
            if (declaredEnd < size || declaredEnd > size + 1 || size < 44) return bad ();
            const auto end { size };
            juce::int64 dataBytes { -1 }, metadataBytes { 12 };
            int blockAlign { 0 }, chunks { 0 };
            bool foundFormat { false }, omittedFinalDataPadding { false };
            while (stream.getPosition () < end)
            {
                char chunk[8];
                if (++chunks > 65536 || stream.read (chunk, 8) != 8) return bad ();
                const auto length { static_cast<juce::int64> (juce::ByteOrder::littleEndianInt (chunk + 4)) };
                const auto padded { length + (length & 1) };
                // JUCE 9.0.2 accepts files omitting the final odd-data padding
                // byte. That byte is not PCM, unlike a genuinely truncated
                // sample: accept only when every declared data byte is present.
                const auto unpaddedData { std::memcmp (chunk, "data", 4) == 0 && (length & 1) != 0
                    && length == end - stream.getPosition () };
                if (padded > end - stream.getPosition () && ! unpaddedData) return bad ();
                const auto next { stream.getPosition () + (unpaddedData ? length : padded) };
                if (std::memcmp (chunk, "data", 4) == 0)
                {
                    if (dataBytes >= 0) return bad ();
                    dataBytes = length;
                    omittedFinalDataPadding = unpaddedData;
                }
                else
                {
                    metadataBytes += length + 8;
                    if (metadataBytes > 16 * 1024 * 1024) return bad ();
                    if (std::memcmp (chunk, "fmt ", 4) == 0)
                    {
                        char format[40] {};
                        if (foundFormat || length < 16 || stream.read (format, 16) != 16) return bad ();
                        foundFormat = true;
                        const auto code { juce::ByteOrder::littleEndianShort (format) };
                        const auto channels { juce::ByteOrder::littleEndianShort (format + 2) };
                        const auto bits { juce::ByteOrder::littleEndianShort (format + 14) };
                        blockAlign = juce::ByteOrder::littleEndianShort (format + 12);
                        if (channels < 1 || channels > 2 || bits < 8 || bits > 32 || bits % 8 != 0
                            || blockAlign != channels * (bits / 8)) return bad ();
                        if (code == 0xfffe)
                        {
                            static constexpr unsigned char pcmSubtype[16] { 1, 0, 0, 0, 0, 0, 16, 0, 128, 0, 0, 170, 0, 56, 155, 113 };
                            if (length < 40 || stream.read (format + 16, 24) != 24 || std::memcmp (format + 24, pcmSubtype, 16) != 0) return bad ();
                        }
                        else if (code != 1) return bad ();
                    }
                }
                if (! stream.setPosition (next)) return bad ();
            }
            return foundFormat && dataBytes > 0 && blockAlign > 0 && dataBytes % blockAlign == 0 && stream.getStatus ().wasOk ()
                && (declaredEnd == end || omittedFinalDataPadding)
                ? juce::Result::ok () : bad ();
        }

        struct Source
        {
            Result::CreatedFile before;
            std::unique_ptr<juce::AudioFormatReader> reader;
            bool cv {};
        };

        struct Job
        {
            Source* left {};
            Source* right {};
            int leftSide {}, rightSide {};
            juce::File staged, final;
        };

        juce::Result readBlock (Job& job, juce::int64 offset, int frames, Mode mode,
                                juce::AudioBuffer<float>& left, juce::AudioBuffer<float>& right, juce::AudioBuffer<float>& mono)
        {
            if (! job.left->reader->read (&left, 0, frames, offset, true, true)
                || (job.right != job.left && ! job.right->reader->read (&right, 0, frames, offset, true, true)))
                return juce::Result::fail ("Unable to read the complete source samples; no preset change was made.");
            const auto* a { left.getReadPointer (job.leftSide) };
            const auto* b { (job.right == job.left ? left : right).getReadPointer (job.rightSide) };
            auto* output { mono.getWritePointer (0) };
            for (int index { 0 }; index < frames; ++index)
            {
                if (! std::isfinite (a[index]) || ! std::isfinite (b[index]) || std::abs (a[index]) > 1.0f || std::abs (b[index]) > 1.0f)
                    return juce::Result::fail ("The source contains non-finite or out-of-range samples; conversion was canceled without clipping.");
                output[index] = mode == Mode::merge ? a[index] * 0.5f + b[index] * 0.5f : mode == Mode::keepLeft ? a[index] : b[index];
            }
            return juce::Result::ok ();
        }

        juce::Result convert (Job& job, Mode mode)
        {
            auto fileStream { job.staged.createOutputStream () };
            if (! fileStream || fileStream->getStatus ().failed ()) return juce::Result::fail ("Cannot create the staged mono WAV.");
            auto* output { fileStream.get () };
            std::unique_ptr<juce::OutputStream> stream { std::move (fileStream) };
            // Purpose is preserved explicitly, but no generator recipe is
            // copied: selecting/averaging PCM cannot retain its original recipe.
            const auto purpose { CvSampleSafety::exportMetadata (job.left->cv) };
            std::unordered_map<juce::String, juce::String> metadata;
            for (int index { 0 }; index < purpose.size (); ++index)
                metadata.emplace (purpose.getAllKeys ()[index], purpose.getAllValues ()[index]);
            juce::WavAudioFormat format;
            auto writer { format.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (job.left->reader->sampleRate)
                                                  .withNumChannels (1).withBitsPerSample (24).withMetadataValues (metadata)) };
            if (! writer) return juce::Result::fail ("Cannot create the mono PCM24 writer.");
            juce::AudioBuffer<float> left (2, blockFrames), right (2, blockFrames), mono (1, blockFrames), verify (1, blockFrames);
            const auto length { job.left->reader->lengthInSamples };
            for (juce::int64 offset { 0 }; offset < length; offset += blockFrames)
            {
                const auto frames { static_cast<int> (std::min<juce::int64> (blockFrames, length - offset)) };
                if (const auto read { readBlock (job, offset, frames, mode, left, right, mono) }; read.failed ()) return read;
                if (! writer->writeFromAudioSampleBuffer (mono, 0, frames)) return juce::Result::fail ("Writing the mono WAV failed.");
            }
            if (! writer->flush ()) return juce::Result::fail ("Unable to finish the mono WAV.");
            output->flush ();
            const auto status { output->getStatus () };
            writer.reset ();
            if (status.failed ()) return status;
            AudioManager audio;
            auto reader { audio.getReaderFor (job.staged) };
            if (! reader || reader->numChannels != 1 || reader->bitsPerSample != 24 || reader->usesFloatingPointData
                || reader->lengthInSamples != length || reader->sampleRate != job.left->reader->sampleRate
                || CvSampleSafety::isCv (job.staged, *reader) != job.left->cv)
                return juce::Result::fail ("The mono WAV failed format, duration or audio/CV verification.");
            for (juce::int64 offset { 0 }; offset < length; offset += blockFrames)
            {
                const auto frames { static_cast<int> (std::min<juce::int64> (blockFrames, length - offset)) };
                if (const auto read { readBlock (job, offset, frames, mode, left, right, mono) }; read.failed ()) return read;
                if (! reader->read (&verify, 0, frames, offset, true, false)) return juce::Result::fail ("Unable to verify all mono samples.");
                for (int index { 0 }; index < frames; ++index)
                    if (! std::isfinite (verify.getSample (0, index)) || std::abs (verify.getSample (0, index) - mono.getSample (0, index)) > 2.0f / 8388608.0f)
                        return juce::Result::fail ("The mono WAV did not preserve the expected sample values.");
            }
            return juce::Result::ok ();
        }
    }

    int pairLeftIndex (const juce::ValueTree& preset, int eitherChannel)
    {
        if (! preset.hasType (PresetProperties::PresetTypeId) || preset.getNumChildren () != 8 || eitherChannel < 0 || eitherChannel >= 8) return -1;
        const auto modeAt = [&] (int channel) { return static_cast<int> (preset.getChild (channel).getProperty (ChannelProperties::ChannelModePropertyId)); };
        const auto left { modeAt (eitherChannel) == ChannelProperties::stereoRight ? eitherChannel - 1 : eitherChannel };
        if (left < 0 || left >= 7 || modeAt (left) < ChannelProperties::master || modeAt (left) > ChannelProperties::cycle
            || modeAt (left) == ChannelProperties::stereoRight || modeAt (left + 1) != ChannelProperties::stereoRight
            || (left + 2 < 8 && modeAt (left + 2) == ChannelProperties::stereoRight)) return -1;
        return left;
    }

    juce::Result cleanup (Result& result)
    {
        std::vector<Result::CreatedFile> remaining;
        juce::StringArray messages;
        for (const auto& owned : result.ownership)
        {
            if (! owned.file.exists () && ! owned.file.isSymbolicLink ()) continue;
            Result::CreatedFile current;
            if (identify (owned.file, current).failed () || ! same (owned, current))
            {
                remaining.push_back (owned);
                messages.add ("Changed file preserved: " + owned.file.getFullPathName ());
            }
            else if (! owned.file.deleteFile ())
            {
                remaining.push_back (owned);
                messages.add ("Unable to remove unused converted file: " + owned.file.getFullPathName ());
            }
        }
        result.editedPreset = {};
        result.leftChannel = -1;
        result.createdFiles.clear ();
        result.ownership = std::move (remaining);
        for (const auto& owned : result.ownership) result.createdFiles.add (owned.file);
        return messages.isEmpty () ? juce::Result::ok () : juce::Result::fail (messages.joinIntoString ("\n"));
    }

    juce::Result prepare (const juce::File& folder, const juce::ValueTree& preset,
                           int eitherChannel, Mode mode, Result& result, Publish publish)
    {
        result = {};
        if (! regular (folder, true)) return juce::Result::fail ("The preset folder is unavailable or is a symbolic link.");
        if (mode != Mode::merge && mode != Mode::keepLeft && mode != Mode::keepRight) return juce::Result::fail ("Choose a supported stereo-to-mono operation.");
        const auto leftIndex { pairLeftIndex (preset, eitherChannel) };
        if (leftIndex < 0) return juce::Result::fail ("Choose a valid adjacent stereo pair; orphan or chained Stereo Right channels cannot be converted.");
        for (int channel { 0 }; channel < 8; ++channel)
        {
            const auto tree { preset.getChild (channel) };
            if (! tree.hasType (ChannelProperties::ChannelTypeId) || tree.getNumChildren () != 8
                || static_cast<int> (tree.getProperty (ChannelProperties::IdPropertyId)) != channel + 1)
                return juce::Result::fail ("The preset has invalid channel structure.");
            for (int zone { 0 }; zone < 8; ++zone)
                if (! tree.getChild (zone).hasType (ZoneProperties::ZoneTypeId) || tree.getChild (zone).getNumChildren () != 0
                    || static_cast<int> (tree.getChild (zone).getProperty (ZoneProperties::IdPropertyId)) != zone + 1)
                    return juce::Result::fail ("The preset has invalid zone structure.");
        }
        std::map<juce::String, Source> sources;
        AudioManager audio;
        auto source = [&] (const juce::String& name, Source*& value) -> juce::Result
        {
            if (! safeName (name)) return juce::Result::fail ("Stereo conversion requires safe, flat WAV references: " + name);
            const auto key { name.toLowerCase () };
            if (auto found { sources.find (key) }; found != sources.end ())
            {
                if (found->second.before.file.getFileName () != name) return juce::Result::fail ("Sample references differing only by case are ambiguous.");
                value = &found->second;
                return juce::Result::ok ();
            }
            Source opened;
            const auto file { folder.getChildFile (name) };
            if (const auto checked { identify (file, opened.before) }; checked.failed ()) return checked;
            if (const auto checked { validateWaveContainer (file) }; checked.failed ()) return checked;
            opened.reader = audio.getReaderFor (file);
            const auto* reader { opened.reader.get () };
            constexpr juce::int64 maximumFrames { (static_cast<juce::int64> (std::numeric_limits<juce::uint32>::max ()) - 65536) / 3 };
            if (reader == nullptr || reader->usesFloatingPointData || reader->numChannels < 1 || reader->numChannels > 2
                || reader->bitsPerSample < 8 || reader->bitsPerSample > 32 || ! std::isfinite (reader->sampleRate)
                || reader->sampleRate <= 0 || reader->sampleRate > 192000 || reader->lengthInSamples < 1 || reader->lengthInSamples > maximumFrames)
                return juce::Result::fail ("The source is not a supported mono/stereo integer PCM WAV, or is too long for a mono PCM24 RIFF file: " + name);
            opened.cv = CvSampleSafety::isCv (file, *reader);
            value = &sources.emplace (key, std::move (opened)).first->second;
            return juce::Result::ok ();
        };
        using Key = std::tuple<juce::String, int, juce::String, int>;
        std::map<Key, size_t> reused;
        std::vector<Job> jobs;
        std::array<int, 8> zoneJobs;
        zoneJobs.fill (-1);
        auto anyCv { false }, anyAudio { false };
        for (int index { 0 }; index < 8; ++index)
        {
            ZoneProperties left (preset.getChild (leftIndex).getChild (index), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            ZoneProperties right (preset.getChild (leftIndex + 1).getChild (index), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            if (left.getSample ().isEmpty () != right.getSample ().isEmpty ())
                return juce::Result::fail ("Both sides must contain the same occupied zones; zone " + juce::String (index + 1) + " is incomplete.");
            if (left.getSample ().isEmpty ()) continue;
            Job job;
            if (const auto opened { source (left.getSample (), job.left) }; opened.failed ()) return opened;
            if (const auto opened { source (right.getSample (), job.right) }; opened.failed ()) return opened;
            job.leftSide = left.getSide ();
            job.rightSide = right.getSide ();
            if (job.leftSide < 0 || job.leftSide >= static_cast<int> (job.left->reader->numChannels)
                || job.rightSide < 0 || job.rightSide >= static_cast<int> (job.right->reader->numChannels))
                return juce::Result::fail ("A zone selects a nonexistent audio side. Mono WAVs must select side 0.");
            if (job.left->reader->sampleRate != job.right->reader->sampleRate || job.left->reader->lengthInSamples != job.right->reader->lengthInSamples)
                return juce::Result::fail ("The two sides of zone " + juce::String (index + 1) + " must have identical sample rates and full-file lengths; no implicit resampling or trimming is performed.");
            if (job.left->cv != job.right->cv) return juce::Result::fail ("Audio and CV cannot be combined or selected from a mixed-purpose stereo pair.");
            anyCv = anyCv || job.left->cv;
            anyAudio = anyAudio || ! job.left->cv;
            const Key key { left.getSample (), job.leftSide, right.getSample (), job.rightSide };
            auto [item, inserted] { reused.emplace (key, jobs.size ()) };
            zoneJobs[static_cast<size_t> (index)] = static_cast<int> (item->second);
            if (inserted) jobs.push_back (std::move (job));
        }
        if (anyCv && anyAudio) return juce::Result::fail ("CV and audio zones must be on separate channels before converting.");
        if (anyCv)
            for (int index : { leftIndex, leftIndex + 1 })
            {
                ChannelProperties channel (preset.getChild (index), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
                if (channel.getMixLevel () != -90 || std::get<0> (channel.getMixMod ()) != "Off")
                    return juce::Result::fail ("CV requires Mix Off and Mix modulation Off on both source channels before conversion.");
            }

        const auto stage { folder.getChildFile (".a8-stereo-collapse-" + juce::Uuid ().toString ()) };
        std::error_code error;
        if (! std::filesystem::create_directory (path (stage), error) || error)
            return juce::Result::fail ("Unable to create a private stereo conversion staging folder.");
        struct RemoveStage { juce::File directory; ~RemoveStage () { directory.deleteRecursively (); } } removeStage { stage };
        for (auto& job : jobs)
        {
            const auto stem { WaveformDesign::ExportSupport::safeStem (job.left->before.file.getFileNameWithoutExtension ()).substring (0, 25) };
            const auto name { stem + "-mono-" + juce::Uuid ().toString ().substring (0, 12) + ".wav" };
            job.staged = stage.getChildFile (name);
            job.final = folder.getChildFile (name);
            if (const auto converted { convert (job, mode) }; converted.failed ()) return converted;
        }
        for (const auto& item : sources)
        {
            Result::CreatedFile after;
            if (const auto checked { identify (item.second.before.file, after) }; checked.failed ()) return checked;
            if (! same (item.second.before, after)) return juce::Result::fail ("A source WAV changed during conversion. No preset change was made.");
        }
        if (! publish) publish = WaveformDesign::ExportSupport::publishExclusive;
        auto fail = [&] (const juce::String& message)
        {
            const auto rolledBack { cleanup (result) };
            return juce::Result::fail (message + (rolledBack.failed () ? "\n" + rolledBack.getErrorMessage () : juce::String ()));
        };
        for (const auto& job : jobs)
        {
            Result::CreatedFile staged;
            if (const auto checked { identify (job.staged, staged) }; checked.failed ()) return fail (checked.getErrorMessage ());
            if (const auto published { publish (job.staged, job.final) }; published.failed ()) return fail (published.getErrorMessage ());
            staged.file = job.final; // Exclusive rename retains file identity.
            result.ownership.push_back (staged);
            result.createdFiles.add (job.final);
            Result::CreatedFile published;
            if (const auto checked { identify (job.final, published) }; checked.failed ()) return fail (checked.getErrorMessage ());
            if (! same (staged, published)) return fail ("A published mono file changed before assignment; it was preserved for inspection.");
        }
        auto edited { preset.createCopy () };
        for (int index { 0 }; index < 8; ++index)
        {
            ZoneProperties zone (edited.getChild (leftIndex).getChild (index), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            const auto job { zoneJobs[static_cast<size_t> (index)] };
            if (job >= 0) zone.setSample (jobs[static_cast<size_t> (job)].final.getFileName (), false);
            zone.setSide (0, false);
        }
        const auto defaults { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ()
            .getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType) };
        edited.getChild (leftIndex + 1).copyPropertiesAndChildrenFrom (defaults.getChild (leftIndex + 1), nullptr);
        result.editedPreset = edited;
        result.leftChannel = leftIndex;
        return juce::Result::ok ();
    }
}
