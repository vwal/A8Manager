#pragma once

#include "ZoneProperties.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

// Source-frame coordinates, with exclusive sample/loop ends. Reading never
// repairs a legacy preset: callers choose whether loops must be contained in
// SAMPLE or may use the whole physical file. Repairs/edits are explicit.
namespace ZoneSampleRanges
{
    using Frame = juce::int64;
    constexpr double minimumLoopLength { 4.0 };
    enum class Marker { sampleStart, sampleEnd, loopStart, loopEnd };
    enum class Mode { length, end };

    struct Stored
    {
        std::optional<Frame> sampleStart, sampleEnd, loopStart;
        std::optional<double> loopLength;
    };

    struct Resolved
    {
        Frame fileLength {}, sampleStart {}, sampleEnd {}, loopStart {};
        double loopLength {};
        bool sampleValid {}, loopValid {}, implicitLoop {};
        double loopEnd () const { return static_cast<double> (loopStart) + loopLength; }
    };

    struct Limits { double minimum {}, maximum {}; bool editable {}; };

    inline Stored read (ZoneProperties& zone)
    {
        return { zone.getSampleStart (), zone.getSampleEnd (), zone.getLoopStart (), zone.getLoopLength () };
    }

    inline Resolved resolve (const Stored& stored, Frame fileLength, bool allowOutsideSample = false)
    {
        Resolved result;
        result.fileLength = std::max<Frame> (0, fileLength);
        result.sampleStart = stored.sampleStart.value_or (0);
        result.sampleEnd = stored.sampleEnd.value_or (result.fileLength);
        result.loopStart = stored.loopStart.value_or (result.sampleStart);
        const auto defaultLoopEnd { allowOutsideSample && stored.loopStart && result.loopStart >= result.sampleEnd
            ? result.fileLength : result.sampleEnd };
        result.loopLength = stored.loopLength.value_or (static_cast<double> (defaultLoopEnd) - result.loopStart);
        result.implicitLoop = ! stored.loopStart && ! stored.loopLength;
        result.sampleValid = result.sampleStart >= 0 && result.sampleEnd <= result.fileLength
            && static_cast<double> (result.sampleEnd) - result.sampleStart >= minimumLoopLength;
        result.loopValid = (allowOutsideSample ? result.fileLength >= 4 : result.sampleValid)
            && std::isfinite (result.loopLength) && result.loopLength >= minimumLoopLength
            && result.loopStart >= (allowOutsideSample ? 0 : result.sampleStart)
            && result.loopEnd () <= (allowOutsideSample ? result.fileLength : result.sampleEnd);
        return result;
    }

    namespace Detail
    {
        inline void sample (Stored& stored, Frame start, Frame end, Frame fileLength)
        {
            stored.sampleStart = start == 0 ? std::optional<Frame> {} : start;
            stored.sampleEnd = end == fileLength ? std::optional<Frame> {} : end;
        }

        inline void loop (Stored& stored, Frame start, double length)
        {
            // Zero and a full-sample length are intentional values here, not
            // the unset sentinel: touching either edge creates an explicit loop.
            stored.loopStart = start;
            stored.loopLength = length;
        }

        inline void retainLoop (Stored& stored, Frame fileLength, Frame start, double length, bool allowOutsideSample = false)
        {
            if (! stored.loopStart && ! stored.loopLength) return;
            const auto current { resolve (stored, fileLength, allowOutsideSample) };
            if (current.loopStart != start || current.loopLength != length) loop (stored, start, length);
        }

        inline Frame integer (double value, Frame minimum, Frame maximum)
        {
            return static_cast<Frame> (std::clamp (std::round (value), static_cast<double> (minimum), static_cast<double> (maximum)));
        }
    }

    inline Stored repair (const Stored& original, Frame fileLength, Mode, bool allowOutsideSample = false)
    {
        if (fileLength < 4)
        {
            auto result { original };
            Detail::sample (result, 0, std::max<Frame> (0, fileLength), std::max<Frame> (0, fileLength));
            result.loopStart.reset ();
            result.loopLength.reset ();
            return result;
        }
        const auto before { resolve (original, fileLength, allowOutsideSample) };
        if (before.sampleValid && before.loopValid) return original;
        auto result { original };
        const auto start { std::clamp<Frame> (before.sampleStart, 0, fileLength - 4) };
        const auto end { std::clamp<Frame> (before.sampleEnd, start + 4, fileLength) };
        Detail::sample (result, start, end, fileLength);
        if (allowOutsideSample)
        {
            // Sample repair must not alter a deliberate, file-bounded loop.
            Detail::retainLoop (result, fileLength, before.loopStart, before.loopLength, true);
            if (resolve (result, fileLength, true).loopValid) return result;
        }
        // Invalid saved loops are not reinterpreted as a nearby explicit
        // loop. Forget them so their replacement follows the selected sample.
        result.loopStart.reset ();
        result.loopLength.reset ();
        return result;
    }

    inline Limits limits (const Stored& stored, Frame fileLength, Marker marker, Mode mode, bool keepOpposite = false,
                          bool allowOutsideSample = false)
    {
        if (fileLength < 4) return {};
        const auto state { resolve (repair (stored, fileLength, mode, allowOutsideSample), fileLength, allowOutsideSample) };
        const auto sampleMinimum { ! allowOutsideSample && ! state.implicitLoop && mode == Mode::length
            ? static_cast<Frame> (std::ceil (state.loopLength)) : Frame { 4 } };
        switch (marker)
        {
            case Marker::sampleStart:
                return { 0.0, static_cast<double> (! allowOutsideSample && ! state.implicitLoop && mode == Mode::end
                    ? std::min<Frame> (state.sampleEnd - 4, static_cast<Frame> (std::floor (state.loopEnd () - 4.0)))
                    : state.sampleEnd - sampleMinimum), true };
            case Marker::sampleEnd:
                return { static_cast<double> (! allowOutsideSample && ! state.implicitLoop && mode == Mode::end
                    ? std::max<Frame> (state.sampleStart + 4, state.loopStart + 4) : state.sampleStart + sampleMinimum),
                    static_cast<double> (fileLength), true };
            case Marker::loopStart:
                return { static_cast<double> (allowOutsideSample ? 0 : state.sampleStart), std::floor ((mode == Mode::end || keepOpposite)
                    ? state.loopEnd () - 4.0 : (allowOutsideSample ? fileLength : state.sampleEnd) - state.loopLength), true };
            case Marker::loopEnd:
                return { static_cast<double> (state.loopStart + 4), static_cast<double> (allowOutsideSample ? fileLength : state.sampleEnd), true };
        }
        return {};
    }

    inline Stored edit (const Stored& stored, Frame fileLength, Marker marker, double value, Mode mode, bool keepOpposite = false,
                        bool allowOutsideSample = false)
    {
        if (fileLength < 4 || ! std::isfinite (value)) return stored;
        auto result { repair (stored, fileLength, mode, allowOutsideSample) };
        const auto before { resolve (result, fileLength, allowOutsideSample) };
        const auto bounds { limits (result, fileLength, marker, mode, keepOpposite, allowOutsideSample) };
        value = std::clamp (value, bounds.minimum, bounds.maximum);
        if (marker == Marker::sampleStart || marker == Marker::sampleEnd)
        {
            const auto start { marker == Marker::sampleStart ? Detail::integer (value, 0, fileLength) : before.sampleStart };
            const auto end { marker == Marker::sampleEnd ? Detail::integer (value, 0, fileLength) : before.sampleEnd };
            if (start == before.sampleStart && end == before.sampleEnd) return result;
            Detail::sample (result, start, end, fileLength);
            if (! before.implicitLoop)
            {
                const auto loopStart { allowOutsideSample ? before.loopStart : mode == Mode::length
                    ? std::clamp<Frame> (before.loopStart, start, static_cast<Frame> (std::floor (end - before.loopLength)))
                    : std::max (before.loopStart, start) };
                const auto length { allowOutsideSample || mode == Mode::length ? before.loopLength : std::min (before.loopEnd (), static_cast<double> (end)) - loopStart };
                Detail::retainLoop (result, fileLength, loopStart, length, allowOutsideSample);
            }
        }
        else if (marker == Marker::loopStart)
        {
            const auto start { Detail::integer (value, static_cast<Frame> (bounds.minimum), static_cast<Frame> (bounds.maximum)) };
            Detail::loop (result, start, mode == Mode::end || keepOpposite ? before.loopEnd () - start : before.loopLength);
        }
        else Detail::loop (result, before.loopStart, value - before.loopStart);
        return result;
    }

    inline Stored move (const Stored& stored, Frame fileLength, bool loop, double delta, bool allowOutsideSample = false)
    {
        if (fileLength < 4 || ! std::isfinite (delta)) return stored;
        auto result { repair (stored, fileLength, Mode::length, allowOutsideSample) };
        const auto before { resolve (result, fileLength, allowOutsideSample) };
        if (loop)
        {
            const auto start { Detail::integer (static_cast<double> (before.loopStart) + delta, allowOutsideSample ? 0 : before.sampleStart,
                                                static_cast<Frame> (std::floor ((allowOutsideSample ? fileLength : before.sampleEnd) - before.loopLength))) };
            if (start == before.loopStart) return result;
            Detail::loop (result, start, before.loopLength);
        }
        else
        {
            const auto length { before.sampleEnd - before.sampleStart };
            const auto start { Detail::integer (static_cast<double> (before.sampleStart) + delta, 0, fileLength - length) };
            if (start == before.sampleStart) return result;
            Detail::sample (result, start, start + length, fileLength);
            if (! before.implicitLoop)
            {
                const auto loopStart { allowOutsideSample ? before.loopStart
                    : std::clamp<Frame> (before.loopStart, start, static_cast<Frame> (std::floor (start + length - before.loopLength))) };
                Detail::retainLoop (result, fileLength, loopStart, before.loopLength, allowOutsideSample);
            }
        }
        return result;
    }

    inline void apply (ZoneProperties& zone, const Stored& stored, Frame fileLength, bool callbacks = true, bool allowOutsideSample = false)
    {
        if (! zone.isValid ()) return;
        const auto oldStored { read (zone) };
        const auto before { resolve (oldStored, fileLength, allowOutsideSample) };
        const auto after { resolve (stored, fileLength, allowOutsideSample) };
        if ((! after.sampleValid || ! after.loopValid) && ! (fileLength < 4 && after.implicitLoop && after.sampleStart == 0 && after.sampleEnd == std::max<Frame> (0, fileLength))) return;
        auto setStart = [&] (Frame value) { zone.setSampleStart (value == 0 ? -1 : value, callbacks); };
        auto setEnd = [&] (Frame value) { zone.setSampleEnd (value == fileLength ? -1 : value, callbacks); };
        // Widen first, then move/shrink the loop, then contract the sample.
        // Synchronous listeners retain the selected containment policy when
        // both old and new ranges are valid; file bounds always apply.
        setStart (before.sampleValid ? std::min (before.sampleStart, after.sampleStart) : 0);
        setEnd (before.sampleValid ? std::max (before.sampleEnd, after.sampleEnd) : fileLength);
        if (! after.implicitLoop)
        {
            const auto widened { resolve (read (zone), fileLength, allowOutsideSample) };
            zone.setLoopLength (std::min (after.loopLength, widened.loopValid ? widened.loopLength : minimumLoopLength), callbacks);
            zone.setLoopStart (after.loopStart, callbacks);
            zone.setLoopLength (after.loopLength, callbacks);
        }
        else if (! before.implicitLoop)
        {
            zone.setLoopStart (-1, callbacks);
            zone.setLoopLength (-1, callbacks);
        }
        setStart (after.sampleStart);
        setEnd (after.sampleEnd);
        zone.setSampleStart (stored.sampleStart.value_or (-1), callbacks);
        zone.setSampleEnd (stored.sampleEnd.value_or (-1), callbacks);
        if (! after.implicitLoop)
        {
            zone.setLoopStart (stored.loopStart.value_or (-1), callbacks);
            zone.setLoopLength (stored.loopLength.value_or (-1), callbacks);
        }
    }
}
