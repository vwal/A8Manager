#pragma once

#include "WaveformDesign.h"
#include <memory>

// Monitor-only source for the existing audio device. It never edits a preset,
// exports files, or routes modulation/CV designs to speakers.
class WaveformAudition
{
public:
    class Payload
    {
    public:
        const WaveformDesign::Settings& getSettings () const noexcept { return settings; }
        int getCycleFrames () const noexcept { return frames; }
        int getVoiceCount () const noexcept { return static_cast<int> (voices.size ()); }
    private:
        friend class WaveformAudition;
        Payload () = default;
        struct Table { int harmonics { 1 }; std::vector<float> samples; };
        struct Voice { double detune { 1.0 }, left { 0.0 }, right { 0.0 }; std::vector<Table> tables; };
        WaveformDesign::Settings settings;
        int frames { 0 };
        std::vector<Voice> voices;
    };
    using PayloadPtr = std::shared_ptr<const Payload>;

    WaveformAudition ();
    ~WaveformAudition ();

    // Worker/non-audio thread only: copies the already-rendered cycle and builds
    // band-limited mipmaps. Does not regenerate the design or modify the render.
    static juce::Result preparePayload (const WaveformDesign::Settings& settings,
                                       const WaveformDesign::Render& rendered, PayloadPtr& payload);
    // Non-audio thread only. nullptr invalidates readiness and ramps playback
    // down; publishing a new valid payload never starts a stopped monitor.
    void setPayload (PayloadPtr payload);
    void prepareToPlay (double deviceSampleRate);
    juce::Result start ();
    void setPlaying (bool playing);
    void stopImmediately ();
    void setMonitorGain (double gain); // linear 0..1; default -18 dB
    juce::Result setTransposeSemitones (double semitones); // monitor-only, -48..48
    bool isActive () const noexcept; // includes the stop ramp
    bool isReady () const;

    // Audio callback: true means the monitor owns the active output region,
    // including ramps/contention silence. No allocations, buffer destruction,
    // blocking locks, UI callbacks or property writes occur here.
    bool process (const juce::AudioSourceChannelInfo& output) noexcept;

private:
    struct State;
    std::unique_ptr<State> state;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformAudition)
};
