#pragma once

#include "../Preset/ZoneProperties.h"
#include <cmath>
#include <optional>

// This is a forward sample-into-loop preview, not an emulation of every A8
// gate/loop-mode combination. A loop ending before playback starts is excluded.
namespace SampleLoopSimulation
{
    struct Range
    {
        juce::int64 sampleStart, sampleEnd, loopStart;
        double loopEnd;
    };

    inline std::optional<Range> resolve (ZoneProperties& zone, juce::int64 frames)
    {
        if (! zone.isValid () || frames <= 0) return {};
        const auto start { zone.getSampleStart ().value_or (0) };
        const auto end { zone.getSampleEnd ().value_or (frames) };
        const auto loopStart { zone.getLoopStart ().value_or (0) };
        // Validate before subtraction: value_or eagerly evaluates its default,
        // even when a corrupt preset also supplies an explicit loop length.
        if (start < 0 || end <= start || end > frames || loopStart < 0 || loopStart >= frames)
            return {};
        const auto loopLength { zone.getLoopLength ().value_or (static_cast<double> (frames - loopStart)) };
        const auto loopEnd { static_cast<double> (loopStart) + loopLength };
        if (! std::isfinite (loopLength) || loopLength <= 0.0 || ! std::isfinite (loopEnd) || loopEnd > frames || loopEnd <= start)
            return {};
        return Range { start, end, loopStart, loopEnd };
    }
}
