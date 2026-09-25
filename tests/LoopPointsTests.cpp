#include "GUI/Assimil8or/Editor/LoopPoints/LoopPointsView.h"
#include "GUI/ModernTheme.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

void testLoopPoints ()
{
    auto check = [] (bool ok, const char* message)
    {
        if (! ok) throw std::runtime_error (message);
    };
    LoopPointsView view;
    view.setBounds (0, 0, 162, 64);
    juce::AudioBuffer<float> audio (2, 256);
    audio.clear ();
    auto fill = [&] (float level)
    {
        for (auto i { 0 }; i < audio.getNumSamples (); ++i)
            audio.setSample (0, i, level * (i < 128 ? 1.0f : 0.25f) * std::sin (juce::MathConstants<float>::twoPi * i / 20.0f));
    };
    auto snapshot = [&] () { return view.createComponentSnapshot (view.getLocalBounds ()); };
    auto traceHeight = [] (const juce::Image& image, bool left)
    {
        auto top { image.getHeight () }, bottom { 0 };
        for (auto x { left ? 2 : image.getWidth () / 2 + 2 }; x < (left ? image.getWidth () / 2 - 2 : image.getWidth () - 2); ++x)
            for (auto y { 14 }; y < image.getHeight () - 2; ++y)
            {
                const auto c { image.getPixelAt (x, y) };
                const auto isTrace { left ? c.getRed () > 180 && c.getGreen () > 130 && c.getBlue () < 160
                                         : c.getRed () < 120 && c.getGreen () > 160 && c.getBlue () > 120 };
                if (isTrace) { top = std::min (top, y); bottom = std::max (bottom, y); }
            }
        return std::max (0, bottom - top);
    };
    view.setAudioBuffer (&audio);
    view.setLoopPoints (0, 256, 0); // the selected end is the exact end of the allocation
    fill (0.001f);
    const auto quiet { snapshot () };
    check (traceHeight (quiet, false) > 32, "Quiet waveform must be visually normalised");
    check (traceHeight (quiet, true) < traceHeight (quiet, false) * 0.5,
           "Both halves must share gain so a level mismatch remains visible");
    fill (1.0f);
    const auto loud { snapshot () };
    check (std::abs (traceHeight (quiet, false) - traceHeight (loud, false)) <= 1,
           "Normalisation must give quiet and loud audio comparable visibility");
    const auto sampleBefore { audio.getSample (0, 17) };
    snapshot ();
    check (sampleBefore == audio.getSample (0, 17), "Drawing must not alter audio samples");

    view.setLoopPoints (252, 4, 0);
    check (traceHeight (snapshot (), false) > 0, "Minimum four-sample loops at EOF must draw safely");
    view.setLoopPoints (0, 256, 1);
    check (traceHeight (snapshot (), false) <= 2, "Silent selected channel must remain flat (apart from stroke thickness)");
    for (const auto offset : { juce::int64 (-1), juce::int64 (253), std::numeric_limits<juce::int64>::max () })
    {
        view.setLoopPoints (offset, 4, 0);
        check (traceHeight (snapshot (), false) == 0, "Invalid ranges must not read outside the buffer");
    }
    view.setLoopPoints (0, 256, -1);
    check (traceHeight (snapshot (), false) == 0, "Negative channel index must be safe");
    view.setLoopPoints (0, 256, 2);
    check (traceHeight (snapshot (), false) == 0, "Missing channel index must be safe");
    view.setLoopPoints (0, 0, 0);
    snapshot ();
    audio.setSample (0, 0, std::numeric_limits<float>::quiet_NaN ());
    audio.setSample (0, 255, std::numeric_limits<float>::infinity ());
    view.setLoopPoints (0, 256, 0);
    snapshot ();
    view.setAudioBuffer (nullptr);
    check (traceHeight (snapshot (), false) == 0, "Missing audio must clear the trace");

    // Optional visual artifact; normal CTest runs do not write any image files.
    const auto artifactDirectory { juce::SystemStats::getEnvironmentVariable ("A8MANAGER_TEST_ARTIFACTS", {}) };
    if (artifactDirectory.isNotEmpty ())
    {
        const juce::File directory { artifactDirectory };
        directory.createDirectory ();
        auto stream { directory.getChildFile ("loop-preview.png").createOutputStream () };
        check (stream != nullptr && stream->setPosition (0), "Open loop-preview artifact");
        check (juce::PNGImageFormat ().writeImageToStream (quiet, *stream), "Write loop-preview artifact");
        check (stream->truncate ().wasOk (), "Truncate any previous artifact tail");
    }
    std::cout << "PASS: visible quiet waveform, shared gain, unchanged audio, tiny/EOF loops, silence and invalid inputs\n";
}
