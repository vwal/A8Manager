#include "Assimil8or/Preset/ZoneSampleRanges.h"
#include "GUI/Assimil8or/Editor/ZoneEditor.h"
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace ZoneSampleRanges;
    void check (bool value, const char* message)
    {
        if (! value) throw std::runtime_error (message);
    }
    void expect (const Stored& state, Frame start, Frame end, Frame loopStart, double length, const char* message)
    {
        const auto range { resolve (state, 1000) };
        check (range.sampleValid && range.loopValid && range.sampleStart == start && range.sampleEnd == end &&
               range.loopStart == loopStart && range.loopLength == length, message);
    }

    void pureRanges ()
    {
        const Stored implicit { 100, 900, {}, {} };
        expect (implicit, 100, 900, 100, 800, "Implicit loop follows selected sample, not file");
        auto changed { edit (implicit, 1000, Marker::sampleStart, 200, Mode::length) };
        expect (changed, 200, 900, 200, 700, "Implicit sample-start edit follows selection");
        check (! changed.loopStart && ! changed.loopLength, "Sample edits preserve both unset loop fields");
        expect (Stored { 100, 900, 200, {} }, 100, 900, 200, 700, "Partial length defaults to sample end");
        expect (Stored { 100, 900, {}, 200.5 }, 100, 900, 100, 200.5, "Partial start defaults to sample start");

        const Stored explicitLoop { 100, 900, 300, 200.5 };
        changed = edit (explicitLoop, 1000, Marker::sampleStart, 400, Mode::length);
        expect (changed, 400, 900, 400, 200.5, "Length mode pushes loop forward with exact fractional length");
        changed = edit (explicitLoop, 1000, Marker::sampleEnd, 450, Mode::length);
        expect (changed, 100, 450, 249, 200.5, "Length mode pushes loop backward without fractional truncation");
        expect (edit (explicitLoop, 1000, Marker::sampleStart, 950, Mode::length), 699, 900, 699, 200.5,
                "Length sample minimum is ceiling of fractional loop length");
        expect (edit (explicitLoop, 1000, Marker::sampleEnd, 0, Mode::length), 100, 301, 100, 200.5,
                "Length-mode sample end cannot shrink below loop span");
        expect (edit (explicitLoop, 1000, Marker::sampleStart, 400, Mode::end), 400, 900, 400, 100.5,
                "End mode contracts loop start while fixing loop end");
        expect (edit (explicitLoop, 1000, Marker::sampleEnd, 450, Mode::end), 100, 450, 300, 150,
                "End mode contracts loop end while fixing loop start");
        expect (edit (explicitLoop, 1000, Marker::sampleStart, 900, Mode::end), 496, 900, 496, 4.5,
                "Integer sample start respects fractional loop end and four-frame minimum");
        expect (edit (explicitLoop, 1000, Marker::sampleEnd, 0, Mode::end), 100, 304, 300, 4,
                "End-mode sample end retains four loop frames");
        expect (edit (explicitLoop, 1000, Marker::loopStart, 1000, Mode::length), 100, 900, 699, 200.5,
                "Length-mode loop start is confined inside sample");
        expect (edit (explicitLoop, 1000, Marker::loopStart, 400, Mode::end), 100, 900, 400, 100.5,
                "End-mode loop start fixes end");
        expect (edit (explicitLoop, 1000, Marker::loopStart, 400, Mode::length, true), 100, 900, 400, 100.5,
                "Boundary matching fixes opposite edge independently of Length mode");
        expect (edit (explicitLoop, 1000, Marker::loopEnd, 2000, Mode::length), 100, 900, 300, 600,
                "Loop end stops at sample end");
        changed = edit (Stored {}, 1000, Marker::loopStart, 0, Mode::length);
        check (changed.loopStart == 0 && changed.loopLength == 1000.0, "User loop edits materialize zero and full-file length explicitly");
        changed = move (implicit, 1000, true, 100);
        check (! changed.loopStart && ! changed.loopLength, "No-op implicit loop move preserves optional sentinels");
        expect (move (explicitLoop, 1000, false, 300), 200, 1000, 300, 200.5, "Whole sample move clips at file end and leaves contained loop fixed");
        expect (move (Stored { 100, 500, 150, 200.5 }, 1000, false, 400), 500, 900, 500, 200.5,
                "Whole sample move pushes explicit loop without resizing");
        changed = repair (Stored { 100, 900, 0, 20 }, 1000, Mode::length);
        expect (changed, 100, 900, 100, 800, "Invalid legacy loop resets to selected sample");
        check (! changed.loopStart && ! changed.loopLength, "Legacy reset retains automatic loop intent");
        check (! resolve (Stored { 100, 900, 0, 20 }, 1000).loopValid, "Resolver reports invalid legacy loop without silently repairing");
        for (Frame frames { 0 }; frames < 4; ++frames)
        {
            const auto tiny { repair (explicitLoop, frames, Mode::length) };
            const auto range { resolve (tiny, frames) };
            check (range.sampleStart == 0 && range.sampleEnd == frames && ! range.loopValid &&
                   ! limits (tiny, frames, Marker::sampleStart, Mode::length).editable,
                   "Tiny files retain actual frame count and disallow loop editing");
        }

        // Sweep edit directions/modes across fractional and implicit states.
        for (const auto& state : { implicit, explicitLoop, Stored { 100, 900, {}, 4.03125 } })
            for (const auto mode : { Mode::length, Mode::end })
                for (const auto marker : { Marker::sampleStart, Marker::sampleEnd, Marker::loopStart, Marker::loopEnd })
                    for (int value { -100 }; value <= 1100; value += 7)
                        check (resolve (edit (state, 1000, marker, value, mode), 1000).loopValid,
                               "Every bounded edit retains valid contained sample/loop geometry");
    }

    void orderedApply ()
    {
        ZoneProperties zone;
        zone.setSampleStart (100, false);
        zone.setSampleEnd (900, false);
        zone.setLoopStart (300, false);
        zone.setLoopLength (200.5, false);
        ZoneProperties observer (zone.getValueTree (), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::yes);
        int observed {};
        auto verify = [&] (auto)
        {
            ++observed;
            check (resolve (read (observer), 1000).loopValid, "Synchronous observers always see valid contained loop during multi-field commits");
        };
        observer.onSampleStartChange = verify;
        observer.onSampleEndChange = verify;
        observer.onLoopStartChange = verify;
        observer.onLoopLengthChange = verify;
        const auto original { read (zone) };
        const auto originalTree { zone.getValueTree ().createCopy () };
        for (const auto& destination : { edit (original, 1000, Marker::sampleStart, 600, Mode::length),
             edit (original, 1000, Marker::sampleEnd, 350, Mode::length),
             move (original, 1000, false, -100), Stored { 100, 900, {}, {} },
             Stored { 100, 900, {}, 200.5 }, Stored { 100, 900, 300, {} } })
        {
            apply (zone, destination, 1000);
            const auto actual { read (zone) };
            check (actual.sampleStart == destination.sampleStart && actual.sampleEnd == destination.sampleEnd &&
                   actual.loopStart == destination.loopStart && actual.loopLength == destination.loopLength,
                   "Commit preserves exact optional state including partial defaults");
            apply (zone, original, 1000);
            check (originalTree.isEquivalentTo (zone.getValueTree ()), "Returning to captured range restores original tree exactly");
        }
        check (observed > 10, "Ordered-commit observer exercises intermediate states");
    }

    void independentRanges ()
    {
        const Stored external { 300, 600, 700, 200.5 };
        check (! resolve (external, 1000).loopValid && resolve (external, 1000, true).loopValid,
               "Outside-sample loops require explicit channel permission");
        for (const auto& state : { external, Stored { 300, 600, 20, 200.5 }, Stored { 300, 600, 20, 800.5 } })
        {
            check (resolve (state, 1000, true).loopValid, "Before/after/enclosing loops remain valid when file-bounded and enabled");
            for (const auto mode : { Mode::length, Mode::end })
            {
                for (const auto marker : { Marker::sampleStart, Marker::sampleEnd })
                    for (int value { -100 }; value <= 1100; value += 7)
                    {
                        const auto edited { edit (state, 1000, marker, value, mode, false, true) };
                        const auto range { resolve (edited, 1000, true) };
                        check (range.sampleValid && range.loopValid && edited.loopStart == state.loopStart && edited.loopLength == state.loopLength,
                               "Enabled sample-edge edits are independent from explicit loop, in both Length and End modes");
                    }
                for (const auto marker : { Marker::loopStart, Marker::loopEnd })
                    for (int value { -100 }; value <= 1100; value += 7)
                    {
                        const auto edited { edit (state, 1000, marker, value, mode, false, true) };
                        const auto range { resolve (edited, 1000, true) };
                        check (range.sampleValid && range.loopValid && edited.sampleStart == state.sampleStart && edited.sampleEnd == state.sampleEnd,
                               "Enabled loop edits are file-bounded and leave sample unchanged");
                    }
            }
            const auto moved { move (state, 1000, false, -200, true) };
            check (moved.loopStart == state.loopStart && moved.loopLength == state.loopLength,
                   "Enabled whole-sample move never pushes an explicit loop");
        }
        auto shifted { move (external, 1000, true, 9999, true) };
        check (shifted.loopStart == 799 && shifted.loopLength == 200.5, "Enabled loop move retains exact fractional length at physical EOF");
        const auto partial { resolve (Stored { 100, 500, 700, {} }, 1000, true) };
        check (partial.loopValid && partial.loopLength == 300, "Enabled beyond-sample start with absent length defaults to EOF");
        const auto partialBefore { resolve (Stored { 300, 600, 100, {} }, 1000, true) };
        check (partialBefore.loopLength == 500, "Absent length otherwise defaults to selected sample end");
        const Stored implicit { 300, 600, {}, {} };
        shifted = edit (implicit, 1000, Marker::sampleStart, 500, Mode::length, false, true);
        check (! shifted.loopStart && ! shifted.loopLength && resolve (shifted, 1000, true).loopStart == 500,
               "Fully automatic loop keeps following sample even with outside editing enabled");
        const auto shortSample { resolve (Stored { 300, 302, 500, 100 }, 1000, true) };
        check (! shortSample.sampleValid && shortSample.loopValid, "Independent file-valid loop survives a short imported SAMPLE");
        check (! resolve (Stored { 300, 600, 999, 4 }, 1000, true).loopValid &&
               ! resolve (Stored { 300, 600, 100, 3.5 }, 1000, true).loopValid,
               "Outside permission never bypasses physical bounds or four-frame loop minimum");
        ZoneProperties zone;
        zone.setSampleStart (300, false); zone.setSampleEnd (600, false);
        zone.setLoopStart (320, false); zone.setLoopLength (100, false);
        const auto before { zone.getValueTree ().createCopy () };
        apply (zone, external, 1000);
        check (zone.getValueTree ().isEquivalentTo (before), "Default apply refuses an unpermitted external loop");
        apply (zone, external, 1000, true, true);
        check (zone.getLoopStart () == 700 && zone.getLoopLength () == 200.5, "Permitted apply commits external loop exactly");
    }

    juce::MouseEvent eventFor (juce::Component& component, float y, int flags)
    {
        return { juce::Desktop::getInstance ().getMainMouseSource (), { 20, y }, juce::ModifierKeys (flags),
                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &component, &component, juce::Time::getCurrentTime (),
                 { 20, 50 }, juce::Time::getCurrentTime (), 1, y != 50 };
    }
}

struct ZoneSampleRangesTestAccess
{
    static void numericFields ()
    {
        using namespace ZoneSampleRanges;
        ZoneEditor editor;
        editor.sampleProperties.setLengthInSamples (1000, false);
        editor.sampleProperties.setSampleRate (48000, false);
        editor.sampleProperties.setStatus (SampleStatus::exists, false);
        editor.displayToolsMenu = [] (int) {};
        editor.setSize (250, 450);
        auto reset = [&] (bool endMode, bool explicitLoop)
        {
            editor.zoneProperties.setSampleStart (100, false);
            editor.zoneProperties.setSampleEnd (900, false);
            editor.zoneProperties.setLoopStart (explicitLoop ? 300 : -1, false);
            editor.zoneProperties.setLoopLength (explicitLoop ? 200.5 : -1, false);
            editor.setLoopLengthIsEnd (endMode);
            editor.setEditComponentsEnabled (true);
            editor.updateSamplePositionInfo ();
        };
        reset (false, false);
        check (editor.loopStartTextEditor.getText ().getLargeIntValue () == 100 &&
               editor.loopLengthTextEditor.getText ().getDoubleValue () == 800, "Implicit loop fields display selected sample bounds");
        editor.loopStartTextEditor.onFocusLost ();
        editor.loopLengthTextEditor.onFocusLost ();
        check (! editor.zoneProperties.getLoopStart () && ! editor.zoneProperties.getLoopLength (),
               "Focus without edits does not turn automatic loop into explicit loop");
        editor.sampleStartTextEditor.setText ("200");
        editor.sampleStartTextEditor.onReturnKey ();
        expect (read (editor.zoneProperties), 200, 900, 200, 700, "Typed sample start updates implicit loop");
        reset (false, true);
        editor.sampleStartTextEditor.setText ("999");
        editor.sampleStartTextEditor.onReturnKey ();
        expect (read (editor.zoneProperties), 699, 900, 699, 200.5, "Typed sample boundary pushes loop and clamps sample size");
        reset (true, true);
        editor.loopStartTextEditor.setText ("10000");
        editor.loopStartTextEditor.onReturnKey ();
        expect (read (editor.zoneProperties), 100, 900, 496, 4.5, "Typed End-mode loop start retains opposite boundary");
        check (editor.loopLengthTextEditor.getText ().getDoubleValue () == 500.5, "Large absolute END retains fractional loop length");

        const auto left { juce::ModifierKeys::leftButtonModifier };
        for (const auto fine : { false, true })
        {
            reset (false, true);
            auto& field { static_cast<juce::Component&> (editor.sampleStartTextEditor) };
            const auto flags { left | (fine ? juce::ModifierKeys::shiftModifier : 0) };
            field.mouseDown (eventFor (field, 50, flags));
            // Normal dragging deliberately caps a single event to 1% of the
            // range. Exercise sustained movement instead of one giant jump.
            for (int step { 1 }; step <= 160; ++step)
            {
                field.mouseDrag (eventFor (field, 50.0f - step * 128.0f, flags));
                const auto range { resolve (read (editor.zoneProperties), 1000) };
                check (range.loopValid && range.loopLength == 200.5,
                       "Every normal/Shift drag event retains containment and exact fractional length");
            }
            field.mouseUp (eventFor (field, 50.0f - 160.0f * 128.0f, flags));
            expect (read (editor.zoneProperties), 699, 900, 699, 200.5, "Normal and Shift drags obey sample-contained loop limits");
            auto& loopField { static_cast<juce::Component&> (editor.loopStartTextEditor) };
            juce::MouseWheelDetails wheel;
            wheel.deltaY = 1.0f;
            loopField.mouseWheelMove (eventFor (loopField, 50, juce::ModifierKeys::commandModifier |
                (fine ? juce::ModifierKeys::shiftModifier : 0)), wheel);
            expect (read (editor.zoneProperties), 699, 900, 699, 200.5, "Command and Shift-Command wheel cannot push loop outside sample");
        }
        reset (true, false);
        editor.loopLengthTextEditor.setText ("2000");
        editor.loopLengthTextEditor.onReturnKey ();
        check (editor.zoneProperties.getLoopStart () == 100 && editor.zoneProperties.getLoopLength () == 800,
               "Explicit end entry materializes both loop bounds even at full selected span");
        editor.sampleProperties.setLengthInSamples (3, false);
        editor.setEditComponentsEnabled (true);
        check (! editor.sampleStartTextEditor.isEnabled () && ! editor.loopLengthTextEditor.isEnabled (),
               "Tiny sample disables boundary editing without inventing out-of-file frames");
    }
};

void testZoneSampleRanges ()
{
    pureRanges ();
    orderedApply ();
    independentRanges ();
    ZoneSampleRangesTestAccess::numericFields ();
    std::cout << "Zone sample range regression tests passed\n";
}
