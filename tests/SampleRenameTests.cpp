#include "Assimil8or/Audio/SampleRename.h"
#include "Assimil8or/Audio/WaveformDesignAssignment.h"
#include "Assimil8or/Audio/WaveformDesignExport.h"
#include "Assimil8or/Audio/WaveformDesignRecall.h"
#include "Assimil8or/Audio/WaveformDesignSidecars.h"
#include "Assimil8or/Audio/AudioManager.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "Assimil8or/Preset/PresetProperties.h"
#include "Assimil8or/PresetFolderCopy.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
    void require (bool okay, const char* message) { if (! okay) throw std::runtime_error (message); }
    void succeeded (const juce::Result& result) { if (result.failed ()) throw std::runtime_error (result.getErrorMessage ().toStdString ()); }
    juce::ValueTree defaults ()
    {
        return ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ()
            .getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType).createCopy ();
    }
    void wave (const juce::File& file, int bits = 24, int channels = 2, int frames = 128)
    {
        juce::AudioBuffer<float> samples (channels, frames);
        for (int channel { 0 }; channel < channels; ++channel)
            for (int frame { 0 }; frame < frames; ++frame)
                samples.setSample (channel, frame, (channel == 0 ? 1.0f : -1.0f) * (0.2f + static_cast<float> (frame % 30) / 100.0f));
        std::unique_ptr<juce::OutputStream> stream { file.createOutputStream () };
        juce::WavAudioFormat format;
        auto writer { format.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (48000)
            .withNumChannels (channels).withBitsPerSample (bits)
            .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::integral)) };
        require (writer && writer->writeFromAudioSampleBuffer (samples, 0, frames) && writer->flush (), "Create PCM fixture");
    }
    juce::ValueTree assigned (const juce::String& filename)
    {
        auto tree { defaults () };
        for (int channel { 0 }; channel < 2; ++channel)
            for (int zone { 0 }; zone < 2; ++zone)
            {
                auto item { tree.getChild (channel).getChild (zone) };
                item.setProperty (ZoneProperties::SamplePropertyId, filename, nullptr);
                item.setProperty (ZoneProperties::SidePropertyId, channel, nullptr);
                item.setProperty (ZoneProperties::SampleStartPropertyId, 9 + zone, nullptr);
                item.setProperty (ZoneProperties::SampleEndPropertyId, 100, nullptr);
                item.setProperty (ZoneProperties::LoopStartPropertyId, 20, nullptr);
                item.setProperty (ZoneProperties::LoopLengthPropertyId, 64.0, nullptr);
                item.setProperty (ZoneProperties::PitchOffsetPropertyId, 3.01, nullptr);
            }
        tree.getChild (1).setProperty (ChannelProperties::ChannelModePropertyId, ChannelProperties::stereoRight, nullptr);
        return tree;
    }
    juce::String hash (const juce::File& file) { return juce::SHA256 (file).toHexString (); }
}

void testSampleRename ()
{
    const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("a8-sample-rename-" + juce::Uuid ().toString ()) };
    succeeded (folder.createDirectory ());
    struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } remove { folder };
    juce::String name;
    succeeded (SampleRename::validateName ("Short take", name));
    require (name == "Short take.wav", "Omitting extension preserves exact chosen short name");
    for (const auto* invalid : { "", "../escape", ".hidden", "CON", "LPT1", "aux.wav", "NUL.extra.wav", "bad/name", "bad\\name", "bad:name", "trailing.", "trailing .wav" })
        require (SampleRename::validateName (invalid, name).failed (), "Unsafe and reserved names are refused");
    require (SampleRename::validateName (juce::String::repeatedString ("x", 44), name).failed (), "Enforce 47-character WAV name limit");

    for (int bits : { 8, 16, 24, 32 })
    {
        const auto source { folder.getChildFile ("Original" + juce::String (bits) + ".wav") };
        wave (source, bits);
        const auto originalHash { hash (source) };
        const auto preset { assigned (source.getFileName ()) };
        const auto untouched { preset.createCopy () };
        SampleRename::Result result;
        succeeded (SampleRename::prepare (folder, preset, source.getFileName (), "Short" + juce::String (bits), result));
        require (result.filename == "Short" + juce::String (bits) + ".wav" && result.references == 4 && result.createdFiles.size () == 1,
            "Copy updates all shared current-preset references including stereo right");
        auto expected { untouched.createCopy () };
        for (int channel { 0 }; channel < 2; ++channel)
            for (int zone { 0 }; zone < 2; ++zone)
                expected.getChild (channel).getChild (zone).setProperty (ZoneProperties::SamplePropertyId, result.filename, nullptr);
        require (result.editedPreset.isEquivalentTo (expected) && preset.isEquivalentTo (untouched), "Markers, sides, channel settings and original tree stay unchanged");
        require (hash (result.createdFiles[0]) == originalHash && hash (source) == originalHash, "All integer PCM bit depths copy byte-for-byte, originals unchanged");
        SampleRename::Result noop;
        succeeded (SampleRename::prepare (folder, preset, source.getFileName (), source.getFileName (), noop));
        require (noop.references == 0 && noop.createdFiles.isEmpty () && noop.editedPreset.isEquivalentTo (preset), "Same name is a clean no-op");
        SampleRename::Result collision;
        require (SampleRename::prepare (folder, preset, source.getFileName (), result.filename.toUpperCase (), collision).failed (), "Case-insensitive collisions never overwrite");
        succeeded (SampleRename::cleanup (result));
        require (source.existsAsFile () && hash (source) == originalHash, "Rollback deletes only the new copy");
    }

    const auto ordinary { folder.getChildFile ("voice-01.wav") };
    wave (ordinary);
    auto ordinaryPreset { assigned (ordinary.getFileName ()) };
    SampleRename::Result normal;
    succeeded (SampleRename::prepare (folder, ordinaryPreset, ordinary.getFileName (), "Ordinary", normal));
    require (normal.createdFiles.size () == 1, "An ordinary generated-looking filename without recipe remains renamable");
    succeeded (SampleRename::cleanup (normal));
    require (folder.getChildFile ("Occupied.design.json").replaceWithText ("unrelated"), "Create conflicting sidecar fixture");
    require (SampleRename::prepare (folder, ordinaryPreset, ordinary.getFileName (), "Occupied", normal).failed (), "Refuse destination sidecar collision even for ordinary audio");
    require (folder.getChildFile ("prst001.yml").replaceWithText ("Other presets must not be rewritten"), "Create preset preservation fixture");
    SampleRename::Result changed;
    succeeded (SampleRename::prepare (folder, ordinaryPreset, ordinary.getFileName (), "External edit", changed));
    require (changed.createdFiles[0].appendText ("external modification"), "Modify owned output fixture");
    require (SampleRename::cleanup (changed).failed () && folder.getChildFile ("External edit.wav").existsAsFile (), "Rollback preserves an externally edited new copy");
    require (folder.getChildFile ("prst001.yml").loadFileAsString () == "Other presets must not be rewritten", "No YAML is modified");

    using namespace WaveformDesign;
    for (const auto mode : { Mode::oscillator, Mode::modulation, Mode::layers })
    {
        auto settings { startingPoint (mode, Shape::triangle) };
        settings.cycleFrames = 128;
        settings.durationSeconds = 0.01;
        if (mode == Mode::layers) spreadVoices (settings, 3, 15, 160, 0.6);
        AssignmentResult assignment;
        succeeded (prepareAssignment (settings, folder, "Generated", defaults (), 0, 0, assignment));
        const auto voice { mode == Mode::layers ? 2 : 0 };
        const auto source { assignment.waves[voice] };
        const auto before { hash (source) };
        const auto requested { mode == Mode::layers ? juce::String::repeatedString ("z", 43) : mode == Mode::modulation ? "Slow CV" : "Lead" };
        SampleRename::Result result;
        succeeded (SampleRename::prepare (folder, assignment.editedPreset, source.getFileName (), requested, result));
        require (result.createdFiles.size () == 2 && hash (source) == before && hash (folder.getChildFile (result.filename)) == before,
            "Generated audio/CV/bank copy preserves exact WAV and original");
        WaveformDesignRecall::RecalledDesign recalled;
        succeeded (WaveformDesignRecall::recallWave (folder.getChildFile (result.filename), recalled));
        require (recalled.voiceIndex == voice && juce::JSON::toString (toJson (recalled.settings)) == juce::JSON::toString (toJson (settings)),
            "Short-name recall retains full bank settings and selected voice");
        require (recalled.recipe.getFileName ().length () <= 47 && WaveformDesignSidecars::identify (recalled.recipe) == WaveformDesignSidecars::Kind::recipe,
            "Long WAV names get recognized, hardware-safe sidecar names");
        AudioManager audio;
        auto reader { audio.getReaderFor (folder.getChildFile (result.filename)) };
        require (reader && CvSampleSafety::isCv (folder.getChildFile (result.filename), *reader) == (mode == Mode::modulation), "Renaming preserves CV protection");
        reader.reset ();
        result.editedPreset.setProperty (PresetProperties::NamePropertyId, "Copy " + juce::String (static_cast<int> (mode)), nullptr);
        juce::File copied;
        succeeded (PresetFolderCopy::createOrUpdate (folder, result.editedPreset, copied));
        succeeded (WaveformDesignRecall::recallWave (copied.getChildFile (result.filename), recalled));
        SampleRename::Result again;
        succeeded (SampleRename::prepare (folder, result.editedPreset, result.filename, "Again " + juce::String (static_cast<int> (mode)), again));
        succeeded (WaveformDesignRecall::recallWave (folder.getChildFile (again.filename), recalled));
        require (recalled.voiceIndex == voice, "A renamed generated copy can be renamed again");
        succeeded (SampleRename::cleanup (again));
        SampleRename::Result rollback;
        int publications { 0 };
        require (SampleRename::prepare (folder, assignment.editedPreset, source.getFileName (), "Fail" + juce::String (static_cast<int> (mode)), rollback,
            [&] (const juce::File& staged, const juce::File& target)
            {
                if (++publications == 2) return juce::Result::fail ("Injected sidecar publication failure");
                return ExportSupport::publishExclusive (staged, target);
            }).failed () && rollback.createdFiles.isEmpty (), "Sidecar publication failure rolls back its owned WAV");
        require (hash (source) == before, "Failed rename leaves original audio unchanged");
        SampleRename::Result raced;
        const auto raceName { "Race" + juce::String (static_cast<int> (mode)) + ".wav" };
        require (SampleRename::prepare (folder, assignment.editedPreset, source.getFileName (), raceName, raced,
            [&] (const juce::File& staged, const juce::File& target)
            {
                require (target.replaceWithText ("Concurrent owner"), "Inject a race immediately before exclusive publication");
                return ExportSupport::publishExclusive (staged, target);
            }).failed () && folder.getChildFile (raceName).loadFileAsString () == "Concurrent owner", "Publication race cannot overwrite another writer");
        SampleRename::Result tampered;
        succeeded (SampleRename::prepare (folder, result.editedPreset, result.filename, "Tamper" + juce::String (static_cast<int> (mode)), tampered));
        const auto tamperedWave { folder.getChildFile (tampered.filename) };
        juce::MemoryBlock bytes;
        require (tamperedWave.loadFileAsData (bytes), "Read alias content test fixture");
        auto* data { static_cast<unsigned char*> (bytes.getData ()) };
        data[bytes.getSize () - 2] ^= 1;
        require (tamperedWave.replaceWithData (bytes.getData (), bytes.getSize ()), "Modify copied waveform without changing its size");
        require (WaveformDesignRecall::recallWave (tamperedWave, recalled).failed (), "Alias fingerprint rejects altered PCM even when dimensions still match");
        juce::var json;
        succeeded (WaveformDesignRecall::loadRecipe (WaveformDesignRecall::copiedRecipe (folder.getChildFile (result.filename)), recalled, &json));
        json.getDynamicObject ()->getProperty ("copiedWave").getDynamicObject ()->setProperty ("filename", "wrong.wav");
        require (WaveformDesignRecall::copiedRecipe (folder.getChildFile (result.filename)).replaceWithText (juce::JSON::toString (json)),
            "Replace owned alias fixture with a mismatched binding");
        require (WaveformDesignRecall::recallWave (folder.getChildFile (result.filename), recalled).failed (), "A mismatched alias binding never falls back to filename recognition");
    }

    const auto legacy { folder.getChildFile ("Legacy") };
    succeeded (legacy.createDirectory ());
    auto cvSettings { startingPoint (Mode::modulation, Shape::sine) };
    cvSettings.durationSeconds = 0.01;
    wave (legacy.getChildFile ("voice-01.wav"), 24, 1, 480);
    succeeded (ExportSupport::writeText (legacy.getChildFile ("design.json"), juce::JSON::toString (toJson (cvSettings))));
    auto legacyPreset { defaults () };
    ExportSupport::configureChannel (legacyPreset.getChild (0), cvSettings, 0, 1);
    ExportSupport::configureZone (legacyPreset.getChild (0).getChild (0), "voice-01.wav", 480);
    const auto legacyHash { hash (legacy.getChildFile ("voice-01.wav")) };
    SampleRename::Result legacyResult;
    succeeded (SampleRename::prepare (legacy, legacyPreset, "voice-01.wav", "Legacy CV", legacyResult));
    AudioManager audio;
    auto legacyReader { audio.getReaderFor (legacy.getChildFile (legacyResult.filename)) };
    require (legacyReader && CvSampleSafety::hasCvMetadata (legacyReader->metadataValues) && hash (legacy.getChildFile ("voice-01.wav")) == legacyHash,
        "Legacy recipe-only CV gets a persistent tag in its copy without touching original");
    WaveformDesignRecall::RecalledDesign recalled;
    succeeded (WaveformDesignRecall::recallWave (legacy.getChildFile (legacyResult.filename), recalled));
    require (recalled.settings.mode == Mode::modulation, "Legacy CV copy remains recallable and classified as CV");

    SampleRename::Result failure;
    require (SampleRename::prepare (folder, assigned ("missing.wav"), "missing.wav", "New", failure).failed (), "Missing source fails safely");
    require (folder.getChildFile ("voice-01.design.json").replaceWithText ("malformed recipe"), "Add malformed adjacent recipe fixture");
    require (SampleRename::prepare (folder, ordinaryPreset, ordinary.getFileName (), "Bad recipe", failure).failed ()
        && ! folder.getChildFile ("Bad recipe.wav").exists (), "Malformed associated recipes fail before publishing audio");
    std::error_code error;
    std::filesystem::create_symlink (std::filesystem::path (reinterpret_cast<const char8_t*> (ordinary.getFullPathName ().toRawUTF8 ())),
        std::filesystem::path (reinterpret_cast<const char8_t*> (folder.getChildFile ("Linked.wav").getFullPathName ().toRawUTF8 ())), error);
    if (! error) require (SampleRename::prepare (folder, assigned ("Linked.wav"), "Linked.wav", "Unlinked", failure).failed (), "Linked sources are refused");
    std::cout << "Sample rename/copy regression tests passed\n";
}
