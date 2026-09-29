#include "Assimil8or/Audio/HardwareTestOutput.h"
#include "Assimil8or/Audio/AudioManager.h"
#include "Assimil8or/Audio/CvSampleSafety.h"
#include "Assimil8or/Audio/ChannelCvSafety.h"
#include "Assimil8or/Assimil8orPreset.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace
{
    void require (bool condition, const char* message) { if (! condition) throw std::runtime_error (message); }
    constexpr double tolerance { 3.0 / 8388608.0 };

    juce::AudioBuffer<float> readWave (const juce::File& file, double rate, bool cv)
    {
        AudioManager manager;
        auto reader { manager.getReaderFor (file) };
        require (reader && reader->sampleRate == rate && reader->numChannels == 1 && reader->bitsPerSample == 24 && ! reader->usesFloatingPointData,
                 "Diagnostic WAV is mono PCM24 at the selected rate");
        require (CvSampleSafety::isCv (file, *reader) == cv, "Diagnostic WAV has the correct persistent CV/audio purpose");
        require (reader->metadataValues[juce::WavAudioFormat::riffInfoComment2] == (cv ? CvSampleSafety::cvMarker : CvSampleSafety::audioMarker),
                 "Purpose metadata is explicit; reference AUDIO is not guessed as CV from nearby recipe");
        juce::AudioBuffer<float> decoded (1, static_cast<int> (reader->lengthInSamples));
        require (reader->read (&decoded, 0, decoded.getNumSamples (), 0, true, false), "Diagnostic WAV can be decoded completely");
        return decoded;
    }

    juce::ValueTree readPreset (const juce::File& file)
    {
        juce::StringArray lines;
        file.readLines (lines);
        Assimil8orPreset parser;
        parser.parse (lines);
        require (! lines.isEmpty () && parser.getParseErrorsVT ().getNumChildren () == 0, "Test preset round-trips through hardware parser");
        return parser.getPresetVT ().createCopy ();
    }

    void checkPreset (const HardwareTestOutput::Settings& settings, const HardwareTestOutput::ExportResult& result)
    {
        auto tree { readPreset (result.preset) };
        PresetProperties preset (tree, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        require (preset.getId () == settings.presetNumber && preset.getMidiSetup () == 0, "Requested preset slot and no MIDI setup survive export");
        for (int index { 0 }; index < 8; ++index)
        {
            ChannelProperties channel (preset.getChannelVT (index), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            require (! channel.getAutoTrigger (), "No diagnostic or unused channel automatically starts on load");
            if (index < result.waves.size ())
            {
                require (channel.getChannelMode () == (index == 0 ? ChannelProperties::master : ChannelProperties::link), "Reference and sources follow the master trigger as Link");
                require (channel.getMixLevel () == -90.0 && std::get<0> (channel.getMixMod ()) == "Off" && std::get<1> (channel.getMixMod ()) == 0.0,
                         "Every populated channel has Mix and Mix modulation Off, including audio reference");
                require (channel.getLevel () == 0.0 && channel.getAttack () == 0.0 && channel.getRelease () == 0.0,
                         "Diagnostic playback does not double-apply gain or envelopes");
                require (channel.getZonesCV () == "Off" && std::get<0> (channel.getPitchCV ()) == "Off", "No external zone/pitch CV is assigned");
                const auto isReference { index == result.waves.size () - 1 };
                if (settings.signal != HardwareTestOutput::Signal::currentDesign || isReference)
                    require (channel.getPlayMode () == 1 && channel.getLoopMode () == 0 && channel.getPitch () == 0.0,
                             "Built-in tests and reference are one shot, no looping, unity rate");
                else
                {
                    const auto& design { settings.design };
                    const auto voice { design.mode == WaveformDesign::Mode::layers ? design.voices[static_cast<size_t> (index)] : WaveformDesign::Voice {} };
                    require (channel.getPitch () == juce::String (voice.detuneCents / 100.0).getDoubleValue ()
                             && channel.getPan () == juce::String (voice.pan).getDoubleValue (),
                             "Current design detune and pan use the existing hardware-preset decimal precision");
                    require (channel.getPlayMode () == (design.playback == WaveformDesign::Playback::gatedLoop ? 0 : 1)
                             && channel.getLoopMode () == (design.playback == WaveformDesign::Playback::oneShot ? 0 : design.playback == WaveformDesign::Playback::loop ? 1 : 2),
                             "Current design play/loop modes remain unchanged");
                }
            }
            for (int zoneIndex { 0 }; zoneIndex < 8; ++zoneIndex)
            {
                ZoneProperties zone (channel.getZoneVT (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                if (index >= result.waves.size () || zoneIndex != 0)
                    require (zone.getSample ().isEmpty (), "All other zones and channels remain empty");
                else
                {
                    AudioManager manager;
                    const auto reader { manager.getReaderFor (result.waves[index]) };
                    require (zone.getSample () == result.waves[index].getFileName () && zone.getSampleStart ().value_or (-1) == 0
                             && zone.getSampleEnd ().value_or (-1) == reader->lengthInSamples && zone.getLoopStart ().value_or (-1) == 0
                             && zone.getLoopLength ().value_or (-1) == reader->lengthInSamples && zone.getSide () == 0,
                             "Every used zone references its exact mono WAV with end-exclusive markers");
                    require (zone.getMinVoltage () == -5.0 && zone.getPitchOffset () == 0.0 && zone.getLevelOffset () == 0.0,
                             "Single zone covers CV range without extra pitch or gain");
                }
            }
        }
        require (ChannelCvSafety::validatePreset (tree, result.folder).wasOk (), "Diagnostic preset passes production CV-routing safety");
    }

    void checkReference (const HardwareTestOutput::Settings& settings, const HardwareTestOutput::ExportResult& result)
    {
        const auto manifest { juce::JSON::parse (result.manifest) };
        require (manifest["type"].toString () == "A8Manager.HardwareTestOutput" && static_cast<int> (manifest["schemaVersion"]) == 1,
                 "Manifest has a dedicated versioned signature, not an editable waveform recipe");
        require (static_cast<int> (manifest["referenceChannel"]) == result.waves.size () && static_cast<int> (manifest["presetNumber"]) == settings.presetNumber,
                 "Manifest routes agree with preset slot and adjacent reference channel");
        const auto rate { settings.design.sampleRate };
        const auto window { static_cast<int> (std::llround (settings.durationSeconds * rate)) };
        require (static_cast<int> (manifest["windowFrames"]) == window, "Scheduled test window is rounded to an exact source frame");
        const auto reference { readWave (result.waves.getLast (), rate, false) };
        require (reference.getNumSamples () == window + std::llround (rate * 0.12), "Reference file includes complete end pattern and trailing silence");
        juce::AudioBuffer<float> expected (1, reference.getNumSamples ());
        expected.clear ();
        const auto* bursts { manifest["referenceBursts"].getArray () };
        require (bursts != nullptr && bursts->size () == 5, "Reference has two start and three end coded pulses");
        for (int index { 0 }; index < bursts->size (); ++index)
        {
            const auto& burst { bursts->getReference (index) };
            const auto start { static_cast<int> (burst["startFrame"]) };
            const auto end { static_cast<int> (burst["endFrameExclusive"]) };
            const auto fade { static_cast<int> (burst["fadeFrames"]) };
            const auto frequency { static_cast<double> (burst["frequencyHz"]) };
            const auto peak { static_cast<double> (burst["peakFs"]) };
            const auto expectedStart { index < 2 ? index * static_cast<int> (std::llround (rate * 0.04))
                                                : window + (index - 2) * static_cast<int> (std::llround (rate * 0.04)) };
            require (start == expectedStart && end == start + std::llround (rate * 0.02) && fade == std::llround (rate * 0.002),
                     "Burst intervals and fades are known exactly in samples");
            require (frequency == (index < 2 ? 1000.0 : 2000.0) && burst["event"].toString () == (index < 2 ? "start" : "end")
                     && std::abs (20.0 * std::log10 (peak) + 30.0) < 1.0e-8, "Reference coding distinguishes events at -30 dBFS peak");
            for (int frame { start }; frame < end; ++frame)
            {
                const auto local { frame - start };
                const auto distance { std::min (local, end - frame - 1) };
                const auto gain { distance >= fade ? 1.0 : 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * distance / fade) };
                expected.setSample (0, frame, static_cast<float> (peak * gain * std::sin (juce::MathConstants<double>::twoPi * frequency * local / rate)));
            }
        }
        for (int frame { 0 }; frame < reference.getNumSamples (); ++frame)
            require (std::abs (reference.getSample (0, frame) - expected.getSample (0, frame)) <= tolerance,
                     "Reference PCM matches every described burst and is zero elsewhere");
    }
}

void testHardwareTestOutput ()
{
    using namespace HardwareTestOutput;
    const auto root { juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("a8-hardware-test-" + juce::Uuid ().toString ()) };
    require (root.createDirectory ().wasOk (), "Create owned hardware-test fixture directory");
    struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { root };
    const auto defaults { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType) };
    const auto untouched { defaults.createCopy () };
    Settings settings;
    settings.durationSeconds = 1.00003125; // Non-integral frame count tests rounding and section boundaries.
    settings.presetNumber = 47;
    settings.level = 0.125;
    juce::File firstFolder;
    juce::String originalManifest;
    for (const auto signal : { Signal::cvLevels, Signal::cvSine, Signal::cvRamp, Signal::audioTone })
    {
        settings.signal = signal;
        ExportResult exported;
        const auto status { exportPackage (settings, root, "diagnostic", exported) };
        require (status.wasOk (), status.getErrorMessage ().toRawUTF8 ());
        require (exported.preset.getFileName () == "prst047.yml" && exported.waves.size () == 2, "Built-in package uses requested slot and reserves separate reference");
        require (! exported.folder.getChildFile ("design.json").exists (), "Built-ins do not masquerade as editable designer recipes");
        const auto readme { exported.folder.getChildFile ("README.txt").loadFileAsString () };
        require (readme.startsWith ("A8Manager Hardware Test Output\n\n") && readme.contains ("NOT proof") && readme.contains ("NOT volts"),
                 "Instructions distinguish scheduled markers and digital values from observed hardware behavior");
        checkPreset (settings, exported);
        checkReference (settings, exported);
        const auto audio { readWave (exported.waves[0], settings.design.sampleRate, signal != Signal::audioTone) };
        const auto frames { static_cast<int> (std::llround (settings.durationSeconds * settings.design.sampleRate)) };
        require (audio.getNumSamples () == frames && audio.getSample (0, 0) == 0.0f && audio.getSample (0, frames - 1) == 0.0f,
                 "Built-in output has exact rounded duration and zero endpoints");
        for (int frame { 0 }; frame < frames; ++frame)
        {
            double expected { 0.0 };
            if (signal == Signal::cvLevels)
                expected = frame >= frames / 5 && frame < frames * 2 / 5 ? settings.level
                         : frame >= frames * 3 / 5 && frame < frames * 4 / 5 ? -settings.level : 0.0;
            else if (signal == Signal::cvSine)
                expected = settings.level * std::sin (juce::MathConstants<double>::twoPi * frame / (frames - 1));
            else if (signal == Signal::cvRamp)
                expected = settings.level * (1.0 - std::abs (2.0 * frame / (frames - 1) - 1.0));
            else
            {
                const auto fade { static_cast<int> (std::llround (settings.design.sampleRate * 0.005)) };
                const auto distance { std::min (frame, frames - frame - 1) };
                const auto gain { distance >= fade ? 1.0 : 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * distance / fade) };
                expected = settings.level * gain * std::sin (juce::MathConstants<double>::twoPi * 440.0 * frame / settings.design.sampleRate);
            }
            require (std::abs (audio.getSample (0, frame) - expected) <= tolerance, "All diagnostic PCM values, fades and held DC windows match intended formula");
        }
        if (signal == Signal::cvLevels)
        {
            firstFolder = exported.folder;
            originalManifest = exported.manifest.loadFileAsString ();
            const auto manifest { juce::JSON::parse (exported.manifest) };
            const auto* sections { manifest["cvSections"].getArray () };
            require (sections != nullptr && sections->size () == 5, "DC sequence documents all five held windows");
            for (int section { 0 }; section < 5; ++section)
            {
                const auto& item { sections->getReference (section) };
                require (static_cast<int> (item["startFrame"]) == frames * section / 5 && static_cast<int> (item["endFrameExclusive"]) == frames * (section + 1) / 5,
                         "Held CV manifest intervals match PCM boundaries");
                require (std::abs (static_cast<double> (item["expectedDigitalSample"]) - audio.getSample (0, frames * section / 5)) <= tolerance,
                         "Manifest expected CV values are digital sample values");
            }
        }
        else require (exported.folder != firstFolder && firstFolder.getChildFile ("test-manifest.json").loadFileAsString () == originalManifest,
                      "Repeated package names never replace an earlier export");
    }

    settings.signal = Signal::currentDesign;
    settings.design = WaveformDesign::startingPoint (WaveformDesign::Mode::layers, WaveformDesign::Shape::saw);
    settings.design.sampleRate = 96000.0;
    settings.design.voiceCount = 3;
    settings.design.amplitude = 0.7;
    settings.design.voices[0] = { -8.5, 10.0, -1.0, 0.8 };
    settings.design.voices[1] = { 0.0, 80.0, 0.0, 0.6 };
    settings.design.voices[2] = { 8.5, 170.0, 1.0, 0.4 };
    settings.design.playback = WaveformDesign::Playback::loop;
    WaveformDesign::Render rendered;
    require (WaveformDesign::render (settings.design, rendered).wasOk (), "Render exact current-design reference");
    ExportResult current;
    const auto currentStatus { exportPackage (settings, root, "current", current) };
    require (currentStatus.wasOk (), currentStatus.getErrorMessage ().toRawUTF8 ());
    require (current.waves.size () == 4, "Three source voices get a fourth independent reference channel");
    checkPreset (settings, current);
    checkReference (settings, current);
    WaveformDesign::Settings recalled;
    require (WaveformDesign::fromJson (juce::JSON::parse (current.folder.getChildFile ("design.json")), recalled).wasOk (), "Current design retains ordinary editable recipe");
    require (juce::JSON::toString (WaveformDesign::toJson (recalled)) == juce::JSON::toString (WaveformDesign::toJson (settings.design)),
             "Test package preserves every current-design setting without applying built-in level");
    for (int voice { 0 }; voice < 3; ++voice)
    {
        const auto decoded { readWave (current.waves[voice], settings.design.sampleRate, false) };
        require (decoded.getNumSamples () == rendered.frames, "Current cycle file is not stretched to marker-window duration");
        for (int frame { 0 }; frame < decoded.getNumSamples (); ++frame)
            require (std::abs (decoded.getSample (0, frame) - rendered.voices[static_cast<size_t> (voice)].getSample (0, frame)) <= tolerance,
                     "Current design waveform and per-voice gain are unchanged at every sample");
    }
    settings.design = WaveformDesign::startingPoint (WaveformDesign::Mode::layers, WaveformDesign::Shape::saw);
    settings.design.voiceCount = 7; // Keeps the fractional detune/pan of default spread voices.
    settings.design.cycleFrames = 64;
    ExportResult seven;
    const auto sevenStatus { exportPackage (settings, root, "seven-voice", seven) };
    require (sevenStatus.wasOk (), sevenStatus.getErrorMessage ().toRawUTF8 ());
    require (seven.waves.size () == 8, "A default seven-voice bank succeeds with reference on channel eight");
    checkPreset (settings, seven);
    checkReference (settings, seven);
    const auto sevenManifest { juce::JSON::parse (seven.manifest) };
    const auto* routes { sevenManifest["channels"].getArray () };
    require (routes != nullptr && routes->size () == 8, "Seven-voice manifest contains every source and reference route");
    require (WaveformDesign::render (settings.design, rendered).wasOk (), "Render exact seven-voice reference");
    for (int index { 0 }; index < 7; ++index)
    {
        const auto& route { routes->getReference (index) };
        const auto requested { settings.design.voices[static_cast<size_t> (index)].detuneCents / 100.0 };
        const auto emitted { juce::String (requested).getDoubleValue () };
        require (static_cast<double> (route["pitchSemitones"]) == emitted
                 && std::abs (static_cast<double> (route["requestedPitchSemitones"]) - requested) < 1.0e-14,
                 "Manifest distinguishes requested precision from actual serialized pitch");
        require (std::abs (static_cast<double> (route["nominalPlaybackSeconds"]) - rendered.frames / rendered.sampleRate / std::pow (2.0, emitted / 12.0)) < 1.0e-14,
                 "Manifest timing uses the pitch actually emitted to the module");
        const auto decoded { readWave (seven.waves[index], settings.design.sampleRate, false) };
        for (int frame { 0 }; frame < decoded.getNumSamples (); ++frame)
            require (std::abs (decoded.getSample (0, frame) - rendered.voices[static_cast<size_t> (index)].getSample (0, frame)) <= tolerance,
                     "Preset decimal precision never changes current-design PCM");
    }
    settings.design = WaveformDesign::startingPoint (WaveformDesign::Mode::modulation, WaveformDesign::Shape::sine);
    settings.design.offset = 0.1;
    settings.design.durationSeconds = 0.13;
    settings.design.amplitude = 0.2;
    settings.design.playback = WaveformDesign::Playback::gatedLoop;
    ExportResult currentCv;
    require (exportPackage (settings, root, "current-cv", currentCv).wasOk (), "Current CV design can be tested without removing its DC offset");
    checkPreset (settings, currentCv);
    checkReference (settings, currentCv);
    require (WaveformDesign::render (settings.design, rendered).wasOk (), "Render CV current-design reference");
    const auto cv { readWave (currentCv.waves[0], settings.design.sampleRate, true) };
    for (int frame { 0 }; frame < cv.getNumSamples (); ++frame)
        require (std::abs (cv.getSample (0, frame) - rendered.voices[0].getSample (0, frame)) <= tolerance,
                 "Current CV retains DC, waveform length and endpoints untouched");

    const auto valid { settings };
    auto rejected = [&] (Settings candidate)
    {
        ExportResult result { current };
        require (validate (candidate).failed () && exportPackage (candidate, root, "invalid", result).failed (), "Invalid settings are rejected before export");
        require (result.folder == juce::File () && result.preset == juce::File () && result.manifest == juce::File () && result.waves.isEmpty (),
                 "Failed export clears result rather than returning stale files");
    };
    for (const auto duration : { 0.99, 60.01, std::numeric_limits<double>::quiet_NaN (), std::numeric_limits<double>::infinity () })
    { auto invalid { valid }; invalid.durationSeconds = duration; rejected (invalid); }
    for (const auto level : { 0.009, 0.251, std::numeric_limits<double>::quiet_NaN () })
    { auto invalid { valid }; invalid.level = level; rejected (invalid); }
    for (const auto preset : { 0, 200 }) { auto invalid { valid }; invalid.presetNumber = preset; rejected (invalid); }
    { auto invalid { valid }; invalid.design.sampleRate = std::numeric_limits<double>::quiet_NaN (); rejected (invalid); }
    { auto invalid { valid }; invalid.signal = static_cast<Signal> (99); rejected (invalid); }
    { auto invalid { valid }; invalid.design.mode = WaveformDesign::Mode::layers; invalid.design.voiceCount = 8; rejected (invalid);
      invalid.signal = Signal::cvLevels; require (validate (invalid).wasOk (), "Built-ins do not inherit an unrelated eight-voice design restriction"); }
    ExportResult missing;
    require (exportPackage (valid, root.getChildFile ("does-not-exist"), "invalid", missing).failed (), "Missing parent cannot cause implicit writes elsewhere");
    require (defaults.isEquivalentTo (untouched), "Diagnostic export never changes shared default preset data");
    for (const auto& folder : root.findChildFiles (juce::File::findDirectories, false))
        require (! folder.getFileName ().startsWith (".a8-hardware-test-"), "No private staging folders remain after success or validation failure");
}
