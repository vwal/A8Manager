#pragma once
#include "SampleManager/SampleProperties.h"
#include "../../../Assimil8or/Preset/ZoneProperties.h"
#include "../../../Assimil8or/Preset/ZoneSampleRanges.h"
#include "../../../Assimil8or/Audio/PlaybackPitch.h"
#include <cmath>

namespace WaveformDuration
{
    // Match the hardware's channel + zone pitch-adjusted duration, not audition speed.
    // Markers are end-exclusive; an implicit loop follows the selected sample.
    inline std::optional<double> selected (ZoneProperties& zone, SampleProperties& sample, int region, double channelPitch = 0.0,
                                           bool allowOutsideSample = false)
    {
        if (region < 0 || region > 2 || sample.getStatus () != SampleStatus::exists || zone.getSample ().isEmpty ())
            return std::nullopt;
        const auto frames { static_cast<double> (sample.getLengthInSamples ()) };
        const auto rate { sample.getSampleRate () };
        const auto pitch { zone.getPitchOffset () };
        if (frames <= 0 || ! std::isfinite (rate) || rate <= 0 || ! std::isfinite (pitch) || ! std::isfinite (channelPitch))
            return std::nullopt;
        double start { 0.0 }, end { frames };
        const auto range { ZoneSampleRanges::resolve (ZoneSampleRanges::read (zone), sample.getLengthInSamples (), allowOutsideSample) };
        if (region == 1)
        {
            start = static_cast<double> (range.sampleStart);
            end = static_cast<double> (range.sampleEnd);
        }
        else if (region == 2)
        {
            if (! range.loopValid) return std::nullopt;
            start = static_cast<double> (range.loopStart);
            end = range.loopEnd ();
        }
        if (! std::isfinite (end) || start < 0 || end > frames || start >= end)
            return std::nullopt;
        const auto seconds { (end - start) / rate / PlaybackPitch::rateRatio (channelPitch, pitch, rate) };
        return std::isfinite (seconds) && seconds > 0 ? std::optional<double> (seconds) : std::nullopt;
    }
}
