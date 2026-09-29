#pragma once

#include "WaveformDesign.h"
#include "CvSampleSafety.h"
#include <cstring>

// These are desktop design artifacts, not sample/preset files for the module.
// Recognize their actual format, never all files with a JSON/TXT extension.
namespace WaveformDesignSidecars
{
    enum class Kind { unknown, recipe, instructions };

    inline Kind identify (const juce::File& file)
    {
        const auto filename { file.getFileName () };
        const auto recipe { filename.equalsIgnoreCase ("design.json") || filename.endsWithIgnoreCase (".design.json") };
        const auto instructions { filename.equalsIgnoreCase ("README.txt") };
        if ((! recipe && ! instructions) || ! file.existsAsFile ()) return Kind::unknown;

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
        if (recipe)
        {
            if (! CvSampleSafety::detail::boundedJsonObject (data, length)) return Kind::unknown;
            WaveformDesign::Settings settings;
            return WaveformDesign::fromJson (juce::JSON::parse (juce::String::fromUTF8 (data, length)), settings).wasOk ()
                ? Kind::recipe : Kind::unknown;
        }

        const auto text { juce::String::fromUTF8 (data, length).replace ("\r\n", "\n") };
        // Existing packages already carry these stable exporter signatures, so
        // no new marker or rewrite of the user's exported folders is needed.
        return text.startsWith ("A8Manager Waveform Design\n\n") &&
            text.contains ("design.json is the editable design recipe. This README and recipe are not hardware configuration files.") &&
            text.contains ("\nTRIGGERING AND PLAYBACK\n") && text.contains ("\nFILES AND TIMING\n") &&
            text.contains ("\nCV SAFETY AND CALIBRATION\n") ? Kind::instructions : Kind::unknown;
    }
}
