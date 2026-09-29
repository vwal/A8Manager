#include "GUI/Assimil8or/Editor/Waveform/WaveformDisplay.h"
#include "oolib/Properties/RuntimeRootProperties.h"
#include <iostream>
#include <deque>
#include <stdexcept>

struct WaveformTestAccess
{
    static void run ()
    {
        auto check = [] (bool condition, const char* message) { if (! condition) throw std::runtime_error (message); };
        using namespace WaveformPresentation;
        check (samples (500000) == "500,000" && samples (1234567.5) == "1,234,567.5", "Comma grouping preserves fractional frames");
        check (samples (0) == "0" && samples (-1000) == "-1,000", "Zero and signed sample formatting");
        check (duration (48000, 48000, 12) == "0:00.500" && duration (48000, 48000, -12) == "0:02.000", "Pitch offset scales nominal duration");
        check (duration (4, 48000) == "0:00.000083", "Sub-millisecond loops remain readable");
        check (duration (1000, 0) == "--:--" && time (59.9999) == "1:00.000", "Unknown rate and minute rounding");
        juce::AudioBuffer<float> audio (2, 1000);
        for (auto frame { 0 }; frame < 1000; ++frame)
        {
            audio.setSample (0, frame, frame % 100 < 50 ? 0.4f : -0.4f);
            audio.setSample (1, frame, frame % 160 < 80 ? 0.2f : -0.2f);
        }
        check (zeroCrossing (audio, 0, 240, 0, 999, true) == 250 && zeroCrossing (audio, 0, 240, 0, 999, false) == 200, "Nearest directional crossing");
        check (zeroCrossing (audio, 1, 240, 0, 999, true) == 320, "Crossings use the chosen stereo side and exclude the current point");
        check (! zeroCrossing (audio, 0, 240, 210, 249, true) && ! zeroCrossing (audio, 2, 0, 0, 999, true), "Crossing bounds and invalid side");
        check (zeroCrossing (audio, 0, 1000, 0, 1000, false) == 950 && ! zeroCrossing (audio, 0, 1000, 0, 1000, true), "Exclusive EOF is safe");

        juce::ValueTree root { "Root" };
        RuntimeRootProperties runtime (root, RuntimeRootProperties::WrapperType::owner, RuntimeRootProperties::EnableCallbacks::no);
        SampleManagerProperties manager (runtime.getValueTree (), SampleManagerProperties::WrapperType::owner, SampleManagerProperties::EnableCallbacks::no);
        AudioPlayerProperties audition (runtime.getValueTree (), AudioPlayerProperties::WrapperType::owner, AudioPlayerProperties::EnableCallbacks::no);
        auto channelTree { ChannelProperties::create (1) };
        ChannelProperties channel (channelTree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        for (auto index { 0 }; index < 8; ++index)
        {
            channelTree.addChild (ZoneProperties::create (index + 1), -1, nullptr);
            ZoneProperties z (channel.getZoneVT (index), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            z.setSample ("test" + juce::String (index) + ".wav", false);
            z.setSampleStart (200, false);
            z.setSampleEnd (600, false);
            z.setLoopStart (300, false);
            z.setLoopLength (100.5, false);
            SampleProperties s (manager.getSamplePropertiesVT (0, index), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
            s.setAudioBufferPtr (&audio, false);
            s.setLengthInSamples (1000, false);
            s.setNumChannels (2, false);
            s.setSampleRate (1000, false);
            s.setStatus (SampleStatus::exists, false);
        }
        ModernLookAndFeel look;
        WaveformDisplay view;
        view.setLookAndFeel (&look);
        view.setSize (760, 220);
        view.init (channelTree, root);
        check (view.markerPosition (3) == 400.5 && view.markerLabel (3).contains ("400.5"), "Loop end model and label retain fractional frame precision");
        auto selectedLoop { false };
        view.onRegionSelected = [&] (bool loop) { selectedLoop = loop; };
        check (view.durationInfo.getText ().contains ("SAMPLE 0:00.400") && view.durationInfo.getText ().contains ("Loop 0:00.101"), "Region lengths use source sample rate");
        view.zoneProperties.setPitchOffset (12, true);
        check (view.durationInfo.getText ().contains ("SAMPLE 0:00.200"), "Pitch change updates duration immediately");
        const auto lengths { view.durationInfo.getText () };
        audition.setAuditionRate (0.5, true);
        check (view.durationInfo.getText () == lengths, "Audition speed never changes nominal lengths");
        view.zoneProperties.setPitchOffset (0, true);

        view.applyMenuAction (32, 340.0);
        check (selectedLoop && view.zoneProperties.getLoopStart () == 340 && view.zoneProperties.getLoopLength () == 100.5, "Set loop start selects LOOP and retains length in Length mode");
        channel.setLoopLengthIsEnd (true, true);
        view.applyMenuAction (32, 350.0);
        check (view.zoneProperties.getLoopStart () == 350 && view.zoneProperties.getLoopLength () == 90.5, "End mode keeps the opposite edge fixed");
        view.applyMenuAction (30, 220.0);
        check (! selectedLoop && view.zoneProperties.getSampleStart () == 220, "Set sample start selects SAMPLE");
        view.nudgeMarker (0, true);
        check (view.zoneProperties.getSampleStart () == 250, "Menu nudge edits the actual zone");
        view.nudgeMarker (0, false);
        check (view.zoneProperties.getSampleStart () == 200, "Left nudge finds previous crossing");
        view.applyMenuAction (31, 99999.0);
        check (! view.zoneProperties.getSampleEnd (), "Set-here clamps end to EOF");
        view.zoneProperties.setSampleEnd (600, true);

        auto point = [&] (double frame, bool bottom = false)
        {
            return juce::Point<float> { view.waveform.sampleToX (frame), bottom ? view.waveform.getHeight () - 2.0f : view.waveform.getHeight () * 0.5f };
        };
        view.setLoopSelected (false);
        check (! view.beginRegionMove (point (50)), "Option drag outside both regions must not move either");
        check (view.beginRegionMove (point (400)) && view.movingRegion->target == RegionMove::Target::sample, "Overlap follows current sample selection");
        view.setLoopSelected (true);
        check (view.beginRegionMove (point (400)) && view.movingRegion->target == RegionMove::Target::loop, "Overlap follows current loop selection");
        view.waveform.onMoveRegion (50.0);
        check (view.zoneProperties.getLoopStart () == 400 && view.zoneProperties.getLoopLength () == 90.5, "Modifier move preserves length even in End mode");
        check (view.beginRegionMove (point (250)) && ! selectedLoop, "Non-overlapping region selects itself");
        view.setLoopSelected (false);
        check (view.beginRegionMove (point (400, true)) && selectedLoop, "Loop handle overrides sample selection");
        view.applyMenuAction (2, {});
        check (view.waveform.getSamplesPerPixel () * view.waveform.getWidth () < 1000, "Zoom to sample markers");
        view.waveform.setVerticalZoom (4.0f);
        view.zoomInfo.onClick ();
        check (std::abs (view.waveform.getSamplesPerPixel () * view.waveform.getWidth () - 1000.0) < 0.01 && view.waveform.getVerticalZoom () == 1.0f, "Clickable zoom resets both axes");
        view.focusLoop ();
        view.keyPressed (juce::KeyPress ('1'));
        check (std::abs (view.waveform.xToSample (view.waveform.getWidth () * 0.5f) - 200.0) < 0.01, "Key 1 centres sample start without changing zoom");
        auto checkMenu = [&] (bool context, bool editable)
        {
            const auto menu { view.buildWaveformMenu (context ? std::optional<double> (200.0) : std::nullopt) };
            juce::Array<int> actions;
            juce::StringArray headings, submenus;
            juce::StringArray layout;
            juce::PopupMenu::MenuItemIterator items (menu);
            while (items.next ())
            {
                const auto& item { items.getItem () };
                if (item.isSectionHeader) { headings.add (item.text); layout.add (item.text); }
                else if (item.subMenu != nullptr)
                {
                    submenus.add (item.text);
                    check (item.isEnabled == editable, "Waveform editing submenus respect read-only state");
                    if (item.text == "Match Opposite Boundary")
                    {
                        juce::PopupMenu::MenuItemIterator matches (*item.subMenu);
                        auto marker { 0 };
                        while (matches.next ())
                        {
                            const auto& match { matches.getItem () };
                            check (marker < 4 && match.text == markerNames[static_cast<size_t> (marker)] +
                                   " to " + (marker % 2 == 0 ? "End" : "Start"), "All four match conditions retain their labels and order");
                            check (match.subMenu != nullptr && match.isEnabled == editable, "Every match condition exposes a guarded direction submenu");
                            juce::PopupMenu::MenuItemIterator directions (*match.subMenu);
                            auto direction { 0 };
                            while (directions.next ())
                            {
                                const auto& action { directions.getItem () };
                                check (direction < 2 && action.text == (direction == 0 ? "Left <<" : "Right >>") &&
                                       action.itemID == 40 + marker * 2 + direction && action.subMenu == nullptr,
                                       "Each match condition offers distinct Left and Right actions in order");
                                check (action.isEnabled == editable, "Directional match actions respect read-only state");
                                ++direction;
                            }
                            check (direction == 2, "Every match condition has exactly two directions");
                            ++marker;
                        }
                        check (marker == 4, "Matching includes Sample Start, Sample End, Loop Start and Loop End");
                    }
                }
                else if (item.isSeparator) layout.add ("---");
                else
                {
                    layout.add (juce::String (item.itemID));
                    actions.add (item.itemID);
                    check (item.isEnabled == (item.itemID < 30 || editable), "Direct actions preserve zoom/jump access and guard marker placement");
                    if (item.itemID >= 10 && item.itemID < 14)
                        check (item.shortcutKeyDescription == juce::String (item.itemID - 9), "Jump shortcuts occupy the menu's right-hand shortcut column");
                }
            }
            auto expected { juce::Array<int> { 1, 2, 3, 10, 11, 12, 13 } };
            if (context) expected.addArray (juce::Array<int> { 30, 31, 32, 33 });
            check (actions == expected, "Zoom, jump and context placement actions are top-level and in the requested order");
            check (headings == (context ? juce::StringArray { "ZOOM", "JUMP TO MARKER", "SET MARKER HERE" }
                                       : juce::StringArray { "ZOOM", "JUMP TO MARKER" }), "Menu headings distinguish frequent actions");
            check (submenus == juce::StringArray { "Zero Crossing Nudge", "Match Opposite Boundary" }, "Only nudge and matching remain submenus");
            auto expectedLayout { juce::StringArray { "ZOOM", "1", "2", "3", "---", "JUMP TO MARKER", "10", "11", "12", "13", "---" } };
            if (context) expectedLayout.addArray ({ "SET MARKER HERE", "30", "31", "32", "33", "---" });
            check (layout == expectedLayout, "Separators precede Jump/Set headings, never follow headings or the initial Zoom title");
        };
        checkMenu (false, true);
        checkMenu (true, true);

        view.resetZoom ();
        auto mouse = [] (juce::Component& component, juce::Point<float> position, juce::Point<float> origin, int flags)
        {
            return juce::MouseEvent (juce::Desktop::getInstance ().getMainMouseSource (), position, juce::ModifierKeys (flags),
                1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &component, &component, juce::Time::getCurrentTime (), origin,
                juce::Time::getCurrentTime (), 1, position != origin);
        };
        const auto left { juce::ModifierKeys::leftButtonModifier };
        const auto alt { left | juce::ModifierKeys::altModifier };
        const juce::Point<float> sampleHandle { view.waveform.sampleToX (200) + 3.0f, 2.0f };
        view.markerOverlay.mouseDown (mouse (view.markerOverlay, sampleHandle, sampleHandle, left));
        check (! selectedLoop, "Plain sample-handle mouse-down selects the sample pair");
        auto movedHandle { sampleHandle.translated (30, 0) };
        view.markerOverlay.mouseDrag (mouse (view.markerOverlay, movedHandle, sampleHandle, left));
        view.markerOverlay.mouseUp (mouse (view.markerOverlay, movedHandle, sampleHandle, left));
        check (view.zoneProperties.getSampleStart ().value_or (0) > 200 && view.zoneProperties.getSampleEnd () == 600, "Plain handle drag edits only its edge");
        const juce::Point<float> loopHandle { view.waveform.sampleToX (400) + 3.0f, view.waveform.getHeight () - 2.0f };
        view.setLoopSelected (false);
        view.markerOverlay.mouseDown (mouse (view.markerOverlay, loopHandle, loopHandle, alt));
        movedHandle = loopHandle.translated (30, 0);
        view.markerOverlay.mouseDrag (mouse (view.markerOverlay, movedHandle, loopHandle, alt));
        view.markerOverlay.mouseUp (mouse (view.markerOverlay, movedHandle, loopHandle, alt));
        check (selectedLoop && view.zoneProperties.getLoopStart ().value_or (0) > 400 && view.zoneProperties.getLoopLength () == 90.5,
               "Option-drag on a loop handle moves the pair, regardless of End mode and previous selection");
        auto menus { 0 };
        const auto contextCallback { view.waveform.onContextMenu };
        view.waveform.onContextMenu = [&] (juce::Point<float> position) { ++menus; check (position.x == 80.0f, "Menu retains original click position"); };
        const juce::Point<float> contextPoint { 80, 40 };
        const auto right { juce::ModifierKeys::rightButtonModifier };
        view.waveform.mouseDown (mouse (view.waveform, contextPoint, contextPoint, right));
        view.waveform.mouseUp (mouse (view.waveform, contextPoint, contextPoint, right));
        check (menus == 1, "Stationary right-click opens the menu");
        view.waveform.mouseDown (mouse (view.waveform, contextPoint, contextPoint, right));
        view.waveform.mouseDrag (mouse (view.waveform, contextPoint.translated (10, 0), contextPoint, right));
        view.waveform.mouseUp (mouse (view.waveform, contextPoint.translated (10, 0), contextPoint, right));
        check (menus == 1, "Right-drag zoom must not also open a menu");
        view.waveform.onContextMenu = contextCallback;

        view.setExpanded (true);
        view.setSize (760, 560);
        view.setZone (1);
        check (view.expanded && view.expandButton.getToggleState () && view.zoneProperties.getId () == 2, "Expanded viewer stays open across zone changes");
        checkMenu (false, true);
        checkMenu (true, true);
        view.resetZoom ();
        view.setLoopSelected (true);
        const auto shade { view.markerOverlay.createComponentSnapshot (view.markerOverlay.getLocalBounds ()) };
        const auto midY { shade.getHeight () / 2 };
        check (shade.getPixelAt (juce::roundToInt (view.waveform.sampleToX (100)), midY).getAlpha () >
               shade.getPixelAt (juce::roundToInt (view.waveform.sampleToX (350)), midY).getAlpha (), "Outside of selected loop is dimmer");
        // Make all four labels collide horizontally; every label must get a lane.
        view.zoneProperties.setSampleStart (490, true);
        view.zoneProperties.setSampleEnd (510, true);
        view.zoneProperties.setLoopStart (495, true);
        view.zoneProperties.setLoopLength (10.0, true);
        for (const auto size : { juce::Point<int> { 530, 140 }, { 760, 170 }, { 760, 220 }, { 760, 560 } })
        {
            view.setSize (size.x, size.y);
            const auto labels { view.markerOverlay.labelBounds () };
            for (auto i { 0 }; i < 4; ++i)
            {
                check (! labels[i].isEmpty () && view.markerOverlay.getLocalBounds ().contains (labels[i]), "Visible marker labels remain inside the view");
                for (auto j { 0 }; j < i; ++j) check (! labels[i].intersects (labels[j]), "Nearby marker labels must never overlap");
            }
        }
        view.setEnabled (false);
        checkMenu (false, false);
        checkMenu (true, false);
        const auto before { view.zoneProperties.getValueTree ().createCopy () };
        view.applyMenuAction (30, 0.0);
        check (view.zoneProperties.getValueTree ().isEquivalentTo (before) && ! view.beginRegionMove (point (500)), "Disabled stereo-right editing remains guarded");
        view.setEnabled (true);

        // A nonzero join like the reported case, through the actual menu actions.
        for (auto side { 0 }; side < 2; ++side)
            for (auto i { 0 }; i < audio.getNumSamples (); ++i) audio.setSample (side, i, 0.6f);
        audio.setSample (0, 300, -0.25f);
        audio.setSample (0, 599, 0.0f);
        audio.setSample (0, 561, -0.25f);
        view.zoneProperties.setLoopStart (300, true);
        view.zoneProperties.setLoopLength (300, true);
        view.zoneProperties.setSampleStart (300, true);
        view.zoneProperties.setSampleEnd (600, true);
        view.zoneProperties.setSide (0, true);
        channel.setLoopLengthIsEnd (false, true);
        view.applyMenuAction (46, {});
        check (selectedLoop && view.markerPosition (2) == 300 && view.markerPosition (3) == 562 &&
               view.markerPosition (1) == 600, "Match Loop End keeps Loop Start and sample boundaries fixed");
        view.applyMenuAction (42, {});
        check (! selectedLoop && view.markerPosition (0) == 300 && view.markerPosition (1) == 562,
               "Match Sample End selects SAMPLE and retains its start");
        audio.setSample (0, 300, 0.0f);
        audio.setSample (0, 318, -0.25f);
        view.zoneProperties.setLoopLength (262.5, true);
        view.applyMenuAction (45, {});
        check (selectedLoop && view.markerPosition (2) == 318 && view.markerPosition (3) == 562.5 &&
               view.zoneProperties.getLoopLength () == 244.5 && ! channel.getLoopLengthIsEnd (),
               "Match Loop Start keeps the fractional end fixed even in Length mode, without changing mode");
        view.applyMenuAction (41, {});
        check (! selectedLoop && view.markerPosition (0) == 318 && view.markerPosition (1) == 562,
               "Match Sample Start retains its end and selects SAMPLE");
        view.applyMenuAction (41, {});
        check (view.markerPosition (0) == 318 && view.durationInfo.getText ().contains ("unchanged"), "No improvement reports a no-op");
        view.setEnabled (false);
        const auto matched { view.zoneProperties.getValueTree ().createCopy () };
        for (auto action { 40 }; action < 48; ++action) view.applyMenuAction (action, {});
        check (view.zoneProperties.getValueTree ().isEquivalentTo (matched), "Disabled channels cannot invoke boundary matching");
        view.setEnabled (true);

        // Route every directional action through the menu dispatcher, in both
        // loop modes. Matches exist on both sides, so neither direction may
        // silently choose the other side's candidate.
        for (const auto endMode : { false, true })
        {
            channel.setLoopLengthIsEnd (endMode, true);
            for (auto marker { 0 }; marker < 4; ++marker)
            {
                for (const auto rightward : { false, true })
                {
                    view.zoneProperties.setSampleStart (300, true);
                    view.zoneProperties.setSampleEnd (600, true);
                    view.zoneProperties.setLoopStart (350, true);
                    view.zoneProperties.setLoopLength (300.5, true);
                    view.zoneProperties.setSide (marker % 2, true);
                    view.setLoopSelected (marker < 2);
                    const std::array<double, 4> original { 300.0, 600.0, 350.0, 650.5 };
                    const auto offset { marker % 2 };
                    const auto opposite { marker + (offset == 0 ? 1 : -1) };
                    const auto moving { original[static_cast<size_t> (marker)] };
                    const auto leftMatch { static_cast<int> (std::floor (moving)) - 20 };
                    const auto rightMatch { static_cast<int> (std::ceil (moving)) + 20 };
                    const auto chosenMatch { static_cast<int> (moving) + (rightward ? 7 : -7) };
                    for (auto side { 0 }; side < 2; ++side)
                        for (auto frame { 0 }; frame < audio.getNumSamples (); ++frame) audio.setSample (side, frame, 0.6f);
                    const auto side { marker % 2 };
                    audio.setSample (side, static_cast<int> (std::floor (original[static_cast<size_t> (opposite)])) - (1 - offset), -0.25f);
                    audio.setSample (side, leftMatch - offset, -0.25f);
                    audio.setSample (side, rightMatch - offset, -0.25f);
                    // Cross the target amplitude nearby without hitting it
                    // exactly. A distant perfect match must not win instead.
                    audio.setSample (side, chosenMatch - offset, -0.3f);
                    view.applyMenuAction (40 + marker * 2 + (rightward ? 1 : 0), {});
                    check (view.markerPosition (marker) == chosenMatch, "All eight menu actions prefer the nearest useful crossing over a farther perfect match on the selected stereo side");
                    for (auto other { 0 }; other < 4; ++other)
                        if (other != marker)
                            check (view.markerPosition (other) == original[static_cast<size_t> (other)], "Directional matching leaves every other boundary fixed, including fractional Loop End");
                    check (selectedLoop == (marker >= 2) && channel.getLoopLengthIsEnd () == endMode,
                           "Directional matching selects the edited pair without changing Length/End mode");
                    audio.setSample (side, chosenMatch - offset, -0.25f);
                    const auto alreadyMatched { view.zoneProperties.getValueTree ().createCopy () };
                    view.applyMenuAction (40 + marker * 2 + (rightward ? 1 : 0), {});
                    check (view.zoneProperties.getValueTree ().isEquivalentTo (alreadyMatched) && view.durationInfo.getText ().contains ("unchanged"),
                           "Every direction preserves an already perfect match and reports a no-op");
                }
            }
        }
        view.zoneProperties.setSide (0, true);
        channel.setLoopLengthIsEnd (false, true);

        // The first target-amplitude crossing is not always an improvement.
        // Do not skip over it to a farther crossing which trims more material.
        for (const auto rightward : { false, true })
        {
            view.zoneProperties.setSampleStart (300, true);
            view.zoneProperties.setSampleEnd (600, true);
            for (auto frame { 0 }; frame < audio.getNumSamples (); ++frame) audio.setSample (0, frame, 0.6f);
            audio.setSample (0, 300, 0.0f);
            audio.setSample (0, 599, 0.1f);
            audio.setSample (0, 599 + (rightward ? 10 : -10), -0.5f);
            audio.setSample (0, 599 + (rightward ? 30 : -30), 0.0f);
            view.applyMenuAction (rightward ? 43 : 42, {});
            check (view.markerPosition (1) == 600 && view.durationInfo.getText ().contains ("unchanged"),
                   "An unhelpful nearest crossing does not cause a jump to a more distant perfect match");
        }

        // Headless confirmation seam: invoke the same guarded completion used
        // by the real Yes/No dialog, without opening a native window in tests.
        auto prompts { 0 };
        juce::String promptText;
        std::function<void (bool)> answer;
        view.confirmBoundaryMatch = [&] (const juce::String& message, std::function<void (bool)> callback)
        {
            ++prompts;
            promptText = message;
            answer = std::move (callback);
        };
        auto prepareMatch = [&] (int marker, bool rightward, int distance = 100)
        {
            view.zoneProperties.setSampleStart (300, true);
            view.zoneProperties.setSampleEnd (600, true);
            view.zoneProperties.setLoopStart (350, true);
            view.zoneProperties.setLoopLength (300.5, true);
            view.zoneProperties.setSide (0, true);
            view.sampleProperties.setSampleRate (1000, true);
            for (auto side { 0 }; side < 2; ++side)
                for (auto frame { 0 }; frame < audio.getNumSamples (); ++frame) audio.setSample (side, frame, 0.6f);
            const auto offset { marker % 2 };
            const auto opposite { marker + (offset == 0 ? 1 : -1) };
            const auto candidate { static_cast<int> (view.markerPosition (marker)) + (rightward ? distance : -distance) };
            audio.setSample (0, static_cast<int> (view.markerPosition (opposite)) - (1 - offset), -0.25f);
            audio.setSample (0, candidate - offset, -0.25f);
            return candidate;
        };
        for (const auto endMode : { false, true })
        {
            channel.setLoopLengthIsEnd (endMode, true);
            for (auto marker { 0 }; marker < 4; ++marker)
            {
                for (const auto rightward : { false, true })
                {
                    const auto candidate { prepareMatch (marker, rightward) };
                    const auto beforePrompt { view.zoneProperties.getValueTree ().createCopy () };
                    const auto oldPrompts { prompts };
                    view.applyMenuAction (40 + marker * 2 + (rightward ? 1 : 0), {});
                    check (prompts == oldPrompts + 1 && view.zoneProperties.getValueTree ().isEquivalentTo (beforePrompt),
                           "Distant matching waits for approval before editing any marker");
                    check (promptText.contains (markerNames[static_cast<size_t> (marker)]) &&
                           promptText.contains (rightward ? "right" : "left") && promptText.contains ("ms") &&
                           promptText.contains ("opposite boundary will stay fixed"),
                           "Long-move prompt identifies marker, source-time distance, direction and fixed boundary");
                    answer (false);
                    check (view.zoneProperties.getValueTree ().isEquivalentTo (beforePrompt), "No preserves all original boundaries");
                    answer (true);
                    check (view.zoneProperties.getValueTree ().isEquivalentTo (beforePrompt), "Canceled confirmation cannot later be reused");
                    view.applyMenuAction (40 + marker * 2 + (rightward ? 1 : 0), {});
                    std::array<double, 4> original;
                    for (auto i { 0 }; i < 4; ++i) original[static_cast<size_t> (i)] = view.markerPosition (i);
                    answer (true);
                    check (view.markerPosition (marker) == candidate, "Yes applies each long directional match");
                    for (auto i { 0 }; i < 4; ++i)
                        if (i != marker) check (view.markerPosition (i) == original[static_cast<size_t> (i)],
                                               "Approved long move preserves all opposite/unrelated boundaries, including fractional loop ends");
                    check (channel.getLoopLengthIsEnd () == endMode, "Approved long match never switches Length/End mode");
                }
            }
        }
        channel.setLoopLengthIsEnd (false, true);
        for (const auto distance : { 49, 50, 51 })
        {
            const auto candidate { prepareMatch (1, true, distance) };
            const auto oldPrompts { prompts };
            view.applyMenuAction (43, {});
            check (prompts == oldPrompts + (distance > 50 ? 1 : 0), "Only distances strictly above 50 ms require approval");
            if (distance > 50)
            {
                check (view.markerPosition (1) == 600, "51 ms match waits for confirmation");
                answer (true);
            }
            check (view.markerPosition (1) == candidate, "Threshold-adjacent matches use the exact candidate");
        }
        prepareMatch (1, false, 50);
        view.sampleProperties.setSampleRate (999.9, true);
        const auto beforeFractionalPrompt { prompts };
        view.applyMenuAction (42, {});
        check (prompts == beforeFractionalPrompt + 1 && view.markerPosition (1) == 600,
               "A fractional displacement just above 50 ms prompts even if display rounding is close to 50");
        answer (false);

        prepareMatch (1, false, 428);
        // Sample End cannot pass Sample Start: use a larger sample span for 428 ms.
        view.zoneProperties.setSampleStart (100, true);
        audio.setSample (0, 300, 0.6f);
        audio.setSample (0, 100, -0.25f);
        view.applyMenuAction (42, {});
        check (promptText.contains ("428.0 ms to the left"), "Confirmation shows the actual source-time displacement");
        answer (false);

        auto checkStale = [&] (std::function<void ()> change)
        {
            prepareMatch (1, true);
            view.applyMenuAction (43, {});
            check (view.markerPosition (1) == 600, "Stale-approval fixture starts with an unapplied long match");
            change ();
            const auto changed { view.zoneProperties.getValueTree ().createCopy () };
            answer (true);
            check (view.zoneProperties.getValueTree ().isEquivalentTo (changed), "Stale approval never modifies changed editor state");
        };
        checkStale ([&] { view.zoneProperties.setSampleEnd (610, true); view.zoneProperties.setSampleEnd (600, true); });
        checkStale ([&] { view.zoneProperties.setLoopLength (290, true); });
        checkStale ([&] { view.zoneProperties.setSide (1, true); view.zoneProperties.setSide (0, true); });
        checkStale ([&] { view.setEnabled (false); view.setEnabled (true); });
        checkStale ([&] { channel.setLoopLengthIsEnd (true, true); channel.setLoopLengthIsEnd (false, true); });
        checkStale ([&] { view.sampleProperties.setSampleRate (2000, true); view.sampleProperties.setSampleRate (1000, true); });
        checkStale ([&] { view.sampleProperties.setStatus (SampleStatus::doesNotExist, true); view.sampleProperties.setStatus (SampleStatus::exists, true); });
        checkStale ([&] { const auto file { view.zoneProperties.getSample () }; view.zoneProperties.setSample ("replaced.wav", true); view.zoneProperties.setSample (file, true); });
        checkStale ([&] { view.setZone (2); view.setZone (1); });
        prepareMatch (1, true);
        view.applyMenuAction (43, {});
        const auto firstAnswer { answer };
        view.applyMenuAction (43, {});
        firstAnswer (true);
        check (view.markerPosition (1) == 600, "Starting another match invalidates earlier outstanding confirmations");
        answer (false);

        prepareMatch (1, true);
        std::function<void (bool)> destroyedAnswer;
        {
            auto temporary { std::make_unique<WaveformDisplay> () };
            temporary->init (channelTree, root);
            temporary->setZone (1);
            temporary->confirmBoundaryMatch = [&] (const juce::String&, std::function<void (bool)> callback) { destroyedAnswer = std::move (callback); };
            temporary->applyMenuAction (43, {});
            check (static_cast<bool> (destroyedAnswer), "Destruction fixture obtains a long-match confirmation");
        }
        destroyedAnswer (true);
        check (view.markerPosition (1) == 600, "Closing a waveform view invalidates its pending approval safely");

        // Zero-crossing nudges use the same distance approval and stale-state
        // guards, but retain normal Loop Start / Length mode editing semantics.
        auto prepareNudge = [&] (int marker, bool rightward, int distance = 100)
        {
            view.zoneProperties.setSampleStart (300, true);
            view.zoneProperties.setSampleEnd (600, true);
            view.zoneProperties.setLoopStart (350, true);
            view.zoneProperties.setLoopLength (300.5, true);
            view.zoneProperties.setSide (marker % 2, true);
            view.sampleProperties.setSampleRate (1000, true);
            for (auto side { 0 }; side < 2; ++side)
                for (auto frame { 0 }; frame < audio.getNumSamples (); ++frame) audio.setSample (side, frame, 0.6f);
            const auto candidate { static_cast<int> (view.markerPosition (marker)) + (rightward ? distance : -distance) };
            audio.setSample (marker % 2, candidate - marker % 2, 0.0f);
            return candidate;
        };
        for (const auto endMode : { false, true })
        {
            channel.setLoopLengthIsEnd (endMode, true);
            for (auto marker { 0 }; marker < 4; ++marker)
            {
                for (const auto rightward : { false, true })
                {
                    const auto candidate { prepareNudge (marker, rightward) };
                    view.setLoopSelected (marker < 2);
                    const auto beforePrompt { view.zoneProperties.getValueTree ().createCopy () };
                    const auto oldPrompts { prompts };
                    view.applyMenuAction (20 + marker * 2 + (rightward ? 1 : 0), {});
                    check (prompts == oldPrompts + 1 && view.zoneProperties.getValueTree ().isEquivalentTo (beforePrompt),
                           "Every distant zero-crossing action waits for approval without editing markers");
                    check (promptText.contains (markerNames[static_cast<size_t> (marker)]) && promptText.contains ("zero crossing") &&
                           promptText.contains (rightward ? "right" : "left") && promptText.contains ("ms"),
                           "Zero-crossing prompt identifies marker, source-time displacement and direction");
                    check (marker != 2 || endMode || promptText.containsIgnoreCase ("length"),
                           "Loop Start prompt explains the preserved loop length when its end also moves");
                    answer (false);
                    check (view.zoneProperties.getValueTree ().isEquivalentTo (beforePrompt), "Canceling a long zero crossing preserves all markers");
                    answer (true);
                    check (view.zoneProperties.getValueTree ().isEquivalentTo (beforePrompt), "A canceled zero-crossing approval cannot be reused");
                    view.applyMenuAction (20 + marker * 2 + (rightward ? 1 : 0), {});
                    std::array<double, 4> original;
                    for (auto i { 0 }; i < 4; ++i) original[static_cast<size_t> (i)] = view.markerPosition (i);
                    answer (true);
                    check (view.markerPosition (marker) == candidate, "Approval applies each long directional zero-crossing candidate");
                    for (auto i { 0 }; i < 4; ++i)
                    {
                        if (i == marker) continue;
                        const auto expected { original[static_cast<size_t> (i)] +
                            (marker == 2 && i == 3 && ! endMode ? candidate - original[2] : 0.0) };
                        check (view.markerPosition (i) == expected, "Zero crossing preserves unrelated boundaries and moves Loop End only for Loop Start in Length mode");
                    }
                    check (selectedLoop == (marker >= 2) && channel.getLoopLengthIsEnd () == endMode,
                           "Approved zero crossing selects the edited pair without changing Length/End mode");
                    const auto afterApproval { view.zoneProperties.getValueTree ().createCopy () };
                    answer (true);
                    check (view.zoneProperties.getValueTree ().isEquivalentTo (afterApproval), "A successful zero-crossing approval is single-use");
                }
            }
        }
        channel.setLoopLengthIsEnd (false, true);
        for (const auto distance : { 49, 50, 51 })
        {
            const auto candidate { prepareNudge (1, true, distance) };
            const auto oldPrompts { prompts };
            view.applyMenuAction (23, {});
            check (prompts == oldPrompts + (distance > 50 ? 1 : 0), "Zero crossings prompt only for source-time displacements strictly above 50 ms");
            if (distance > 50)
            {
                check (view.markerPosition (1) == 600, "51 ms zero crossing does not move before approval");
                answer (true);
            }
            check (view.markerPosition (1) == candidate, "Threshold-adjacent zero crossings apply the exact candidate");
        }
        prepareNudge (1, false, 50);
        view.sampleProperties.setSampleRate (999.9, true);
        const auto fractionalZeroPrompts { prompts };
        view.applyMenuAction (22, {});
        check (prompts == fractionalZeroPrompts + 1 && view.markerPosition (1) == 600,
               "Zero crossing uses unrounded source-time distance above 50 ms");
        answer (false);
        prepareNudge (3, false, 50);
        const auto fractionalEndPrompts { prompts };
        view.applyMenuAction (26, {});
        check (prompts == fractionalEndPrompts + 1 && view.markerPosition (3) == 650.5,
               "Fractional loop endpoint displacement of 50.5 frames prompts at 1000 Hz");
        answer (true);
        check (view.markerPosition (3) == 600, "Approved fractional end nudge retains exclusive-end accuracy");
        prepareNudge (3, false, 50);
        view.sampleProperties.setSampleRate (1010, true);
        const auto exactFractionalPrompts { prompts };
        view.applyMenuAction (26, {});
        check (prompts == exactFractionalPrompts && view.markerPosition (3) == 600,
               "Exactly 50 ms including fractional endpoint movement requires no confirmation");

        auto checkStaleNudge = [&] (std::function<void ()> change)
        {
            prepareNudge (1, true);
            view.applyMenuAction (23, {});
            check (view.markerPosition (1) == 600, "Stale zero-crossing fixture starts with an unapplied move");
            change ();
            const auto changed { view.zoneProperties.getValueTree ().createCopy () };
            answer (true);
            check (view.zoneProperties.getValueTree ().isEquivalentTo (changed), "Stale zero-crossing approval cannot alter changed editor state");
        };
        checkStaleNudge ([&] { view.zoneProperties.setSampleEnd (610, true); view.zoneProperties.setSampleEnd (600, true); });
        checkStaleNudge ([&] { view.zoneProperties.setLoopLength (290, true); });
        checkStaleNudge ([&] { view.zoneProperties.setSide (0, true); view.zoneProperties.setSide (1, true); });
        checkStaleNudge ([&] { view.setEnabled (false); view.setEnabled (true); });
        checkStaleNudge ([&] { channel.setLoopLengthIsEnd (true, true); channel.setLoopLengthIsEnd (false, true); });
        checkStaleNudge ([&] { view.sampleProperties.setSampleRate (2000, true); view.sampleProperties.setSampleRate (1000, true); });
        checkStaleNudge ([&] { view.sampleProperties.setStatus (SampleStatus::doesNotExist, true); view.sampleProperties.setStatus (SampleStatus::exists, true); });
        checkStaleNudge ([&] { const auto file { view.zoneProperties.getSample () }; view.zoneProperties.setSample ("replaced.wav", true); view.zoneProperties.setSample (file, true); });
        checkStaleNudge ([&] { view.setZone (2); view.setZone (1); });
        prepareNudge (1, true);
        view.applyMenuAction (23, {});
        const auto previousZeroAnswer { answer };
        view.applyMenuAction (23, {});
        previousZeroAnswer (true);
        check (view.markerPosition (1) == 600, "A new zero-crossing request invalidates its previous outstanding approval");
        answer (false);
        for (const auto matchFirst : { false, true })
        {
            prepareMatch (1, true);
            view.applyMenuAction (matchFirst ? 43 : 23, {});
            const auto supersededAnswer { answer };
            const auto beforeReplacement { prompts };
            view.applyMenuAction (matchFirst ? 23 : 43, {});
            check (prompts == beforeReplacement + 1, "Replacing match with nudge, or nudge with match, starts a new confirmation");
            supersededAnswer (true);
            check (view.markerPosition (1) == 600, "Boundary matching and zero-crossing requests supersede one another's approvals");
            answer (false);
        }
        prepareNudge (1, true);
        std::function<void (bool)> destroyedZeroAnswer;
        {
            auto temporary { std::make_unique<WaveformDisplay> () };
            temporary->init (channelTree, root);
            temporary->setZone (1);
            temporary->confirmBoundaryMatch = [&] (const juce::String&, std::function<void (bool)> callback) { destroyedZeroAnswer = std::move (callback); };
            temporary->applyMenuAction (23, {});
            check (static_cast<bool> (destroyedZeroAnswer), "Destruction fixture obtains a zero-crossing confirmation");
        }
        destroyedZeroAnswer (true);
        check (view.markerPosition (1) == 600, "Destroyed waveform views safely discard zero-crossing approvals");
        view.setEnabled (false);
        const auto disabledNudge { view.zoneProperties.getValueTree ().createCopy () };
        const auto disabledPrompts { prompts };
        for (auto action { 20 }; action < 28; ++action) view.applyMenuAction (action, {});
        check (prompts == disabledPrompts && view.zoneProperties.getValueTree ().isEquivalentTo (disabledNudge),
               "Disabled stereo-right editing neither prompts for nor applies zero crossings");
        view.setEnabled (true);
        view.zoneProperties.setSide (0, true);
        view.sampleProperties.setSampleRate (1000, true);

        view.zoneProperties.setSampleStart (200, true);
        view.zoneProperties.setSampleEnd (400, true);
        view.zoneProperties.setLoopStart (600, true);
        view.zoneProperties.setLoopLength (250.5, true);
        view.resetZoom ();
        channel.setLoopMode (0, true);
        auto bridge { view.markerOverlay.loopExtensionBounds () };
        check (std::abs (bridge.getX () - view.waveform.sampleToX (400)) < 0.01f &&
               std::abs (bridge.getRight () - view.waveform.sampleToX (600)) < 0.01f,
               "Separated bridge remains visible with hardware No Loop; audition looping is independent");
        for (const auto height : { 170, 560 })
        {
            view.setExpanded (height == 560);
            view.setSize (760, height);
            const auto rendered { view.markerOverlay.createComponentSnapshot (view.markerOverlay.getLocalBounds ()) };
            const auto x { juce::roundToInt (view.waveform.sampleToX (500)) };
            const auto middle { rendered.getHeight () / 2 };
            bool varies { false };
            for (auto y { middle }; y < middle + 16; ++y)
                varies |= rendered.getPixelAt (x, y) != rendered.getPixelAt (x, middle);
            check (varies, "No Loop bridge paints visible bands in compact and expanded views");
        }
        view.setExpanded (false);
        view.setSize (760, 170);
        channel.setLoopMode (1, true);
        auto extension { view.markerOverlay.loopExtensionBounds () };
        check (std::abs (extension.getX () - view.waveform.sampleToX (400)) < 0.01f &&
               std::abs (extension.getRight () - view.waveform.sampleToX (850.5)) < 0.01f, "Loop stripes span Sample End to exact Loop End, including the gap before Loop Start");
        view.setLoopSelected (false);
        const auto sampleSelectedExtension { view.markerOverlay.loopExtensionBounds () };
        view.setLoopSelected (true);
        check (view.markerOverlay.loopExtensionBounds () == sampleSelectedExtension, "Hardware loop extension is independent of audition region selection");
        channel.setLoopMode (2, true);
        check (view.markerOverlay.loopExtensionBounds () == extension, "Loop/Release also shows the enabled loop extent");
        const auto hatched { view.markerOverlay.createComponentSnapshot (view.markerOverlay.getLocalBounds ()) };
        const auto gapX { juce::roundToInt (view.waveform.sampleToX (500)) };
        const auto outsideX { juce::roundToInt (view.waveform.sampleToX (950)) };
        const auto centre { hatched.getHeight () / 2 };
        auto bandVariation { false };
        for (auto y { centre - 12 }; y < centre + 12; ++y)
        {
            bandVariation = bandVariation || hatched.getPixelAt (gapX, y) != hatched.getPixelAt (gapX, centre);
            check (hatched.getPixelAt (outsideX, y) == hatched.getPixelAt (outsideX, centre), "Ordinary unused audio stays uniformly dimmed");
        }
        check (bandVariation, "Loop extension actually paints contrasting diagonal bands");
        view.waveform.setVisibleRange (450, 300);
        extension = view.markerOverlay.loopExtensionBounds ();
        check (extension.getX () == 0.0f && extension.getWidth () == view.markerOverlay.getWidth (), "Hatching clips correctly when zoomed into the extension");
        view.waveform.setVisibleRange (900, 100);
        check (view.markerOverlay.loopExtensionBounds ().isEmpty (), "Offscreen extension does not shade unrelated audio");
        view.resetZoom ();
        view.setZone (2);
        check (view.markerOverlay.loopExtensionBounds ().isEmpty (), "Changing zones does not carry the previous extension");
        view.setZone (1);
        view.sampleProperties.setStatus (SampleStatus::doesNotExist, true);
        check (view.markerOverlay.loopExtensionBounds ().isEmpty (), "Unloading a sample clears its loop extension");
        view.sampleProperties.setStatus (SampleStatus::exists, true);
        view.zoneProperties.setLoopStart (300, true);
        view.zoneProperties.setLoopLength (50, true);
        check (view.markerOverlay.loopExtensionBounds ().isEmpty (), "Loops ending before Sample End retain ordinary selection shading");
        view.zoneProperties.setLoopLength (200, true);
        check (! view.markerOverlay.loopExtensionBounds ().isEmpty (), "A loop straddling Sample End also identifies its extended portion");
        channel.setLoopMode (0, true);

        const auto artifacts { juce::SystemStats::getEnvironmentVariable ("A8MANAGER_TEST_ARTIFACTS", {}) };
        if (artifacts.isNotEmpty ())
        {
            // Restore the overview fixture after the deliberately DC-offset
            // join checks, keeping the optional screenshots useful for layout QA.
            for (auto frame { 0 }; frame < 1000; ++frame)
            {
                audio.setSample (0, frame, frame % 100 < 50 ? 0.4f : -0.4f);
                audio.setSample (1, frame, frame % 160 < 80 ? 0.2f : -0.2f);
            }
            view.zoneProperties.setSampleStart (490, true);
            view.zoneProperties.setSampleEnd (510, true);
            view.zoneProperties.setLoopStart (495, true);
            view.zoneProperties.setLoopLength (10.0, true);
            view.setLoopSelected (true);
            view.updateAudioSource ();
            view.resetZoom ();
            const juce::File directory { artifacts };
            directory.createDirectory ();
            for (const auto height : { 170, 560 })
            {
                view.setExpanded (height == 560);
                view.setSize (760, height);
                auto stream { directory.getChildFile (height == 170 ? "waveform-compact.png" : "waveform-expanded.png").createOutputStream () };
                check (stream != nullptr && stream->setPosition (0), "Open waveform render artifact");
                check (juce::PNGImageFormat ().writeImageToStream (view.createComponentSnapshot (view.getLocalBounds (), true, 1.5f), *stream), "Write waveform render artifact");
                stream->truncate ();
            }
            view.zoneProperties.setSampleStart (100, true);
            view.zoneProperties.setSampleEnd (350, true);
            view.zoneProperties.setLoopStart (550, true);
            view.zoneProperties.setLoopLength (250, true);
            channel.setLoopMode (1, true);
            view.setLoopSelected (false);
            view.setExpanded (false);
            view.setSize (760, 220);
            view.resetZoom ();
            auto stream { directory.getChildFile ("waveform-loop-extension.png").createOutputStream () };
            check (stream != nullptr && stream->setPosition (0), "Open striped loop-extension artifact");
            check (juce::PNGImageFormat ().writeImageToStream (view.createComponentSnapshot (view.getLocalBounds (), true, 1.5f), *stream), "Write striped loop-extension artifact");
            stream->truncate ();
            stream.reset ();
            channel.setLoopMode (0, true);
            for (const auto height : { 170, 560 })
            {
                view.setExpanded (height == 560);
                view.setSize (760, height);
                auto bridgeStream { directory.getChildFile (height == 170 ? "waveform-bridge-compact.png" : "waveform-bridge-expanded.png").createOutputStream () };
                check (bridgeStream != nullptr && bridgeStream->setPosition (0), "Open No Loop bridge artifact");
                check (juce::PNGImageFormat ().writeImageToStream (view.createComponentSnapshot (view.getLocalBounds (), true, 1.5f), *bridgeStream), "Write No Loop bridge artifact");
                bridgeStream->truncate ();
            }
        }
        // Schedule the production continuation callbacks in a deterministic
        // FIFO, including buffers freed before the next batch. This is portable
        // even when a console test runner has no native application event loop.
        auto makeLongAudio = []
        {
            auto buffer { std::make_unique<juce::AudioBuffer<float>> (1, 200000) };
            for (auto i { 0 }; i < buffer->getNumSamples (); ++i) buffer->setSample (0, i, 0.6f);
            buffer->setSample (0, 100, -0.25f);
            buffer->setSample (0, 199998, -0.25f);
            return buffer;
        };
        auto makeLongView = [&] (int zone, juce::AudioBuffer<float>* buffer)
        {
            ZoneProperties z (channel.getZoneVT (zone), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            z.setSampleStart (100, false);
            z.setSampleEnd (1000, false);
            z.setSide (0, false);
            SampleProperties s (manager.getSamplePropertiesVT (0, zone), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
            s.setStatus (SampleStatus::uninitialized, false);
            s.setAudioBufferPtr (buffer, false);
            s.setLengthInSamples (buffer->getNumSamples (), false);
            s.setNumChannels (1, false);
            s.setSampleRate (1000, false);
            s.setStatus (SampleStatus::exists, false);
            auto result { std::make_unique<WaveformDisplay> () };
            result->setSize (760, 220);
            result->init (channelTree, root);
            result->setZone (zone);
            return result;
        };
        auto successAudio { makeLongAudio () }, unloadedAudio { makeLongAudio () }, destroyedAudio { makeLongAudio () };
        auto successView { makeLongView (3, successAudio.get ()) };
        auto unloadedView { makeLongView (4, unloadedAudio.get ()) };
        auto destroyedView { makeLongView (5, destroyedAudio.get ()) };
        std::deque<std::function<void ()>> scheduled;
        auto schedule = [&] (std::function<void ()> callback) { scheduled.push_back (std::move (callback)); };
        successView->scheduleBoundaryMatch = unloadedView->scheduleBoundaryMatch = destroyedView->scheduleBoundaryMatch = schedule;
        auto queuedApprovals { 0 }, invalidPrompts { 0 };
        successView->confirmBoundaryMatch = [&] (const juce::String&, std::function<void (bool)> callback)
        {
            ++queuedApprovals;
            callback (true);
        };
        unloadedView->confirmBoundaryMatch = destroyedView->confirmBoundaryMatch =
            [&] (const juce::String&, std::function<void (bool)>) { ++invalidPrompts; };
        successView->applyMenuAction (43, {});
        unloadedView->applyMenuAction (43, {});
        destroyedView->applyMenuAction (43, {});
        check (scheduled.size () == 3 && queuedApprovals == 0 && successView->markerPosition (1) == 1000 &&
               successView->durationInfo.getText ().contains ("Searching"),
               "Large-file menu action yields before finishing instead of blocking or applying a partial result");
        unloadedView->sampleProperties.setStatus (SampleStatus::doesNotExist, true);
        unloadedView->sampleProperties.setAudioBufferPtr (nullptr, true);
        unloadedAudio.reset ();
        destroyedView.reset ();
        SampleProperties destroyedSample (manager.getSamplePropertiesVT (0, 5), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
        destroyedSample.setStatus (SampleStatus::doesNotExist, false);
        destroyedSample.setAudioBufferPtr (nullptr, false);
        destroyedAudio.reset ();
        auto dispatched { 0 };
        while (! scheduled.empty () && dispatched < 16)
        {
            auto callback { std::move (scheduled.front ()) };
            scheduled.pop_front ();
            callback ();
            if (++dispatched == 1)
                check (successView->markerPosition (1) == 1000 && queuedApprovals == 0 && scheduled.size () == 3,
                       "Each queued batch yields again before the distant candidate is reached");
        }
        check (scheduled.empty () && dispatched == 5 && queuedApprovals == 1 && successView->markerPosition (1) == 199999,
               "Queued batches complete a distant search and apply its approved candidate");
        check (invalidPrompts == 0 && unloadedView->zoneProperties.getSampleEnd () == 1000,
               "Queued searches cancel safely after source unload/free or view destruction without prompting or editing");

        view.setLookAndFeel (nullptr);
        std::cout << "PASS: waveform menus, pitch-adjusted durations, comma formatting, zero crossings, region selection/move hit bounds, label collisions, zoom and expanded zone switching\n";
    }
};

void testWaveformWorkflow () { WaveformTestAccess::run (); }
