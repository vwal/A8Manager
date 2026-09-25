#pragma once
#include "ZoneProperties.h"

namespace ZoneContinuation
{
    // Prepare a detached copy; the caller handles confirmation and voltage ranges.
    inline juce::ValueTree makeNext (juce::ValueTree source, juce::int64 sampleLength, bool continueSlice)
    {
        if (! source.isValid ()) return {};
        ZoneProperties original (source, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        if (original.getSample ().isEmpty ()) return {};
        auto result { source.createCopy () };
        if (! continueSlice) return result;
        const auto start { original.getSampleStart ().value_or (0) };
        const auto end { original.getSampleEnd ().value_or (sampleLength) };
        if (start < 0 || end <= start || end > sampleLength || sampleLength - end < 4) return {};
        const auto nextLength { std::min (sampleLength - end, std::max (juce::int64 { 4 }, end - start)) };
        ZoneProperties next (result, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        next.setSampleStart (end, false);
        next.setSampleEnd (end + nextLength, false);
        next.setLoopStart (end, false);
        next.setLoopLength (static_cast<double> (nextLength), false);
        return result;
    }
}
