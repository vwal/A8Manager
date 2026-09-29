#include "AuditionStretch.h"
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <cmath>

struct AuditionStretch::Impl
{
    // Provide look-ahead without reading outside the selected region. Loops
    // repeat, including regions shorter than an FFT; one-shots zero-pad. An
    // intro-to-loop stream zero-pads before its intro and wraps only its tail.
    struct Input
    {
        const juce::AudioBuffer<float>* buffer { nullptr };
        int start { 0 }, length { 0 };
        juce::int64 position { 0 };
        float fraction { 0.0f };
        bool looping { false };
        int loopOffset { 0 };
        bool hasIntro { false };

        struct Channel
        {
            const Input& input;
            int channel;
            float at (juce::int64 frame) const
            {
                if (input.looping && (! input.hasIntro || frame >= input.length))
                {
                    const auto loopLength { input.length - input.loopOffset };
                    frame = input.loopOffset + ((frame - input.loopOffset) % loopLength + loopLength) % loopLength;
                }
                else if (frame < 0 || frame >= input.length)
                    return 0.0f;
                return input.buffer->getSample (channel, input.start + static_cast<int> (frame));
            }
            float operator[] (int index) const
            {
                const auto frame { input.position + index };
                const auto value { at (frame) };
                return value + input.fraction * (at (frame + 1) - value);
            }
        };
        Channel operator[] (int channel) const { return { *this, channel }; }
    } input;

    signalsmith::stretch::SignalsmithStretch<float> stretch;
    static constexpr int chunkSize { 128 };
    juce::AudioBuffer<float> scratch { 2, chunkSize };
    double inputRemainder { 0.0 };
};

AuditionStretch::AuditionStretch () : impl (std::make_unique<Impl> ()) {}
AuditionStretch::~AuditionStretch () = default;

void AuditionStretch::prepare (double sampleRate)
{
    impl->stretch.presetDefault (2, static_cast<float> (sampleRate));
}

void AuditionStretch::reset (const juce::AudioBuffer<float>& source, int start, int end, double cursor,
                            double speed, double pitchSemitones, bool looping, int loopStart)
{
    jassert (source.getNumChannels () >= 2 && start >= 0 && end > start && end <= source.getNumSamples ());
    auto& state { *impl };
    const auto relative { cursor - start };
    state.input = { &source, start, end - start, static_cast<juce::int64> (std::floor (relative)),
                    static_cast<float> (relative - std::floor (relative)), looping,
                    loopStart >= start && loopStart < end ? loopStart - start : 0, loopStart > start && loopStart < end };
    state.inputRemainder = 0.0;
    state.stretch.setTransposeSemitones (static_cast<float> (pitchSemitones));
    // Compensate both input and output latency. Read-ahead is not audible
    // transport movement, and must not prematurely finish a short one-shot.
    const auto preRoll { state.stretch.outputSeekLength (static_cast<float> (speed)) };
    state.stretch.outputSeek (state.input, preRoll);
    state.input.position += preRoll;
}

void AuditionStretch::process (const juce::AudioSourceChannelInfo& output, double speed)
{
    auto& state { *impl };
    for (auto written { 0 }; written < output.numSamples;)
    {
        const auto count { juce::jmin (Impl::chunkSize, output.numSamples - written) };
        const auto wantedInput { count * speed + state.inputRemainder };
        const auto inputCount { static_cast<int> (std::floor (wantedInput)) };
        state.inputRemainder = wantedInput - inputCount;
        state.stretch.process (state.input, inputCount, state.scratch.getArrayOfWritePointers (), count);
        state.input.position += inputCount;
        // Keep the running index bounded even during hours of loop playback.
        if (state.input.looping && state.input.position >= state.input.length)
            state.input.position = state.input.loopOffset + (state.input.position - state.input.loopOffset) % (state.input.length - state.input.loopOffset);
        for (auto channel { 0 }; channel < juce::jmin (2, output.buffer->getNumChannels ()); ++channel)
            output.buffer->copyFrom (channel, output.startSample + written, state.scratch, channel, 0, count);
        written += count;
    }
}
