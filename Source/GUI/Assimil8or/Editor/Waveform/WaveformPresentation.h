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

    // Find the first crossing of the opposite boundary's amplitude, not the
    // globally smallest amplitude error. Choose the closer of its two PCM
    // frames; never skip a nearby join to chase a more exact distant match.
    // Small batches let the UI yield between large-file scans without a time cap.
    // The caller must keep the buffer alive and unchanged between advance calls.
    class BoundaryMatchSearch
    {
    public:
        BoundaryMatchSearch (const juce::AudioBuffer<float>& audio, int side,
                             double moving, double opposite, juce::int64 minimum,
                             juce::int64 maximum, double sampleRate, bool endBoundary, bool right)
        {
            const auto count { audio.getNumSamples () };
            if (side < 0 || side >= audio.getNumChannels () || count == 0 ||
                ! std::isfinite (moving) || ! std::isfinite (opposite) ||
                ! std::isfinite (sampleRate) || sampleRate <= 0.0) return;
            // END is exclusive, including fractional ends: use its last audible
            // frame, exactly as the join preview does.
            offset = endBoundary ? 1 : 0;
            if (moving < offset || moving > count - 1 + offset ||
                opposite < 1 - offset || opposite > count - offset) return;
            const auto frame { static_cast<juce::int64> (std::floor (moving)) - offset };
            const auto targetFrame { static_cast<juce::int64> (std::floor (opposite)) - (1 - offset) };
            data = audio.getReadPointer (side);
            target = data[targetFrame];
            if (! std::isfinite (target)) return;
            originalError = std::isfinite (data[frame]) ? std::abs (static_cast<double> (data[frame]) - target)
                                                       : std::numeric_limits<double>::infinity ();
            if (originalError == 0.0) return;
            minimum = std::max (minimum, static_cast<juce::int64> (offset));
            maximum = std::min (maximum, static_cast<juce::int64> (count - 1 + offset));
            legalMinimum = minimum;
            legalMaximum = maximum;
            movingPosition = moving;
            sampleCount = count;
            if (right) minimum = std::max (minimum, static_cast<juce::int64> (std::floor (moving)) + 1);
            else maximum = std::min (maximum, static_cast<juce::int64> (std::ceil (moving)) - 1);
            position = right ? minimum : maximum;
            limit = right ? maximum : minimum;
            step = right ? 1 : -1;
            complete = minimum > maximum;
        }

        bool advance (int budget)
        {
            while (! complete && budget-- > 0)
            {
                const auto value { static_cast<double> (data[position - offset]) };
                if (std::isfinite (value))
                {
                    const auto error { std::abs (value - target) };
                    if (error == 0.0)
                    {
                        best = position;
                        complete = true;
                        return true;
                    }
                    const auto inward { position - step };
                    if (inward >= offset && inward - offset < sampleCount)
                    {
                        const auto neighbour { static_cast<double> (data[inward - offset]) };
                        if (std::isfinite (neighbour) &&
                            ((value < target && neighbour > target) || (value > target && neighbour < target)))
                        {
                            const auto crossing { inward + (target - neighbour) / (value - neighbour) * step };
                            // The first pair may straddle the current fractional
                            // marker or a legal limit. The crossing itself must be
                            // on the requested side and inside the valid range.
                            if (crossing >= legalMinimum && crossing <= legalMaximum &&
                                (step > 0 ? crossing > movingPosition : crossing < movingPosition))
                            {
                                const auto neighbourError { std::abs (neighbour - target) };
                                const auto candidate { error < neighbourError ? position :
                                    error > neighbourError ? inward :
                                    std::abs (position - movingPosition) < std::abs (inward - movingPosition) ? position : inward };
                                // If the better frame is the current frame (or
                                // outside the legal/directional range), leave the
                                // marker alone. Do not hunt a farther crossing.
                                if (candidate >= legalMinimum && candidate <= legalMaximum &&
                                    (step > 0 ? candidate > movingPosition : candidate < movingPosition) &&
                                    std::min (error, neighbourError) < originalError)
                                    best = candidate;
                                complete = true;
                                return true;
                            }
                        }
                    }
                }
                complete = position == limit;
                position += step;
            }
            return complete;
        }
        std::optional<juce::int64> result () const { return complete ? best : std::nullopt; }

    private:
        const float* data { nullptr };
        double target { 0.0 }, originalError { 0.0 }, movingPosition { 0.0 };
        juce::int64 position { 0 }, limit { 0 }, legalMinimum { 0 }, legalMaximum { 0 };
        int offset { 0 }, step { 1 }, sampleCount { 0 };
        bool complete { true };
        std::optional<juce::int64> best;
    };

    inline std::optional<juce::int64> matchBoundary (const juce::AudioBuffer<float>& audio, int side,
                                                    double moving, double opposite, juce::int64 minimum,
                                                    juce::int64 maximum, double sampleRate, bool endBoundary, bool right)
    {
        BoundaryMatchSearch search (audio, side, moving, opposite, minimum, maximum, sampleRate, endBoundary, right);
        while (! search.advance (65536)) {}
        return search.result ();
    }
}
