#include "GUI/Assimil8or/Editor/Waveform/WaveformDisplay.h"
#include "oolib/Properties/RuntimeRootProperties.h"
#include <iostream>
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
        check (view.buildWaveformMenu ({}).getNumItems () == 4 && view.buildWaveformMenu (200.0).getNumItems () == 5, "Gear and context menus offer separate boundary matching; only context offers Set Marker Here");

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
        view.applyMenuAction (43, {});
        check (selectedLoop && view.markerPosition (2) == 300 && view.markerPosition (3) == 562 &&
               view.markerPosition (1) == 600, "Match Loop End keeps Loop Start and sample boundaries fixed");
        view.applyMenuAction (41, {});
        check (! selectedLoop && view.markerPosition (0) == 300 && view.markerPosition (1) == 562,
               "Match Sample End selects SAMPLE and retains its start");
        audio.setSample (0, 300, 0.0f);
        audio.setSample (0, 318, -0.25f);
        view.zoneProperties.setLoopLength (262.5, true);
        view.applyMenuAction (42, {});
        check (selectedLoop && view.markerPosition (2) == 318 && view.markerPosition (3) == 562.5 &&
               view.zoneProperties.getLoopLength () == 244.5 && ! channel.getLoopLengthIsEnd (),
               "Match Loop Start keeps the fractional end fixed even in Length mode, without changing mode");
        view.applyMenuAction (40, {});
        check (! selectedLoop && view.markerPosition (0) == 318 && view.markerPosition (1) == 562,
               "Match Sample Start retains its end and selects SAMPLE");
        view.applyMenuAction (40, {});
        check (view.markerPosition (0) == 318 && view.durationInfo.getText ().contains ("unchanged"), "No improvement reports a no-op");
        view.setEnabled (false);
        const auto matched { view.zoneProperties.getValueTree ().createCopy () };
        view.applyMenuAction (43, {});
        check (view.zoneProperties.getValueTree ().isEquivalentTo (matched), "Disabled channels cannot invoke boundary matching");
        view.setEnabled (true);

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
        }
        view.setLookAndFeel (nullptr);
        std::cout << "PASS: waveform menus, pitch-adjusted durations, comma formatting, zero crossings, region selection/move hit bounds, label collisions, zoom and expanded zone switching\n";
    }
};

void testWaveformWorkflow () { WaveformTestAccess::run (); }
