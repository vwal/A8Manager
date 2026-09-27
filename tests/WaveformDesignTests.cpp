#include "Assimil8or/Audio/WaveformDesign.h"
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    using namespace WaveformDesign;

    void check (bool condition, const char* message) { if (! condition) throw std::runtime_error (message); }
    bool near (double first, double second, double tolerance = 0.00001) { return std::abs (first - second) <= tolerance; }

    Render generated (const Settings& settings)
    {
        Render result;
        const auto status { render (settings, result) };
        if (status.failed ()) throw std::runtime_error (status.getErrorMessage ().toStdString ());
        return result;
    }

    double harmonic (const juce::AudioBuffer<float>& buffer, int bin)
    {
        std::complex<double> sum {};
        for (int frame { 0 }; frame < buffer.getNumSamples (); ++frame)
        {
            const auto angle { -juce::MathConstants<double>::twoPi * bin * frame / buffer.getNumSamples () };
            sum += static_cast<double> (buffer.getSample (0, frame)) * std::complex<double> (std::cos (angle), std::sin (angle));
        }
        return std::abs (sum) / buffer.getNumSamples ();
    }

    bool sameAudio (const Render& first, const Render& second)
    {
        if (first.frames != second.frames || first.sampleRate != second.sampleRate || first.voices.size () != second.voices.size ()) return false;
        for (size_t voice { 0 }; voice < first.voices.size (); ++voice)
            for (int frame { 0 }; frame < first.voices[voice].getNumSamples (); ++frame)
                if (first.voices[voice].getSample (0, frame) != second.voices[voice].getSample (0, frame)) return false;
        return true;
    }

    void checkFinite (const Render& result)
    {
        check (std::isfinite (result.peak) && std::isfinite (result.dc) && std::isfinite (result.boundaryJump), "Preview statistics are finite");
        for (const auto& voice : result.voices)
        {
            check (voice.getNumChannels () == 1 && voice.getNumSamples () == result.frames, "Every rendered voice is a full-length mono file");
            for (int frame { 0 }; frame < voice.getNumSamples (); ++frame)
                check (std::isfinite (voice.getSample (0, frame)) && std::abs (voice.getSample (0, frame)) <= 1.0f, "Output samples are finite and bounded by digital full scale");
        }
    }
}

void testWaveformDesign ()
{
    using namespace WaveformDesign;
    auto sine { startingPoint (Mode::oscillator, Shape::sine) };
    const auto clean { generated (sine) };
    check (clean.frames == 512 && clean.sampleRate == 48000.0 && clean.voices.size () == 1, "Oscillator renders exactly one requested cycle");
    for (int frame { 0 }; frame < 512; ++frame)
        check (near (clean.voices[0].getSample (0, frame), sine.amplitude * std::sin (juce::MathConstants<double>::twoPi * frame / 512.0)), "Sine samples match one complete periodic cycle without a duplicate endpoint");
    check (near (clean.peak, 0.8) && near (clean.dc, 0.0) && clean.clippedSamples == 0, "Audio normalizes to requested amplitude and removes generated DC");
    check (clean.voices[0].getSample (0, 511) != clean.voices[0].getSample (0, 0), "The last frame is not an extra copy of the first");

    for (const auto shape : { Shape::sine, Shape::triangle, Shape::saw, Shape::pulse, Shape::trapezoid })
    {
        auto settings { startingPoint (Mode::oscillator, shape) };
        settings.cycleFrames = 256;
        settings.harmonics = 12;
        settings.symmetry = 0.37;
        settings.pulseWidth = 0.23;
        settings.drive = 0.45;
        settings.fold = 0.32;
        settings.brightness = 1.0;
        const auto result { generated (settings) };
        checkFinite (result);
        check (near (result.peak, settings.amplitude) && near (result.dc, 0.0), "Every shaped audio cycle is normalized after harmonic filtering");
        for (int bin { 13 }; bin <= 128; ++bin)
            check (harmonic (result.voices[0], bin) < 0.000001, "Drive/fold cannot leave discrete spectral bins above the harmonic cutoff");
    }
    auto saw { startingPoint (Mode::oscillator, Shape::saw) };
    saw.brightness = 1.0;
    const auto bright { generated (saw) };
    saw.brightness = 0.0;
    const auto dark { generated (saw) };
    check (harmonic (dark.voices[0], 3) / harmonic (dark.voices[0], 1)
           < harmonic (bright.voices[0], 3) / harmonic (bright.voices[0], 1) * 0.1, "Brightness changes relative harmonic energy, not just loudness");
    saw.cycleFrames = 64;
    saw.harmonics = 1024;
    const auto limited { generated (saw) };
    checkFinite (limited);
    check (! limited.warnings.isEmpty () && harmonic (limited.voices[0], 32) < 0.000001, "Requested excessive harmonics stop below Nyquist and report the limit");

    auto layers { startingPoint (Mode::layers, Shape::sine) };
    spreadVoices (layers, 2, 17.0, 90.0, 0.8);
    layers.voices[0].level = 1.0;
    layers.voices[1].level = 0.25;
    const auto stack { generated (layers) };
    check (stack.voices.size () == 2 && stack.frames == layers.cycleFrames, "Layer files each retain one cycle");
    check (near (stack.voices[0].getMagnitude (0, layers.cycleFrames), 0.8) && near (stack.voices[1].getMagnitude (0, layers.cycleFrames), 0.2), "Voice levels are baked into the WAV data");
    check (near (stack.voices[0].getSample (0, 0), 0.0) && near (stack.voices[1].getSample (0, 0), 0.2), "Voice phases produce different cycle data");
    layers.voices[0].detuneCents = -1100.0;
    layers.voices[1].detuneCents = 1100.0;
    layers.voices[0].pan = 0.99;
    layers.voices[1].pan = -0.99;
    check (sameAudio (stack, generated (layers)), "Detune and pan remain preset metadata and never alter single-cycle samples");
    spreadVoices (layers, 8, 20.0, 315.0, 1.0);
    check (layers.voiceCount == 8 && layers.voices.front ().detuneCents == -20.0 && layers.voices.back ().detuneCents == 20.0
           && layers.voices.front ().pan == -1.0 && layers.voices.back ().pan == 1.0 && near (layers.voices[7].phaseDegrees, 315.0), "Voice spread endpoints and count are deterministic");
    check (near (layers.voices[0].level, 1.0 / std::sqrt (8.0)), "Spread applies a predictable per-voice headroom level");
    spreadVoices (layers, 1, 20.0, 315.0, 1.0);
    check (layers.voices[0].detuneCents == 0.0 && layers.voices[0].pan == 0.0 && layers.voices[0].phaseDegrees == 0.0 && layers.voices[0].level == 1.0, "Single-voice spread is centred and full-level");

    auto cv { startingPoint (Mode::modulation, Shape::steps) };
    cv.durationSeconds = 0.01;
    cv.amplitude = 0.4;
    cv.offset = 0.2;
    cv.steps.fill (0.125);
    auto constant { generated (cv) };
    check (constant.frames == 480 && near (constant.peak, 0.25) && near (constant.dc, 0.25) && constant.boundaryJump == 0.0, "CV constant level preserves amplitude and DC without normalization");
    cv.measuredFullScaleVolts = 5.1;
    check (sameAudio (constant, generated (cv)), "Measured output voltage is calibration metadata, not an implicit gain change");
    cv.unipolar = true;
    cv.amplitude = 0.5;
    cv.offset = 0.1;
    cv.stepCount = 2;
    cv.steps[0] = -1.0;
    cv.steps[1] = 1.0;
    const auto uni { generated (cv) };
    check (near (uni.voices[0].getSample (0, 0), 0.1) && near (uni.voices[0].getSample (0, 240), 0.6) && near (uni.dc, 0.35), "Unipolar mapping has the requested floor and ceiling");
    cv.invert = true;
    const auto inverted { generated (cv) };
    check (near (inverted.voices[0].getSample (0, 0), 0.6) && near (inverted.voices[0].getSample (0, 240), 0.1), "Invert reverses unipolar direction without shifting it negative");
    cv.unipolar = false;
    cv.invert = false;
    cv.amplitude = 1.0;
    cv.offset = 0.5;
    cv.steps.fill (0.8);
    const auto clipped { generated (cv) };
    check (clipped.clippedSamples == clipped.frames && clipped.peak == 1.0 && clipped.dc == 1.0 && ! clipped.warnings.isEmpty (), "Clipping is explicitly counted, limited, and warned rather than wrapped");

    auto contour { startingPoint (Mode::modulation, Shape::envelope) };
    contour.durationSeconds = 0.01;
    contour.sustain = 0.4;
    auto env { generated (contour) };
    check (contour.unipolar && contour.playback == Playback::oneShot && near (env.voices[0].getSample (0, 0), 0.0), "Envelope starting point is one-shot unipolar with zero start");
    check (near (env.voices[0].getSample (0, 48), 0.8) && near (env.voices[0].getSample (0, 144), 0.32)
           && near (env.voices[0].getSample (0, 336), 0.32) && env.voices[0].getSample (0, 479) < 0.01, "ADSR fractions place attack, decay, hold and release correctly");
    check (env.voices[0].getSample (0, 479) > 0.0 && near (env.voices[0].getSample (0, 479), 0.32 / (0.3 * 480.0)), "One-shot file ends one frame before the mathematical zero-release endpoint");
    contour.curve = 0.8;
    const auto curved { generated (contour) };
    check (curved.voices[0].getSample (0, 24) < env.voices[0].getSample (0, 24), "Envelope curve shapes stage interpolation");
    contour.cycles = 2.0;
    auto repeated { generated (contour) };
    for (int frame { 0 }; frame < 240; ++frame)
        check (near (repeated.voices[0].getSample (0, frame), repeated.voices[0].getSample (0, frame + 240)), "CV cycles repeat the whole contour without changing file duration");
    contour.sampleRate = 96000.0;
    contour.durationSeconds = 0.00123;
    check (generated (contour).frames == 118, "CV duration rounds once to the nearest sample at the selected rate");

    auto drawn { startingPoint (Mode::modulation, Shape::drawn) };
    drawn.durationSeconds = 0.01;
    drawn.amplitude = 0.8;
    drawn.offset = -0.1;
    drawn.drawn.fill (0.2);
    check (near (generated (drawn).dc, 0.06), "Drawn contour retains its DC and absolute level");
    for (size_t index { 0 }; index < drawn.drawn.size (); ++index) drawn.drawn[index] = -1.0 + 2.0 * index / 32.0;
    const auto ramp { generated (drawn) };
    check (near (ramp.voices[0].getSample (0, 120), -0.5) && near (ramp.voices[0].getSample (0, 240), -0.1), "Drawn contour linearly interpolates all 33 points");

    auto random { startingPoint (Mode::modulation, Shape::random) };
    random.durationSeconds = 0.01;
    random.seed = 123456789;
    const auto firstRandom { generated (random) };
    check (sameAudio (firstRandom, generated (random)), "Seeded random render is deterministic");
    ++random.seed;
    check (! sameAudio (firstRandom, generated (random)), "Changing seed changes the random contour");
    --random.seed;
    random.smoothing = 1.0;
    const auto smoothRandom { generated (random) };
    check (smoothRandom.boundaryJump < firstRandom.boundaryJump, "Random step smoothing includes the wraparound transition");
    checkFinite (smoothRandom);
    random.durationSeconds = 0.001;
    random.cycles = 1024.0;
    check (generated (random).warnings.joinIntoString (" ").contains ("Nyquist"), "Undersampled CV cycle rates have an explicit aliasing warning");

    sine.offset = 0.13;
    sine.phaseDegrees = 90.0;
    check (near (generated (sine).dc, 0.13) && near (generated (sine).voices[0].getSample (0, 0), 0.93), "Audio phase and intentional DC offset are retained after normalization");
    sine.amplitude = 0.0;
    check (near (generated (sine).dc, 0.13) && generated (sine).boundaryJump == 0.0, "Zero amplitude leaves only the requested offset");

    auto recipe { startingPoint (Mode::layers, Shape::trapezoid) };
    recipe.sampleRate = 96000.0;
    recipe.cycleFrames = 1024;
    recipe.phaseDegrees = -117.25;
    recipe.seed = std::numeric_limits<unsigned int>::max ();
    recipe.steps[15] = -0.125;
    recipe.drawn[32] = 0.25;
    recipe.voices[7].level = 0.25;
    recipe.measuredFullScaleVolts = 5.2;
    Settings loaded;
    const auto encoded { juce::JSON::toString (toJson (recipe)) };
    check (fromJson (juce::JSON::parse (encoded), loaded).wasOk () && juce::JSON::toString (toJson (loaded)) == encoded, "JSON recipe round-trips every setting, including inactive voices and unsigned seed");
    check (sameAudio (generated (recipe), generated (loaded)), "Recipe reload reproduces generated audio exactly");
    const auto loadedBefore { juce::JSON::toString (toJson (loaded)) };
    auto rejected = [&] (juce::var json)
    {
        check (fromJson (json, loaded).failed () && juce::JSON::toString (toJson (loaded)) == loadedBefore, "Bad recipe rejection is atomic");
    };
    auto json { toJson (recipe) };
    json.getDynamicObject ()->setProperty ("version", 2);
    rejected (json);
    json = toJson (recipe);
    json.getProperty ("settings", {}).getDynamicObject ()->setProperty ("cycleFrames", 100.0);
    rejected (json);
    json = toJson (recipe);
    json.getProperty ("settings", {}).getDynamicObject ()->setProperty ("unipolar", "false");
    rejected (json);
    json = toJson (recipe);
    json.getProperty ("settings", {}).getDynamicObject ()->removeProperty ("voices");
    rejected (json);
    json = toJson (recipe);
    json.getProperty ("settings", {}).getDynamicObject ()->setProperty ("amplitude", std::numeric_limits<double>::quiet_NaN ());
    rejected (json);
    json = toJson (recipe);
    json.getProperty ("settings", {}).getDynamicObject ()->setProperty ("cycleFrames", 1.0e100);
    rejected (json);
    json = toJson (recipe);
    json.getProperty ("settings", {}).getDynamicObject ()->setProperty ("seed", -1);
    rejected (json);
    rejected (juce::var ("not a recipe"));

    auto invalid { startingPoint (Mode::oscillator, Shape::sine) };
    invalid.durationSeconds = std::numeric_limits<double>::infinity ();
    auto unchanged { generated (startingPoint (Mode::oscillator, Shape::sine)) };
    check (validate (invalid).failed () && render (invalid, unchanged).failed () && sameAudio (clean, unchanged), "Nonfinite inactive settings are rejected before rendering or replacing a preview");
    invalid = startingPoint (Mode::modulation, Shape::envelope);
    invalid.attack = 0.6;
    invalid.release = 0.6;
    check (validate (invalid).failed (), "Envelope durations cannot overrun a cycle");
    invalid = startingPoint (Mode::modulation, Shape::sine);
    invalid.durationSeconds = 61.0;
    check (validate (invalid).failed (), "Excessive durations are rejected before allocation");
    invalid = startingPoint (Mode::layers, Shape::sine);
    invalid.voiceCount = 9;
    check (validate (invalid).failed (), "Layer count cannot exceed the eight hardware channels");
    invalid = startingPoint (Mode::oscillator, Shape::sine);
    invalid.shape = Shape::envelope;
    check (validate (invalid).failed (), "Nonperiodic CV shapes cannot accidentally become audio oscillator cycles");
    invalid = startingPoint (Mode::oscillator, Shape::sine);
    invalid.symmetry = 0.0;
    check (validate (invalid).failed (), "Zero-width shape sections are rejected before division");
    invalid = startingPoint (Mode::oscillator, Shape::sine);
    invalid.mode = static_cast<Mode> (99);
    check (validate (invalid).failed (), "Unknown enum values are rejected");
    invalid = startingPoint (Mode::oscillator, Shape::sine);
    invalid.shape = static_cast<Shape> (-1);
    check (validate (invalid).failed (), "Unknown negative shape is rejected");
    invalid = startingPoint (Mode::oscillator, Shape::sine);
    invalid.playback = static_cast<Playback> (99);
    check (validate (invalid).failed (), "Unknown playback mode is rejected");
    for (const int frames : { 64, 8192 })
    {
        auto settings { startingPoint (Mode::layers, Shape::pulse) };
        settings.cycleFrames = frames;
        settings.pulseWidth = 0.01;
        settings.symmetry = 0.99;
        settings.harmonics = 1024;
        settings.drive = 1.0;
        settings.fold = 1.0;
        settings.voiceCount = 8;
        checkFinite (generated (settings));
    }
    std::cout << "PASS: band-limited periodic designs, CV/DC preservation, layers, deterministic recipes, statistics and bounded validation\n";
}
