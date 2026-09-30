#pragma once

#include "ZoneProperties.h"

// Hardware gets concrete loop points when an automatic loop follows a custom
// sample range. A YAML comment preserves the editor's unset/default intent.
// Restore that intent only if the saved sample and all four points still match;
// editing the preset on the module or elsewhere therefore takes precedence.
namespace AutomaticLoopDefaults
{
    inline const juce::String prefix { "# A8Manager loop defaults v1 " };

    inline juce::String signature (ZoneProperties& zone)
    {
        juce::Array<juce::var> values;
        values.add (zone.getSample ());
        values.add (juce::String (zone.getSampleStart ().value_or (-1)));
        values.add (juce::String (zone.getSampleEnd ().value_or (-1)));
        values.add (juce::String (zone.getLoopStart ().value_or (-1)));
        values.add (juce::String (zone.getLoopLength ().value_or (-1.0)));
        return juce::JSON::toString (juce::var (values), true);
    }

    inline juce::String comment (ZoneProperties& original, ZoneProperties& written)
    {
        const auto mask { (original.getLoopStart () ? 0 : 1) | (original.getLoopLength () ? 0 : 2) };
        if (mask == 0 || signature (original) == signature (written)) return {};
        return prefix + juce::String (mask) + " " + signature (written);
    }

    inline void restore (ZoneProperties& zone, const juce::String& line)
    {
        if (! line.startsWith (prefix) || line.length () > 4096) return;
        const auto data { line.substring (prefix.length ()) };
        if (data.length () < 3 || data[0] < '1' || data[0] > '3' || data[1] != ' '
            || data.substring (2) != signature (zone)) return;
        const auto mask { static_cast<int> (data[0] - '0') };
        if ((mask & 1) != 0) zone.setLoopStart (-1, false);
        if ((mask & 2) != 0) zone.setLoopLength (-1.0, false);
    }
}
