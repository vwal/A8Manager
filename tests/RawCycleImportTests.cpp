#include "Assimil8or/Audio/RawCycleImport.h"
#include "Assimil8or/Audio/WaveformDesignExport.h"
#include "Assimil8or/Audio/WaveformDesignRecall.h"
#include "Assimil8or/Audio/WaveformDesignSidecars.h"
#include <juce_cryptography/juce_cryptography.h>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    void check (bool condition, const char* message) { if (! condition) throw std::runtime_error (message); }
    void success (const juce::Result& result) { if (result.failed ()) throw std::runtime_error (result.getErrorMessage ().toStdString ()); }
    juce::File folder (juce::File parent, const juce::String& name)
    {
        const auto result { parent.getChildFile (name) };
        success (result.createDirectory ());
        return result;
    }

    // Tiny independent RIFF fixture writer. It also permits intentionally bad
    // float data and headers that a normal audio writer would sanitize/reject.
    void wav (juce::File file, int frames, int channels, int bits, bool floating,
              std::function<double (int, int)> value, const juce::String& metadata = {})
    {
        juce::MemoryOutputStream body;
        body.write ("WAVEfmt ", 8);
        body.writeInt (16);
        body.writeShort (static_cast<short> (floating ? 3 : 1));
        body.writeShort (static_cast<short> (channels));
        body.writeInt (44100);
        body.writeInt (44100 * channels * bits / 8);
        body.writeShort (static_cast<short> (channels * bits / 8));
        body.writeShort (static_cast<short> (bits));
        if (metadata.isNotEmpty ())
        {
            const auto text { metadata.toUTF8 () };
            const auto length { static_cast<int> (text.sizeInBytes ()) };
            body.write ("ICMT", 4);
            body.writeInt (length);
            body.write (text.getAddress (), static_cast<size_t> (length));
            if ((length & 1) != 0) body.writeByte (0);
        }
        const auto bytes { frames * channels * bits / 8 };
        body.write ("data", 4);
        body.writeInt (bytes);
        for (int frame { 0 }; frame < frames; ++frame)
            for (int channel { 0 }; channel < channels; ++channel)
            {
                const auto sample { value (frame, channel) };
                if (floating && bits == 32) body.writeInt (std::bit_cast<std::int32_t> (static_cast<float> (sample)));
                else if (floating && bits == 64) body.writeInt64 (std::bit_cast<std::int64_t> (sample));
                else if (bits == 8) body.writeByte (static_cast<char> (std::clamp (std::llround (sample * 128.0 + 128.0), 0LL, 255LL)));
                else
                {
                    const auto scale { std::ldexp (1.0, bits - 1) };
                    const auto integer { static_cast<juce::int64> (std::clamp (std::llround (sample * scale), -static_cast<juce::int64> (scale), static_cast<juce::int64> (scale) - 1)) };
                    for (int byte { 0 }; byte < bits / 8; ++byte)
                        body.writeByte (static_cast<char> ((static_cast<juce::uint64> (integer) >> (byte * 8)) & 255));
                }
            }
        if ((bytes & 1) != 0) body.writeByte (0);
        juce::MemoryOutputStream complete;
        complete.write ("RIFF", 4);
        complete.writeInt (static_cast<int> (body.getDataSize ()));
        complete.write (body.getData (), body.getDataSize ());
        check (file.replaceWithData (complete.getData (), complete.getDataSize ()), "Write WAV fixture");
    }

    WaveformDesign::Render render (const WaveformDesign::Settings& settings)
    {
        WaveformDesign::Render result;
        success (WaveformDesign::render (settings, result));
        return result;
    }
    bool sameAudio (const WaveformDesign::Render& a, const WaveformDesign::Render& b)
    {
        if (a.frames != b.frames || a.voices.size () != b.voices.size ()) return false;
        for (size_t voice { 0 }; voice < a.voices.size (); ++voice)
            for (int frame { 0 }; frame < a.voices[voice].getNumSamples (); ++frame)
                if (a.voices[voice].getSample (0, frame) != b.voices[voice].getSample (0, frame)) return false;
        return true;
    }
    bool sameSource (const std::vector<double>& a, const std::vector<double>& b)
    {
        // JUCE's JSON writer rounds doubles to 15 decimal places. This bound
        // is far below a float-render or 24-bit WAV quantization step.
        return a.size () == b.size () && std::equal (a.begin (), a.end (), b.begin (), [] (double first, double second)
        {
            return std::abs (first - second) < 1.0e-14;
        });
    }
    double maximumAudioDifference (const WaveformDesign::Render& a, const WaveformDesign::Render& b)
    {
        if (a.frames != b.frames || a.voices.size () != b.voices.size ()) return std::numeric_limits<double>::infinity ();
        double maximum { 0.0 };
        for (size_t voice { 0 }; voice < a.voices.size (); ++voice)
            for (int frame { 0 }; frame < a.voices[voice].getNumSamples (); ++frame)
                maximum = std::max (maximum, std::abs (static_cast<double> (a.voices[voice].getSample (0, frame)) - b.voices[voice].getSample (0, frame)));
        return maximum;
    }
}

void testRawCycleImport ()
{
    using namespace WaveformDesign;
    using Side = RawCycleImport::StereoChannel;
    const auto root { juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("a8-raw-cycle-test-" + juce::Uuid ().toString ()) };
    success (root.createDirectory ());
    struct Cleanup { juce::File file; ~Cleanup () { file.deleteRecursively (); } } cleanup { root };
    constexpr double tau { juce::MathConstants<double>::twoPi };

    for (const auto bits : { 8, 16, 24, 32, 64 })
    {
        const auto floating { bits == 64 };
        const auto file { root.getChildFile ("cycle-" + juce::String (bits) + ".wav") };
        wav (file, 512, 1, bits, floating, [=] (int frame, int) { return 0.4 * std::sin (tau * frame / 512.0); });
        Settings loaded;
        RawCycleImport::Info info;
        success (RawCycleImport::inspect (file, info));
        success (RawCycleImport::load (file, Side::unspecified, loaded));
        check (info.frames == 512 && info.channels == 1 && info.bitsPerSample == bits && info.floatingPoint == floating
               && info.sampleRate == 44100.0 && loaded.shape == Shape::imported && loaded.importedCycle.size () == 512,
               "Supported PCM/float formats retain source information and all original samples");
        const auto tolerance { floating ? 1.0e-12 : 1.1 / std::ldexp (1.0, bits - 1) };
        for (int frame { 0 }; frame < 512; ++frame)
            check (std::abs (loaded.importedCycle[static_cast<size_t> (frame)] - 0.4 * std::sin (tau * frame / 512.0)) <= tolerance,
                   "Raw decoder preserves PCM polarity, scale and interleaving");
        const auto generated { render (loaded) };
        check (generated.peak > 0.39 && generated.peak < 0.41, "Imported cycles are not boosted to full scale by automatic normalization");
    }

    const auto source { root.getChildFile ("AKWF-like-600.wav") };
    wav (source, 600, 1, 24, false, [=] (int frame, int) { return 0.2 + 0.4 * std::sin (tau * frame / 600.0); });
    const auto sourceHash { juce::SHA256 (source) };
    Settings imported;
    RawCycleImport::Info info;
    success (RawCycleImport::load (source, Side::unspecified, imported, &info));
    check (imported.cycleFrames == 1024 && imported.sampleRate == 48000.0 && imported.amplitude == 1.0 && imported.brightness == 1.0
           && imported.harmonics == 1024 && imported.importedCycle.size () == 600 && ! info.warnings.isEmpty (),
           "Non-power-of-two cycles round UP without dropping source frames and start with neutral shaping");
    const auto neutral { render (imported) };
    check (std::abs (neutral.dc) < 1.0e-6 && std::abs (neutral.peak - 0.4) < 0.001, "Neutral imported render removes DC while preserving the audio-cycle level");
    for (int frame { 0 }; frame < 1024; ++frame)
        check (std::abs (neutral.voices[0].getSample (0, frame) - 0.4 * std::sin (tau * frame / 1024.0)) < 0.001,
               "Periodic resampling retains phase and shape instead of truncating/padding the source cycle");
    check (juce::SHA256 (source) == sourceHash, "Import never modifies the original WAV");

    const auto stereo { root.getChildFile ("stereo.wav") };
    wav (stereo, 256, 2, 32, true, [=] (int frame, int channel) { return (channel == 0 ? 0.5 : -0.25) * std::sin (tau * frame / 256.0); });
    Settings left, right, averaged;
    success (RawCycleImport::inspect (stereo, info));
    check (info.channels == 2 && info.floatingPoint, "Stereo inspection identifies the channel-choice requirement");
    const auto before { juce::JSON::toString (toJson (imported)) };
    check (RawCycleImport::load (stereo, Side::unspecified, imported).failed () && juce::JSON::toString (toJson (imported)) == before,
           "Stereo cannot silently pick a channel and failure preserves the current design");
    success (RawCycleImport::load (stereo, Side::left, left));
    success (RawCycleImport::load (stereo, Side::right, right));
    success (RawCycleImport::load (stereo, Side::average, averaged));
    check (std::abs (left.importedCycle[64] - 0.5) < 1.0e-7 && std::abs (right.importedCycle[64] + 0.25) < 1.0e-7
           && std::abs (averaged.importedCycle[64] - 0.125) < 1.0e-7, "Explicit stereo left/right/average choices preserve channel polarity and gain");
    check (RawCycleImport::load (source, Side::right, imported).failed (), "Mono files have no silently invented right channel");

    const auto shapedFile { root.getChildFile ("harmonic-cycle.wav") };
    wav (shapedFile, 512, 1, 32, true, [=] (int frame, int) { return 0.35 * std::sin (tau * frame / 512.0) + 0.15 * std::cos (2.0 * tau * frame / 512.0); });
    Settings shaped;
    success (RawCycleImport::load (shapedFile, Side::unspecified, shaped));
    const auto originalRender { render (shaped) };
    for (int control { 0 }; control < 7; ++control)
    {
        auto edited { shaped };
        switch (control)
        {
            case 0: edited.phaseDegrees = 65.0; break;
            case 1: edited.symmetry = 0.25; break;
            case 2: edited.harmonics = 1; break;
            case 3: edited.brightness = 0.2; break;
            case 4: edited.drive = 0.4; break;
            case 5: edited.fold = 0.4; break;
            case 6: edited.amplitude = 0.4; break;
        }
        check (! sameAudio (originalRender, render (edited)), "Existing phase/symmetry/harmonics/brightness/drive/fold/amplitude controls shape imported cycles");
    }
    auto layered { shaped };
    layered.mode = Mode::layers;
    spreadVoices (layered, 3, 9.0, 160.0, 0.5);
    const auto voices { render (layered) };
    check (voices.voices.size () == 3 && voices.frames == 512 && voices.voices[0].getSample (0, 0) != voices.voices[1].getSample (0, 0),
           "Imported cycles can become related phase/level-varied layer voices");
    auto prohibited { shaped };
    prohibited.mode = Mode::modulation;
    check (validate (prohibited).failed (), "Raw audio import does not silently become a CV generation shape");

    const auto encoded { juce::JSON::toString (toJson (layered)) };
    check (encoded.getNumBytesAsUTF8 () < 1024 * 1024 && static_cast<int> (toJson (layered).getProperty ("version", 0)) == 2,
           "Imported data uses a bounded self-contained version-2 recipe");
    Settings recalled;
    success (fromJson (juce::JSON::parse (encoded), recalled));
    check (sameSource (recalled.importedCycle, layered.importedCycle) && recalled.importedCycleName == layered.importedCycleName,
           "Version-2 recipe preserves every imported source sample within JSON double precision");
    const auto recipeError { maximumAudioDifference (render (recalled), voices) };
    std::cout << "Raw-cycle JSON render maximum error: " << recipeError << '\n';
    // Decimal JSON rounding can tip an interpolated sample across a float
    // rounding midpoint before the FFT. Require sub-PCM24-step accuracy rather
    // than bit identity of intermediate floating-point calculations.
    check (recipeError <= 1.0 / 8388608.0,
           "Version-2 recipe reproduces the shaped render within one 24-bit PCM step");
    auto invalidJson { toJson (layered) };
    invalidJson.getDynamicObject ()->setProperty ("version", 1);
    check (fromJson (invalidJson, recalled).failed (), "Imported payloads cannot masquerade as version-1 recipes");
    invalidJson = toJson (layered);
    invalidJson.getProperty ("settings", {}).getDynamicObject ()->removeProperty ("importedCycle");
    check (fromJson (invalidJson, recalled).failed (), "Missing embedded import data is rejected");
    invalidJson = toJson (layered);
    invalidJson.getProperty ("settings", {}).getProperty ("importedCycle", {}).getArray ()->getReference (0) = std::numeric_limits<double>::quiet_NaN ();
    check (fromJson (invalidJson, recalled).failed (), "Nonfinite recipe samples are rejected before rendering");
    auto badSettings { shaped };
    badSettings.importedCycle.resize (8193);
    check (validate (badSettings).failed (), "Embedded samples have a hard 8192-frame limit");

    ExportResult package;
    success (exportDesign (layered, root, "Imported source", package));
    check (WaveformDesignSidecars::identify (package.recipe) == WaveformDesignSidecars::Kind::recipe, "Desktop validator recognizes version-2 recipes");
    check (shapedFile.deleteFile (), "Remove owned test input to prove detached recall");
    WaveformDesignRecall::RecalledDesign recalledWave;
    success (WaveformDesignRecall::recallWave (package.waves[1], recalledWave));
    check (sameSource (recalledWave.settings.importedCycle, layered.importedCycle) && recalledWave.voiceIndex == 1
           && maximumAudioDifference (render (recalledWave.settings), voices) <= 1.0 / 8388608.0,
           "Exported imported-cycle layers retain designer recall within one 24-bit PCM step after source deletion");

    for (int count : { 0, 3, 8193 })
    {
        const auto bad { root.getChildFile ("frames-" + juce::String (count) + ".wav") };
        wav (bad, count, 1, 24, false, [] (int, int) { return 0.0; });
        check (RawCycleImport::load (bad, Side::unspecified, imported).failed (), "Empty, too-short and long recordings are rejected");
    }
    for (int count : { 4, 8192 })
    {
        const auto valid { root.getChildFile ("boundary-" + juce::String (count) + ".wav") };
        wav (valid, count, 1, 24, false, [=] (int frame, int) { return 0.3 * std::sin (tau * frame / count); });
        Settings loaded;
        success (RawCycleImport::load (valid, Side::unspecified, loaded));
        check (loaded.importedCycle.size () == static_cast<size_t> (count) && juce::JSON::toString (toJson (loaded)).getNumBytesAsUTF8 () < 1024 * 1024,
               "Boundary source sizes remain within the recipe loader's allocation limit");
    }
    const auto unsafe { root.getChildFile ("unsafe-float.wav") };
    for (const auto bad : { std::numeric_limits<double>::quiet_NaN (), std::numeric_limits<double>::infinity (), 1.01, -1.01 })
    {
        wav (unsafe, 64, 1, 64, true, [=] (int, int) { return bad; });
        check (RawCycleImport::inspect (unsafe, info).failed (), "Nonfinite or beyond-full-scale float samples are rejected during inspection");
    }
    const auto cv { root.getChildFile ("cv.wav") };
    wav (cv, 64, 1, 24, false, [] (int, int) { return 0.0; }, CvSampleSafety::cvMarker);
    check (RawCycleImport::load (cv, Side::unspecified, imported).failed (), "Embedded CV provenance blocks raw audio import");
    const auto test { root.getChildFile ("test.wav") };
    wav (test, 64, 1, 24, false, [] (int, int) { return 0.0; }, "A8Manager.HardwareTestOutput");
    check (RawCycleImport::inspect (test, info).failed (), "Hardware-test metadata blocks raw audio import");
    const auto legacyFolder { folder (root, "legacy") };
    const auto legacyWave { legacyFolder.getChildFile ("voice-01.wav") };
    wav (legacyWave, 64, 1, 24, false, [] (int, int) { return 0.0; });
    check (legacyFolder.getChildFile ("design.json").replaceWithText ("malformed recipe must not become audio"), "Create authoritative malformed sidecar fixture");
    check (RawCycleImport::load (legacyWave, Side::unspecified, imported).failed (), "Broken or legacy generated recipes cannot be bypassed through raw import");
    check (legacyFolder.getChildFile ("design.json").deleteFile (), "Remove owned malformed recipe fixture");
    check (RawCycleImport::inspect (legacyWave, info).failed (), "Recognized generated filename families remain protected even after their recipe is missing");
    const auto testFolder { folder (root, "hardware") };
    const auto testWave { testFolder.getChildFile ("source.wav") };
    wav (testWave, 64, 1, 24, false, [] (int, int) { return 0.0; });
    check (testFolder.getChildFile ("test-manifest.json").replaceWithText ("{}"), "Create test-export provenance fixture");
    check (RawCycleImport::inspect (testWave, info).failed (), "Diagnostic folders cannot fall back into ordinary raw audio import");
    const auto corrupt { root.getChildFile ("corrupt.wav") };
    check (source.copyFileTo (corrupt), "Copy valid source for corruption test");
    juce::MemoryBlock corruptBytes;
    check (corrupt.loadFileAsData (corruptBytes), "Read owned corruption fixture");
    static_cast<unsigned char*> (corruptBytes.getData ())[16] = 0xff; // fmt chunk extends past EOF
    static_cast<unsigned char*> (corruptBytes.getData ())[17] = 0xff;
    check (corrupt.replaceWithData (corruptBytes.getData (), corruptBytes.getSize ()), "Write malformed RIFF extent");
    check (RawCycleImport::inspect (corrupt, info).failed (), "Corrupt RIFF chunk lengths are rejected before sample decoding");
    std::error_code linkError;
    const auto linked { root.getChildFile ("linked.wav") };
    std::filesystem::create_symlink (std::filesystem::path (source.getFullPathName ().toStdString ()), std::filesystem::path (linked.getFullPathName ().toStdString ()), linkError);
    if (! linkError) check (RawCycleImport::inspect (linked, info).failed (), "Raw import does not follow symbolic-link input files");
    std::cout << "PASS: raw cycle import (PCM/float validation, stereo choice, periodic resampling/shaping, bounded v2 recipe/recall, CV/test safeguards)\n";
}
