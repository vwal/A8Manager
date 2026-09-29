#pragma once

#include "WaveformDesign.h"

// Detached, opt-in diagnostic exports. This never plays audio or assigns a live
// preset; CV stays explicitly tagged and every used channel has its mix muted.
namespace HardwareTestOutput
{
    enum class Signal { currentDesign, audioTone, cvLevels, cvSine, cvRamp };

    struct Settings
    {
        Signal signal { Signal::cvLevels };
        WaveformDesign::Settings design;
        double durationSeconds { 10.0 };
        double level { 0.1 }; // Digital full-scale fraction, not volts.
        int presetNumber { 1 };
    };

    struct ExportResult
    {
        juce::File folder, preset, manifest;
        juce::Array<juce::File> waves; // Source voices first; reference last.
    };

    juce::Result validate (const Settings& settings);
    juce::Result exportPackage (const Settings& settings, const juce::File& parentFolder,
                                const juce::String& name, ExportResult& result);
}
