#include "Assimil8or/Audio/WaveformDesignExport.h"
#include "Assimil8or/Audio/AudioManager.h"
#include "Assimil8or/Audio/CvSampleSafety.h"
#include "Assimil8or/Assimil8orPreset.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace
{
    void require (bool condition, const char* message) { if (! condition) throw std::runtime_error (message); }

    bool empty (const WaveformDesign::ExportResult& result)
    {
        return result.folder == juce::File () && result.preset == juce::File () && result.recipe == juce::File () && result.waves.isEmpty ();
    }

    juce::ValueTree readPreset (const juce::File& file)
    {
        juce::StringArray lines;
        file.readLines (lines);
        require (! lines.isEmpty (), "Generated preset is readable");
        Assimil8orPreset reader;
        reader.parse (lines);
        require (reader.getParseErrorsVT ().getNumChildren () == 0, "Generated preset passes real Assimil8or parser");
        return reader.getPresetVT ().createCopy ();
    }

    void compareWave (const juce::File& file, const juce::AudioBuffer<float>& expected, double rate)
    {
        AudioManager manager;
        auto reader { manager.getReaderFor (file) };
        require (reader && reader->numChannels == 1 && reader->sampleRate == rate && reader->bitsPerSample == 24
                 && ! reader->usesFloatingPointData && reader->lengthInSamples == expected.getNumSamples (), "Generated WAV is exact-length mono PCM24 at intended rate");
        require (manager.isAssimil8orSupportedAudioFile (file), "Exported WAV passes production hardware-format validator");
        juce::AudioBuffer<float> decoded (1, expected.getNumSamples ());
        require (reader->read (&decoded, 0, decoded.getNumSamples (), 0, true, false), "Read generated WAV data");
        for (int index { 0 }; index < decoded.getNumSamples (); ++index)
            require (std::abs (decoded.getSample (0, index) - expected.getSample (0, index)) < 3.0 / 8388608.0,
                     "Export preserves rendered samples, DC, phase and gain without normalizing");
    }

    void writeWaveFixture (const juce::File& file, const juce::AudioBuffer<float>& audio, double rate,
                           const juce::StringPairArray& metadata = {})
    {
        std::unique_ptr<juce::OutputStream> stream { file.createOutputStream () };
        std::unordered_map<juce::String, juce::String> values;
        for (int index { 0 }; index < metadata.size (); ++index)
            values.emplace (metadata.getAllKeys ()[index], metadata.getAllValues ()[index]);
        juce::WavAudioFormat format;
        auto writer { format.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (rate)
                                             .withNumChannels (audio.getNumChannels ()).withBitsPerSample (24).withMetadataValues (values)) };
        require (writer && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples ()) && writer->flush (), "Write owned purpose fixture");
    }

    bool isCvFile (const juce::File& file)
    {
        AudioManager manager;
        const auto reader { manager.getReaderFor (file) };
        require (reader != nullptr, "Read provenance fixture");
        return CvSampleSafety::isCv (file, *reader);
    }

    void checkCvProvenance (const juce::File& root, const WaveformDesign::Settings& cv, const WaveformDesign::Render& rendered,
                            const WaveformDesign::ExportResult& exported)
    {
        const auto original { exported.waves[0] };
        require (isCvFile (original), "New modulation WAV declares its CV purpose");
        AudioManager manager;
        const auto reader { manager.getReaderFor (original) };
        require (CvSampleSafety::hasCvMetadata (reader->metadataValues)
                 && reader->metadataValues[juce::WavAudioFormat::riffInfoComment2] == CvSampleSafety::cvMarker,
                 "New CV export carries the exact reserved RIFF INFO/ICMT marker");
        const auto copied { root.getChildFile ("renamed-export.wav") };
        const auto moved { root.getChildFile ("another-name.wav") };
        require (original.copyFileTo (copied) && isCvFile (copied) && copied.moveFileTo (moved) && isCvFile (moved),
                 "Embedded CV purpose survives copy, move and rename without its recipe");
        compareWave (moved, rendered.voices[0], rendered.sampleRate);

        auto metadata { CvSampleSafety::exportMetadata (false) };
        metadata.set (juce::WavAudioFormat::riffInfoArtist, "Fixture author");
        metadata.set ("unrelated", "Preserve me");
        metadata.set (juce::WavAudioFormat::riffInfoComment2, juce::String (CvSampleSafety::audioMarker) + "\nExisting descriptive comment");
        CvSampleSafety::markCv (metadata);
        const auto markedComment { metadata[juce::WavAudioFormat::riffInfoComment2] };
        CvSampleSafety::markCv (metadata);
        require (CvSampleSafety::hasCvMetadata (metadata) && metadata[juce::WavAudioFormat::riffInfoComment2] == markedComment
                 && markedComment.contains ("Existing descriptive comment") && metadata[juce::WavAudioFormat::riffInfoArtist] == "Fixture author"
                 && metadata["unrelated"] == "Preserve me", "Marking CV is idempotent and preserves unrelated metadata and comments");
        const auto conflicting { root.getChildFile ("conflicting-purpose.wav") };
        writeWaveFixture (conflicting, rendered.voices[0], rendered.sampleRate, metadata);
        require (isCvFile (conflicting), "An explicit CV tag takes safety precedence over a conflicting audio tag");
        compareWave (conflicting, rendered.voices[0], rendered.sampleRate);

        const auto legacyFolder { root.getChildFile ("legacy-cv") };
        require (legacyFolder.createDirectory ().wasOk (), "Create old-format export fixture");
        const auto legacy { legacyFolder.getChildFile ("voice-01.wav") };
        const auto recipe { legacyFolder.getChildFile ("design.json") };
        writeWaveFixture (legacy, rendered.voices[0], rendered.sampleRate);
        auto writeRecipe = [&] (const juce::var& json)
        {
            require (recipe.replaceWithText (juce::JSON::toString (json)), "Write owned legacy recipe fixture");
        };
        require (! isCvFile (legacy), "The old voice filename alone is not evidence of CV purpose");
        writeRecipe (WaveformDesign::toJson (cv));
        require (isCvFile (legacy), "Old untagged modulation export is recognized from a matching recipe and PCM dimensions");
        compareWave (legacy, rendered.voices[0], rendered.sampleRate);
        const auto ordinary { legacyFolder.getChildFile ("ordinary-audio.wav") };
        require (legacy.copyFileTo (ordinary) && ! isCvFile (ordinary), "An unrelated filename next to a CV recipe is not classified as an old exported voice");
        const auto separated { root.getChildFile ("cv-slow-wave.wav") };
        require (legacy.copyFileTo (separated) && ! isCvFile (separated), "Untagged samples are not inferred to be CV from a suggestive name or DC signal");

        auto changed { cv };
        changed.durationSeconds *= 2.0;
        writeRecipe (WaveformDesign::toJson (changed));
        require (! isCvFile (legacy), "A recipe with the wrong frame count cannot claim an unrelated WAV");
        changed = cv;
        changed.sampleRate = 96000.0;
        writeRecipe (WaveformDesign::toJson (changed));
        require (! isCvFile (legacy), "A recipe with the wrong sample rate is rejected");
        changed = cv;
        changed.mode = WaveformDesign::Mode::oscillator;
        writeRecipe (WaveformDesign::toJson (changed));
        require (! isCvFile (legacy), "An audio recipe never declares an untagged WAV to be CV");
        auto wrongVersion { WaveformDesign::toJson (cv) };
        wrongVersion.getDynamicObject ()->setProperty ("version", 2);
        writeRecipe (wrongVersion);
        require (! isCvFile (legacy), "An unsupported recipe version is not guessed");
        auto unrelated { WaveformDesign::toJson (cv) };
        unrelated.getDynamicObject ()->setProperty ("type", "SomeOtherTool.WaveformDesign");
        writeRecipe (unrelated);
        require (! isCvFile (legacy), "Other applications' sidecars do not establish A8Manager provenance");
        auto invalidNumeric { WaveformDesign::toJson (cv) };
        invalidNumeric.getDynamicObject ()->getProperty ("settings").getDynamicObject ()->setProperty ("sampleRate", "48000");
        writeRecipe (invalidNumeric);
        require (! isCvFile (legacy), "String-coerced dimension fields are rejected");
        require (recipe.replaceWithText ("{ broken json") && ! isCvFile (legacy), "Malformed recipes cannot classify a file");
        const auto validRecipe { juce::JSON::toString (WaveformDesign::toJson (cv)) };
        require (recipe.replaceWithText (validRecipe + " trailing garbage") && ! isCvFile (legacy),
                 "A valid JSON prefix followed by garbage is not a complete recipe");
        require (recipe.replaceWithText (validRecipe + "{}") && ! isCvFile (legacy), "Concatenated JSON documents are rejected");
        require (recipe.replaceWithText ("{\"nested\":" + juce::String::repeatedString ("[", 4096) + "0"
                                         + juce::String::repeatedString ("]", 4096) + "}") && ! isCvFile (legacy),
                 "Deeply nested JSON is rejected before entering the recursive parser");
        auto quoted { WaveformDesign::toJson (cv) };
        quoted.getDynamicObject ()->setProperty ("notes", "Braces {} and arrays [] within a string, escaped quotes \" and a backslash \\, are not nesting.");
        writeRecipe (quoted);
        require (isCvFile (legacy), "Structural preflight respects escaped quotes and brackets within strings");
        const char invalidUtf8[] { '{', '"', 'x', '"', ':', '"', static_cast<char> (0xff), '"', '}' };
        require (recipe.replaceWithData (invalidUtf8, sizeof (invalidUtf8)) && ! isCvFile (legacy),
                 "Invalid UTF-8 is rejected before constructing a JUCE string");
        require (recipe.replaceWithText (juce::JSON::toString (WaveformDesign::toJson (cv)) + juce::String::repeatedString (" ", 65536))
                 && ! isCvFile (legacy), "Oversized sidecars are ignored without unbounded JSON reads");
        writeRecipe (WaveformDesign::toJson (cv));

        const auto explicitAudioFolder { root.getChildFile ("explicit-audio") };
        require (explicitAudioFolder.createDirectory ().wasOk (), "Create explicit audio fixture directory");
        const auto explicitAudio { explicitAudioFolder.getChildFile ("voice-01.wav") };
        writeWaveFixture (explicitAudio, rendered.voices[0], rendered.sampleRate, CvSampleSafety::exportMetadata (false));
        require (recipe.copyFileTo (explicitAudioFolder.getChildFile ("design.json")) && ! isCvFile (explicitAudio),
                 "An explicit audio tag overrides a coincidental otherwise matching legacy CV recipe");
        compareWave (explicitAudio, rendered.voices[0], rendered.sampleRate);

        metadata.clear ();
        metadata.set (juce::WavAudioFormat::riffInfoComment2, "A sentence mentions A8Manager.SamplePurpose/1=CV, not a declaration.");
        const auto mention { root.getChildFile ("mention.wav") };
        writeWaveFixture (mention, rendered.voices[0], rendered.sampleRate, metadata);
        require (! isCvFile (mention), "Only an exact reserved marker line declares purpose, not arbitrary comment substrings");
        metadata.set (juce::WavAudioFormat::riffInfoComment2, "A8Manager.SamplePurpose/2=CV");
        const auto future { root.getChildFile ("future-marker.wav") };
        writeWaveFixture (future, rendered.voices[0], rendered.sampleRate, metadata);
        require (! isCvFile (future), "Unknown purpose versions are not silently interpreted");
    }

    void checkPreset (juce::ValueTree tree, const WaveformDesign::Settings& settings, const WaveformDesign::ExportResult& exported,
                      const WaveformDesign::Render& rendered)
    {
        PresetProperties preset (tree, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        require (preset.getId () == 1 && preset.getName ().length () <= 12, "Export uses preset 001 with bounded display name");
        require (tree.getNumChildren () == 8, "Parsed preset contains all eight channels");
        for (int index { 0 }; index < 8; ++index)
        {
            ChannelProperties channel (preset.getChannelVT (index), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            require (channel.getValueTree ().getNumChildren () == 8, "Parsed channel contains all eight zones");
            for (int zoneIndex { 0 }; zoneIndex < 8; ++zoneIndex)
            {
                ZoneProperties zone (channel.getZoneVT (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                if (index >= exported.waves.size () || zoneIndex != 0)
                {
                    require (zone.getSample ().isEmpty (), "Unused channels and zones remain empty defaults");
                    continue;
                }
                require (zone.getSample () == exported.waves[index].getFileName () && zone.getSide () == 0, "Voice is assigned to zone one as mono");
                require (zone.getSampleStart ().value_or (-1) == 0 && zone.getSampleEnd ().value_or (-1) == rendered.frames
                         && zone.getLoopStart ().value_or (-1) == 0 && zone.getLoopLength ().value_or (-1) == rendered.frames,
                         "Sample and loop markers span exactly the file, with exclusive end");
                require (zone.getMinVoltage () == -5.0 && zone.getLevelOffset () == 0.0 && zone.getPitchOffset () == 0.0,
                         "Zone selects full voltage range without double-applying level or pitch");
            }
            if (index >= exported.waves.size ()) continue;
            const auto voice { settings.mode == WaveformDesign::Mode::layers ? settings.voices[static_cast<size_t> (index)] : WaveformDesign::Voice {} };
            require (channel.getChannelMode () == (index == 0 ? ChannelProperties::master : ChannelProperties::link), "Layers use Master/Link, not stereo inheritance");
            require (std::abs (channel.getPitch () - voice.detuneCents / 100.0) < 0.000001 && std::abs (channel.getPan () - voice.pan) < 0.000001,
                     "Each layer's pitch in semitones and pan survive preset round-trip");
            require (channel.getLevel () == 0.0 && channel.getAttack () == 0.0 && channel.getRelease () == 0.0 && ! channel.getAutoTrigger (),
                     "Individual gain is unity, contours are not doubled, and load does not trigger playback");
            const auto mixDb { settings.mode == WaveformDesign::Mode::modulation ? -90.0
                : std::floor (-20.0 * std::log10 (static_cast<double> (exported.waves.size ())) * 10.0) / 10.0 };
            require (std::abs (channel.getMixLevel () - mixDb) < 0.000001, "CV uses the intended Mix Off sentinel; audio retains dB mix headroom without attenuating individual outputs");
            require (channel.getPlayMode () == (settings.playback == WaveformDesign::Playback::gatedLoop ? 0 : 1)
                     && channel.getLoopMode () == (settings.playback == WaveformDesign::Playback::oneShot ? 0 : settings.playback == WaveformDesign::Playback::loop ? 1 : 2),
                     "Requested one-shot, continuous and gated-loop modes map correctly");
            require (std::get<0> (channel.getPitchCV ()) == "Off" && channel.getZonesCV () == "Off", "Exporter does not silently assign external CV inputs");
        }
    }
}

void testWaveformDesignExport ()
{
    using namespace WaveformDesign;
    const auto root { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-design-export-test", "", false) };
    require (root.createDirectory ().wasOk (), "Create owned export fixture folder");
    struct Cleanup { juce::File directory; ~Cleanup () { directory.deleteRecursively (); } } cleanup { root };
    const auto defaults { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType) };
    const auto untouchedDefaults { defaults.createCopy () };

    auto settings { startingPoint (Mode::layers, Shape::sine) };
    settings.voiceCount = 3;
    settings.cycleFrames = 512;
    settings.amplitude = 0.7;
    settings.voices[0] = { -7.0, 0.0, -1.0, 1.0 };
    settings.voices[1] = { 0.0, 90.0, 0.0, 0.6 };
    settings.voices[2] = { 7.0, 180.0, 1.0, 0.25 };
    settings.playback = Playback::loop;
    Render rendered;
    require (render (settings, rendered).wasOk (), "Render layered fixture");
    ExportResult exported;
    require (exportDesign (settings, root, "Layered test", exported).wasOk (), "Export layered design");
    require (exported.folder.getFileName () == "Layered-test", "New package names begin with the supplied name, not an automatic product prefix");
    require (readPreset (exported.preset).getProperty (PresetProperties::NamePropertyId).toString () == "Layered-test",
             "Generated preset name uses the entered name without an automatic prefix");
    require (exported.waves.size () == 3 && exported.folder.getParentDirectory () == root && exported.preset.existsAsFile ()
             && exported.recipe.existsAsFile () && exported.folder.getChildFile ("README.txt").existsAsFile (), "Export contains complete self-contained bundle");
    for (int index { 0 }; index < exported.waves.size (); ++index)
    {
        require (exported.waves[index].getFileName ().length () <= 47, "Hardware sample filename bound");
        compareWave (exported.waves[index], rendered.voices[static_cast<size_t> (index)], rendered.sampleRate);
        AudioManager manager;
        const auto audioReader { manager.getReaderFor (exported.waves[index]) };
        require (audioReader && ! CvSampleSafety::isCv (exported.waves[index], *audioReader)
                 && audioReader->metadataValues[juce::WavAudioFormat::riffInfoComment2] == CvSampleSafety::audioMarker,
                 "Audio/layer exports are explicitly audio-tagged rather than inferred from their name or signal");
    }
    juce::MemoryBlock firstWave, secondWave;
    require (exported.waves[0].loadFileAsData (firstWave) && exported.waves[1].loadFileAsData (secondWave) && firstWave != secondWave,
             "Different phase/gain layers are distinct files");
    checkPreset (readPreset (exported.preset), settings, exported, rendered);
    Settings recovered;
    require (fromJson (juce::JSON::parse (exported.recipe), recovered).wasOk () && recovered.voiceCount == 3
             && recovered.voices[0].detuneCents == -7.0 && recovered.voices[1].level == 0.6, "JSON recipe preserves editable voice settings");
    const auto notes { exported.folder.getChildFile ("README.txt").loadFileAsString () };
    const auto frequency { rendered.sampleRate / static_cast<double> (rendered.frames) * std::pow (2.0, -7.0 / 1200.0) };
    require (notes.contains (juce::String (frequency, 6) + " Hz") && notes.contains ("manual stop") && notes.contains ("not volts")
             && notes.contains ("UNKNOWN") && notes.contains ("speakers"), "README documents detuned frequency, stop behavior and unknown CV calibration");

    ExportResult again;
    require (exportDesign (settings, root, "Layered test", again).wasOk () && again.folder != exported.folder && exported.preset.existsAsFile (),
             "Repeated exports create new folders without replacing earlier designs");
    const auto occupied { root.getChildFile ("Collision") };
    const auto occupiedFolder { root.getChildFile ("Collision-2") };
    require (occupied.replaceWithText ("original file") && occupiedFolder.createDirectory ().wasOk (), "Create file and empty-folder collisions");
    require (exportDesign (settings, root, "Collision", again).wasOk () && again.folder.getFileName () == "Collision-3"
             && occupied.loadFileAsString () == "original file" && occupiedFolder.isDirectory ()
             && occupiedFolder.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0, "File and even empty-folder collisions are never overwritten");
    for (const auto* reserved : { "CON", "prn", "Aux", "NUL", "COM1", "com9", "LPT1", "lpt9" })
        require (ExportSupport::safeStem (reserved) == juce::String (reserved) + "_", "Reserved Windows device names get a suffix without a new prefix");
    require (ExportSupport::safeStem ("COM10") == "COM10" && ExportSupport::safeStem ("A8-user-name") == "A8-user-name"
             && ExportSupport::safeStem ("...") == "waveform", "Ordinary names and user-entered A8 names are retained while empty sanitization has a safe fallback");
    require (exportDesign (settings, root, "CON", again).wasOk () && again.folder.getFileName () == "CON_", "Reserved names export portably without an automatic A8 prefix");

    auto cv { startingPoint (Mode::modulation, Shape::sine) };
    cv.amplitude = 0.0;
    cv.offset = 0.25;
    cv.durationSeconds = 0.1;
    cv.playback = Playback::oneShot;
    cv.measuredFullScaleVolts = 8.0;
    require (render (cv, rendered).wasOk () && exportDesign (cv, root, "../CV:" + juce::String::repeatedString ("x", 80), again).wasOk (), "Export DC CV with sanitized bounded name");
    require (again.folder.getFileName ().length () <= 31 && again.folder.getParentDirectory () == root, "Export name cannot escape parent or exceed hardware folder length");
    compareWave (again.waves[0], rendered.voices[0], rendered.sampleRate);
    checkPreset (readPreset (again.preset), cv, again, rendered);
    require (again.preset.loadFileAsString ().contains ("MixLevel : -90"), "CV Mix Off sentinel is written numerically and survives parser read-back");
    require (again.folder.getChildFile ("README.txt").loadFileAsString ().contains ("8.0000 V"), "README records user calibration without assuming nominal output volts");
    require (again.folder.getChildFile ("README.txt").loadFileAsString ().contains ("hardware mapping needs module verification"),
             "CV routing notes explicitly require verification of the minimum mix-level sentinel on hardware");
    require (again.folder.getChildFile ("README.txt").loadFileAsString ().contains ("Editors/converters may strip metadata"),
             "Generated safety instructions explain purpose tagging and its limits");
    checkCvProvenance (root, cv, rendered, again);

    settings.voiceCount = 8;
    settings.playback = Playback::gatedLoop;
    require (render (settings, rendered).wasOk () && exportDesign (settings, root, "Eight voices", again).wasOk () && again.waves.size () == 8, "Export all eight voices");
    checkPreset (readPreset (again.preset), settings, again, rendered);
    const auto countBeforeFailure { root.getNumberOfChildFiles (juce::File::findFilesAndDirectories) };
    require (exportDesign (settings, occupied, "Cannot export", again).failed () && empty (again), "Non-directory parent fails and clears stale export result");
    settings.cycleFrames = 0;
    require (exportDesign (settings, root, "Invalid", again).failed () && empty (again)
             && root.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == countBeforeFailure,
             "Invalid design publishes nothing and clears all output paths");
    for (const auto& child : root.findChildFiles (juce::File::findFilesAndDirectories, false))
        require (! child.getFileName ().startsWith (".a8-design-"), "No private staging folder remains after success or failure");
    require (defaults.isEquivalentTo (untouchedDefaults), "Export never modifies shared default preset tree");
    std::cout << "PASS: waveform design export PCM24/DC/layers, preset and recipe round-trips, headroom, safe filenames, collision and failure handling\n";
}
