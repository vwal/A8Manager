#include "GUI/Assimil8or/Editor/ZoneEditor.h"
#include "GUI/Assimil8or/Editor/ChannelEditor.h"
#include "GUI/Assimil8or/Editor/SampleManager/SampleManagerProperties.h"
#include "GUI/ModernTheme.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "Assimil8or/PresetManagerProperties.h"
#include "SystemServices.h"
#include "oolib/Properties/PersistentRootProperties.h"
#include "oolib/Properties/RuntimeRootProperties.h"
#include <iostream>
#include <stdexcept>

struct SimulationUiTestAccess
{
    static void run ()
    {
        auto check = [] (bool ok, const char* message) { if (! ok) throw std::runtime_error (message); };
        using State = AudioPlayerProperties::PlayState;
        using Phase = AudioPlayerProperties::SimulationPhase;
        using Selection = AudioPlayerProperties::SamplePointsSelector;
        juce::ValueTree root { "Root" };
        PersistentRootProperties persistent (root, PersistentRootProperties::WrapperType::owner, PersistentRootProperties::EnableCallbacks::no);
        RuntimeRootProperties runtime (root, RuntimeRootProperties::WrapperType::owner, RuntimeRootProperties::EnableCallbacks::no);
        AppProperties preferences;
        preferences.wrap (persistent.getValueTree (), AppProperties::WrapperType::owner, AppProperties::EnableCallbacks::no);
        AudioPlayerProperties audition (runtime.getValueTree (), AudioPlayerProperties::WrapperType::owner, AudioPlayerProperties::EnableCallbacks::no);
        SampleManagerProperties samples (runtime.getValueTree (), SampleManagerProperties::WrapperType::owner, SampleManagerProperties::EnableCallbacks::no);
        AudioManager audio;
        EditManager edits;
        SystemServices services (runtime.getValueTree (), SystemServices::WrapperType::owner, SystemServices::EnableCallbacks::no);
        services.setAudioManager (&audio);
        services.setEditManager (&edits);
        const auto defaults { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType) };
        PresetProperties preset (defaults.createCopy (), PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        PresetManagerProperties presets (runtime.getValueTree (), PresetManagerProperties::WrapperType::owner, PresetManagerProperties::EnableCallbacks::no);
        presets.addPreset ("edit", preset.getValueTree ());
        presets.addPreset ("unedited", preset.getValueTree ().createCopy ());
        juce::AudioBuffer<float> buffer (1, 2048);
        for (int frame { 0 }; frame < buffer.getNumSamples (); ++frame)
            buffer.setSample (0, frame, std::sin (static_cast<float> (frame) * 0.04f) * 0.25f);
        for (int channelIndex { 0 }; channelIndex < 2; ++channelIndex)
        {
            ChannelProperties channel (preset.getChannelVT (channelIndex), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            channel.setChannelMode (ChannelProperties::master, false);
            for (int zoneIndex { 0 }; zoneIndex < 2; ++zoneIndex)
            {
                ZoneProperties zone (channel.getZoneVT (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                zone.setSample ("fixture.wav", false);
                zone.setSampleStart (100, false); zone.setSampleEnd (800, false);
                zone.setLoopStart (400, false); zone.setLoopLength (200, false);
                SampleProperties sample (samples.getSamplePropertiesVT (channelIndex, zoneIndex), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
                sample.setName ("fixture.wav", false);
                sample.setAudioBufferPtr (&buffer, false);
                sample.setLengthInSamples (buffer.getNumSamples (), false);
                sample.setNumChannels (1, false); sample.setSampleRate (48000, false);
                sample.setStatus (SampleStatus::exists, false);
            }
        }
        ModernLookAndFeel look;
        std::vector<std::function<void ()>> pendingRefreshes;
        auto createEditor = [&] (int channelIndex, int zoneIndex)
        {
            auto editor { std::make_unique<ZoneEditor> () };
            editor->setLookAndFeel (&look);
            editor->displayToolsMenu = [] (int) {};
            ChannelProperties channel (preset.getChannelVT (channelIndex), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            editor->init (channel.getZoneVT (zoneIndex), channel.getZoneVT (zoneIndex).createCopy (), root);
            editor->setSize (180, 440);
            editor->setStereoRightChannelMode (false);
            editor->deferPlaybackDisplayUpdate = [&] (std::function<void ()> callback) { pendingRefreshes.push_back (std::move (callback)); };
            return editor;
        };
        auto editor { createEditor (0, 0) };
        auto otherZone { createEditor (0, 1) };
        auto otherChannel { createEditor (1, 0) };
        const auto untouched { preset.getValueTree ().createCopy () };
        bool selectedLoop { false };
        int regionChanges { 0 }, availabilityChanges { 0 };
        editor->onRegionSelected = [&] (bool loop) { selectedLoop = loop; ++regionChanges; };
        editor->onSimulationAvailabilityChanged = [&] { ++availabilityChanges; };
        auto expectControls = [&] (ZoneEditor& target, bool onceStop, bool loopStop, const char* message)
        {
            check (target.oneShotPlayButton.getButtonText () == (onceStop ? "STOP" : "ONCE") &&
                   target.loopPlayButton.getButtonText () == (loopStop ? "STOP" : "LOOP") &&
                   target.oneShotPlayButton.getToggleState () == onceStop && target.loopPlayButton.getToggleState () == loopStop, message);
        };
        check (editor->canStartSampleIntoLoop (), "A valid contained loop supports sample-into-loop simulation");
        editor->startSampleIntoLoop ();
        check (audition.getPlayState () == State::sampleIntoLoop && audition.getSampleSource () == std::make_tuple (0, 0),
               "The zone starts the dedicated simulation transport for its exact source");
        expectControls (*editor, true, false, "Simulation initially highlights SAMPLE/ONCE as STOP before the first phase tick");
        check (! editor->isLoopSelected () && editor->activePointBackground == &editor->samplePointsBackground, "Initial simulation uses SAMPLE panel");
        audition.setSimulationPhase (Phase::sample, false);
        audition.setPlaybackPosition (300, false); // Inside SAMPLE, before LOOP START.
        editor->updatePlaybackDisplay ();
        expectControls (*editor, true, false, "The sample intro remains in SAMPLE with ONCE highlighted as STOP");
        editor->selectLoop (true);
        check (! editor->isLoopSelected () && audition.getPlayState () == State::sampleIntoLoop,
               "Marker/section selection during the sample phase cannot stop or prematurely switch the simulation");

        audition.setSimulationPhase (Phase::loop, false);
        audition.setSamplePointsSelector (Selection::LoopPoints, false);
        editor->updatePlaybackDisplay ();
        expectControls (*editor, false, true, "Loop entry highlights LOOP playback as STOP and clears ONCE highlight");
        check (editor->isLoopSelected () && selectedLoop && regionChanges == 1 &&
               editor->activePointBackground == &editor->loopPointsBackground && audition.getPlayState () == State::sampleIntoLoop,
               "Audible loop phase follows through panel/region callback without reconfiguring or stopping transport");
        editor->selectLoop (false);
        check (editor->isLoopSelected () && audition.getPlayState () == State::sampleIntoLoop, "Loop phase remains primary while editing sample markers");
        otherZone->updatePlaybackDisplay (); otherChannel->updatePlaybackDisplay ();
        expectControls (*otherZone, false, false, "Another zone cannot display STOP for this audition");
        expectControls (*otherChannel, false, false, "Another channel cannot display STOP for this audition");
        editor->loopPlayButton.onClick ();
        check (audition.getPlayState () == State::stop, "The highlighted loop STOP ends simulation");
        expectControls (*editor, false, false, "Stopping clears both playback highlights");
        // The engine publishes at 30 Hz; retrigger must not reuse its last LOOP
        // phase while waiting for the first new audible-position update.
        audition.setSimulationPhase (Phase::loop, false);
        editor->startSampleIntoLoop ();
        check (audition.getSimulationPhase () == Phase::sample && ! editor->isLoopSelected (), "Retrigger immediately resets a stale loop phase to the new sample lead-in");
        editor->oneShotPlayButton.onClick ();
        check (audition.getPlayState () == State::stop, "The highlighted sample ONCE STOP ends the contained lead-in simulation");
        editor->startSampleIntoLoop ();
        audition.setSimulationPhase (Phase::loop, false);
        editor->oneShotPlayButton.onClick ();
        check (audition.getPlayState () == State::stop,
               "A still-visible SAMPLE STOP remains a stop if the audible loop phase arrives before the queued UI refresh");
        check (preset.getValueTree ().isEquivalentTo (untouched), "Simulation and phase following never alter preset data");

        editor->zoneProperties.setLoopStart (1200, true);
        editor->zoneProperties.setLoopLength (200, true);
        check (! editor->canStartSampleIntoLoop (), "A detached legacy loop is not eligible for simulation");
        editor->startSampleIntoLoop ();
        check (audition.getPlayState () == State::stop, "A stale trigger cannot traverse beyond Sample End into an invalid loop");
        editor->zoneProperties.setLoopStart (50, true);
        editor->zoneProperties.setLoopLength (200, true);
        check (! editor->canStartSampleIntoLoop (), "A loop overlapping before Sample Start is invalid");
        editor->zoneProperties.setLoopStart (100, true);
        editor->startSampleIntoLoop ();
        check (audition.getSimulationPhase () == Phase::loop && audition.getSamplePointsSelector () == Selection::LoopPoints && editor->isLoopSelected (),
               "Equal Sample and Loop Start immediately selects loop phase and marker range");
        expectControls (*editor, false, true, "Equal Sample and Loop Start highlights LOOP STOP immediately");
        editor->loopPlayButton.onClick ();
        editor->zoneProperties.setLoopStart (400, true);
        editor->zoneProperties.setLoopLength (200, true);

        // Regular audition retains its two independent transport modes.
        editor->oneShotPlayButton.onClick ();
        check (audition.getPlayState () == State::play, "Ordinary ONCE remains one-shot playback");
        expectControls (*editor, true, false, "Ordinary ONCE uses its own STOP highlight");
        editor->loopPlayButton.onClick ();
        check (audition.getPlayState () == State::loop, "Ordinary LOOP still switches to looping selected markers");
        expectControls (*editor, false, true, "Ordinary LOOP uses its own STOP highlight");
        editor->loopPlayButton.onClick ();

        // Follow the actual waveform -> ChannelEditor -> selected ZoneEditor
        // wiring, and the opposite phase -> zone -> large-waveform path.
        edits.init (root, preset.getValueTree ());
        bool copyHasData { false };
        auto channelEditor { std::make_unique<ChannelEditor> () };
        channelEditor->setLookAndFeel (&look);
        channelEditor->displayToolsMenu = [] (int) {};
        channelEditor->init (preset.getChannelVT (0), preset.getChannelVT (0).createCopy (), root,
                            preset.getChannelVT (0).getChild (0).createCopy (), &copyHasData);
        channelEditor->setSize (1140, 720);
        channelEditor->zoneTabs.setCurrentTabIndex (1);
        auto& waveform { channelEditor->sampleWaveformDisplay };
        waveform.refreshSimulationControls ();
        check (waveform.simulationButton.isEnabled (), "Real selected-zone wiring enables the waveform simulation control");
        waveform.simulationButton.onClick ();
        check (audition.getSampleSource () == std::make_tuple (0, 1) && audition.getPlayState () == State::sampleIntoLoop &&
               waveform.simulationButton.getButtonText () == "Stop simulation" && ! waveform.loopSelected,
               "Waveform toolbar starts the currently selected zone, not a cached source");
        audition.setSimulationPhase (Phase::loop, false);
        channelEditor->zoneEditors[1].updatePlaybackDisplay ();
        check (waveform.loopSelected, "Zone phase following selects loop presentation in the actual large waveform");
        waveform.onExpandRequested ();
        check (waveform.expanded && waveform.loopSelected, "Expanded waveform retains the same live simulation phase");
        channelEditor->zoneEditors[1].loopPlayButton.onClick ();
        check (audition.getPlayState () == State::stop && waveform.simulationButton.getButtonText () == "Sample > Loop",
               "Actual zone STOP synchronizes the toolbar in expanded view");
        channelEditor->zoneTabs.setCurrentTabIndex (2);
        check (! waveform.simulationButton.isEnabled (), "Selecting an empty zone disables simulation in the real waveform");
        channelEditor->setLookAndFeel (nullptr);
        channelEditor.reset ();

        editor->zoneProperties.setLoopStart (0, true);
        editor->zoneProperties.setLoopLength (100, true);
        check (! editor->canStartSampleIntoLoop () && availabilityChanges > 0, "Loop ending at Sample Start is disabled and publishes availability changes");
        editor->startSampleIntoLoop ();
        check (audition.getPlayState () == State::stop, "A stale trigger cannot start an unsupported earlier loop");
        editor->zoneProperties.setLoopStart (400, true);
        editor->zoneProperties.setLoopLength (200, true);
        editor->sampleProperties.setIsCv (true, true);
        check (! editor->canStartSampleIntoLoop (), "CV simulation is disabled at its entry point");
        editor->startSampleIntoLoop ();
        check (audition.getPlayState () == State::stop, "Direct CV simulation calls remain blocked");
        expectControls (*editor, false, false, "CV cannot acquire an audition STOP highlight");
        editor->sampleProperties.setIsCv (false, true);
        otherChannel->parentChannelProperties.setChannelMode (ChannelProperties::stereoRight, true);
        otherChannel->sampleProperties.setIsCv (true, true);
        check (! editor->canStartSampleIntoLoop (), "CV on a stereo partner disables simulation of the pair");
        otherChannel->sampleProperties.setIsCv (false, true);
        otherChannel->setStereoRightChannelMode (true);
        check (editor->canStartSampleIntoLoop () && ! otherChannel->canStartSampleIntoLoop (), "Only the left side of an audio stereo pair offers simulation");
        editor->sampleProperties.setStatus (SampleStatus::doesNotExist, true);
        check (! editor->canStartSampleIntoLoop (), "Missing audio disables simulation");
        editor->sampleProperties.setStatus (SampleStatus::exists, true);

        // Exercise the actual deferred callback with rapid state/source changes,
        // including a destroyed editor. Inject only the queue, since a console
        // test has no native application's dispatch loop on every platform.
        audition.setSampleSource (0, 0, false);
        audition.setPlayState (State::sampleIntoLoop, false);
        audition.setSimulationPhase (Phase::loop, false);
        audition.setSampleSource (0, 1, false);
        audition.setPlayState (State::stop, false);
        audition.setSimulationPhase (Phase::inactive, false);
        editor->oneShotPlayButton.setButtonText ("STOP");
        editor->oneShotPlayButton.setToggleState (true, juce::dontSendNotification);
        editor->loopPlayButton.setButtonText ("STOP");
        editor->loopPlayButton.setToggleState (true, juce::dontSendNotification);
        otherChannel->setLookAndFeel (nullptr);
        otherChannel.reset ();
        check (! pendingRefreshes.empty (), "Runtime notifications scheduled actual zone refresh callbacks");
        while (! pendingRefreshes.empty ())
        {
            const auto callbacks { std::move (pendingRefreshes) };
            pendingRefreshes.clear ();
            for (const auto& callback : callbacks) callback ();
        }
        expectControls (*editor, false, false, "Queued callbacks read latest state/source, and callbacks for destroyed editors are safe");
        expectControls (*otherZone, false, false, "No stale notification can reinstate STOP on another source");
        editor->setLookAndFeel (nullptr); otherZone->setLookAndFeel (nullptr);
        std::cout << "PASS: contained simulation phase/STOP highlights, source isolation, ordinary transport, invalid legacy eligibility, CV/stereo safety and deferred lifetime/state handling\n";
    }
};

void testSimulationUi () { SimulationUiTestAccess::run (); }
