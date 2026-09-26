#include "GUI/Assimil8or/Editor/ZoneEditor.h"
#include "GUI/Assimil8or/Editor/Envelope/AREnvelopeComponent.h"
#include "GUI/Assimil8or/Editor/Waveform/WaveformPresentation.h"
#include "GUI/ModernTheme.h"
#include "Assimil8or/Preset/ZonePurge.h"
#include "Assimil8or/SafeRename.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void check (bool value, const char* message) { if (! value) throw std::runtime_error (message); }
    void testRename ()
    {
        const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-rename-regression", "", false) };
        check (folder.createDirectory ().wasOk (), "Create isolated rename fixture");
        struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { folder };
        const auto source { folder.getChildFile (juce::String::repeatedString ("long", 20) + ".wav") };
        check (source.replaceWithText ("preserve these bytes"), "Create overlong-name file");
        check (! source.isDirectory (), "Overlong audio names are still files");
        juce::File destination;
        check (SafeRename::destination (source, "restored", destination).wasOk () && destination.getFileName () == "restored.wav", "Omitted extension is restored");
        check (SafeRename::apply (source, "../escape.wav").failed () && source.existsAsFile (), "Reject path traversal without moving original");
        check (SafeRename::apply (source, juce::String::repeatedString ("x", 48) + ".wav").failed () && source.exists (), "Reject excessive new names, keeping original recoverable");
        const auto collision { folder.getChildFile ("occupied.wav") };
        collision.replaceWithText ("do not overwrite");
        check (SafeRename::apply (source, "occupied.wav").failed () && collision.loadFileAsString () == "do not overwrite", "No overwriting another file");
        check (SafeRename::apply (source, "restored").wasOk () && ! source.exists () && destination.loadFileAsString () == "preserve these bytes", "Recover a file after an invalid rename attempt");
        check (SafeRename::apply (source, "missing").failed (), "Missing source reports failure");
    }

    void testPurge ()
    {
        juce::ValueTree preset { PresetProperties::PresetTypeId };
        for (auto channelIndex { 0 }; channelIndex < 2; ++channelIndex)
        {
            auto tree { ChannelProperties::create (channelIndex + 1) };
            preset.addChild (tree, -1, nullptr);
            ChannelProperties channel (tree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            channel.setChannelMode (channelIndex == 0 ? ChannelProperties::ChannelMode::master : ChannelProperties::ChannelMode::stereoRight, false);
            for (auto i { 0 }; i < 8; ++i)
            {
                auto z { ZoneProperties::create (i + 1) };
                tree.addChild (z, -1, nullptr);
                ZoneProperties zone (z, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                if (i < 3) zone.setSample (juce::String (channelIndex) + "-" + juce::String (i) + ".wav", false);
                zone.setSampleStart (100 + i, false);
                zone.setMinVoltage (i == 0 ? 0.0 : i == 1 ? -2.5 : -5.0, false);
            }
        }
        const auto defaults { ZoneProperties::create (1) };
        ZoneProperties defaultZone (defaults, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        defaultZone.setSampleStart (-1, false);
        defaultZone.setSampleEnd (-1, false);
        defaultZone.setLoopStart (-1, false);
        defaultZone.setLoopLength (-1, false);
        defaultZone.setMinVoltage (-5.0, false);
        check (ZonePurge::apply (preset.getChild (0), 1, defaults), "Purge occupied middle zone");
        for (auto c { 0 }; c < 2; ++c)
        {
            ChannelProperties channel (preset.getChild (c), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            ZoneProperties moved (channel.getZoneVT (1), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            ZoneProperties empty (channel.getZoneVT (2), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            check (moved.getSample () == juce::String (c) + "-2.wav" && moved.getSampleStart () == 102 && moved.getId () == 2, "Stereo sides shift together, keeping settings and destination IDs");
            check (moved.getMinVoltage () == -5.0 && empty.getSample ().isEmpty () && ! empty.getSampleStart (), "Tail cleared and final voltage boundary preserved");
        }
        check (! ZonePurge::apply (preset.getChild (1), 0, defaults), "Read-only stereo-right channel cannot be purged independently");
        check (ZonePurge::apply (preset.getChild (0), 1, defaults) && ZonePurge::apply (preset.getChild (0), 0, defaults), "Purge last and only zones");
        check (preset.getChild (0).getChild (0).getProperty (ZoneProperties::SamplePropertyId).toString ().isEmpty (), "Only-zone purge leaves an empty channel");
    }

    void testCrossings ()
    {
        juce::AudioBuffer<float> data (2, 10);
        const float wave[] { 0.4f, 0.01f, -0.8f, -0.2f, 0.0f, 0.3f, -0.001f, -0.5f, 0.8f, 0.0f };
        for (auto i { 0 }; i < 10; ++i) { data.setSample (0, i, wave[i]); data.setSample (1, i, 0.2f); }
        using WaveformPresentation::zeroCrossing;
        check (zeroCrossing (data, 0, 0, 0, 9, true) == 1, "Choose quieter frame BEFORE a crossing");
        check (zeroCrossing (data, 0, 1, 0, 9, true) == 4, "Do not stop a second time on the loud side of the same crossing");
        check (zeroCrossing (data, 0, 5, 0, 9, true) == 6, "Choose quieter frame AFTER a crossing");
        check (zeroCrossing (data, 0, 5, 0, 9, false) == 4, "Find exact zeros going left");
        check (zeroCrossing (data, 0, 4, 1, 10, true, true) == 5, "Exclusive end leaves the zero as the final audible frame");
        check (zeroCrossing (data, 0, 8, 1, 10, true, true) == 10, "Exclusive EOF endpoint can include a final zero frame");
        check (! zeroCrossing (data, 1, 0, 0, 9, true), "No crossing on other stereo side does not move a marker");
        check (! zeroCrossing (data, 0, 1, 0, 3, true), "Respect marker limits without false loud-side crossings");
    }

    void testBoundaryMatching ()
    {
        using WaveformPresentation::matchBoundary;
        juce::AudioBuffer<float> data (2, 400);
        for (auto side { 0 }; side < 2; ++side)
        {
            for (auto i { 0 }; i < 400; ++i) data.setSample (side, i, 0.6f);
            data.setSample (side, 20, -0.25f);
            data.setSample (side, 31, 0.0f);
            data.setSample (side, 174, 0.0f);
            data.setSample (side, 199, 0.0f);
        }
        data.setSample (0, 161, -0.25f);
        data.setSample (1, 185, -0.25f);
        check (matchBoundary (data, 0, 200, 20, 24, 400, 1000, true) == 162, "Nonzero END join matches START 38 frames earlier, with exclusive-end offset");
        check (WaveformPresentation::zeroCrossing (data, 0, 200, 24, 400, false, true) == 175, "True zero nudge remains distinct from amplitude matching");
        check (matchBoundary (data, 1, 200, 20, 24, 400, 1000, true) == 186, "Join matching uses the displayed stereo side");
        check (matchBoundary (data, 0, 20, 200, 0, 196, 1000, false) == 31, "START can match the fixed END's last audible frame");
        check (matchBoundary (data, 0, 20, 200.5, 0, 196, 1000, false) == 31, "Fractional fixed END uses the same final frame as the join preview");
        check (! matchBoundary (data, 0, 162, 20, 24, 400, 1000, true), "An already matched boundary stays put");
        check (! matchBoundary (data, 0, 200, 20, 170, 210, 1000, true), "Do not move outside legal marker limits");
        check (! matchBoundary (data, 0, 200, 20, 24, 400, 500, true), "Search radius follows source rate, not a whole-file scan");
        data.setSample (0, 198, -0.24f);
        check (matchBoundary (data, 0, 200, 20, 170, 210, 1000, true) == 199, "Choose a closer amplitude even when no exact match exists");
        data.setSample (0, 204, -0.25f);
        check (matchBoundary (data, 0, 200, 20, 24, 400, 1000, true) == 205, "Equal-quality matches favour the nearer boundary, including to the right");
        data.setSample (0, 399, -0.25f);
        check (matchBoundary (data, 0, 390, 20, 24, 400, 1000, true) == 400, "Matching an exclusive EOF is safe");
        data.setSample (0, 0, -0.25f);
        check (matchBoundary (data, 0, 5, 400, 0, 396, 1000, false) == 0, "START can match the final file frame at frame zero");
        const auto nan { std::numeric_limits<float>::quiet_NaN () };
        data.setSample (0, 204, nan);
        data.setSample (0, 199, nan);
        check (matchBoundary (data, 0, 200, 20, 24, 400, 1000, true) == 162, "Nonfinite candidates are skipped and a nonfinite current boundary can be repaired");
        data.setSample (0, 20, nan);
        check (! matchBoundary (data, 0, 200, 20, 24, 400, 1000, true), "Nonfinite opposite endpoint cannot be matched");
        check (! matchBoundary (data, 2, 200, 20, 24, 400, 1000, true) &&
               ! matchBoundary (data, 1, 200, 20, 24, 400, 0, true) &&
               ! matchBoundary (data, 1, 401, 20, 24, 400, 1000, true) &&
               ! matchBoundary (data, 1, 20, 0, 0, 399, 1000, false) &&
               ! matchBoundary (data, 1, 200, 20, 300, 100, 1000, true), "Invalid sides, rates, endpoints and inverted bounds are safe");
        data.clear ();
        check (! matchBoundary (data, 0, 200, 20, 24, 400, 1000, true), "Silence never causes arbitrary marker movement");
    }
}

struct ZoneEditorTestAccess
{
    static void run ()
    {
        ModernLookAndFeel look;
        ZoneEditor editor;
        editor.setLookAndFeel (&look);
        editor.displayToolsMenu = [] (int) {};
        editor.parentChannelIndex = 0;
        editor.zoneIndex = 0;
        juce::AudioBuffer<float> audio (2, 48000);
        for (auto i { 0 }; i < 48000; ++i)
        {
            audio.setSample (0, i, static_cast<float> (0.5 * std::sin (i * juce::MathConstants<double>::twoPi / 64.0)));
            audio.setSample (1, i, 0.0f);
        }
        editor.sampleProperties.setLengthInSamples (48000, false);
        editor.sampleProperties.setSampleRate (48000.0, false);
        editor.sampleProperties.setNumChannels (2, false);
        editor.sampleProperties.setAudioBufferPtr (&audio, false);
        editor.sampleProperties.setStatus (SampleStatus::exists, false);
        editor.zoneProperties.setSample ("preview.wav", false);
        editor.zoneProperties.setSampleStart (4800, false);
        editor.zoneProperties.setSampleEnd (28800, false);
        editor.zoneProperties.setLoopStart (9600, false);
        editor.zoneProperties.setLoopLength (4800.0, false);
        editor.zoneProperties.setPitchOffset (0.0, false);
        editor.sampleDataChanged ("preview.wav");
        editor.minVoltageDataChanged (-5.0);
        editor.levelOffsetDataChanged (0.0);
        editor.sideDataChanged (0);
        editor.updateSamplePositionInfo ();
        check (editor.sampleDurationLabel.getText () == "SAMPLE 0:00.500" && editor.loopDurationLabel.getText () == "LOOP 0:00.100", "Zone panel shows separate sample and loop lengths");
        editor.pitchOffsetUiChanged (12.0);
        editor.pitchOffsetDataChanged (12.0);
        check (editor.sampleDurationLabel.getText () == "SAMPLE 0:00.250" && editor.loopDurationLabel.getText () == "LOOP 0:00.050", "Zone lengths follow directly edited pitch");
        editor.audioPlayerProperties.setAuditionRate (0.5, false);
        editor.updateDurations ();
        check (editor.sampleDurationLabel.getText () == "SAMPLE 0:00.250", "Audition speed does not leak into zone durations");
        using Selector = AudioPlayerProperties::SamplePointsSelector;
        editor.audioPlayerProperties.setSamplePointsSelector (Selector::LoopPoints, false); // another zone's last choice
        editor.oneShotPlayButton.onClick ();
        check (editor.audioPlayerProperties.getSamplePointsSelector () == Selector::SamplePoints, "Starting restores the visible SAMPLE choice, not another zone's LOOP choice");
        editor.oneShotPlayButton.onClick (); // stop
        editor.selectLoop (true);
        editor.audioPlayerProperties.setSamplePointsSelector (Selector::SamplePoints, false);
        editor.loopPlayButton.onClick ();
        check (editor.audioPlayerProperties.getSamplePointsSelector () == Selector::LoopPoints, "Looping restores this zone's LOOP choice");
        editor.loopPlayButton.onClick ();
        editor.setSize (182, 520);
        editor.setEditComponentsEnabled (true);
        editor.oneShotPlayButton.setEnabled (true);
        editor.loopPlayButton.setEnabled (true);
        struct Panel : juce::Component { void paint (juce::Graphics& g) override { g.fillAll (Theme::panel); } } panel;
        panel.setSize (editor.getWidth (), editor.getHeight ());
        panel.addAndMakeVisible (editor);
        check (editor.sampleDurationLabel.getBottom () <= editor.loopPointsView.getY () && editor.loopDurationLabel.getBottom () < editor.minVoltageLabel.getY (), "Duration rows do not overlap other controls");
        AREnvelopeComponent envelope;
        envelope.setLookAndFeel (&look);
        envelope.setSize (150, 90);
        AREnvelopeProperties properties (envelope.getPropertiesVT (), AREnvelopeProperties::WrapperType::client, AREnvelopeProperties::EnableCallbacks::no);
        properties.setAttackPercent (0.3, false);
        properties.setReleasePercent (0.45, false);
        const auto snapshot { envelope.createComponentSnapshot (envelope.getLocalBounds ()) };
        check (snapshot.getPixelAt (3, 3) == Theme::field, "Envelope uses modern dark background");
        const auto artifacts { juce::SystemStats::getEnvironmentVariable ("A8MANAGER_TEST_ARTIFACTS", {}) };
        if (artifacts.isNotEmpty ())
        {
            const juce::File dir { artifacts }; dir.createDirectory ();
            auto save = [&] (juce::Component& component, const char* name)
            {
                auto out { dir.getChildFile (name).createOutputStream () };
                check (out != nullptr && out->setPosition (0), "Open render artifact");
                check (juce::PNGImageFormat ().writeImageToStream (component.createComponentSnapshot (component.getLocalBounds (), true, 2.0f), *out), "Write render artifact");
                out->truncate ();
            };
            save (panel, "zone-refinements.png");
            save (envelope, "envelope-refinements.png");
        }
        envelope.setLookAndFeel (nullptr);
        editor.setLookAndFeel (nullptr);
    }
};

void testEditorRefinements ()
{
    testCrossings ();
    testBoundaryMatching ();
    testRename ();
    testPurge ();
    ZoneEditorTestAccess::run ();
    std::cout << "PASS: crossing accuracy, nonzero boundary matching, safe rename recovery, stereo-aware zone purge, zone durations, audition selection and envelope rendering\n";
}
