#pragma once

#include "../../../../Assimil8or/Preset/ZoneProperties.h"
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
    };

    inline std::optional<Region> capture (ZoneProperties& zone, Target target, juce::int64 fileLength)
    {
        if (! zone.isValid () || fileLength <= 0)
            return {};
        const auto start { target == Target::sample ? zone.getSampleStart ().value_or (0) : zone.getLoopStart ().value_or (0) };
        if (start < 0 || start >= fileLength)
            return {};
        const auto length { target == Target::sample
            ? static_cast<double> (zone.getSampleEnd ().value_or (fileLength)) - static_cast<double> (start)
            : zone.getLoopLength ().value_or (static_cast<double> (fileLength - start)) };
        if (! std::isfinite (length) || length < (target == Target::sample ? 1.0 : 4.0) || length > fileLength - start)
            return {};
        return Region { target, start, length, fileLength };
    }

    inline void apply (ZoneProperties& zone, const Region& region, double sampleDelta)
    {
        if (! zone.isValid () || ! std::isfinite (sampleDelta))
            return;
        // Loop lengths can be fractional. Floor the latest integer start so the
        // fractional end never exceeds the file, and retain the exact length.
        const auto latestStart { std::floor (static_cast<double> (region.fileLength) - region.length) };
        const auto start { static_cast<juce::int64> (std::clamp (
            static_cast<double> (region.start) + std::round (sampleDelta), 0.0, latestStart)) };
        if (region.target == Target::sample)
        {
            if (start == zone.getSampleStart ().value_or (0))
                return;
            const auto end { start + static_cast<juce::int64> (region.length) };
            auto setStart = [&] () { zone.setSampleStart (start == 0 ? -1 : start, true); };
            auto setEnd = [&] () { zone.setSampleEnd (end == region.fileLength ? -1 : end, true); };
            // Synchronous observers must never see inverted start/end points.
            if (start > zone.getSampleStart ().value_or (0)) { setEnd (); setStart (); }
            else { setStart (); setEnd (); }
        }
        else
        {
            if (start == zone.getLoopStart ().value_or (0))
                return;
            // An implicit length follows EOF. Make it explicit before moving
            // its start, otherwise the move would silently resize the loop.
            zone.setLoopLength (region.length, true);
            zone.setLoopStart (start == 0 ? -1 : start, true);
        }
    }
}
