#pragma once

#include "../Preset/ZoneSampleRanges.h"
#include <optional>

// This is a forward sample-into-loop preview, not an emulation of every A8
// gate/loop-mode combination. Hardware requires the entire loop to be inside
// the selected sample; invalid legacy ranges are not silently repaired here.
namespace SampleLoopSimulation
{
    struct Range
    {
        juce::int64 sampleStart, sampleEnd, loopStart;
        double loopEnd;
    };

    inline std::optional<Range> resolve (ZoneProperties& zone, juce::int64 frames)
    {
        if (! zone.isValid ()) return {};
        const auto range { ZoneSampleRanges::resolve (ZoneSampleRanges::read (zone), frames) };
        if (! range.loopValid) return {};
        return Range { range.sampleStart, range.sampleEnd, range.loopStart, range.loopEnd () };
    }
}
