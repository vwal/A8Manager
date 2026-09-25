#pragma once
#include <JuceHeader.h>
#include <cmath>

// Display-only CV guidance. Never write a derived access value into MinVoltage.
namespace ZoneVoltageDisplay
{
    inline juce::String accessValue (double lower, double upper)
    {
        if (! std::isfinite (lower) || ! std::isfinite (upper) || lower < -5.0 || upper > 5.0 || lower >= upper)
            return {};

        const auto midpoint { lower + (upper - lower) * 0.5 };
        for (auto decimals { 2 }; decimals <= 6; ++decimals)
        {
            const auto text { juce::String (midpoint, decimals) };
            const auto displayed { text.getDoubleValue () };
            // Extra precision preserves half-steps and avoids a boundary "hit".
            // Balanced thirds/sevenths repeat forever: allow tiny rounding
            // error (at most 0.05% of the range) rather than hiding their target.
            if (displayed > lower && displayed < upper && std::abs (displayed - midpoint) <= (upper - lower) * 0.0005)
                return (displayed >= 0.0 ? "+" : "") + (displayed == 0.0 ? juce::String (0.0, decimals) : text);
        }
        return {}; // No trustworthy target at the available display precision.
    }

    inline juce::String tabName (int zoneNumber, bool populated, double lower, double upper)
    {
        auto name { juce::String (zoneNumber) };
        if (populated)
        {
            const auto access { accessValue (lower, upper) };
            name += "\r" + juce::String (lower >= 0.0 ? "+" : "") + juce::String (lower, 2);
            name += "\r(" + (access.isNotEmpty () ? access : juce::String ("--")) + ")";
        }
        return name;
    }

    inline juce::String tooltip (int zoneNumber, bool populated, double lower, double upper)
    {
        auto text { "Zone " + juce::String (zoneNumber) };
        if (! populated)
            return text + " - no sample assigned";
        text += "\nLower boundary: " + juce::String (lower, 6) + " V; upper boundary: " + juce::String (upper, 6) + " V.";
        const auto access { accessValue (lower, upper) };
        if (access.isEmpty ())
            return text + "\nNo reliable access value: check for invalid, zero-width or extremely narrow voltage ranges.";
        return text + "\nAccess value: " + access + " V (midpoint). Send this CV to select the zone."
                      "\nParentheses show a read-only suggestion, not a saved boundary or a gate/trigger.";
    }
}
