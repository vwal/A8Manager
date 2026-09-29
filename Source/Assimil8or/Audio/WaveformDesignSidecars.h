#pragma once

#include "WaveformDesign.h"
#include "CvSampleSafety.h"
#include <cstring>

// These are desktop design artifacts, not sample/preset files for the module.
// Recognize their actual format, never all files with a JSON/TXT extension.
namespace WaveformDesignSidecars
{
    enum class Kind { unknown, recipe, instructions, testManifest, testInstructions };

    // Recognizes a bounded analysis-record schema, not the correctness of its
    // referenced WAVs or actual hardware output. Those are validated separately.
    inline bool isTestManifest (const juce::var& json)
    {
        const auto* root { json.getDynamicObject () };
        if (root == nullptr || root->getProperty ("type") != "A8Manager.HardwareTestOutput") return false;
        auto integer = [] (const juce::DynamicObject& object, const juce::Identifier& key, juce::int64 low, juce::int64 high)
        {
            const auto& value { object.getProperty (key) };
            return (value.isInt () || value.isInt64 ()) && static_cast<juce::int64> (value) >= low
                && static_cast<juce::int64> (value) <= high;
        };
        if (! integer (*root, "schemaVersion", 1, 1) || ! integer (*root, "presetNumber", 1, 199)
            || ! integer (*root, "referenceChannel", 2, 8)) return false;
        const auto signal { root->getProperty ("signal").toString () };
        if (signal != "currentDesign" && signal != "audioTone" && signal != "cvLevels" && signal != "cvSine" && signal != "cvRamp")
            return false;
        double rate {}, duration {}, level {};
        if (! CvSampleSafety::detail::number (*root, "sampleRate", rate) || (rate != 48000.0 && rate != 96000.0)
            || ! CvSampleSafety::detail::number (*root, "durationSeconds", duration) || duration < 1.0 || duration > 60.0
            || ! CvSampleSafety::detail::number (*root, "level", level) || level < 0.01 || level > 0.25
            || ! integer (*root, "windowFrames", static_cast<juce::int64> (rate), static_cast<juce::int64> (rate * 60.0))) return false;
        const auto frames { static_cast<juce::int64> (root->getProperty ("windowFrames")) };
        if (frames != std::llround (rate * duration)) return false;
        const auto referenceFrames { frames + std::llround (rate * 0.12) };
        const auto reference { static_cast<int> (root->getProperty ("referenceChannel")) };
        const auto* channels { root->getProperty ("channels").getArray () };
        if (channels == nullptr || channels->size () != reference) return false;
        juce::StringArray files;
        for (int index { 0 }; index < channels->size (); ++index)
        {
            const auto* channel { channels->getReference (index).getDynamicObject () };
            if (channel == nullptr || ! integer (*channel, "channel", index + 1, index + 1)
                || ! integer (*channel, "frames", 4, index + 1 == reference ? referenceFrames : static_cast<juce::int64> (rate * 60.0))
                || ! integer (*channel, "playMode", 0, 1) || ! integer (*channel, "loopMode", 0, 2)) return false;
            const auto filename { channel->getProperty ("file").toString () };
            const auto purpose { channel->getProperty ("purpose").toString () };
            double pitch {};
            if (filename.isEmpty () || filename.containsAnyOf ("/\\:") || ! filename.endsWithIgnoreCase (".wav")
                || files.contains (filename, true) || (purpose != "AUDIO" && purpose != "CV")
                || ! CvSampleSafety::detail::number (*channel, "pitchSemitones", pitch) || std::abs (pitch) > 120.0) return false;
            files.add (filename);
            if (index + 1 == reference && (purpose != "AUDIO" || pitch != 0.0
                || static_cast<juce::int64> (channel->getProperty ("frames")) != referenceFrames)) return false;
        }
        const auto* bursts { root->getProperty ("referenceBursts").getArray () };
        if (bursts == nullptr || bursts->size () < 2 || bursts->size () > 32) return false;
        auto hasStart { false }, hasEnd { false };
        for (const auto& value : *bursts)
        {
            const auto* burst { value.getDynamicObject () };
            if (burst == nullptr || ! integer (*burst, "startFrame", 0, referenceFrames - 1)
                || ! integer (*burst, "endFrameExclusive", 1, referenceFrames)) return false;
            if (static_cast<juce::int64> (burst->getProperty ("startFrame")) >= static_cast<juce::int64> (burst->getProperty ("endFrameExclusive")))
                return false;
            const auto event { burst->getProperty ("event").toString () };
            if (event == "start") hasStart = true;
            else if (event == "end") hasEnd = true;
            else return false;
            double frequency {}, peak {};
            if (! CvSampleSafety::detail::number (*burst, "frequencyHz", frequency) || frequency <= 0.0 || frequency >= rate * 0.5
                || ! CvSampleSafety::detail::number (*burst, "peakFs", peak) || peak <= 0.0 || peak > 1.0) return false;
        }
        return hasStart && hasEnd;
    }

    inline Kind identify (const juce::File& file)
    {
        const auto filename { file.getFileName () };
        const auto recipe { filename.equalsIgnoreCase ("design.json") || filename.endsWithIgnoreCase (".design.json") };
        const auto testManifest { filename.equalsIgnoreCase ("test-manifest.json") };
        const auto instructions { filename.equalsIgnoreCase ("README.txt") };
        if ((! recipe && ! testManifest && ! instructions) || ! file.existsAsFile ()) return Kind::unknown;

        // Match the recipe loader's size limit. Bound the actual read as well:
        // another process could grow the file after getSize (). Reject binary,
        // invalid UTF-8 and deeply nested/trailing JSON without recursive parsing.
        constexpr int maximumBytes { 1024 * 1024 };
        const auto bytes { file.getSize () };
        if (bytes <= 0 || bytes > maximumBytes) return Kind::unknown;
        auto stream { file.createInputStream () };
        if (! stream || stream->getStatus ().failed ()) return Kind::unknown;
        juce::MemoryBlock contents;
        stream->readIntoMemoryBlock (contents, maximumBytes + 1);
        const auto length { static_cast<int> (contents.getSize ()) };
        if (length <= 0 || length > maximumBytes || stream->getStatus ().failed ()) return Kind::unknown;
        const auto* data { static_cast<const char*> (contents.getData ()) };
        if (std::memchr (data, 0, contents.getSize ()) != nullptr || ! juce::CharPointer_UTF8::isValidString (data, length)) return Kind::unknown;
        if (recipe || testManifest)
        {
            if (! CvSampleSafety::detail::boundedJsonObject (data, length)) return Kind::unknown;
            const auto json { juce::JSON::parse (juce::String::fromUTF8 (data, length)) };
            if (testManifest) return isTestManifest (json) ? Kind::testManifest : Kind::unknown;
            WaveformDesign::Settings settings;
            return WaveformDesign::fromJson (json, settings).wasOk ()
                ? Kind::recipe : Kind::unknown;
        }

        const auto text { juce::String::fromUTF8 (data, length).replace ("\r\n", "\n") };
        if (text.startsWith ("A8Manager Hardware Test Output\n\n")
            && text.contains ("test-manifest.json is the exact-frame analysis record; it is not an A8 configuration file.")
            && text.contains ("\nROUTING AND SAFETY\n")
            && text.contains ("\nREFERENCE MARKERS\n") && text.contains ("\nFILES AND EXPECTED VALUES\n")) return Kind::testInstructions;
        // Existing packages already carry these stable exporter signatures, so
        // no new marker or rewrite of the user's exported folders is needed.
        return text.startsWith ("A8Manager Waveform Design\n\n") &&
            text.contains ("design.json is the editable design recipe. This README and recipe are not hardware configuration files.") &&
            text.contains ("\nTRIGGERING AND PLAYBACK\n") && text.contains ("\nFILES AND TIMING\n") &&
            text.contains ("\nCV SAFETY AND CALIBRATION\n") ? Kind::instructions : Kind::unknown;
    }
}
