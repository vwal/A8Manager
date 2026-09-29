#pragma once

#include <algorithm>
#include <cmath>

// Static preset pitch only: physical CV/FM modulation is not simulated. The
// manual's +84/+72/+60/+48 st at 24/48/96/192 kHz describe the same maximum
// source-frame traversal rate, not the computer audio device's sample rate.
namespace PlaybackPitch
{
    constexpr double maximumSourceRate { 3072000.0 };
    constexpr double maximumRatio { 1024.0 }; // Two independent +60 st controls.

    inline double parameterSemitones (double semitones) noexcept
    {
        return std::isfinite (semitones) ? std::clamp (semitones, -96.0, 60.0) : 0.0;
    }

    inline double maximumSemitones (double sourceRate) noexcept
    {
        if (! std::isfinite (sourceRate) || sourceRate <= 0.0) return 0.0;
        return std::min (120.0, 12.0 * (std::log2 (maximumSourceRate) - std::log2 (sourceRate)));
    }

    inline double effectiveSemitones (double channelPitch, double zoneOffset, double sourceRate) noexcept
    {
        if (! std::isfinite (sourceRate) || sourceRate <= 0.0) return 0.0;
        // Each field reaches -96 st; the manual defines zone pitch relative
        // to channel pitch and specifies no additional combined lower limit.
        return std::min (parameterSemitones (channelPitch) + parameterSemitones (zoneOffset), maximumSemitones (sourceRate));
    }

    inline double rateRatio (double channelPitch, double zoneOffset, double sourceRate) noexcept
    {
        return std::exp2 (effectiveSemitones (channelPitch, zoneOffset, sourceRate) / 12.0);
    }
}
