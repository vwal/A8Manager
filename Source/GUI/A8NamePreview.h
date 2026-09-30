#pragma once

#include <JuceHeader.h>

// Hardware-confirmed filename display budgets; actual filenames stay unchanged.
namespace A8NamePreview
{
    struct Preview { juce::String select, channel; };

    inline Preview fromFilename (const juce::String& filename)
    {
        const auto basename { filename.endsWithIgnoreCase (".wav") ? filename.dropLastCharacters (4) : filename };
        return { basename.substring (0, 6), basename.length () > 10
            ? basename.substring (0, 10) + "..." + basename.getLastCharacters (2) : basename };
    }

    // A name-editing aid, not the complete physical filename display. Generated
    // filenames always exceed the Channels width once their ID/voice suffix is
    // added. Keep the editable prefix separate from that automatic identifier.
    inline Preview fromGeneratedPrefix (const juce::String& prefix, int voiceNumber)
    {
        if (prefix.isEmpty () || voiceNumber < 1 || voiceNumber > 8) return {};
        return { prefix.substring (0, 6), prefix.substring (0, 10) + "..." + juce::String (voiceNumber).paddedLeft ('0', 2) };
    }

    inline juce::String generatedAdvice ()
    {
        return "These previews show the user-controlled name portion, plus the automatic voice number in A8 Channels. "
               "They omit the automatic separator and unique identifier. The actual hardware display can reveal part of that suffix for short names.";
    }

    inline juce::String advice ()
    {
        return "Put identifying information in the first 6-10 characters. A8 Select shows the first 6; A8 Channels shows the first 10 characters of longer names, followed by ... and the final 2.";
    }
}
