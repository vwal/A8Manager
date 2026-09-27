#include "WaveformDesign.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace WaveformDesign
{
    namespace
    {
        constexpr double twoPi { juce::MathConstants<double>::twoPi };
        constexpr juce::int64 maximumFrames { 16 * 1024 * 1024 };
        constexpr int oversampling { 4 };

        bool within (double value, double low, double high) { return std::isfinite (value) && value >= low && value <= high; }
        double wrap (double phase) { return phase - std::floor (phase); }
        double smooth (double value) { return value * value * (3.0 - 2.0 * value); }

        juce::String modeName (Mode mode)
        {
            switch (mode)
            {
                case Mode::oscillator: return "oscillator";
                case Mode::modulation: return "modulation";
                case Mode::layers: return "layers";
            }
            return {};
        }

        juce::String playbackName (Playback playback)
        {
            switch (playback)
            {
                case Playback::oneShot: return "oneShot";
                case Playback::loop: return "loop";
                case Playback::gatedLoop: return "gatedLoop";
            }
            return {};
        }

        double bend (double position, double curve)
        {
            return curve >= 0.0 ? std::pow (position, 1.0 + 4.0 * curve)
                                : 1.0 - std::pow (1.0 - position, 1.0 - 4.0 * curve);
        }

        double envelope (double phase, const Settings& settings)
        {
            double value;
            if (settings.attack > 0.0 && phase < settings.attack)
                value = bend (phase / settings.attack, settings.curve);
            else if (settings.decay > 0.0 && phase < settings.attack + settings.decay)
                value = 1.0 + (settings.sustain - 1.0) * bend ((phase - settings.attack) / settings.decay, settings.curve);
            else if (settings.release > 0.0 && phase >= 1.0 - settings.release)
                value = settings.sustain * (1.0 - bend ((phase - (1.0 - settings.release)) / settings.release, settings.curve));
            else
                value = settings.sustain;
            // All raw shapes use -1..1; unipolar conversion is applied once later.
            return 2.0 * value - 1.0;
        }

        std::array<double, 16> randomSteps (unsigned int seed)
        {
            std::array<double, 16> values;
            std::uint32_t state { seed == 0 ? 0x9e3779b9u : static_cast<std::uint32_t> (seed) };
            for (auto& value : values)
            {
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;
                value = 2.0 * static_cast<double> (state) / static_cast<double> (std::numeric_limits<std::uint32_t>::max ()) - 1.0;
            }
            return values;
        }

        double stepped (double phase, const Settings& settings, const std::array<double, 16>& values)
        {
            const auto position { phase * settings.stepCount };
            const auto index { std::min (settings.stepCount - 1, static_cast<int> (position)) };
            const auto fraction { position - index };
            const auto value { values[static_cast<size_t> (index)] };
            if (settings.smoothing <= 0.0 || fraction >= settings.smoothing) return value;
            const auto previous { values[static_cast<size_t> ((index + settings.stepCount - 1) % settings.stepCount)] };
            return previous + (value - previous) * smooth (fraction / settings.smoothing);
        }

        double shapeAt (double phase, const Settings& settings, const std::array<double, 16>& random)
        {
            phase = wrap (phase);
            // Symmetry stretches the two halves without changing cycle length.
            const auto symmetric { phase < settings.symmetry ? 0.5 * phase / settings.symmetry
                                                            : 0.5 + 0.5 * (phase - settings.symmetry) / (1.0 - settings.symmetry) };
            switch (settings.shape)
            {
                case Shape::sine: return std::sin (twoPi * symmetric);
                case Shape::triangle: return symmetric < 0.5 ? -1.0 + 4.0 * symmetric : 3.0 - 4.0 * symmetric;
                case Shape::saw: return 2.0 * symmetric - 1.0;
                case Shape::pulse: return phase < settings.pulseWidth ? 1.0 : -1.0;
                case Shape::trapezoid:
                {
                    const auto triangle { symmetric < 0.5 ? -1.0 + 4.0 * symmetric : 3.0 - 4.0 * symmetric };
                    return std::clamp (triangle / (1.0 - settings.pulseWidth), -1.0, 1.0);
                }
                case Shape::envelope: return envelope (phase, settings);
                case Shape::steps: return stepped (phase, settings, settings.steps);
                case Shape::drawn:
                {
                    const auto position { phase * 32.0 };
                    const auto index { std::min (31, static_cast<int> (position)) };
                    const auto first { settings.drawn[static_cast<size_t> (index)] };
                    return first + (settings.drawn[static_cast<size_t> (index + 1)] - first) * (position - index);
                }
                case Shape::random: return stepped (phase, settings, random);
            }
            return 0.0;
        }

        double shapedAudio (double value, const Settings& settings)
        {
            if (settings.drive > 0.0)
            {
                const auto gain { 1.0 + 15.0 * settings.drive };
                value = std::tanh (gain * value) / std::tanh (gain);
            }
            if (settings.fold > 0.0)
            {
                const auto driven { value * (1.0 + 7.0 * settings.fold) };
                // Triangle folding, not hard clipping or a modulo discontinuity.
                value = 1.0 - 4.0 * std::abs (wrap ((driven + 1.0) / 4.0) - 0.5);
            }
            return value;
        }

        void makeAudio (const Settings& settings, Render& output)
        {
            const auto fftFrames { settings.cycleFrames * oversampling };
            int order { 0 };
            while ((1 << order) < fftFrames) ++order;
            juce::dsp::FFT fft (order);
            std::vector<juce::dsp::Complex<float>> time (static_cast<size_t> (fftFrames)), spectrum (time.size ()), shifted (time.size ());
            const auto random { randomSteps (settings.seed) };
            for (int frame { 0 }; frame < fftFrames; ++frame)
                time[static_cast<size_t> (frame)] = static_cast<float> (shapedAudio (shapeAt (static_cast<double> (frame) / fftFrames, settings, random), settings));
            fft.perform (time.data (), spectrum.data (), false);
            const auto highestHarmonic { std::min (settings.harmonics, settings.cycleFrames / 2 - 1) };
            for (int bin { 0 }; bin < fftFrames; ++bin)
            {
                const auto harmonic { std::min (bin, fftFrames - bin) };
                if (harmonic == 0 || harmonic > highestHarmonic)
                    spectrum[static_cast<size_t> (bin)] = {};
                else
                {
                    // Brightness 1 retains every selected harmonic. At 0 the
                    // fundamental remains while higher partials roll off steeply.
                    const auto gain { std::pow (static_cast<double> (harmonic), -4.0 * (1.0 - settings.brightness)) };
                    spectrum[static_cast<size_t> (bin)] *= static_cast<float> (gain);
                }
            }
            const auto count { settings.mode == Mode::layers ? settings.voiceCount : 1 };
            for (int voiceIndex { 0 }; voiceIndex < count; ++voiceIndex)
            {
                const auto& voice { settings.voices[static_cast<size_t> (voiceIndex)] };
                const auto phase { (settings.phaseDegrees + (settings.mode == Mode::layers ? voice.phaseDegrees : 0.0)) / 360.0 };
                for (int bin { 0 }; bin < fftFrames; ++bin)
                {
                    const auto harmonic { bin <= fftFrames / 2 ? bin : bin - fftFrames };
                    const auto angle { twoPi * harmonic * phase };
                    shifted[static_cast<size_t> (bin)] = spectrum[static_cast<size_t> (bin)] * juce::dsp::Complex<float> (static_cast<float> (std::cos (angle)), static_cast<float> (std::sin (angle)));
                }
                fft.perform (shifted.data (), time.data (), true);
                auto& buffer { output.voices.emplace_back (1, settings.cycleFrames) };
                double peak { 0.0 };
                for (int frame { 0 }; frame < settings.cycleFrames; ++frame)
                {
                    const auto value { time[static_cast<size_t> (frame * oversampling)].real () };
                    buffer.setSample (0, frame, value);
                    peak = std::max (peak, std::abs (static_cast<double> (value)));
                }
                const auto level { settings.mode == Mode::layers ? voice.level : 1.0 };
                const auto sign { settings.invert ? -1.0 : 1.0 };
                for (int frame { 0 }; frame < settings.cycleFrames; ++frame)
                {
                    auto value { peak > 1.0e-12 ? sign * buffer.getSample (0, frame) / peak : 0.0 };
                    if (settings.unipolar) value = (value + 1.0) * 0.5;
                    buffer.setSample (0, frame, static_cast<float> (value * settings.amplitude * level + settings.offset));
                }
            }
            if (settings.harmonics > highestHarmonic)
                output.warnings.add ("Harmonics above this cycle's Nyquist limit were omitted.");
            if (settings.drive > 0.0 || settings.fold > 0.0)
                output.warnings.add ("Drive/fold is oversampled and harmonic-filtered; this is not an alias-free guarantee at every playback pitch.");
        }

        void analyse (Render& output)
        {
            long double sum { 0.0 };
            juce::int64 count { 0 };
            for (auto& buffer : output.voices)
            {
                for (int frame { 0 }; frame < buffer.getNumSamples (); ++frame)
                {
                    auto value { static_cast<double> (buffer.getSample (0, frame)) };
                    if (value < -1.0 || value > 1.0) ++output.clippedSamples;
                    value = std::clamp (value, -1.0, 1.0);
                    buffer.setSample (0, frame, static_cast<float> (value));
                    output.peak = std::max (output.peak, std::abs (value));
                    sum += value;
                    ++count;
                }
                output.boundaryJump = std::max (output.boundaryJump, std::abs (static_cast<double> (buffer.getSample (0, 0)) - buffer.getSample (0, buffer.getNumSamples () - 1)));
            }
            output.dc = count > 0 ? static_cast<double> (sum / count) : 0.0;
            if (output.clippedSamples > 0)
                output.warnings.add (juce::String (output.clippedSamples) + " samples exceeded digital full scale and were limited. Reduce amplitude or offset.");
            if (output.boundaryJump > 0.1)
                output.warnings.add ("The final-to-first sample step is large; audition the loop boundary before use.");
        }

        bool number (const juce::DynamicObject& object, const juce::Identifier& key, double& value)
        {
            const auto& property { object.getProperty (key) };
            if (! (property.isInt () || property.isInt64 () || property.isDouble ())) return false;
            value = static_cast<double> (property);
            return std::isfinite (value);
        }

        bool integer (const juce::DynamicObject& object, const juce::Identifier& key, int& value)
        {
            double candidate;
            if (! number (object, key, candidate) || candidate != std::trunc (candidate)
                || candidate < std::numeric_limits<int>::min () || candidate > std::numeric_limits<int>::max ()) return false;
            value = static_cast<int> (candidate);
            return true;
        }

        bool boolean (const juce::DynamicObject& object, const juce::Identifier& key, bool& value)
        {
            const auto& property { object.getProperty (key) };
            if (! property.isBool ()) return false;
            value = static_cast<bool> (property);
            return true;
        }

        template <size_t size>
        bool array (const juce::DynamicObject& object, const juce::Identifier& key, std::array<double, size>& values)
        {
            const auto* items { object.getProperty (key).getArray () };
            if (items == nullptr || items->size () != static_cast<int> (size)) return false;
            for (size_t index { 0 }; index < size; ++index)
            {
                const auto& item { items->getReference (static_cast<int> (index)) };
                if (! (item.isInt () || item.isInt64 () || item.isDouble ())) return false;
                values[index] = static_cast<double> (item);
                if (! std::isfinite (values[index])) return false;
            }
            return true;
        }
    }

    juce::String shapeName (Shape shape)
    {
        switch (shape)
        {
            case Shape::sine: return "Sine";
            case Shape::triangle: return "Triangle";
            case Shape::saw: return "Saw";
            case Shape::pulse: return "Pulse";
            case Shape::trapezoid: return "Trapezoid";
            case Shape::envelope: return "Envelope";
            case Shape::steps: return "Steps";
            case Shape::drawn: return "Drawn";
            case Shape::random: return "Random";
        }
        return {};
    }

    Settings startingPoint (Mode mode, Shape shape)
    {
        Settings settings;
        settings.mode = mode;
        settings.shape = mode != Mode::modulation && static_cast<int> (shape) > static_cast<int> (Shape::trapezoid) ? Shape::sine : shape;
        settings.playback = mode == Mode::modulation ? (shape == Shape::envelope ? Playback::oneShot : Playback::loop) : Playback::gatedLoop;
        settings.unipolar = mode == Mode::modulation && shape == Shape::envelope;
        for (size_t index { 0 }; index < settings.drawn.size (); ++index)
            settings.drawn[index] = std::sin (twoPi * static_cast<double> (index) / (settings.drawn.size () - 1));
        settings.drawn.back () = settings.drawn.front ();
        if (mode == Mode::layers) spreadVoices (settings, 4, 7.0, 270.0, 0.75);
        return settings;
    }

    void spreadVoices (Settings& settings, int count, double detuneCents, double phaseSpreadDegrees, double panSpread)
    {
        settings.voiceCount = std::clamp (count, 1, 8);
        detuneCents = std::isfinite (detuneCents) ? std::clamp (std::abs (detuneCents), 0.0, 2400.0) : 0.0;
        phaseSpreadDegrees = std::isfinite (phaseSpreadDegrees) ? std::clamp (phaseSpreadDegrees, -360.0, 360.0) : 0.0;
        panSpread = std::isfinite (panSpread) ? std::clamp (std::abs (panSpread), 0.0, 1.0) : 0.0;
        for (size_t index { 0 }; index < settings.voices.size (); ++index)
        {
            auto& voice { settings.voices[index] };
            voice = {};
            if (static_cast<int> (index) >= settings.voiceCount) continue;
            const auto fraction { settings.voiceCount > 1 ? static_cast<double> (index) / (settings.voiceCount - 1) : 0.5 };
            const auto spread { 2.0 * fraction - 1.0 };
            voice.detuneCents = spread * detuneCents;
            voice.phaseDegrees = settings.voiceCount > 1 ? fraction * phaseSpreadDegrees : 0.0;
            voice.pan = spread * panSpread;
            voice.level = 1.0 / std::sqrt (static_cast<double> (settings.voiceCount));
        }
    }

    juce::Result validate (const Settings& s)
    {
        auto fail = [] (const char* reason) { return juce::Result::fail (reason); };
        if (modeName (s.mode).isEmpty () || shapeName (s.shape).isEmpty () || playbackName (s.playback).isEmpty ()) return fail ("Unknown waveform mode, shape or playback setting.");
        if (s.mode != Mode::modulation && static_cast<int> (s.shape) > static_cast<int> (Shape::trapezoid)) return fail ("Audio oscillators/layers require a periodic audio shape.");
        if (s.sampleRate != 48000.0 && s.sampleRate != 96000.0) return fail ("Choose a sample rate of 48 or 96 kHz.");
        if (s.cycleFrames < 64 || s.cycleFrames > 8192 || (s.cycleFrames & (s.cycleFrames - 1)) != 0) return fail ("Cycle length must be a power of two from 64 to 8192 frames.");
        if (! within (s.durationSeconds, 0.001, 60.0) || ! within (s.cycles, 0.01, 1024.0)) return fail ("CV duration must be 0.001..60 seconds and cycles 0.01..1024.");
        if (! within (s.amplitude, 0.0, 1.0) || ! within (s.offset, -1.0, 1.0)) return fail ("Amplitude must be 0..1 and offset -1..1 of digital full scale.");
        if (! within (s.phaseDegrees, -360.0, 360.0) || ! within (s.symmetry, 0.01, 0.99) || ! within (s.pulseWidth, 0.01, 0.99)) return fail ("Phase must be -360..360 degrees; symmetry and pulse width must be 0.01..0.99.");
        if (s.harmonics < 1 || s.harmonics > 1024 || ! within (s.brightness, 0.0, 1.0) || ! within (s.drive, 0.0, 1.0) || ! within (s.fold, 0.0, 1.0)) return fail ("Harmonics must be 1..1024; brightness, drive and fold must be 0..1.");
        if (! within (s.attack, 0.0, 1.0) || ! within (s.decay, 0.0, 1.0) || ! within (s.sustain, 0.0, 1.0) || ! within (s.release, 0.0, 1.0)
            || s.attack + s.decay + s.release > 1.0 + 1.0e-12 || ! within (s.curve, -1.0, 1.0)) return fail ("Envelope stages must fit within one cycle; sustain is 0..1 and curve -1..1.");
        if (! within (s.smoothing, 0.0, 1.0) || s.stepCount < 1 || s.stepCount > 16) return fail ("Choose 1..16 steps and smoothing 0..1.");
        for (const auto value : s.steps) if (! within (value, -1.0, 1.0)) return fail ("Every step value must be -1..1.");
        for (const auto value : s.drawn) if (! within (value, -1.0, 1.0)) return fail ("Every drawn point must be -1..1.");
        if (s.voiceCount < 1 || s.voiceCount > 8) return fail ("Layer count must be 1..8.");
        for (const auto& voice : s.voices)
            if (! within (voice.detuneCents, -2400.0, 2400.0) || ! within (voice.phaseDegrees, -360.0, 360.0) || ! within (voice.pan, -1.0, 1.0) || ! within (voice.level, 0.0, 1.0))
                return fail ("Voice detune must be within two octaves, phase -360..360 degrees, pan -1..1 and level 0..1.");
        if (! within (s.measuredFullScaleVolts, 0.0, 100.0)) return fail ("Optional measured full-scale voltage must be 0..100 V; use 0 if unknown.");
        const auto frames { s.mode == Mode::modulation ? std::llround (s.sampleRate * s.durationSeconds) : s.cycleFrames };
        const auto count { s.mode == Mode::layers ? s.voiceCount : 1 };
        if (frames <= 0 || frames > maximumFrames / count) return fail ("The requested waveform exceeds the total frame limit.");
        return juce::Result::ok ();
    }

    juce::Result render (const Settings& settings, Render& result)
    {
        if (const auto valid { validate (settings) }; valid.failed ()) return valid;
        try
        {
            Render generated;
            generated.sampleRate = settings.sampleRate;
            generated.frames = settings.mode == Mode::modulation ? std::llround (settings.sampleRate * settings.durationSeconds) : settings.cycleFrames;
            if (settings.mode == Mode::modulation)
            {
                auto& buffer { generated.voices.emplace_back (1, static_cast<int> (generated.frames)) };
                const auto random { randomSteps (settings.seed) };
                for (int frame { 0 }; frame < buffer.getNumSamples (); ++frame)
                {
                    // The file covers [0, duration), not an extra endpoint at
                    // duration. A one-shot envelope's final frame can therefore
                    // be slightly above its mathematical release endpoint.
                    auto value { shapeAt (static_cast<double> (frame) * settings.cycles / generated.frames + settings.phaseDegrees / 360.0, settings, random) };
                    if (settings.invert) value = -value;
                    if (settings.unipolar) value = (value + 1.0) * 0.5;
                    buffer.setSample (0, frame, static_cast<float> (value * settings.amplitude + settings.offset));
                }
                if (settings.measuredFullScaleVolts == 0.0) generated.warnings.add ("CV values are digital full-scale fractions, not calibrated hardware voltages.");
                if (settings.cycles >= generated.frames / 2.0)
                    generated.warnings.add ("The requested CV cycle rate reaches or exceeds Nyquist; it aliases and cannot be represented faithfully at this sample rate.");
                if (std::abs (settings.cycles - std::round (settings.cycles)) > 1.0e-9 && settings.playback != Playback::oneShot)
                    generated.warnings.add ("A fractional cycle count can introduce a jump when the file loops.");
            }
            else
                makeAudio (settings, generated);
            analyse (generated);
            result = std::move (generated);
            return juce::Result::ok ();
        }
        catch (const std::bad_alloc&)
        {
            return juce::Result::fail ("There is not enough memory to render this waveform.");
        }
    }

    juce::var toJson (const Settings& s)
    {
        auto* root { new juce::DynamicObject };
        juce::var result (root);
        root->setProperty ("type", "A8Manager.WaveformDesign");
        root->setProperty ("version", 1);
        auto* object { new juce::DynamicObject };
        root->setProperty ("settings", juce::var (object));
        object->setProperty ("mode", modeName (s.mode));
        object->setProperty ("shape", shapeName (s.shape).toLowerCase ());
        object->setProperty ("playback", playbackName (s.playback));
        object->setProperty ("sampleRate", s.sampleRate);
        object->setProperty ("cycleFrames", s.cycleFrames);
        object->setProperty ("durationSeconds", s.durationSeconds);
        object->setProperty ("cycles", s.cycles);
        object->setProperty ("amplitude", s.amplitude);
        object->setProperty ("offset", s.offset);
        object->setProperty ("unipolar", s.unipolar);
        object->setProperty ("invert", s.invert);
        object->setProperty ("phaseDegrees", s.phaseDegrees);
        object->setProperty ("symmetry", s.symmetry);
        object->setProperty ("pulseWidth", s.pulseWidth);
        object->setProperty ("harmonics", s.harmonics);
        object->setProperty ("brightness", s.brightness);
        object->setProperty ("drive", s.drive);
        object->setProperty ("fold", s.fold);
        object->setProperty ("attack", s.attack);
        object->setProperty ("decay", s.decay);
        object->setProperty ("sustain", s.sustain);
        object->setProperty ("release", s.release);
        object->setProperty ("curve", s.curve);
        object->setProperty ("smoothing", s.smoothing);
        object->setProperty ("stepCount", s.stepCount);
        object->setProperty ("voiceCount", s.voiceCount);
        object->setProperty ("seed", static_cast<juce::int64> (s.seed));
        object->setProperty ("measuredFullScaleVolts", s.measuredFullScaleVolts);
        juce::Array<juce::var> steps, drawn, voices;
        for (const auto value : s.steps) steps.add (value);
        for (const auto value : s.drawn) drawn.add (value);
        for (const auto& voice : s.voices)
        {
            auto* item { new juce::DynamicObject };
            item->setProperty ("detuneCents", voice.detuneCents);
            item->setProperty ("phaseDegrees", voice.phaseDegrees);
            item->setProperty ("pan", voice.pan);
            item->setProperty ("level", voice.level);
            voices.add (juce::var (item));
        }
        object->setProperty ("steps", steps);
        object->setProperty ("drawn", drawn);
        object->setProperty ("voices", voices);
        return result;
    }

    juce::Result fromJson (const juce::var& json, Settings& settings)
    {
        auto fail = [] () { return juce::Result::fail ("Invalid or incomplete waveform recipe. Expected A8Manager.WaveformDesign version 1."); };
        const auto* root { json.getDynamicObject () };
        int version { 0 };
        if (root == nullptr || root->getProperty ("type") != juce::var ("A8Manager.WaveformDesign") || ! integer (*root, "version", version) || version != 1) return fail ();
        const auto* object { root->getProperty ("settings").getDynamicObject () };
        if (object == nullptr) return fail ();
        Settings candidate;
        bool foundMode { false }, foundShape { false }, foundPlayback { false };
        for (const auto mode : { Mode::oscillator, Mode::modulation, Mode::layers })
            if (object->getProperty ("mode") == juce::var (modeName (mode))) { candidate.mode = mode; foundMode = true; }
        for (const auto shape : { Shape::sine, Shape::triangle, Shape::saw, Shape::pulse, Shape::trapezoid, Shape::envelope, Shape::steps, Shape::drawn, Shape::random })
            if (object->getProperty ("shape") == juce::var (shapeName (shape).toLowerCase ())) { candidate.shape = shape; foundShape = true; }
        for (const auto playback : { Playback::oneShot, Playback::loop, Playback::gatedLoop })
            if (object->getProperty ("playback") == juce::var (playbackName (playback))) { candidate.playback = playback; foundPlayback = true; }
        if (! foundMode || ! foundShape || ! foundPlayback) return fail ();
        if (! number (*object, "sampleRate", candidate.sampleRate) || ! integer (*object, "cycleFrames", candidate.cycleFrames)
            || ! number (*object, "durationSeconds", candidate.durationSeconds) || ! number (*object, "cycles", candidate.cycles)
            || ! number (*object, "amplitude", candidate.amplitude) || ! number (*object, "offset", candidate.offset)
            || ! boolean (*object, "unipolar", candidate.unipolar) || ! boolean (*object, "invert", candidate.invert)
            || ! number (*object, "phaseDegrees", candidate.phaseDegrees) || ! number (*object, "symmetry", candidate.symmetry)
            || ! number (*object, "pulseWidth", candidate.pulseWidth) || ! integer (*object, "harmonics", candidate.harmonics)
            || ! number (*object, "brightness", candidate.brightness) || ! number (*object, "drive", candidate.drive)
            || ! number (*object, "fold", candidate.fold) || ! number (*object, "attack", candidate.attack)
            || ! number (*object, "decay", candidate.decay) || ! number (*object, "sustain", candidate.sustain)
            || ! number (*object, "release", candidate.release) || ! number (*object, "curve", candidate.curve)
            || ! number (*object, "smoothing", candidate.smoothing) || ! integer (*object, "stepCount", candidate.stepCount)
            || ! integer (*object, "voiceCount", candidate.voiceCount) || ! number (*object, "measuredFullScaleVolts", candidate.measuredFullScaleVolts)
            || ! array (*object, "steps", candidate.steps) || ! array (*object, "drawn", candidate.drawn)) return fail ();
        double seed;
        if (! number (*object, "seed", seed) || seed < 0.0 || seed > std::numeric_limits<unsigned int>::max () || seed != std::trunc (seed)) return fail ();
        candidate.seed = static_cast<unsigned int> (seed);
        const auto* voices { object->getProperty ("voices").getArray () };
        if (voices == nullptr || voices->size () != 8) return fail ();
        for (size_t index { 0 }; index < candidate.voices.size (); ++index)
        {
            const auto* voice { voices->getReference (static_cast<int> (index)).getDynamicObject () };
            auto& target { candidate.voices[index] };
            if (voice == nullptr || ! number (*voice, "detuneCents", target.detuneCents) || ! number (*voice, "phaseDegrees", target.phaseDegrees)
                || ! number (*voice, "pan", target.pan) || ! number (*voice, "level", target.level)) return fail ();
        }
        if (const auto valid { validate (candidate) }; valid.failed ()) return valid;
        settings = candidate;
        return juce::Result::ok ();
    }
}
