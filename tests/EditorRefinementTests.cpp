#include "GUI/Assimil8or/Editor/ZoneEditor.h"
#include "GUI/Assimil8or/Editor/Envelope/AREnvelopeComponent.h"
#include "GUI/Assimil8or/Editor/Waveform/WaveformPresentation.h"
#include "GUI/ModernTheme.h"
#include "GUI/WorkspaceHeaderLayout.h"
#include "Assimil8or/Preset/ZonePurge.h"
#include "Assimil8or/SafeRename.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void check (bool value, const char* message) { if (! value) throw std::runtime_error (message); }
    void testWorkspaceHeaderLayout ()
    {
        for (const auto width : { 800, 900, 1039, 1040, 1400, 3000 })
        {
            const auto layout { WorkspaceHeaderLayout::forWidth (width) };
            const juce::Rectangle<int> header { 0, 0, width, layout.height };
            const juce::Rectangle<int> title { 16, 5, 250, 40 };
            const std::array<juce::Rectangle<int>, 9> controls { layout.samples, layout.designer, layout.scaleLabel,
                layout.scaleSelector, layout.audioSettings, layout.help, layout.outputDevice, layout.appearanceLabel, layout.appearanceSelector };
            for (size_t i { 0 }; i < controls.size (); ++i)
            {
                check (header.contains (controls[i]) && ! controls[i].isEmpty (), "Global header controls remain visible at the minimum window width");
                check (! title.intersects (controls[i]), "Header controls do not cover the title");
                for (size_t j { i + 1 }; j < controls.size (); ++j)
                    check (! controls[i].intersects (controls[j]), "Header controls never overlap");
            }
            check (layout.scaleSelector.getRight () < layout.audioSettings.getX () && layout.audioSettings.getRight () < layout.help.getX (),
                   "Audio Settings stays between UI size and Quick help");
            check (layout.audioSettings.getWidth () >= 132 && layout.audioSettings.getHeight () >= 32,
                   "Audio Settings retains a readable, clickable size");
            check (layout.height == 92 && layout.outputDevice.getWidth () >= 280,
                   "Output device and appearance remain readable on a second fixed header row at minimum width");
        }
    }
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
        check (matchBoundary (data, 0, 200, 20, 24, 400, 1000, true, false) == 162, "Nonzero END join matches START 38 frames earlier, with exclusive-end offset");
        check (WaveformPresentation::zeroCrossing (data, 0, 200, 24, 400, false, true) == 175, "True zero nudge remains distinct from amplitude matching");
        check (matchBoundary (data, 1, 200, 20, 24, 400, 1000, true, false) == 186, "Join matching uses the displayed stereo side");
        for (auto i { 21 }; i < 31; ++i) data.setSample (0, i, -0.25f);
        check (matchBoundary (data, 0, 20, 200, 0, 196, 1000, false, true) == 31, "START can match the fixed END's last audible frame");
        check (matchBoundary (data, 0, 20, 200.5, 0, 196, 1000, false, true) == 31, "Fractional fixed END uses the same final frame as the join preview");
        check (! matchBoundary (data, 0, 162, 20, 24, 400, 1000, true, false), "An already matched boundary stays put");
        check (! matchBoundary (data, 0, 200, 20, 170, 210, 1000, true, false), "Do not move outside legal marker limits");
        check (matchBoundary (data, 0, 200, 20, 24, 400, 500, true, false) == 162, "Matches beyond 50 ms are no longer excluded");
        data.setSample (0, 198, -0.24f);
        check (! matchBoundary (data, 0, 200, 20, 170, 210, 1000, true, false), "A local minimum that never reaches the target amplitude is not a crossing");
        data.setSample (0, 197, -0.40f);
        check (matchBoundary (data, 0, 200, 20, 170, 210, 1000, true, false) == 199, "Choose the better bracketing frame at the nearest target crossing");
        data.setSample (0, 204, -0.25f);
        check (matchBoundary (data, 0, 200, 20, 24, 400, 1000, true, true) == 205, "Rightward END matching chooses a match on the requested side");
        check (matchBoundary (data, 0, 200, 20, 24, 400, 1000, true, false) == 199, "A nearby approximate crossing beats a distant exact match");
        data.setSample (0, 399, -0.25f);
        check (matchBoundary (data, 0, 390, 20, 24, 400, 1000, true, true) == 400, "Matching an exclusive EOF is safe");
        data.setSample (0, 0, -0.25f);
        check (matchBoundary (data, 0, 5, 400, 0, 396, 1000, false, false) == 0, "START can match the final file frame at frame zero");
        const auto nan { std::numeric_limits<float>::quiet_NaN () };
        data.setSample (0, 204, nan);
        data.setSample (0, 199, nan);
        check (matchBoundary (data, 0, 200, 20, 24, 400, 1000, true, false) == 199, "A real finite crossing can repair a nonfinite current boundary");
        data.setSample (0, 20, nan);
        check (! matchBoundary (data, 0, 200, 20, 24, 400, 1000, true, true), "Nonfinite opposite endpoint cannot be matched");
        check (! matchBoundary (data, 2, 200, 20, 24, 400, 1000, true, false) &&
               ! matchBoundary (data, 1, 200, 20, 24, 400, 0, true, true) &&
               ! matchBoundary (data, 1, 401, 20, 24, 400, 1000, true, false) &&
               ! matchBoundary (data, 1, 20, 0, 0, 399, 1000, false, true) &&
               ! matchBoundary (data, 1, 200, 20, 300, 100, 1000, true, false), "Invalid sides, rates, endpoints and inverted bounds are safe");
        data.clear ();
        check (! matchBoundary (data, 0, 200, 20, 24, 400, 1000, true, false) &&
               ! matchBoundary (data, 0, 200, 20, 24, 400, 1000, true, true), "Silence never causes arbitrary marker movement in either direction");

        // Exercise both marker kinds with the same marker-coordinate fixture;
        // END reads marker - 1, while START reads the marker itself.
        for (const auto endBoundary : { false, true })
        {
            const auto offset { endBoundary ? 1 : 0 };
            const auto opposite { endBoundary ? 20.0 : 390.0 };
            auto reset = [&] (float scale = 1.0f)
            {
                for (auto i { 0 }; i < 400; ++i) data.setSample (0, i, 0.6f * scale);
                data.setSample (0, endBoundary ? 20 : 389, -0.25f * scale);
            };
            auto candidate = [&] (int marker, float value) { data.setSample (0, marker - offset, value); };
            auto match = [&] (double moving, bool right, juce::int64 minimum = 0, juce::int64 maximum = 400, double rate = 1000.0)
            {
                return matchBoundary (data, 0, moving, opposite, std::max (minimum, juce::int64 { endBoundary ? 24 : 0 }),
                                      std::min (maximum, juce::int64 { endBoundary ? 400 : 386 }), rate, endBoundary, right);
            };

            for (const auto right : { false, true })
            {
                const auto direction { right ? 1 : -1 };
                const auto near { 200 + direction * 5 };
                const auto far { 200 + direction * 100 };
                for (const auto scale : { 1.0f, 0.00001f })
                {
                    reset (scale);
                    candidate (near, -0.24f * scale);
                    candidate (near + direction, -0.40f * scale);
                    candidate (far, -0.25f * scale);
                    candidate (200 - direction, -0.25f * scale);
                    check (match (200, right) == near, "Nearest crossing beats exact matches farther away or on the wrong side, even in quiet audio");
                }

                reset ();
                candidate (near, 0.0f); candidate (near + direction, -0.5f);
                check (match (200, right) == near, "Equal-error bracketing frames choose the nearer marker in either direction");
                candidate (near, -0.1f); candidate (near + direction, -0.30f);
                check (match (200, right) == near + direction, "The farther bracketing frame wins when its amplitude matches better");

                reset ();
                candidate (near, -0.20f); candidate (far, -0.25f);
                check (match (200, right) == far, "A local amplitude minimum is skipped when it never crosses the target");
                candidate (far, -0.20f);
                check (! match (200, right), "No target crossing produces no move, even with closer amplitude minima");

                reset ();
                candidate (200, -0.24f); candidate (near, -0.20f);
                candidate (near + direction, -0.40f); candidate (far, -0.25f);
                check (! match (200, right), "A nearest crossing that worsens the join is not skipped to hunt a distant exact match");
                candidate (200 + direction, -0.40f);
                check (! match (200, right), "When the current frame is the better bracket, keep it instead of jumping farther");

                reset ();
                candidate (200, -0.10f); candidate (200 + direction, -0.30f);
                check (match (200, right) == 200 + direction, "Crossing between the current frame and next legal frame can improve the join");

                reset ();
                candidate (near, nan); candidate (near + direction, -0.40f);
                candidate (near + direction * 2, nan); candidate (far, -0.25f);
                check (match (200, right) == far, "Nonfinite neighbours never create false crossings");
            }

            reset ();
            candidate (180, -0.25f); candidate (220, -0.25f);
            check (match (200, false) == 180 && match (200, true) == 220, "Both endpoint kinds choose the nearest exact point on the requested side");
            check (! match (200, false, 200, 400) && ! match (200, true, 0, 200), "Legal limits cannot cause a search to cross to the other direction");
            candidate (200, -0.25f);
            check (! match (200, false) && ! match (200, true), "An exact current match stays put even with matches in both directions");

            reset ();
            candidate (150, -0.25f); candidate (250, -0.25f);
            check (match (200, false) == 150 && match (200, true) == 250, "Exact 50 ms matches remain available in both directions");
            check (! match (200, false, 151, 400) && ! match (200, true, 0, 249), "Both directions obey their legal marker limits");
            check (match (200, false, 0, 400, 980) == 150 && match (200, true, 0, 400, 980) == 250, "Source rate no longer limits the search extent");
            check (match (200.5, false) == 150 && match (200.5, true) == 250, "Fractional current markers can match beyond 50 ms");
            candidate (150, 0.6f); candidate (250, 0.6f);
            candidate (50, -0.25f); candidate (350, -0.25f);
            check (match (200, false) == 50 && match (200, true) == 350, "Both endpoint kinds find distant matches in either direction");

            reset ();
            candidate (199, -0.25f); candidate (201, -0.25f);
            check (match (200.5, false) == 199 && match (200.5, true) == 201, "Fractional boundaries search strictly left and right in marker coordinates");
            candidate (199, 0.6f); candidate (200, -0.20f);
            check (match (199.5, true) == 201, "A rightward fractional search skips noncrossing amplitude minima");
            candidate (201, -0.40f);
            check (match (199.5, true) == 200, "Rightward fractional matching includes the immediately following integer marker");
            check (! match (200.5, false), "A fractional marker does not move to its current audible frame without amplitude improvement");

            reset ();
            candidate (200, -0.20f);
            for (auto marker { 201 }; marker < 220; ++marker) candidate (marker, -0.40f);
            candidate (220, -0.25f);
            check (match (200.5, true) == 220, "A crossing at 200.25 is behind a fractional rightward search and cannot stop it");
            check (! match (200.1, true), "An eligible crossing whose better frame is behind the moving marker remains unchanged");
            reset ();
            for (auto marker { 181 }; marker <= 200; ++marker) candidate (marker, -0.40f);
            candidate (201, -0.20f); candidate (180, -0.25f);
            check (match (200.5, false) == 180, "A crossing at 200.75 is behind a fractional leftward search and cannot stop it");
            check (! match (200.9, false), "A leftward fractional search cannot choose the better bracket on its right");

            reset ();
            candidate (198, -0.25f); candidate (202, -0.25f);
            candidate (199, nan); candidate (200, nan); candidate (201, nan);
            check (match (200, false) == 198 && match (200, true) == 202, "Both directions skip nonfinite candidates and repair a nonfinite current endpoint");
            data.setSample (0, endBoundary ? 20 : 389, nan);
            check (! match (200, false) && ! match (200, true), "Neither direction can match a nonfinite fixed endpoint");

            reset ();
            candidate (offset, -0.25f); candidate (399 + offset, -0.25f);
            check (matchBoundary (data, 0, 5 + offset, opposite, 0, 400, 1000, endBoundary, false) == offset &&
                   matchBoundary (data, 0, 394 + offset, opposite, 0, 400, 1000, endBoundary, true) == 399 + offset,
                   "Directional matching safely reaches the first and last audible file frames");
            candidate (offset, 0.6f); candidate (399 + offset, 0.6f);
            check (! match (offset, false) && ! match (399 + offset, true), "Outward searches at file edges are no-ops");
            check (! match (200, false, 0, 400, nan) && ! match (nan, true), "Nonfinite rates and moving markers are safe in either direction");
        }

        juce::AudioBuffer<float> longAudio (1, 200000);
        for (auto i { 0 }; i < longAudio.getNumSamples (); ++i) longAudio.setSample (0, i, 0.5f);
        longAudio.setSample (0, 100, -0.25f);
        longAudio.setSample (0, 199998, -0.25f);
        WaveformPresentation::BoundaryMatchSearch batched (longAudio, 0, 1000, 100, 104, 200000, 48000, true, true);
        check (! batched.advance (65536) && ! batched.result (), "Large-file matching yields without exposing an incomplete result");
        check (! batched.advance (65536) && ! batched.result (), "Incremental search preserves its progress across batches");
        check (! batched.advance (65536) && batched.advance (65536) && batched.result () == 199999,
               "Incremental search reaches a distant exact match without a time cap");
        longAudio.setSample (0, 1000, -0.25f);
        WaveformPresentation::BoundaryMatchSearch nearest (longAudio, 0, 1000, 100, 104, 200000, 48000, true, true);
        check (nearest.advance (1) && nearest.result () == 1001, "Nearest exact match terminates without scanning the remaining file");

        longAudio.setSample (0, 1000, 0.5f);
        longAudio.setSample (0, 1004, -0.24f);
        longAudio.setSample (0, 1005, -0.40f);
        longAudio.setSample (0, 2999, -0.25f);
        check (matchBoundary (longAudio, 0, 1000, 100, 104, 200000, 1000, true, true) == 1005,
               "A useful crossing 5 ms away beats a mathematically exact match 2000 ms away");
        WaveformPresentation::BoundaryMatchSearch splitCrossing (longAudio, 0, 1000, 100, 104, 200000, 1000, true, true);
        check (! splitCrossing.advance (0) && ! splitCrossing.advance (5) && ! splitCrossing.result (),
               "A crossing spanning the next batch is not replaced by an unfinished local-minimum result");
        check (splitCrossing.advance (1) && splitCrossing.result () == 1005,
               "A crossing across a batch boundary retains its better preceding frame");
        check (splitCrossing.advance (1) && splitCrossing.result () == 1005,
               "Advancing a finished search cannot replace the nearby crossing with a distant exact match");
    }
}

struct ZoneEditorTestAccess
{
    static void run ()
    {
        ModernLookAndFeel look;
        ZoneEditor editor;
        editor.setLookAndFeel (&look);
        check (editor.toolsButton.getButtonText () == "Zone tools" && editor.toolsButton.getComponentID () == "zoneTools",
               "Zone menu has its own scope label");
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
        // This detached fixture bypasses init() and its sample-status callback.
        // A direct call to a disabled button must be ignored, then refresh the
        // same enablement that loading a real sample normally updates.
        editor.oneShotPlayButton.onClick ();
        check (editor.audioPlayerProperties.getPlayState () == AudioPlayerProperties::PlayState::stop &&
               editor.audioPlayerProperties.getSamplePointsSelector () == Selector::LoopPoints,
               "Disabled audition controls cannot start playback or change its source range");
        editor.updateAuditionControls ();
        editor.oneShotPlayButton.onClick ();
        check (editor.audioPlayerProperties.getSamplePointsSelector () == Selector::SamplePoints, "Starting restores the visible SAMPLE choice, not another zone's LOOP choice");
        editor.oneShotPlayButton.onClick (); // stop
        editor.selectLoop (true);
        editor.audioPlayerProperties.setSamplePointsSelector (Selector::SamplePoints, false);
        editor.loopPlayButton.onClick ();
        check (editor.audioPlayerProperties.getSamplePointsSelector () == Selector::LoopPoints, "Looping restores this zone's LOOP choice");
        editor.loopPlayButton.onClick ();
        editor.setSize (182, 520);
        check (editor.sampleStartTextEditor.getFont ().getTypefaceName () == juce::Font::getDefaultMonospacedFontName () &&
               editor.sampleEndTextEditor.getFont ().getTypefaceName () == juce::Font::getDefaultMonospacedFontName () &&
               editor.loopStartTextEditor.getFont ().getTypefaceName () == juce::Font::getDefaultMonospacedFontName () &&
               editor.loopLengthTextEditor.getFont ().getTypefaceName () == juce::Font::getDefaultMonospacedFontName (),
               "All four endpoint numeric editors remain monospaced after layout");
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
            Theme::setAppearance (true);
            Theme::refreshComponentTree (panel);
            Theme::refreshComponentTree (envelope);
            save (panel, "zone-refinements-light.png");
            save (envelope, "envelope-refinements-light.png");
            Theme::setAppearance (false);
            Theme::refreshComponentTree (panel);
            Theme::refreshComponentTree (envelope);
        }
        envelope.setLookAndFeel (nullptr);
        editor.setLookAndFeel (nullptr);
    }
};

void testEditorRefinements ()
{
    testWorkspaceHeaderLayout ();
    testCrossings ();
    testBoundaryMatching ();
    testRename ();
    testPurge ();
    ZoneEditorTestAccess::run ();
    std::cout << "PASS: crossing accuracy, nonzero boundary matching, safe rename recovery, stereo-aware zone purge, zone durations, audition selection and envelope rendering\n";
}
