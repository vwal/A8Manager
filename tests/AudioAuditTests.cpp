#include "GUI/Assimil8or/Editor/SampleManager/SampleManager.h"
#include "GUI/Assimil8or/Editor/ZoneEditor.h"
#include "GUI/Assimil8or/Editor/Waveform/WaveformDisplay.h"
#include "Assimil8or/Audio/AudioPlayer.h"
#include "oolib/Properties/RuntimeRootProperties.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void check (bool value, const char* message)
    {
        if (! value) throw std::runtime_error (message);
    }

    void writeFixture (const juce::File& file, int frames, float value, int channels = 2)
    {
        auto output { file.createOutputStream () };
        check (output != nullptr && output->setPosition (0) && output->truncate ().wasOk (), "Create isolated sample fixture");
        std::unique_ptr<juce::OutputStream> stream { std::move (output) };
        juce::WavAudioFormat format;
        auto writer { format.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (48000).withNumChannels (channels).withBitsPerSample (24)) };
        check (writer != nullptr, "Create sample fixture writer");
        juce::AudioBuffer<float> audio (channels, frames);
        for (auto side { 0 }; side < channels; ++side)
            for (auto frame { 0 }; frame < frames; ++frame)
                audio.setSample (side, frame, side == 0 ? value : -value);
        check (writer->writeFromAudioSampleBuffer (audio, 0, frames), "Write sample fixture");
    }
}

struct AudioAuditTestAccess
{
    static void reloads ()
    {
        const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-audio-audit", "", false) };
        check (folder.createDirectory ().wasOk (), "Create isolated reload folder");
        struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { folder };
        const auto file { folder.getChildFile ("sample.wav") };
        AudioManager audioManager;
        AudioPlayer player;
        player.sampleRate = 48000.0;
        player.audioPlayerProperties.enableCallbacks (true);
        player.audioPlayerProperties.onPlayStateChange = [&] (auto state) { player.handlePlayState (state); };
        SampleManager manager;
        manager.audioManager = &audioManager;
        manager.currentFolder = folder;
        for (auto c { 0 }; c < 8; ++c)
        {
            ChannelProperties channel (player.presetProperties.getChannelVT (c), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            for (auto z { 0 }; z < 8; ++z)
            {
                auto& slot { manager.zoneAndSamplePropertiesList[c][z] };
                slot.zoneProperties.wrap (channel.getZoneVT (z), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                slot.zoneProperties.setSampleStart (-1, false);
                slot.zoneProperties.setSampleEnd (-1, false);
                slot.zoneProperties.setLoopStart (-1, false);
                slot.zoneProperties.setLoopLength (-1, false);
                slot.sampleProperties.wrap (player.sampleManagerProperties.getSamplePropertiesVT (c, z), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
            }
        }
        auto& left { manager.zoneAndSamplePropertiesList[0][0] };
        auto& right { manager.zoneAndSamplePropertiesList[1][0] };
        ChannelProperties rightChannel (player.presetProperties.getChannelVT (1), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        rightChannel.setChannelMode (ChannelProperties::ChannelMode::stereoRight, false);
        left.zoneProperties.setSample ("sample.wav", false);
        right.zoneProperties.setSample ("sample.wav", false);
        right.zoneProperties.setSide (1, false);
        writeFixture (file, 1024, 0.25f);
        manager.handleSampleChange (0, 0, "sample.wav");
        manager.handleSampleChange (1, 0, "sample.wav");
        player.initFromZone ({ 0, 0 });
        WaveformDisplay view;
        view.channelProperties.wrap (player.presetProperties.getChannelVT (0), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::yes);
        view.sampleManagerProperties.wrap (player.sampleManagerProperties.getValueTree (), SampleManagerProperties::WrapperType::client, SampleManagerProperties::EnableCallbacks::no);
        view.setSize (600, 200);
        view.setZone (0);
        check (view.waveform.getNumSamples () == 1024, "Waveform sees initial audio length");
        check (std::abs (player.sampleBuffer->getSample (0, 64) - 0.25f) < 0.001f, "Initial audition cache matches source");

        auto invalidations { 0 };
        auto expectedOldLength { 1024 };
        SampleProperties leftObserver (left.sampleProperties.getValueTree (), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::yes);
        SampleProperties rightObserver (right.sampleProperties.getValueTree (), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::yes);
        auto observe = [&] (SampleProperties& properties, SampleStatus status)
        {
            if (status == SampleStatus::uninitialized && properties.getAudioBufferPtr () != nullptr)
            {
                check (properties.getAudioBufferPtr ()->getNumSamples () == expectedOldLength, "Every borrowed buffer is invalidated before mutation");
                ++invalidations;
            }
        };
        leftObserver.onStatusChange = [&] (auto status) { observe (leftObserver, status); };
        rightObserver.onStatusChange = [&] (auto status) { observe (rightObserver, status); };

        player.audioPlayerProperties.setPlayState (AudioPlayerProperties::PlayState::loop, true);
        writeFixture (file, 1024, -0.5f);
        manager.update ();
        check (invalidations == 2, "All zones sharing the sample receive unload before same-path reload");
        check (player.audioPlayerProperties.getPlayState () == AudioPlayerProperties::PlayState::stop, "Reload safely stops current playback");
        check (std::abs (player.sampleBuffer->getSample (0, 64) + 0.5f) < 0.001f &&
               std::abs (player.sampleBuffer->getSample (1, 64) - 0.5f) < 0.001f, "Same-size same-path stereo reload refreshes both audition channels");

        writeFixture (file, 128, 0.6f, 1);
        manager.update ();
        check (invalidations == 4 && view.waveform.getNumSamples () == 128 && player.sampleBuffer->getNumSamples () == 128,
               "Shorter replacement resets waveform bounds and audition allocation");
        check (right.zoneProperties.getSide () == 0, "Stereo-to-mono replacement clears an invalid right-side selection");
        check (std::abs (player.sampleBuffer->getSample (1, 64) - 0.6f) < 0.001f, "Paired mono replacement is safe and current");
        expectedOldLength = 128;
        check (file.deleteFile (), "Delete only the disposable fixture");
        manager.update ();
        check (invalidations == 6 && left.sampleProperties.getStatus () == SampleStatus::doesNotExist &&
               right.sampleProperties.getStatus () == SampleStatus::doesNotExist, "Deletion publishes missing status to all references");
        check (left.sampleProperties.getAudioBufferPtr () == nullptr && left.sampleProperties.getLengthInSamples () == 0 &&
               player.sampleBuffer == nullptr && view.waveform.getNumSamples () == 0, "Deletion releases audio and waveform caches");
        check (file.replaceWithText ("not audio"), "Create corrupt disposable fixture");
        manager.update ();
        check (left.sampleProperties.getStatus () == SampleStatus::wrongFormat && player.sampleBuffer == nullptr &&
               view.waveform.getNumSamples () == 0, "Corrupt replacement remains disabled and uncached");
        writeFixture (file, 256, 0.2f);
        manager.update ();
        check (left.sampleProperties.getStatus () == SampleStatus::exists && view.waveform.getNumSamples () == 256 &&
               player.sampleBuffer->getNumSamples () == 256, "Valid replacement recovers after missing/corrupt source");
    }

    static void markerFields ()
    {
        ZoneEditor editor;
        editor.parentChannelIndex = 0;
        editor.zoneIndex = 0;
        juce::AudioBuffer<float> audio (2, 10000);
        audio.clear ();
        editor.sampleProperties.setAudioBufferPtr (&audio, false);
        editor.sampleProperties.setLengthInSamples (10000, false);
        editor.sampleProperties.setSampleRate (48000, false);
        editor.sampleProperties.setStatus (SampleStatus::exists, false);
        editor.zoneProperties.setLoopStart (1000, false);
        editor.zoneProperties.setLoopLength (500, false);
        editor.setLoopLengthIsEnd (true);
        editor.loopLengthTextEditor.onDragCallback (1.0);
        check (editor.zoneProperties.getLoopLength () == 501.0, "LOOP END +1 drag increments absolute end, not stored length");
        editor.loopLengthTextEditor.onDragCallback (-1.0);
        check (editor.zoneProperties.getLoopLength () == 500.0, "LOOP END reverse drag restores original end");
        editor.setLoopLengthIsEnd (false);
        editor.loopLengthTextEditor.onDragCallback (1.0);
        check (editor.zoneProperties.getLoopLength () == 501.0, "LOOP LENGTH drag continues to adjust length");
        editor.zoneProperties.setLoopLength (-1.0, false);
        editor.setLoopLengthIsEnd (true);
        editor.loopLengthTextEditor.onDragCallback (-1.0);
        check (editor.zoneProperties.getLoopLength () == 8999.0, "Implicit EOF end drag uses remaining length once");

        audio.setSize (2, 16);
        for (auto frame { 0 }; frame < 16; ++frame)
        {
            audio.setSample (0, frame, frame < 5 ? 0.8f : -0.8f);
            audio.setSample (1, frame, 0.2f);
        }
        audio.setSample (0, 4, 0.01f);
        audio.setSample (0, 15, 0.0f);
        editor.sampleProperties.setLengthInSamples (16, false);
        editor.zoneProperties.setSampleStart (0, false);
        editor.zoneProperties.setSampleEnd (16, false);
        editor.zoneProperties.setLoopStart (0, false);
        editor.zoneProperties.setLoopLength (12.0, false);
        editor.zoneProperties.setSide (0, false);

        juce::ValueTree root { "Root" };
        RuntimeRootProperties runtime (root, RuntimeRootProperties::WrapperType::owner, RuntimeRootProperties::EnableCallbacks::no);
        SampleManagerProperties manager (runtime.getValueTree (), SampleManagerProperties::WrapperType::owner, SampleManagerProperties::EnableCallbacks::no);
        auto channel { ChannelProperties::create (1) };
        channel.addChild (editor.zoneProperties.getValueTree (), -1, nullptr);
        editor.parentChannelProperties.wrap (channel, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        editor.parentChannelProperties.setLoopLengthIsEnd (true, false);
        auto sample { manager.getSamplePropertiesVT (0, 0) };
        sample.copyPropertiesFrom (editor.sampleProperties.getValueTree (), nullptr);
        editor.sampleProperties.wrap (sample, SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
        editor.sampleProperties.setNumChannels (2, false);
        WaveformDisplay view;
        view.setSize (600, 200);
        view.init (channel, root);
        view.onRegionSelected = [&] (bool loop) { editor.selectLoop (loop); };
        editor.createBoundaryAdjustmentMenu = [&] (int marker) { return view.boundaryAdjustmentMenu (marker); };
        using Marker = ZoneEditor::SampleMarker;
        auto invoke = [] (const juce::PopupMenu& menu, int id)
        {
            for (juce::PopupMenu::MenuItemIterator items (menu, true); items.next ();)
            {
                const auto& item { items.getItem () };
                if (item.itemID == id && item.isEnabled && item.action) { item.action (); return true; }
            }
            return false;
        };
        auto nudge = [&] (Marker marker, bool right)
        {
            const auto before { editor.zoneProperties.getValueTree ().createCopy () };
            invoke (editor.getSampleAdjustMenu (marker), 20 + static_cast<int> (marker) * 2 + (right ? 1 : 0));
            return ! before.isEquivalentTo (editor.zoneProperties.getValueTree ());
        };
        check (nudge (Marker::sampleStart, true) && editor.zoneProperties.getSampleStart () == 4,
               "Field start nudge selects quieter crossing frame");
        check (nudge (Marker::sampleEnd, false) && editor.zoneProperties.getSampleEnd () == 5,
               "Field end nudge preserves quieter frame before exclusive boundary");
        check (nudge (Marker::sampleEnd, true) && ! editor.zoneProperties.getSampleEnd (),
               "Field end can nudge to final exact zero at EOF");
        check (nudge (Marker::loopStart, true) && editor.zoneProperties.getLoopStart () == 4 &&
               editor.zoneProperties.getLoopLength () == 8.0 && editor.isLoopSelected (), "End-mode start nudge keeps loop end fixed and selects LOOP");
        editor.setLoopLengthIsEnd (false);
        editor.parentChannelProperties.setLoopLengthIsEnd (false, true);
        editor.zoneProperties.setLoopStart (0, false);
        editor.zoneProperties.setLoopLength (8.0, false);
        check (nudge (Marker::loopStart, true) && editor.zoneProperties.getLoopStart () == 4 &&
               editor.zoneProperties.getLoopLength () == 8.0, "Length-mode start nudge retains loop length");
        editor.zoneProperties.setSide (1, false);
        check (! nudge (Marker::loopEnd, false), "Field nudge respects selected stereo side with no crossing");
        editor.zoneProperties.setSide (0, false);
        check (! nudge (Marker::loopEnd, false), "Loop end nudge cannot violate four-frame minimum");
        editor.sampleProperties.setStatus (SampleStatus::doesNotExist, false);
        check (! nudge (Marker::sampleStart, true), "Field nudge safely ignores unavailable audio");

        auto previewCallback { editor.loopPointsView.onContextMenu };
        juce::Array<bool> clickedStarts;
        editor.loopPointsView.onContextMenu = [&] (bool start) { clickedStarts.add (start); };
        editor.loopPointsView.setSize (200, 80);
        for (const auto flags : { juce::ModifierKeys::leftButtonModifier, juce::ModifierKeys::rightButtonModifier })
            for (const auto x : { 20.0f, 180.0f })
            {
                const juce::Point<float> point { x, 30.0f };
                const juce::MouseEvent event { juce::Desktop::getInstance ().getMainMouseSource (), point, juce::ModifierKeys (flags),
                    1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &editor.loopPointsView, &editor.loopPointsView,
                    juce::Time::getCurrentTime (), point, juce::Time::getCurrentTime (), 1, false };
                static_cast<juce::Component&> (editor.loopPointsView).mouseDown (event);
            }
        check (clickedStarts == juce::Array<bool> { false, true }, "Only right-click opens the preview menu: left half END, right half START");
        editor.loopPointsView.onContextMenu = std::move (previewCallback);

        // Every mini-preview half addresses the correct absolute boundary,
        // including LOOP LENGTH mode, where the right-half action is still Start.
        for (const auto loop : { false, true })
        {
            editor.selectLoop (loop);
            for (const auto start : { false, true })
            {
                const auto menu { editor.getLoopPointsMenu (start) };
                const auto marker { (loop ? 2 : 0) + (start ? 0 : 1) };
                juce::PopupMenu::MenuItemIterator items (menu, true);
                auto commands { 0 };
                while (items.next ())
                {
                    const auto id { items.getItem ().itemID };
                    if (id == 20 + marker * 2 || id == 21 + marker * 2 || id == 40 + marker * 2 || id == 41 + marker * 2) ++commands;
                }
                check (commands == 4, "Each END/START preview half exposes both directions of zero crossing and matching for its selected region");
            }
        }

        // A distant zero from a numeric-field menu follows the same approval
        // policy as a waveform action; typing a numeric endpoint stays direct.
        audio.setSize (2, 10000);
        for (auto frame { 0 }; frame < 10000; ++frame) audio.setSample (0, frame, frame == 8000 ? 0.0f : 0.5f);
        editor.sampleProperties.setLengthInSamples (10000, false);
        editor.sampleProperties.setStatus (SampleStatus::exists, false);
        editor.zoneProperties.setSampleStart (0, false);
        editor.zoneProperties.setSampleEnd (5000, false);
        auto prompts { 0 };
        std::function<void (bool)> answer;
        view.confirmBoundaryMatch = [&] (const juce::String&, std::function<void (bool)> callback) { ++prompts; answer = std::move (callback); };
        check (! nudge (Marker::sampleEnd, true) && prompts == 1 && answer, "Distant numeric-menu nudge waits for confirmation");
        answer (false);
        check (editor.zoneProperties.getSampleEnd () == 5000, "Cancelled numeric-menu nudge leaves the endpoint unchanged");
        editor.selectLoop (false);
        invoke (editor.getLoopPointsMenu (false), 23);
        check (prompts == 2 && editor.zoneProperties.getSampleEnd () == 5000, "Mini-preview nudge uses the same confirmation path");
        answer (true);
        check (editor.zoneProperties.getSampleEnd () == 8001, "Approved preview end nudge includes the quiet frame before the exclusive boundary");
        editor.sampleEndTextEditor.setValue (4000);
        check (prompts == 2 && editor.zoneProperties.getSampleEnd () == 4000, "An intentional numeric endpoint edit never invokes automated-move confirmation");

        const auto staleMenu { editor.getSampleAdjustMenu (Marker::sampleEnd) };
        editor.zoneProperties.setSampleEnd (4500, false);
        invoke (staleMenu, 23);
        check (prompts == 2 && editor.zoneProperties.getSampleEnd () == 4500, "A field menu opened before a marker edit cannot act on changed state");
        for (auto frame { 0 }; frame < 10000; ++frame) audio.setSample (0, frame, 0.6f);
        audio.setSample (0, 0, -0.25f);
        audio.setSample (0, 7200, -0.25f);
        invoke (editor.getLoopPointsMenu (false), 43);
        check (prompts == 3 && editor.zoneProperties.getSampleEnd () == 4500, "Mini-preview opposite-boundary matching also confirms moves beyond 50 ms");
        answer (false);
    }
};

void testAudioAudit ()
{
    AudioAuditTestAccess::reloads ();
    AudioAuditTestAccess::markerFields ();
    std::cout << "PASS: reload invalidation, stale-cache prevention, missing/corrupt recovery, LOOP END gestures and field zero-crossing semantics\n";
}
