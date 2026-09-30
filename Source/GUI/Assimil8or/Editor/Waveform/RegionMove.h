#pragma once

#include "../../../../Assimil8or/Preset/ZoneSampleRanges.h"
#include <algorithm>
#include <cmath>

// A drag translates the region captured at mouse-down, never its already-moved
// result. This preserves length and lets the pointer return from either limit.
namespace RegionMove
{
    enum class Target { sample, loop };

    struct Region
    {
        Target target;
        juce::int64 start;
        double length;
        juce::int64 fileLength;
        ZoneSampleRanges::Stored original;
        bool allowOutsideSample { false };
    };

    inline std::optional<Region> capture (ZoneProperties& zone, Target target, juce::int64 fileLength, bool allowOutsideSample = false)
    {
        if (! zone.isValid () || fileLength < 4)
            return {};
        const auto stored { ZoneSampleRanges::read (zone) };
        const auto ranges { ZoneSampleRanges::resolve (stored, fileLength, allowOutsideSample) };
        if (! ranges.sampleValid || ! ranges.loopValid) return {};
        const auto start { target == Target::sample ? ranges.sampleStart : ranges.loopStart };
        const auto length { target == Target::sample ? static_cast<double> (ranges.sampleEnd - ranges.sampleStart) : ranges.loopLength };
        return Region { target, start, length, fileLength, stored, allowOutsideSample };
    }

    inline void apply (ZoneProperties& zone, const Region& region, double sampleDelta)
    {
        if (! zone.isValid () || ! std::isfinite (sampleDelta))
            return;
        // Both coordinates and implicit/explicit semantics come from mouse-
        // down. Reversing after a boundary clamp restores the captured region,
        // including a loop pushed by moving the sample, without cumulative drift.
        const auto moved { ZoneSampleRanges::move (region.original, region.fileLength, region.target == Target::loop, sampleDelta, region.allowOutsideSample) };
        ZoneSampleRanges::apply (zone, moved, region.fileLength, true, region.allowOutsideSample);
    }
}
