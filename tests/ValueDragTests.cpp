#include "GUI/DragValueEditor.h"
#include <iostream>
#include <stdexcept>

void DebugLog (juce::String, juce::String) {}
void FlushDebugLog () {}
void testRootFolderSelection ();
void testAppearance ();
void testHardwareTestOutput ();
void testHardwareTestOutputUi ();
void testPlayback ();
void testStereoPreview ();
void testEditorRefinements ();
void testLoopPoints ();
void testZoneVoltages ();
void testRegionMove ();
void testWaveformWorkflow ();
void testAudioAudit ();
void testAudioFileSafety ();
void testStereoAssignment ();
void testStereoChannelUi ();
void testPairedZoneEdits ();
void testWaveformDesign ();
void testWaveformDesignExport ();
void testWaveformDesignAssignment ();
void testSharedPresetSession ();
void testChannelCvSafety ();
void testGeneratedSidecarValidator ();
void testChannelPurgeUi ();
void testSampleLoopSimulation ();
void testSimulationUi ();
void testWaveformDesignRecall ();
void testWaveformWorkspace ();
void testWaveformDuration ();
void testWaveformAudition ();
void testWaveformAuditionRouting ();
void testCvAudition ();
int runPresetWorkflowAuditTests ();

namespace
{
    void check (bool ok, const char* message)
    {
        if (! ok) throw std::runtime_error (message);
    }

    juce::MouseEvent eventFor (juce::Component& component, float x, float y, int flags = juce::ModifierKeys::leftButtonModifier)
    {
        return { juce::Desktop::getInstance ().getMainMouseSource (), { x, y }, juce::ModifierKeys (flags),
                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &component, &component, juce::Time::getCurrentTime (),
                 { 20.0f, 50.0f }, juce::Time::getCurrentTime (), 1, y != 50.0f };
    }

    template <typename Editor, typename Value>
    void testFineGestures (Value increment)
    {
        Editor editor;
        Value value { 50 };
        editor.setBounds (0, 0, 100, 100);
        editor.getMinValueCallback = [] () { return Value { 0 }; };
        editor.getMaxValueCallback = [] () { return Value { 1000 }; };
        editor.getIncrementCallback = [increment] () { return increment; };
        editor.toStringCallback = [] (Value v) { return juce::String (static_cast<double> (v), 4); };
        editor.updateDataCallback = [&] (Value v) { value = v; };
        editor.onDragCallback = [&] (double delta) { editor.setValue (static_cast<Value> (value + delta)); };
        editor.setValue (value);
        auto& component { static_cast<juce::Component&> (editor) };
        const auto left { juce::ModifierKeys::leftButtonModifier };
        const auto fineDrag { left | juce::ModifierKeys::shiftModifier };
        const auto command { juce::ModifierKeys::commandModifier };
        const auto fineWheel { command | juce::ModifierKeys::shiftModifier };
        auto expect = [&] (double expected, const char* message) { check (std::abs (value - expected) < 1e-8, message); };

        component.mouseDown (eventFor (component, 20, 50));
        component.mouseDrag (eventFor (component, 20, -14));
        component.mouseUp (eventFor (component, 20, -14));
        const auto normalChange { value - 50 };
        editor.setValue (50);
        component.mouseDown (eventFor (component, 20, 50, fineDrag));
        component.mouseDrag (eventFor (component, 20, -14, fineDrag));
        component.mouseUp (eventFor (component, 20, -14));
        expect (50 + 4 * increment, "Shift-drag: 64 logical pixels must be four increments");
        check (value - 50 < normalChange, "Fine drag must be slower even than the first normal drag event");

        editor.setValue (50);
        component.mouseDown (eventFor (component, 20, 50, fineDrag));
        for (auto y { 48 }; y >= 34; y -= 2)
            component.mouseDrag (eventFor (component, 20, static_cast<float> (y), fineDrag));
        expect (50 + increment, "Small fine-drag events must accumulate, including initial travel");
        component.mouseDrag (eventFor (component, 20, 50, fineDrag));
        expect (50, "Reversing fine drag must undo the same travel");
        component.mouseDrag (eventFor (component, 20, 50, left));
        expect (50, "Releasing Shift without motion must not jump");
        component.mouseDrag (eventFor (component, 20, 46, left));
        expect (50 + increment, "Normal drag must resume without fine-mode acceleration history");
        component.mouseDrag (eventFor (component, 20, 30, fineDrag));
        expect (50 + 2 * increment, "Shift pressed during a drag must take effect immediately");
        component.mouseUp (eventFor (component, 20, 30));

        juce::MouseWheelDetails wheel {};
        wheel.deltaX = 0.1f; // macOS can redirect Shift+vertical wheel onto X.
        component.mouseWheelMove (eventFor (component, 20, 50, fineWheel), wheel);
        expect (50 + 3 * increment, "Shift-command horizontal wheel must nudge the value");
        wheel.deltaY = 0.1f;
        for (auto event { 0 }; event < 10; ++event)
            component.mouseWheelMove (eventFor (component, 20, 50, fineWheel), wheel);
        expect (50 + 13 * increment, "Fine wheel must stay at one increment and not double-count both axes");
        wheel.isReversed = true;
        component.mouseWheelMove (eventFor (component, 20, 50, fineWheel), wheel);
        expect (50 + 12 * increment, "Fine wheel must respect natural/reversed scrolling");
        wheel.isReversed = false;
        wheel.deltaX = -0.1f;
        wheel.deltaY = 0.0f;
        component.mouseWheelMove (eventFor (component, 20, 50, fineWheel), wheel);
        expect (50 + 11 * increment, "Fine horizontal wheel must support decrementing");
        wheel.deltaX = 0.0f;
        component.mouseWheelMove (eventFor (component, 20, 50, fineWheel), wheel);
        expect (50 + 11 * increment, "A zero wheel event must not change the value");
        wheel.deltaY = 0.1f;
        component.mouseWheelMove (eventFor (component, 20, 50, juce::ModifierKeys::shiftModifier), wheel);
        expect (50 + 11 * increment, "Shift without command must not edit on scroll");
        component.mouseWheelMove (eventFor (component, 20, 50, command), wheel);
        check (value > 50 + 11 * increment, "Ordinary command-wheel must still work");
        editor.setValue (1000);
        component.mouseWheelMove (eventFor (component, 20, 50, fineWheel), wheel);
        expect (1000, "Fine wheel must clamp at the upper bound");
        editor.setValue (0);
        wheel.deltaY = -0.1f;
        component.mouseWheelMove (eventFor (component, 20, 50, fineWheel), wheel);
        expect (0, "Fine wheel must clamp at the lower bound");
        editor.setValue (50);
        editor.setEnabled (false);
        component.mouseWheelMove (eventFor (component, 20, 50, fineWheel), wheel);
        expect (50, "Disabled fields must ignore fine wheel events");
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI initialise;
    try
    {
        if (argc == 2 && juce::String (argv[1]) == "--hardware-test-output") { testHardwareTestOutput (); return 0; }
        if (argc == 2 && juce::String (argv[1]) == "--hardware-test-output-ui") { testHardwareTestOutputUi (); return 0; }
        if (argc == 2 && juce::String (argv[1]) == "--appearance") { testAppearance (); return 0; }
        if (argc == 2 && juce::String (argv[1]) == "--sample-loop-simulation") { testSampleLoopSimulation (); return 0; }
        if (argc == 2 && juce::String (argv[1]) == "--simulation-ui") { testSimulationUi (); return 0; }
        if (argc == 2 && juce::String (argv[1]) == "--channel-purge-ui") { testChannelPurgeUi (); return 0; }
        if (argc == 2 && juce::String (argv[1]) == "--generated-sidecar-validator") { testGeneratedSidecarValidator (); return 0; }
        if (argc == 2 && juce::String (argv[1]) == "--waveform-design-recall") { testWaveformDesignRecall (); return 0; }
        if (argc == 2 && juce::String (argv[1]) == "--shared-preset-session") { testSharedPresetSession (); return 0; }
        if (argc == 2 && juce::String (argv[1]) == "--waveform-design-assignment") { testWaveformDesignAssignment (); return 0; }
        if (argc == 2 && juce::String (argv[1]) == "--channel-cv-safety") { testChannelCvSafety (); return 0; }
        if (argc == 2 && juce::String (argv [1]) == "--stereo-channel-ui")
        {
            testStereoChannelUi ();
            return 0;
        }
        if (argc == 2 && juce::String (argv [1]) == "--stereo-assignment")
        {
            testStereoAssignment ();
            return 0;
        }
        if (argc == 2 && juce::String (argv [1]) == "--audio-audit")
        {
            testAudioAudit ();
            return 0;
        }
        if (argc == 2 && juce::String (argv [1]) == "--audio-file-safety")
        {
            testAudioFileSafety ();
            return 0;
        }
        if (argc == 2 && juce::String (argv [1]) == "--paired-zone-edit")
        {
            testPairedZoneEdits ();
            return 0;
        }
        if (argc == 2 && juce::String (argv [1]) == "--waveform-design") { testWaveformDesign (); return 0; }
        if (argc == 2 && juce::String (argv [1]) == "--waveform-design-export") { testWaveformDesignExport (); return 0; }
        if (argc == 2 && juce::String (argv [1]) == "--waveform-workspace") { testWaveformWorkspace (); return 0; }
        if (argc == 2 && juce::String (argv [1]) == "--waveform-duration") { testWaveformDuration (); return 0; }
        if (argc == 2 && juce::String (argv [1]) == "--waveform-audition") { testWaveformAudition (); return 0; }
        if (argc == 2 && juce::String (argv [1]) == "--waveform-audition-routing") { testWaveformAuditionRouting (); return 0; }
        if (argc == 2 && juce::String (argv [1]) == "--cv-audition") { testCvAudition (); return 0; }
        if (argc == 2 && juce::String (argv [1]) == "--preset-workflow-audit")
        {
            return runPresetWorkflowAuditTests ();
        }
        if (argc == 2 && juce::String (argv [1]) == "--waveform-workflow")
        {
            testWaveformWorkflow ();
            return 0;
        }
        if (argc == 2 && juce::String (argv [1]) == "--zone-voltages")
        {
            testZoneVoltages ();
            return 0;
        }
        if (argc == 2 && juce::String (argv [1]) == "--region-move")
        {
            testRegionMove ();
            return 0;
        }
        if (argc == 2 && juce::String (argv [1]) == "--loop-preview")
        {
            testLoopPoints ();
            return 0;
        }
        if (argc == 2 && juce::String (argv [1]) == "--editor-refinements")
        {
            testEditorRefinements ();
            return 0;
        }
        if (argc == 2 && juce::String (argv [1]) == "--stereo-preview")
        {
            testStereoPreview ();
            return 0;
        }
        if (argc == 2 && juce::String (argv [1]) == "--playback")
        {
            testPlayback ();
            return 0;
        }
        if (argc == 2 && juce::String (argv [1]) == "--root-folder")
        {
            testRootFolderSelection ();
            return 0;
        }
        testFineGestures<DragValueEditorDouble> (0.01);
        testFineGestures<DragValueEditorInt> (1);
        testFineGestures<DragValueEditorInt64> (juce::int64 { 1 });
        DragValueEditorDouble editor;
        auto value { 5.0 };
        editor.setBounds (0, 0, 100, 100);
        editor.getMinValueCallback = [] () { return 0.0; };
        editor.getMaxValueCallback = [] () { return 10.0; };
        editor.getIncrementCallback = [] () { return 0.01; };
        editor.toStringCallback = [] (double v) { return juce::String (v, 2); };
        editor.updateDataCallback = [&] (double v) { value = v; };
        editor.onDragCallback = [&] (double delta) { editor.setValue (value + delta); };
        editor.setValue (value);
        auto& component { static_cast<juce::Component&> (editor) };
        component.mouseDown (eventFor (component, 20, 50));
        component.mouseUp (eventFor (component, 20, 50));
        check (std::abs (value - 5.0) < 1e-9, "A click must not change the value");
        component.mouseDown (eventFor (component, 20, 50));
        component.mouseDrag (eventFor (component, 20, 10));
        component.mouseUp (eventFor (component, 20, 10));
        check (value > 5.0, "Plain upward drag must increase the value");
        const auto dragged { value };
        component.mouseDown (eventFor (component, 20, 50));
        component.mouseDrag (eventFor (component, 70, 50));
        component.mouseUp (eventFor (component, 70, 50));
        check (std::abs (value - dragged) < 1e-9, "Horizontal text selection must not adjust the value");
        editor.setText ("2.50");
        editor.onReturnKey ();
        check (std::abs (value - 2.5) < 1e-9, "Direct entry must still commit");
        component.mouseDown (eventFor (component, 20, 50));
        component.mouseDrag (eventFor (component, 20, 90, juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
        component.mouseUp (eventFor (component, 20, 90));
        check (std::abs (value - 2.48) < 1e-9, "Shift-drag must use fine increments");
        editor.setValue (10.0);
        component.mouseDown (eventFor (component, 20, 50));
        component.mouseDrag (eventFor (component, 20, 10));
        component.mouseUp (eventFor (component, 20, 10));
        check (std::abs (value - 10.0) < 1e-9, "Drag must respect the upper bound");
        juce::MouseWheelDetails wheel {};
        wheel.deltaY = -0.1f;
        component.mouseWheelMove (eventFor (component, 20, 50, 0), wheel);
        check (std::abs (value - 10.0) < 1e-9, "Ordinary workspace scrolling must not change a value");
        editor.setEnabled (false);
        component.mouseDown (eventFor (component, 20, 50));
        component.mouseDrag (eventFor (component, 20, 90));
        component.mouseUp (eventFor (component, 20, 90));
        check (std::abs (value - 10.0) < 1e-9, "Disabled controls must not adjust");
        std::cout << "PASS: direct drag, click, horizontal selection, typing, fine adjustment, clamping, scroll safety and disabled state\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what () << '\n';
        return 1;
    }
}
