#include "WaveformDesignAssignment.h"
#include "WaveformDesignExport.h"
#include "AudioManager.h"
#include "CvSampleSafety.h"
#include "../Preset/PresetProperties.h"
#include <filesystem>
#include <array>
#include <cmath>

namespace WaveformDesign
{
    namespace
    {
        bool isFollower (int mode)
        {
            return mode == ChannelProperties::link || mode == ChannelProperties::cycle || mode == ChannelProperties::stereoRight;
        }

        juce::Result validateTargets (const Settings& settings, const juce::File& folder, juce::ValueTree tree,
                                      int firstChannel, int zoneIndex, int count)
        {
            if (! tree.hasType (PresetProperties::PresetTypeId) || tree.getNumChildren () != kNumChannels)
                return juce::Result::fail ("The selected preset does not contain eight valid channels.");
            std::array<int, kNumChannels> modes {};
            for (int index { 0 }; index < kNumChannels; ++index)
            {
                const auto channel { tree.getChild (index) };
                if (! channel.hasType (ChannelProperties::ChannelTypeId) || channel.getNumChildren () != kNumZones ||
                    static_cast<int> (channel.getProperty (ChannelProperties::IdPropertyId)) != index + 1)
                    return juce::Result::fail ("The selected preset has invalid channel or zone structure.");
                ChannelProperties properties (channel, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
                modes[static_cast<size_t> (index)] = properties.getChannelMode ();
                for (int zone { 0 }; zone < kNumZones; ++zone)
                    if (! channel.getChild (zone).hasType (ZoneProperties::ZoneTypeId) ||
                        static_cast<int> (channel.getChild (zone).getProperty (ZoneProperties::IdPropertyId)) != zone + 1)
                        return juce::Result::fail ("The selected preset has invalid zone numbering.");
            }
            const auto endChannel { firstChannel + count };
            AudioManager audio;
            const auto cv { settings.mode == Mode::modulation };
            for (int index { firstChannel }; index < endChannel; ++index)
            {
                const auto mode { modes[static_cast<size_t> (index)] };
                if (mode < ChannelProperties::master || mode > ChannelProperties::cycle)
                    return juce::Result::fail ("A target channel has an unsupported channel mode.");
                if (mode == ChannelProperties::stereoRight ||
                    (index + 1 < kNumChannels && modes[static_cast<size_t> (index + 1)] == ChannelProperties::stereoRight))
                    return juce::Result::fail ("A target channel belongs to a stereo pair. Choose unpaired channels.");
                auto groupStart { index }, groupEnd { index + 1 };
                while (groupStart > 0 && isFollower (modes[static_cast<size_t> (groupStart)])) --groupStart;
                while (groupEnd < kNumChannels && isFollower (modes[static_cast<size_t> (groupEnd)])) ++groupEnd;
                if (groupStart < firstChannel || groupEnd > endChannel || (groupStart == 0 && isFollower (modes[0])))
                    return juce::Result::fail ("The target intersects a linked/cycle group outside the assignment. Choose the whole group or independent channels.");

                ChannelProperties channel (tree.getChild (index), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
                auto used { 0 };
                auto foundEmpty { false };
                auto upperVoltage { 5.0 };
                for (int zone { 0 }; zone < kNumZones; ++zone)
                {
                    ZoneProperties properties (channel.getZoneVT (zone), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                    const auto filename { properties.getSample () };
                    if (filename.isEmpty ()) { foundEmpty = true; continue; }
                    if (foundEmpty) return juce::Result::fail ("A target channel has gaps between its used zones. Fill or purge those gaps first.");
                    ++used;
                    const auto voltage { properties.getMinVoltage () };
                    if (! std::isfinite (voltage) || voltage < -5.0 || voltage >= upperVoltage)
                        return juce::Result::fail ("A target channel has invalid or overlapping zone voltage ranges.");
                    upperVoltage = voltage;
                    if (filename.containsAnyOf ("/\\") || juce::File::isAbsolutePath (filename))
                        return juce::Result::fail ("Existing samples must use flat filenames in the current preset folder.");
                    const auto file { folder.getChildFile (filename) };
                    const auto reader { audio.getReaderFor (file) };
                    if (! reader || reader->lengthInSamples <= 0 || reader->numChannels == 0 || ! std::isfinite (reader->sampleRate) || reader->sampleRate <= 0.0)
                        return juce::Result::fail ("Cannot verify the purpose of existing sample '" + filename + "'. Restore/read the file before assigning a design.");
                    if (CvSampleSafety::isCv (file, *reader) != cv)
                        return juce::Result::fail ("A target channel already contains " + juce::String (cv ? "audio" : "CV") +
                            " samples. Keep audio and CV on separate channels, including when replacing a zone.");
                }
                if (zoneIndex > used)
                    return juce::Result::fail ("Choose an existing zone or the next empty zone; assignment cannot leave zone gaps.");
                if (zoneIndex == used)
                {
                    // Append like an ordinary sample drop: divide the former
                    // last range, preserving its contents and all earlier zones.
                    if (used > 0)
                    {
                        ZoneProperties previous (channel.getZoneVT (used - 1), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                        auto upper { 5.0 };
                        if (used > 1)
                        {
                            ZoneProperties before (channel.getZoneVT (used - 2), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                            upper = before.getMinVoltage ();
                        }
                        const auto midpoint { (upper - 5.0) * 0.5 };
                        if (! (midpoint > -5.0 && midpoint < upper))
                            return juce::Result::fail ("The last zone's voltage range is too narrow to split.");
                        previous.setMinVoltage (midpoint, false);
                    }
                    ZoneProperties target (channel.getZoneVT (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                    target.setMinVoltage (-5.0, false);
                }
            }
            return juce::Result::ok ();
        }
    }

    juce::Result cleanupAssignmentFiles (AssignmentResult& result)
    {
        juce::StringArray failures;
        std::vector<AssignmentResult::CreatedFile> remaining;
        for (const auto& owned : result.ownership)
        {
            if (! owned.file.exists ()) continue;
            if (! owned.file.existsAsFile () || owned.file.getFileIdentifier () != owned.identity ||
                owned.file.getSize () != owned.size || owned.file.getLastModificationTime () != owned.modified)
            {
                failures.add ("Changed file preserved: " + owned.file.getFullPathName ());
                continue;
            }
            if (! owned.file.deleteFile ())
            {
                remaining.push_back (owned);
                failures.add ("Unable to remove " + owned.file.getFullPathName ());
            }
        }
        result.editedPreset = {};
        result.waves.clear ();
        result.recipe = juce::File {};
        result.createdFiles.clear ();
        result.ownership = std::move (remaining);
        for (const auto& owned : result.ownership) result.createdFiles.add (owned.file);
        return failures.isEmpty () ? juce::Result::ok () : juce::Result::fail (failures.joinIntoString ("\n"));
    }

    juce::Result prepareAssignment (const Settings& settings, const juce::File& folder, const juce::String& name,
                                    const juce::ValueTree& sourcePreset, int firstChannel, int zone, AssignmentResult& result)
    {
        result = {};
        if (! folder.isDirectory ()) return juce::Result::fail ("Choose an existing current preset folder.");
        if (const auto valid { validate (settings) }; valid.failed ()) return valid;
        const auto count { settings.mode == Mode::layers ? settings.voiceCount : 1 };
        if (firstChannel < 0 || firstChannel >= kNumChannels || count < 1 || count > kNumChannels - firstChannel || zone < 0 || zone >= kNumZones)
            return juce::Result::fail ("The selected channels/zones do not fit this design; a bank cannot extend past channel 8.");
        auto edited { sourcePreset.createCopy () };
        if (const auto valid { validateTargets (settings, folder, edited, firstChannel, zone, count) }; valid.failed ()) return valid;
        Render rendered;
        if (const auto generated { render (settings, rendered) }; generated.failed ()) return generated;
        if (rendered.voices.size () != static_cast<size_t> (count)) return juce::Result::fail ("Rendered voice count does not match the assignment.");

        const auto token { juce::Uuid ().toString () };
        const auto stage { folder.getChildFile (".a8-assignment-" + token) };
        std::error_code error;
        if (! std::filesystem::create_directory (std::filesystem::u8path (stage.getFullPathName ().toStdString ()), error) || error)
            return juce::Result::fail ("Cannot create a private assignment staging folder: " + juce::String (error.message ()));
        struct Cleanup { juce::File stage; ~Cleanup () { if (stage.exists ()) stage.deleteRecursively (); } } cleanup { stage };
        auto fail = [&] (const juce::String& message)
        {
            const auto rollback { cleanupAssignmentFiles (result) };
            const auto removed { stage.deleteRecursively () };
            return juce::Result::fail (message + (rollback.failed () ? "\n" + rollback.getErrorMessage () : juce::String ()) +
                (removed ? juce::String () : "\nIncomplete staging files remain at " + stage.getFullPathName ()));
        };
        const auto stem { ExportSupport::safeStem (name).substring (0, 22) + "-" + token.substring (0, 12) };
        juce::StringArray names;
        for (int index { 0 }; index < count; ++index)
        {
            const auto filename { stem + "-" + juce::String (index + 1).paddedLeft ('0', 2) + ".wav" };
            if (const auto written { ExportSupport::writeWave (stage.getChildFile (filename), rendered.voices[static_cast<size_t> (index)],
                                                               rendered.sampleRate, settings.mode == Mode::modulation) }; written.failed ())
                return fail (written.getErrorMessage ());
            names.add (filename);
            const auto channel { edited.getChild (firstChannel + index) };
            ExportSupport::configureChannel (channel, settings, index, count);
            ExportSupport::configureZone (channel.getChild (zone), filename, rendered.frames);
        }
        const auto recipeName { stem + ".design.json" };
        if (const auto written { ExportSupport::writeText (stage.getChildFile (recipeName), juce::JSON::toString (ExportSupport::namedRecipe (settings, name)) + "\n") }; written.failed ())
            return fail (written.getErrorMessage ());
        names.add (recipeName);
        // All verified content exists privately before publishing any flat file.
        // Exclusive rename handles a concurrent name collision without overwrite.
        for (int index { 0 }; index < names.size (); ++index)
        {
            const auto destination { folder.getChildFile (names[index]) };
            if (const auto published { ExportSupport::publishExclusive (stage.getChildFile (names[index]), destination) }; published.failed ())
                return fail (published.getErrorMessage ());
            result.createdFiles.add (destination);
            result.ownership.push_back ({ destination, destination.getFileIdentifier (), destination.getSize (), destination.getLastModificationTime () });
            if (index < count) result.waves.add (destination);
            else result.recipe = destination;
        }
        result.editedPreset = edited;
        return juce::Result::ok ();
    }
}
