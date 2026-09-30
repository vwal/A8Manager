#include "GUI/Assimil8or/Editor/Waveform/RegionMove.h"
#include "GUI/Assimil8or/Editor/Waveform/RegionMoveWaveform.h"
#include "GUI/Assimil8or/Editor/Waveform/WaveformMarkers.h"
#include <iostream>
#include <limits>
#include <stdexcept>

void testRegionMove ()
{
    auto check = [] (bool condition, const char* message)
    {
        if (! condition) throw std::runtime_error (message);
    };
    ZoneProperties zone (ZoneProperties::create (1), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::yes);
    zone.setSample ("test.wav", false);
    zone.setSampleStart (200, false);
    zone.setSampleEnd (400, false);
    zone.setLoopStart (250, false);
    zone.setLoopLength (100.5, false);
    zone.setMinVoltage (-2.5, false);
    const auto original { zone.getValueTree ().createCopy () };
    using namespace RegionMove;
    const auto sample { capture (zone, Target::sample, 1000) };
    check (sample.has_value (), "Capture sample region");
    auto validDuringCallbacks { true };
    auto observe = [&] (auto)
    {
        validDuringCallbacks = validDuringCallbacks && ZoneSampleRanges::resolve (ZoneSampleRanges::read (zone), 1000).loopValid;
    };
    zone.onSampleStartChange = observe;
    zone.onSampleEndChange = observe;
    zone.onLoopStartChange = observe;
    zone.onLoopLengthChange = observe;
    apply (zone, *sample, 500.0);
    check (zone.getSampleStart () == 700 && zone.getSampleEnd () == 900 && zone.getLoopStart () == 700 && zone.getLoopLength () == 100.5,
           "Moving SAMPLE farther than its length pushes its explicit loop inside without resizing either");
    apply (zone, *sample, 5000.0);
    check (zone.getSampleStart () == 800 && ! zone.getSampleEnd ().has_value (), "EOF clamps the pair without shrinking");
    apply (zone, *sample, -5000.0);
    check (! zone.getSampleStart ().has_value () && zone.getSampleEnd () == 200, "Start-of-file clamps without shrinking");
    apply (zone, *sample, 0.0);
    check (zone.getValueTree ().isEquivalentTo (original), "Returning to drag origin restores the exact original preset");
    check (validDuringCallbacks, "Observers must never see inverted sample bounds or a loop outside SAMPLE");

    const auto loop { capture (zone, Target::loop, 1000) };
    check (loop.has_value (), "Capture fractional loop region");
    apply (zone, *loop, 5000.0);
    check (zone.getLoopStart () == 299 && zone.getLoopLength () == 100.5, "Fractional loop keeps its exact length at SAMPLE END");
    apply (zone, *loop, -5000.0);
    check (zone.getLoopStart () == 200 && zone.getLoopLength () == 100.5, "Loop moves left to SAMPLE START without changing length");
    apply (zone, *loop, 0.0);
    check (zone.getValueTree ().isEquivalentTo (original), "Loop move leaves sample region, voltage and other settings unchanged");
    zone.setLoopStart (300, false);
    zone.setLoopLength (-1.0, false);
    const auto implicitLoop { capture (zone, Target::loop, 1000) };
    apply (zone, *implicitLoop, -300.0);
    check (zone.getLoopStart () == 200 && zone.getLoopLength () == 100.0, "Implicit SAMPLE END length becomes explicit and stays fixed on a genuine move");
    zone.setLoopLength (4.0, false);
    const auto tinyLoop { capture (zone, Target::loop, 1000) };
    apply (zone, *tinyLoop, 5000.0);
    check (zone.getLoopStart () == 396 && zone.getLoopLength () == 4.0, "Minimum loop moves safely to SAMPLE END");
    zone.setLoopLength (3.0, false);
    check (! capture (zone, Target::loop, 1000), "Reject sub-minimum loop");
    zone.setLoopLength (std::numeric_limits<double>::infinity (), false);
    check (! capture (zone, Target::loop, 1000), "Reject non-finite length");
    check (! capture (zone, Target::sample, 0), "Missing audio cannot move");
    zone.setSampleStart (500, false);
    check (! capture (zone, Target::sample, 1000), "Reject inverted sample range");
    zone.setSampleStart (-1, false);
    zone.setSampleEnd (-1, false);
    zone.setLoopStart (-1, false);
    zone.setLoopLength (-1.0, false);
    const auto full { capture (zone, Target::sample, 1000) };
    apply (zone, *full, 100.0);
    check (! zone.getSampleStart () && ! zone.getSampleEnd (), "Full-file region cannot move or acquire unnecessary overrides");
    zone.setSampleStart (200, false);
    zone.setSampleEnd (400, false);
    const auto implicitSample { capture (zone, Target::sample, 1000) };
    apply (zone, *implicitSample, 100.0);
    const auto implicitMoved { ZoneSampleRanges::resolve (ZoneSampleRanges::read (zone), 1000) };
    check (implicitMoved.implicitLoop && implicitMoved.loopStart == 300 && implicitMoved.loopEnd () == 500,
           "Unset loop automatically follows a moved SAMPLE without creating overrides");
    const auto fullSampleLoop { capture (zone, Target::loop, 1000) };
    apply (zone, *fullSampleLoop, 100.0);
    check (! zone.getLoopStart () && ! zone.getLoopLength (), "A sample-wide implicit loop cannot move or acquire unnecessary overrides");
    zone.setSampleEnd (302, false);
    check (! capture (zone, Target::sample, 1000), "Reject sub-four-frame SAMPLE regions");

    // Exercise the actual gesture component and marker hit routing, not copied
    // pointer arithmetic. 1000 samples / 100 pixels = ten samples per pixel.
    juce::AudioBuffer<float> audio (1, 1000);
    audio.clear ();
    RegionMoveWaveform waveform;
    waveform.setBounds (0, 0, 100, 80);
    waveform.setAudioBuffer (&audio);
    waveform.zoomToFit ();
    const auto moveModifiers { juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier };
    auto event = [&] (float x, int flags = juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier)
    {
        return juce::MouseEvent (juce::Desktop::getInstance ().getMainMouseSource (), { x, 40.0f }, juce::ModifierKeys (flags),
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &waveform, &waveform, juce::Time::getCurrentTime (),
            { 20.0f, 40.0f }, juce::Time::getCurrentTime (), 1, x != 20.0f);
    };
    auto begins { 0 }, moves { 0 };
    double delta { 0.0 };
    waveform.onBeginRegionMove = [&] (juce::Point<float>) { ++begins; return true; };
    waveform.onMoveRegion = [&] (double amount) { ++moves; delta = amount; };
    waveform.mouseDown (event (20));
    waveform.mouseDrag (event (30));
    check (std::abs (delta - 100.0) < 0.001, "Normal move drag follows waveform scale");
    waveform.mouseDrag (event (40, moveModifiers | juce::ModifierKeys::shiftModifier));
    check (std::abs (delta - 110.0) < 0.001, "Shift makes movement ten times finer without jumping");
    waveform.mouseUp (event (40));
    waveform.mouseDrag (event (80));
    check (moves == 2, "Mouse-up ends region editing");
    waveform.mouseDown (event (20));
    waveform.cancelDrag ();
    waveform.mouseDrag (event (50));
    check (moves == 2, "Changing zone or sample can cancel a pending move");
    waveform.mouseDown (event (20, juce::ModifierKeys::leftButtonModifier));
    waveform.mouseDrag (event (10, juce::ModifierKeys::leftButtonModifier));
    check (begins == 2 && moves == 2, "Plain drag pans rather than editing");
    waveform.mouseDown (event (20, juce::ModifierKeys::rightButtonModifier));
    waveform.mouseDrag (event (25, juce::ModifierKeys::rightButtonModifier));
    check (begins == 2 && moves == 2, "Right-drag remains zoom in move mode");
    waveform.setEnabled (false);
    waveform.mouseDown (event (20));
    waveform.mouseDrag (event (50));
    check (begins == 2 && moves == 2, "Disabled stereo companion cannot move regions");
    waveform.setEnabled (true);
    waveform.zoomToFit ();
    RegionMarkerOverlay markers;
    markers.attach (&waveform);
    markers.setBounds (waveform.getBounds ());
    MarkerOverlay::Style style;
    style.placement = MarkerOverlay::HandlePlacement::top;
    markers.addMarker ({ "Start", 500.0, style });
    check (markers.hitTest (50, 3), "Normal marker handle must receive clicks");
    markers.setInterceptsMouseClicks (false, false);
    check (! markers.hitTest (50, 3), "Move mode routes handle clicks through to the waveform");
    waveform.setAudioBuffer (nullptr);
    std::cout << "PASS: fixed-length moves, loops constrained to SAMPLE, explicit-loop pushes, fractional/implicit/tiny loops, notifications, fine drag and cancellation\n";
}
