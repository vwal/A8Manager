#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <limits>

namespace WaveformPresentation
{
    // Shared by the waveform, its handles/labels, and the zone parameter keys.
    inline const std::array<juce::Colour, 4> markerColours {
        juce::Colour (0xffff8585), juce::Colour (0xff8fcaff),
        juce::Colour (0xffffc879), juce::Colour (0xfff39db5) };
    inline const std::array<juce::String, 4> markerNames { "Sample Start", "Sample End", "Loop Start", "Loop End" };

    inline juce::String samples (double value)
    {
        if (! std::isfinite (value) || std::abs (value) > 9.0e15) return "--";
        auto text { juce::String (value, 3).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") };
        const auto dot { text.indexOfChar ('.') };
        for (auto i { (dot < 0 ? text.length () : dot) - 3 }; i > (text.startsWithChar ('-') ? 1 : 0); i -= 3)
            text = text.substring (0, i) + "," + text.substring (i);
        return text;
    }
    inline juce::String time (double seconds)
    {
        if (! std::isfinite (seconds) || seconds < 0.0 || seconds > 1.0e10) return "--:--";
        const auto precision { seconds > 0.0 && seconds < 0.01 ? 6 : 3 };
        const auto factor { std::pow (10.0, precision) };
        const auto rounded { std::round (seconds * factor) / factor };
        const auto minutes { static_cast<juce::int64> (rounded / 60.0) };
        return juce::String (minutes) + ":" + juce::String (rounded - static_cast<double> (minutes) * 60.0, precision).paddedLeft ('0', precision + 3);
    }
    inline juce::String duration (double frames, double rate, double pitchOffset = 0.0)
    {
        if (! std::isfinite (rate) || rate <= 0.0 || ! std::isfinite (pitchOffset)) return "--:--";
        return time (frames / rate / std::pow (2.0, pitchOffset / 12.0));
    }
    // Nearest crossing strictly in the requested direction, within legal bounds.
    inline std::optional<juce::int64> zeroCrossing (const juce::AudioBuffer<float>& audio, int side,
                                                   double start, juce::int64 minimum, juce::int64 maximum, bool right, bool endBoundary = false)
    {
        if (side < 0 || side >= audio.getNumChannels () || ! std::isfinite (start) || audio.getNumSamples () == 0) return {};
        // End markers are exclusive: their last audible frame is marker - 1.
        // Snap that frame to zero, not the first frame outside the region.
        const auto offset { endBoundary ? 1 : 0 };
        start -= offset;
        minimum -= offset;
        maximum -= offset;
        minimum = std::max (juce::int64 { 0 }, minimum);
        maximum = std::min (static_cast<juce::int64> (audio.getNumSamples () - 1), maximum);
        if (minimum > maximum || start < -1.0 || start > audio.getNumSamples ()) return {};
        auto position { static_cast<juce::int64> (right ? std::floor (start) + 1.0 : std::ceil (start) - 1.0) };
        position = right ? std::max (minimum, position) : std::min (maximum, position);
        const auto* data { audio.getReadPointer (side) };
        for (; position >= minimum && position <= maximum; position += right ? 1 : -1)
        {
            const auto value { data[position] };
            if (! std::isfinite (value)) continue;
            auto changesSign = [value] (float neighbour)
            {
                return std::isfinite (neighbour) && ((value < 0.0f && neighbour > 0.0f) || (value > 0.0f && neighbour < 0.0f));
            };
            // A crossing normally falls between frames. Choose the quieter of
            // its two endpoints, rather than always the frame after the sign
            // change. Ties consistently choose that latter frame.
            if (value == 0.0f ||
                (position > 0 && changesSign (data[position - 1]) && std::abs (value) <= std::abs (data[position - 1])) ||
                (position + 1 < audio.getNumSamples () && changesSign (data[position + 1]) && std::abs (value) < std::abs (data[position + 1])))
                return position + offset;
        }
        return {};
    }

    // Match the audible endpoint's amplitude, not zero. Search only in the
    // requested direction, within 50 ms, so an adjustment cannot reshape a whole slice.
    // The opposite endpoint stays fixed; END addresses the frame before its
    // exclusive boundary, just as the join preview does, including fractional ends.
    inline std::optional<juce::int64> matchBoundary (const juce::AudioBuffer<float>& audio, int side,
                                                    double moving, double opposite, juce::int64 minimum,
                                                    juce::int64 maximum, double sampleRate, bool endBoundary, bool right)
    {
        const auto count { audio.getNumSamples () };
        if (side < 0 || side >= audio.getNumChannels () || count == 0 ||
            ! std::isfinite (moving) || ! std::isfinite (opposite) ||
            ! std::isfinite (sampleRate) || sampleRate <= 0.0) return {};
        const auto offset { endBoundary ? 1 : 0 };
        if (moving < offset || moving > count - 1 + offset ||
            opposite < 1 - offset || opposite > count - offset) return {};
        const auto frame { static_cast<juce::int64> (std::floor (moving)) - offset };
        const auto targetFrame { static_cast<juce::int64> (std::floor (opposite)) - (1 - offset) };
        const auto* data { audio.getReadPointer (side) };
        const auto target { static_cast<double> (data[targetFrame]) };
        if (! std::isfinite (target)) return {};
        const auto radius { std::max (1.0, std::min (static_cast<double> (count), std::ceil (sampleRate * 0.05))) };
        minimum = std::max ({ minimum, static_cast<juce::int64> (offset), static_cast<juce::int64> (std::ceil (moving - radius)) });
        maximum = std::min ({ maximum, static_cast<juce::int64> (count - 1 + offset), static_cast<juce::int64> (std::floor (moving + radius)) });
        // Compare marker coordinates, including fractional markers, rather than
        // their audible frames. Never cross to the unrequested side to find a match.
        if (right) minimum = std::max (minimum, static_cast<juce::int64> (std::floor (moving)) + 1);
        else maximum = std::min (maximum, static_cast<juce::int64> (std::ceil (moving)) - 1);
        const auto originalError { std::isfinite (data[frame]) ? std::abs (static_cast<double> (data[frame]) - target)
                                                            : std::numeric_limits<double>::infinity () };
        auto bestError { originalError };
        auto bestDistance { std::numeric_limits<double>::infinity () };
        std::optional<juce::int64> best;
        for (auto position { minimum }; position <= maximum; ++position)
        {
            const auto value { static_cast<double> (data[position - offset]) };
            if (! std::isfinite (value)) continue;
            const auto error { std::abs (value - target) };
            const auto distance { std::abs (static_cast<double> (position) - moving) };
            // Do not move an already matched join, or make a move with no
            // amplitude improvement. Equal improvements favour the nearest frame.
            if (error < originalError && error <= bestError && (error < bestError || distance < bestDistance))
            {
                best = position;
                bestError = error;
                bestDistance = distance;
            }
        }
        return best;
    }
}
