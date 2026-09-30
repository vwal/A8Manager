#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>

// Pure generation model shared by the workspace, WAV exporter and tests.
// All CV amplitudes are fractions of digital full scale, never assumed volts.
namespace WaveformDesign
{
    enum class Mode { oscillator, modulation, layers };
    enum class Shape { sine, triangle, saw, pulse, trapezoid, envelope, steps, drawn, random, imported };
    enum class Playback { oneShot, loop, gatedLoop };

    struct Voice
    {
        double detuneCents { 0.0 };
        double phaseDegrees { 0.0 };
        double pan { 0.0 };
        double level { 1.0 };
    };

    struct Settings
    {
        Mode mode { Mode::oscillator };
        Shape shape { Shape::sine };
        Playback playback { Playback::gatedLoop };
        double sampleRate { 48000.0 };
        int cycleFrames { 512 };
        double durationSeconds { 2.0 };
        double cycles { 1.0 };
        double amplitude { 0.8 };
        double offset { 0.0 };
        bool unipolar { false };
        bool invert { false };
        double phaseDegrees { 0.0 };
        double symmetry { 0.5 };
        double pulseWidth { 0.5 };
        int harmonics { 32 };
        double brightness { 0.8 };
        double drive { 0.0 };
        double fold { 0.0 };
        double attack { 0.1 };
        double decay { 0.2 };
        double sustain { 0.6 };
        double release { 0.3 };
        double curve { 0.0 };
        double smoothing { 0.0 };
        int stepCount { 8 };
        std::array<double, 16> steps { 0.0, 0.5, -0.25, 1.0, -0.5, 0.25, -1.0, 0.75 };
        std::array<double, 33> drawn {};
        int voiceCount { 1 };
        std::array<Voice, 8> voices {};
        unsigned int seed { 1 };
        // A detached, single mono audio cycle. Embedded in version-2 recipes;
        // no path or original WAV is needed to render/recall the imported shape.
        std::vector<double> importedCycle;
        juce::String importedCycleName;
        // Optional user-measured positive full-scale output voltage; 0 = unknown.
        double measuredFullScaleVolts { 0.0 };
    };

    struct Render
    {
        std::vector<juce::AudioBuffer<float>> voices;
        double sampleRate { 48000.0 };
        juce::int64 frames { 0 };
        double peak { 0.0 };
        double dc { 0.0 };
        double boundaryJump { 0.0 };
        juce::int64 clippedSamples { 0 };
        juce::StringArray warnings;
    };

    Settings startingPoint (Mode mode, Shape shape);
    void spreadVoices (Settings& settings, int count, double detuneCents, double phaseSpreadDegrees, double panSpread);
    juce::Result validate (const Settings& settings);
    juce::Result render (const Settings& settings, Render& result);
    juce::String shapeName (Shape shape);
    juce::var toJson (const Settings& settings);
    juce::Result fromJson (const juce::var& json, Settings& settings);
}
