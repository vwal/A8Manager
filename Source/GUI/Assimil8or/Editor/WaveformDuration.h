#pragma once
#include "SampleManager/SampleProperties.h"
#include "../../../Assimil8or/Preset/ZoneProperties.h"
#include <cmath>

namespace WaveformDuration
{
    // Match the zone's audible, pitch-adjusted duration, not audition speed.
    // Markers are end-exclusive; unassigned ends extend to the file boundary.
    inline std::optional<double> selected (ZoneProperties& zone, SampleProperties& sample, int region)
    {
        if (region < 0 || region > 2 || sample.getStatus () != SampleStatus::exists || zone.getSample ().isEmpty ())
            return std::nullopt;
        const auto frames { static_cast<double> (sample.getLengthInSamples ()) };
        const auto rate { sample.getSampleRate () };
        const auto pitch { zone.getPitchOffset () };
        if (frames <= 0 || ! std::isfinite (rate) || rate <= 0 || ! std::isfinite (pitch))
            return std::nullopt;
        double start { 0.0 }, end { frames };
        if (region == 1)
        {
            start = static_cast<double> (zone.getSampleStart ().value_or (0));
            end = static_cast<double> (zone.getSampleEnd ().value_or (sample.getLengthInSamples ()));
        }
        else if (region == 2)
        {
            start = static_cast<double> (zone.getLoopStart ().value_or (0));
            end = start + zone.getLoopLength ().value_or (frames - start);
        }
        if (! std::isfinite (end) || start < 0 || end > frames || start >= end)
            return std::nullopt;
        const auto seconds { (end - start) / rate / std::pow (2.0, pitch / 12.0) };
        return std::isfinite (seconds) && seconds > 0 ? std::optional<double> (seconds) : std::nullopt;
    }
}
