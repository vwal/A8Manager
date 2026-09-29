#include "GUI/WaveformWorkspace.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "Assimil8or/Preset/PresetProperties.h"
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
    void check (bool passed, const char* message) { if (! passed) throw std::runtime_error (message); }

    juce::Component* find (juce::Component& component, const juce::String& name)
    {
        if (component.getName () == name) return &component;
        for (int i { 0 }; i < component.getNumChildComponents (); ++i)
            if (auto* result = find (*component.getChildComponent (i), name)) return result;
        return nullptr;
    }

    template <typename Type> Type& control (juce::Component& component, const juce::String& name)
    {
        auto* found { dynamic_cast<Type*> (find (component, name)) };
        if (found == nullptr) throw std::runtime_error (("Missing workspace control " + name).toStdString ());
        return *found;
    }

    juce::Button* button (juce::Component& component, const juce::String& text)
    {
        if (auto* found = dynamic_cast<juce::Button*> (&component); found != nullptr && found->getButtonText () == text) return found;
        for (int i { 0 }; i < component.getNumChildComponents (); ++i)
            if (auto* found = button (*component.getChildComponent (i), text)) return found;
        return nullptr;
    }

    void click (juce::Component& component, const juce::String& text)
    {
        auto* found { button (component, text) };
        check (found != nullptr && found->isEnabled () && found->onClick != nullptr, "Expected an enabled workspace action");
        found->onClick ();
    }

    void checkMonitorLayout (juce::Component& workspace)
    {
        auto& transpose { control<juce::Component> (workspace, "design-monitor-transpose") };
        auto& level { control<juce::Component> (workspace, "design-monitor-level") };
        auto& audition { control<juce::Button> (workspace, "design-audition") };
        const auto transposeBounds { workspace.getLocalArea (&transpose, transpose.getLocalBounds ()) };
        const auto levelBounds { workspace.getLocalArea (&level, level.getLocalBounds ()) };
        const auto auditionBounds { workspace.getLocalArea (&audition, audition.getLocalBounds ()) };
        check (transposeBounds.getRight () <= levelBounds.getX () && transposeBounds.getY () == levelBounds.getY (),
               "Transpose occupies the left monitor-control position, closer to audio-cycle settings");
        check (levelBounds.getY () >= auditionBounds.getBottom ()
               && levelBounds.getX () <= auditionBounds.getCentreX () && levelBounds.getRight () >= auditionBounds.getCentreX (),
               "Monitor level sits below the audition button");
    }

    void snapshot (juce::Component& workspace, const juce::String& name)
    {
        const auto path { juce::SystemStats::getEnvironmentVariable ("A8MANAGER_TEST_ARTIFACTS", {}) };
        if (path.isEmpty ()) return;
        juce::File folder { path };
        check (folder.createDirectory ().wasOk (), "Create workspace artifact directory");
        auto stream { folder.getChildFile (name + ".png").createOutputStream () };
        check (stream != nullptr && stream->setPosition (0), "Open workspace artifact");
        check (juce::PNGImageFormat ().writeImageToStream (workspace.createComponentSnapshot (workspace.getLocalBounds (), true, 1.0f), *stream), "Render workspace artifact");
        check (stream->truncate ().wasOk (), "Truncate old workspace artifact");
    }
}

struct WaveformWorkspaceTestAccess
{
    static void assignmentWorkflow ()
    {
        using namespace WaveformDesign;
        const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-workspace-assignment", "", false) };
        check (folder.createDirectory ().wasOk (), "Create owned assignment workflow folder");
        struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { folder };
        auto live { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType).createCopy () };
        live.setProperty (PresetProperties::IdPropertyId, 7, nullptr);
        live.setProperty (PresetProperties::NamePropertyId, "Unsaved name", nullptr);
        const auto original { live.createCopy () };
        std::uint64_t revision { 1 };
        auto applications { 0 }, prompts { 0 };
        juce::String promptText;
        juce::Array<juce::File> createdFiles;
        juce::File recipe;
        std::function<void (bool)> answer;
        WaveformWorkspace workspace;
        workspace.setSize (1000, 760);
        check (! control<juce::Button> (workspace, "design-assign").isEnabled (), "Assignment remains unavailable without a shared preset context");
        workspace.onGetAssignmentContext = [&] () -> std::optional<WaveformWorkspace::AssignmentContext>
        {
            return WaveformWorkspace::AssignmentContext { folder, live.createCopy (), revision };
        };
        workspace.onApplyAssignment = [&] (const WaveformWorkspace::AssignmentContext& context, const AssignmentResult& output)
        {
            if (context.revision != revision || context.folder != folder || ! context.preset.isEquivalentTo (live))
                return juce::Result::fail ("The preset changed while generating.");
            live = output.editedPreset.createCopy ();
            createdFiles = output.createdFiles;
            recipe = output.recipe;
            ++revision; ++applications;
            return juce::Result::ok ();
        };
        workspace.confirmAssignment = [&] (const juce::String& message, std::function<void (bool)> callback)
        {
            ++prompts; promptText = message; answer = std::move (callback);
        };
        workspace.refreshAssignmentContext ();
        auto& target { control<juce::ComboBox> (workspace, "design-target-channel") };
        auto& zone { control<juce::ComboBox> (workspace, "design-target-zone") };
        auto& slot { control<juce::ComboBox> (workspace, "design-export-slot") };
        check (target.getSelectedId () == 1 && zone.getSelectedId () == 1 && target.getText ().contains ("empty"),
               "Initial assignment suggests an empty independent channel and first zone");
        check (slot.getNumItems () == 199 && slot.getSelectedId () == 1 && slot.getItemId (198) == 199,
               "Separate package export exposes every valid preset slot independently");
        slot.setSelectedId (73, juce::sendNotificationSync);
        check (live.isEquivalentTo (original), "Changing the package preset number never changes the shared preset");
        target.setSelectedId (2, juce::sendNotificationSync);
        control<juce::Slider> (workspace, "design-phase-value").setValue (79, juce::sendNotificationSync);
        control<juce::TextEditor> (workspace, "design-name").setText ("Shared design", false);
        const auto recipeBeforeContext { juce::JSON::toString (toJson (workspace.getSettings ())) };
        ++revision;
        workspace.refreshAssignmentContext ();
        check (target.getSelectedId () == 2 && juce::JSON::toString (toJson (workspace.getSettings ())) == recipeBeforeContext,
               "Refreshing an edited preset retains the chosen target and current waveform design");
        click (workspace, "Generate & Assign...");
        check (prompts == 1 && promptText.contains ("channel 2, zone 1") && promptText.contains ("channel-wide") &&
               promptText.contains ("other zones") && promptText.contains ("CV range") && promptText.contains ("Save"),
               "Assignment confirms the exact destination, channel-wide impact, CV split and unsaved result");
        check (applications == 0 && folder.findChildFiles (juce::File::findFiles, false).isEmpty (), "Confirmation happens before generating files or changing the preset");
        answer (false); answer (true);
        check (applications == 0 && live.isEquivalentTo (original) && folder.findChildFiles (juce::File::findFiles, false).isEmpty (),
               "Canceled assignment is single-use and leaves files and existing unsaved edits untouched");
        click (workspace, "Generate & Assign...");
        ++revision;
        answer (true);
        check (applications == 0 && folder.findChildFiles (juce::File::findFiles, false).isEmpty () &&
               control<juce::Label> (workspace, "design-status").getText ().contains ("confirmation"),
               "A stale confirmation is rejected before starting generation");
        click (workspace, "Generate & Assign...");
        answer (true);
        auto waitForAssignment = [&]
        {
            for (auto tick { 0 }; tick < 500; ++tick)
            {
                workspace.timerCallback ();
                if (control<juce::Button> (workspace, "design-assign").isEnabled ()) return;
                std::this_thread::sleep_for (std::chrono::milliseconds (10));
            }
            throw std::runtime_error ("Waveform assignment worker did not finish");
        };
        waitForAssignment ();
        check (applications == 1 && createdFiles.size () >= 2 && recipe.existsAsFile (), "Worker generates files and recipe before applying its detached preset snapshot");
        for (const auto& file : createdFiles) check (file.existsAsFile (), "Successful assignment retains every generated file");
        check (! folder.getChildFile ("prst007.yml").exists () && live.getProperty (PresetProperties::NamePropertyId) == original.getProperty (PresetProperties::NamePropertyId),
               "Assignment preserves an unsaved preset name and never writes its YAML automatically");
        PresetProperties assignedPreset (live, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        ChannelProperties assignedChannel (assignedPreset.getChannelVT (1), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        ZoneProperties assignedZone (assignedChannel.getZoneVT (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        check (assignedZone.getSample ().isNotEmpty () && folder.getChildFile (assignedZone.getSample ()).existsAsFile (),
               "Assignment maps generated audio into the explicitly selected channel and zone");
        Settings savedDesign;
        check (fromJson (juce::JSON::parse (recipe.loadFileAsString ()), savedDesign).wasOk () && savedDesign.phaseDegrees == 79.0 &&
               juce::JSON::toString (toJson (savedDesign)) == recipeBeforeContext,
               "Saved assignment recipe represents the complete approved design snapshot");
        const auto successfulPreset { live.createCopy () };
        const auto successfulFiles { folder.findChildFiles (juce::File::findFiles, false).size () };
        click (workspace, "Generate & Assign...");
        answer (true);
        ++revision; // Change after worker dispatch but before the UI commit.
        waitForAssignment ();
        check (applications == 1 && live.isEquivalentTo (successfulPreset) && folder.findChildFiles (juce::File::findFiles, false).size () == successfulFiles,
               "Post-render stale assignment cleans up only its new files and retains the live preset and prior generation");

        control<juce::ComboBox> (workspace, "design-mode").setSelectedId (3, juce::sendNotificationSync);
        check (! target.isItemEnabled (3) && target.isItemEnabled (1), "A seven-voice bank disables starting channels without room for all voices");
        live.setProperty (PresetProperties::IdPropertyId, 8, nullptr);
        ChannelProperties thirdChannel (assignedPreset.getChannelVT (2), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        thirdChannel.setChannelMode (ChannelProperties::stereoRight, false);
        ++revision;
        workspace.refreshAssignmentContext ();
        check (! target.isItemEnabled (1) && ! target.isItemEnabled (2) && ! control<juce::Button> (workspace, "design-assign").isEnabled (),
               "Banks cannot overlap either side of an existing stereo pair");
        control<juce::ComboBox> (workspace, "design-mode").setSelectedId (1, juce::sendNotificationSync);
        check (! target.isItemEnabled (2) && ! target.isItemEnabled (3) && target.isItemEnabled (4), "Single-voice targets also protect both stereo partners");
        check (workspace.getSettings ().phaseDegrees == 79, "Preset changes do not reset the remembered audio design");
        control<juce::ComboBox> (workspace, "design-mode").setSelectedId (2, juce::sendNotificationSync);
        target.setSelectedId (4, juce::sendNotificationSync);
        click (workspace, "Generate & Assign...");
        check (promptText.contains ("Mix Off") && promptText.contains ("cannot share a channel"), "CV assignment explicitly warns about individual output routing and audio/CV separation");
        answer (false);
        workspace.setSize (975, 732); // Actual minimum space beside the shared preset sidebar/header.
        settle (workspace);
        for (const auto* name : { "design-assign", "design-export", "design-target-channel", "design-target-zone", "design-export-slot" })
        {
            auto& field { control<juce::Component> (workspace, name) };
            const auto bounds { workspace.getLocalArea (&field, field.getLocalBounds ()) };
            check (workspace.getLocalBounds ().contains (bounds) && bounds.getWidth () >= 80 && bounds.getHeight () >= 20,
                   "Assignment and package controls fit beside the shared preset list at minimum UI size");
        }
        snapshot (workspace, "waveform-workspace-assignment");
        std::cout << "PASS: waveform shared-preset assignment, exact targets, confirmation, worker apply, stale rollback, channel safety and independent package slot\n";
    }

    static void settle (WaveformWorkspace& workspace)
    {
        // Poll the actual message-thread timer path, without a native window,
        // audio device, file chooser, or a global application event loop.
        for (int tick { 0 }; tick < 150; ++tick)
        {
            workspace.timerCallback ();
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
            if (tick > 25 && ! control<juce::Label> (workspace, "design-status").getText ().contains ("Updating")) break;
        }
    }

    static void auditionAndExpandedPreview ()
    {
        using namespace WaveformDesign;
        // Host state precedes the component: its callbacks also run during
        // destruction. No device or native window is opened by this fixture.
        struct Host
        {
            WaveformAudition::PayloadPtr payload;
            bool active { false }, failStart { false }, failMonitor { false };
            int starts { 0 }, stops { 0 }, publications { 0 }, settingsCalls { 0 };
            double monitorDb { 999 }, transpose { 999 };
        } host;
        int closes { 0 };
        {
            WaveformWorkspace workspace;
            workspace.setSize (1160, 800);
            checkMonitorLayout (workspace);
            workspace.onAuditionPayload = [&] (WaveformAudition::PayloadPtr payload)
            {
                host.payload = std::move (payload);
                ++host.publications;
                if (! host.payload) host.active = false;
            };
            workspace.onStartAudition = [&]
            {
                ++host.starts;
                if (host.failStart) return juce::Result::fail ("No output device selected - open Audio settings.");
                host.active = host.payload != nullptr;
                return juce::Result::ok ();
            };
            workspace.onStopAudition = [&] { ++host.stops; host.active = false; };
            workspace.isAuditionActive = [&] { return host.active; };
            workspace.onAuditionMonitorChange = [&] (double db, double semitones)
            {
                host.monitorDb = db; host.transpose = semitones;
                return host.failMonitor ? juce::Result::fail ("Monitor frequency is outside the audible range; adjust Transpose.") : juce::Result::ok ();
            };
            workspace.onAudioSettings = [&] { ++host.settingsCalls; };
            workspace.onClose = [&] { ++closes; };
            settle (workspace);
            auto& compact { *find (workspace, "design-compact-preview") };
            auto& audition { control<juce::Button> (workspace, "design-audition") };
            check (compact.getWidth () >= 400 && compact.getWidth () <= 480 && compact.getWidth () < workspace.getWidth () / 2,
                   "Default waveform is compact, leaving useful space for monitor controls");
            check (host.payload && host.payload->getVoiceCount () == 1 && ! host.active && host.starts == 0 && audition.isEnabled (),
                   "Worker prepares an audio payload without automatically starting audition");
            const auto initialLevel { control<juce::Slider> (workspace, "design-monitor-level-value").getValue () };
            const auto initialTranspose { control<juce::Slider> (workspace, "design-monitor-transpose-value").getValue () };
            check (std::abs (initialLevel + 18) < 1e-7 && std::abs (initialTranspose) < 1e-7,
                   "Monitor starts at a conservative -18 dB with neutral transpose");
            click (workspace, "Audio settings...");
            check (host.settingsCalls == 1, "Audio settings delegates to the existing host device");
            host.failStart = true;
            click (workspace, "Start audition");
            check (! host.active && control<juce::Label> (workspace, "design-audition-hint").getText ().contains ("No output device"),
                   "A failed host start gives actionable feedback without pretending to play");
            host.failStart = false;
            click (workspace, "Start audition");
            check (host.active && std::abs (host.monitorDb + 18) < 1e-7 && std::abs (host.transpose) < 1e-7 && audition.getButtonText () == "Stop audition",
                   "Explicit Start supplies monitor controls and reflects host playback");
            const auto started { host.starts };
            const auto stopped { host.stops };
            click (workspace, "Expand waveform...");
            auto* popup { workspace.getExpandedPreview () };
            check (popup != nullptr && popup->isVisible () && popup->getPeer () == nullptr && popup->getWidth () > compact.getWidth (),
                   "Expand opens a larger separate visual view without a native peer in tests");
            auto* expanded { find (*popup, "design-expanded-preview") };
            check (expanded != nullptr && expanded->getProperties ()["renderGeneration"] == compact.getProperties ()["renderGeneration"],
                   "Expanded view starts on the same completed generation as compact preview");
            const auto initialGeneration { static_cast<int> (compact.getProperties ()["renderGeneration"]) };
            control<juce::Slider> (workspace, "design-phase-value").setValue (37, juce::sendNotificationSync);
            control<juce::Slider> (workspace, "design-phase-value").setValue (73, juce::sendNotificationSync);
            settle (workspace);
            check (host.active && host.starts == started && host.stops == stopped && host.payload->getSettings ().phaseDegrees == 73,
                   "Latest valid shaping edit replaces the payload while playing, with no restart");
            check (static_cast<int> (compact.getProperties ()["renderGeneration"]) > initialGeneration
                   && expanded->getProperties ()["renderGeneration"] == compact.getProperties ()["renderGeneration"],
                   "Open popup follows the latest rendered edit rather than a stale snapshot");
            const auto beforeContinuousDrag { host.publications };
            int intermediateSnapshots { 0 };
            auto lastPublishedGeneration { static_cast<int> (compact.getProperties ()["renderGeneration"]) };
            for (int event { 0 }; event < 70; ++event)
            {
                const auto previousPublications { host.publications };
                // Deliberately make another edit before every timer tick. A
                // reset-on-every-event debounce/latest-only publication path
                // cannot pass this regression merely by rendering quickly.
                control<juce::Slider> (workspace, "design-phase-value").setValue (100 + event, juce::sendNotificationSync);
                control<juce::ComboBox> (workspace, "design-frames").setSelectedId ((event / 15) % 2 == 0 ? 512 : 1024, juce::sendNotificationSync);
                workspace.timerCallback ();
                if (host.publications != previousPublications)
                {
                    const auto publishedGeneration { static_cast<int> (compact.getProperties ()["renderGeneration"]) };
                    check (host.payload && host.active && publishedGeneration > lastPublishedGeneration,
                           "Continuous shaping publishes monotonically newer snapshots without stopping");
                    lastPublishedGeneration = publishedGeneration;
                    const auto& audibleSettings { host.payload->getSettings () };
                    const auto expectedHz { juce::String (audibleSettings.sampleRate / audibleSettings.cycleFrames, 2) + " Hz" };
                    check (control<juce::Label> (workspace, "design-summary").getText ().contains (expectedHz),
                           "Lagging visual statistics describe the published snapshot rather than newest mutable controls");
                    if (audibleSettings.phaseDegrees != workspace.getSettings ().phaseDegrees)
                    {
                        ++intermediateSnapshots;
                        check (control<juce::Label> (workspace, "design-status").getText ().contains ("Updating"),
                               "Intermediate live snapshot keeps Updating status until the latest edit catches up");
                    }
                }
                std::this_thread::sleep_for (std::chrono::milliseconds (10));
            }
            check (host.publications - beforeContinuousDrag >= 3 && intermediateSnapshots >= 2 && host.starts == started,
                   "Sustained sub-debounce slider events produce multiple audible updates before release");
            settle (workspace);
            check (host.payload->getSettings ().phaseDegrees == workspace.getSettings ().phaseDegrees,
                   "The final live edit catches up after the drag ends");
            control<juce::ComboBox> (workspace, "design-frames").setSelectedId (512, juce::sendNotificationSync);
            control<juce::Slider> (workspace, "design-phase-value").setValue (73, juce::sendNotificationSync);
            settle (workspace);
            snapshot (workspace, "waveform-workspace-audition");
            snapshot (*popup, "waveform-workspace-expanded");
            popup->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
            check (! popup->isVisible () && host.active && host.starts == started && host.stops == stopped,
                   "Escape closes only the visual popup, without stopping or duplicating audition");
            click (workspace, "Expand waveform...");
            check (workspace.getExpandedPreview () == popup, "Reopening reuses the same owned visual popup");
            auto* window { dynamic_cast<juce::DocumentWindow*> (popup) };
            check (window != nullptr, "Expanded view is a resizable document window");
            check (window->getName () == "Source waveform - live design view", "Popup keeps a user-facing title, not its test/component identifier");
            window->closeButtonPressed ();
            check (! popup->isVisible () && host.active, "Popup close button affects only the visual view");

            const auto recipeBeforeMonitor { juce::JSON::toString (toJson (workspace.getSettings ())) };
            control<juce::Slider> (workspace, "design-monitor-level-value").setValue (-24, juce::sendNotificationSync);
            control<juce::Slider> (workspace, "design-monitor-transpose-value").setValue (12, juce::sendNotificationSync);
            check (host.active && std::abs (host.monitorDb + 24) < 1e-7 && std::abs (host.transpose - 12) < 1e-7
                   && juce::JSON::toString (toJson (workspace.getSettings ())) == recipeBeforeMonitor,
                   "Monitor controls change live host parameters, never the export/recipe model");
            host.failMonitor = true;
            control<juce::Slider> (workspace, "design-monitor-transpose-value").setValue (-48, juce::sendNotificationSync);
            check (! host.active && control<juce::Label> (workspace, "design-audition-hint").getText ().contains ("audible range"),
                   "Unsafe live monitor range is explained and stops audition");
            host.failMonitor = false;
            control<juce::Slider> (workspace, "design-monitor-transpose-value").setValue (0, juce::sendNotificationSync);
            click (workspace, "Start audition");
            host.failMonitor = true;
            control<juce::Slider> (workspace, "design-phase-value").setValue (81, juce::sendNotificationSync);
            settle (workspace);
            check (! host.active && control<juce::Label> (workspace, "design-audition-hint").getText ().contains ("audible range"),
                   "Live payload publication surfaces a host range stop without waiting for another Start");
            host.failMonitor = false;
            click (workspace, "Start audition");
            click (workspace, "Expand waveform...");
            control<juce::Slider> (workspace, "design-phase-value").setValue (89, juce::sendNotificationSync);
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
            workspace.timerCallback ();
            // Fault-inject an out-of-range control value to exercise the
            // validation barrier; normal UI bounds do not permit this value.
            auto& amplitude { control<juce::Slider> (workspace, "design-amplitude-value") };
            amplitude.setRange (0, 200, 0.1);
            amplitude.setValue (150, juce::sendNotificationSync);
            check (! host.active && ! host.payload && ! audition.isEnabled (), "Invalid design immediately stops/clears audition");
            host.active = true; // Simulate an engine stop ramp still reporting active.
            std::this_thread::sleep_for (std::chrono::milliseconds (30));
            workspace.timerCallback ();
            check (! host.payload, "A stale render cannot cross the invalid-design epoch even during a stop ramp");
            host.active = false;
            amplitude.setRange (0, 100, 0.1);
            amplitude.setValue (80, juce::sendNotificationSync);
            settle (workspace);
            check (audition.isEnabled () && ! host.active, "Correcting invalid settings restores readiness without automatic playback");
            click (workspace, "Start audition");
            control<juce::Slider> (workspace, "design-phase-value").setValue (93, juce::sendNotificationSync);
            std::this_thread::sleep_for (std::chrono::milliseconds (160));
            workspace.timerCallback (); // Dispatch an audio render before switching modes.
            control<juce::ComboBox> (workspace, "design-mode").setSelectedId (2, juce::sendNotificationSync);
            control<juce::ComboBox> (workspace, "design-mode").setSelectedId (1, juce::sendNotificationSync);
            host.active = true; // A stopped engine can briefly remain in its fade-out.
            std::this_thread::sleep_for (std::chrono::milliseconds (30));
            workspace.timerCallback ();
            check (! host.payload, "Rapid mode-away-and-back cannot adopt an old epoch while a stop ramp reports active");
            host.active = false;
            settle (workspace);
            check (host.payload && host.payload->getSettings ().phaseDegrees == 93 && ! host.active,
                   "Rapid return to audio prepares the current cached design without auto-starting");
            control<juce::ComboBox> (workspace, "design-mode").setSelectedId (2, juce::sendNotificationSync);
            check (! host.active && ! host.payload && ! audition.isEnabled ()
                   && ! find (workspace, "design-monitor-level")->isEnabled () && ! find (workspace, "design-monitor-transpose")->isEnabled (),
                   "CV mode immediately clears and disables all speaker audition paths");
            const auto beforeCvClick { host.starts };
            audition.onClick (); // Even a stale queued action cannot bypass CV safety.
            check (host.starts == beforeCvClick, "CV guard also rejects a queued audition callback");
            settle (workspace);
            check (! host.active && ! host.payload && ! audition.isEnabled (),
                   "An audio render completing across a CV mode switch cannot republish stale audio");
            check (popup->isVisible () && expanded->getProperties ()["renderGeneration"] == compact.getProperties ()["renderGeneration"],
                   "Expanded waveform remains synchronized when changing modes");
            check (control<juce::Label> (workspace, "design-audition-hint").getText ().contains ("CV speaker audition is disabled"),
                   "CV explains why speaker monitoring is unavailable");
            control<juce::ComboBox> (workspace, "design-mode").setSelectedId (3, juce::sendNotificationSync);
            control<juce::Slider> (workspace, "design-voices-value").setValue (8, juce::sendNotificationSync);
            control<juce::Slider> (workspace, "design-voice-8-0-value").setValue (21.5, juce::sendNotificationSync);
            control<juce::Slider> (workspace, "design-voice-8-2-value").setValue (-0.75, juce::sendNotificationSync);
            settle (workspace);
            check (host.payload && host.payload->getVoiceCount () == 8 && ! host.active
                   && std::abs (host.payload->getSettings ().voices[7].detuneCents - 21.5) < 1e-7
                   && std::abs (host.payload->getSettings ().voices[7].pan + 0.75) < 1e-7,
                   "Layer audition payload includes every voice's edited detune/pan and does not auto-start");
            click (workspace, "Start audition");
            snapshot (workspace, "waveform-workspace-bank-audition");
            snapshot (*popup, "waveform-workspace-expanded-bank");
            host.active = false; workspace.timerCallback ();
            check (audition.getButtonText () == "Start audition", "Monitor button polls actual host activity rather than stale local state");
            click (workspace, "Start audition");
            control<juce::Slider> (workspace, "design-phase-value").setValue (223, juce::sendNotificationSync);
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
            workspace.timerCallback (); // Back interrupts a submitted, newer edit.
            click (workspace, "Back to preset");
            check (closes == 1 && ! host.active && ! host.payload && ! popup->isVisible (),
                   "Leaving via Back stops/clears the host and closes the visual popup");
            settle (workspace);
            click (workspace, "Start audition");
            check (host.active && host.payload && host.payload->getSettings ().phaseDegrees == 223,
                   "Back during an in-flight edit requeues the current design and restores readiness without another edit");
            click (workspace, "Back to preset");
            workspace.timerCallback ();
            click (workspace, "Start audition");
            check (closes == 2 && host.active && host.payload->getSettings ().phaseDegrees == 223,
                   "Back with an up-to-date cache retains immediate explicit Start readiness");
            click (workspace, "Expand waveform...");
            control<juce::Slider> (workspace, "design-phase-value").setValue (245, juce::sendNotificationSync);
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
            workspace.timerCallback ();
            workspace.visibilityChanged (); // Headless fixture is not showing.
            check (! host.active && ! host.payload && ! popup->isVisible () && ! audition.isEnabled (),
                   "Hidden workspace stops/clears playback, closes popup and disables Start");
            settle (workspace);
            check (! host.payload && ! host.active, "A render finishing while hidden does not publish or reactivate audio");
            workspace.updateAuditionVisibility (true); // Same production adapter, no native peer/device.
            check (audition.isEnabled () && host.payload && ! host.active && host.payload->getSettings ().phaseDegrees == 245,
                   "Returning after a hidden in-flight edit restores the newest design and never auto-starts");
            click (workspace, "Start audition");
            check (host.active, "The recovered newest design can be explicitly started after showing again");
        }
        check (! host.active && ! host.payload, "Workspace destruction leaves no host audition payload or active playback");
        std::cout << "PASS: waveform monitor callbacks, live payloads/layer banks, CV safety, compact/expanded preview sync and lifecycle\n";
    }

    static void run ()
    {
        using namespace WaveformDesign;
        WaveformWorkspace workspace;
        workspace.setSize (1117, 720);
        checkMonitorLayout (workspace);
        int closeCount { 0 }, matchCalls { 0 };
        workspace.onClose = [&] { ++closeCount; };
        workspace.onMatchDuration = [&] (int selection) -> std::optional<double>
        {
            ++matchCalls;
            return selection == 1 ? std::optional<double> (3.25) : std::nullopt;
        };
        settle (workspace);
        check (workspace.getSettings ().mode == Mode::oscillator && validate (workspace.getSettings ()).wasOk (), "Audio-cycle workspace starts with a valid design");
        check (control<juce::Label> (workspace, "design-summary").getText ().contains ("Base"), "Actual background render publishes duration/base note");
        control<juce::Slider> (workspace, "design-amplitude-value").setValue (65, juce::sendNotificationSync);
        control<juce::Slider> (workspace, "design-offset-value").setValue (-10, juce::sendNotificationSync);
        control<juce::Slider> (workspace, "design-phase-value").setValue (45, juce::sendNotificationSync);
        check (std::abs (workspace.getSettings ().amplitude - 0.65) < 1e-9 && std::abs (workspace.getSettings ().offset + 0.1) < 1e-9
               && workspace.getSettings ().phaseDegrees == 45, "Actual sliders update the generation model");
        check (! find (workspace, "design-width")->isVisible (), "Sine hides irrelevant pulse width");
        settle (workspace); snapshot (workspace, "waveform-workspace-audio");

        control<juce::ComboBox> (workspace, "design-mode").setSelectedId (2, juce::sendNotificationSync);
        check (workspace.getSettings ().mode == Mode::modulation && ! find (workspace, "design-harmonics")->isVisible (), "CV mode hides inactive harmonic controls");
        control<juce::Slider> (workspace, "design-bpm-value").setValue (120, juce::sendNotificationSync);
        control<juce::Slider> (workspace, "design-beats-value").setValue (3, juce::sendNotificationSync);
        click (workspace, "Use BPM / beats");
        check (std::abs (workspace.getSettings ().durationSeconds - 1.5) < 1e-9, "BPM and beats explicitly set the CV duration");
        click (workspace, "Use selected length");
        check (matchCalls == 1 && workspace.getSettings ().durationSeconds == 3.25, "Match selected region uses the parent's duration callback");
        control<juce::ComboBox> (workspace, "design-match").setSelectedId (1, juce::sendNotificationSync);
        click (workspace, "Use selected length");
        check (workspace.getSettings ().durationSeconds == 3.25 && control<juce::Label> (workspace, "design-status").getText ().contains ("Select a loaded"), "Missing match is explained without changing the design");
        control<juce::ComboBox> (workspace, "design-shape").setSelectedId (6, juce::sendNotificationSync);
        control<juce::Slider> (workspace, "design-attack-value").setValue (100, juce::sendNotificationSync);
        check (validate (workspace.getSettings ()).wasOk () && workspace.getSettings ().attack + workspace.getSettings ().decay + workspace.getSettings ().release <= 1.000001,
               "Envelope UI constrains stage times to fit one cycle");
        workspace.setSize (1160, 800); // Normal logical workspace size; audio above also covers the compact layout.
        settle (workspace); snapshot (workspace, "waveform-workspace-cv");

        control<juce::ComboBox> (workspace, "design-shape").setSelectedId (7, juce::sendNotificationSync);
        control<juce::Slider> (workspace, "design-steps-value").setValue (4, juce::sendNotificationSync);
        click (workspace, "Ramp");
        check (validate (workspace.getSettings ()).wasOk () && workspace.getSettings ().steps[0] == -1 && workspace.getSettings ().steps[3] == 1,
               "Step ramp fills a valid editable sequence, including unused step slots");
        control<juce::ComboBox> (workspace, "design-shape").setSelectedId (9, juce::sendNotificationSync);
        check (find (workspace, "design-steps")->isVisible () && find (workspace, "design-seed")->isVisible () && find (workspace, "design-smoothing")->isVisible (), "Random exposes step count, reproducible seed and glide");
        control<juce::ComboBox> (workspace, "design-shape").setSelectedId (8, juce::sendNotificationSync);
        check (! find (workspace, "design-seed")->isVisible () && ! find (workspace, "design-smoothing")->isVisible (), "Drawn curves hide unrelated seed/glide controls");
        auto& canvas { *find (workspace, "design-drawing") };
        const auto before { workspace.getSettings ().drawn };
        const juce::MouseEvent drawingEvent { juce::Desktop::getInstance ().getMainMouseSource (), { 25.0f, 18.0f },
            juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            &canvas, &canvas, juce::Time::getCurrentTime (), { 25.0f, 18.0f }, juce::Time::getCurrentTime (), 1, false };
        canvas.mouseDown (drawingEvent);
        check (workspace.getSettings ().drawn != before && validate (workspace.getSettings ()).wasOk (), "Actual drawing gesture updates bounded freehand points");
        settle (workspace);
        auto& viewport { control<juce::Viewport> (workspace, "design-controls") };
        viewport.setViewPosition (0, viewport.getViewedComponent ()->getHeight ());
        snapshot (workspace, "waveform-workspace-drawing");

        const auto retainedCv { juce::JSON::toString (toJson (workspace.getSettings ())) };
        control<juce::ComboBox> (workspace, "design-mode").setSelectedId (3, juce::sendNotificationSync);
        check (workspace.getSettings ().mode == Mode::layers && workspace.getSettings ().voiceCount == 7, "Layer Bank starts with a seven-voice spread");
        control<juce::Slider> (workspace, "design-voices-value").setValue (8, juce::sendNotificationSync);
        control<juce::Slider> (workspace, "design-voice-8-0-value").setValue (17.5, juce::sendNotificationSync);
        control<juce::Slider> (workspace, "design-voice-8-2-value").setValue (-0.75, juce::sendNotificationSync);
        control<juce::Slider> (workspace, "design-voice-8-3-value").setValue (0.5, juce::sendNotificationSync);
        const auto eighthVoice { workspace.getSettings ().voices[7] };
        if (std::abs (eighthVoice.detuneCents - 17.5) > 1.0e-7 || std::abs (eighthVoice.pan + 0.75) > 1.0e-7 || std::abs (eighthVoice.level - 0.5) > 1.0e-7)
            std::cerr << "Voice 8 actual values: " << eighthVoice.detuneCents << ", " << eighthVoice.pan << ", " << eighthVoice.level << "\n";
        check (std::abs (eighthVoice.detuneCents - 17.5) < 1.0e-7 && std::abs (eighthVoice.pan + 0.75) < 1.0e-7
               && std::abs (eighthVoice.level - 0.5) < 1.0e-7, "Eighth voice detune/pan/gain remain independently editable");
        control<juce::Slider> (workspace, "design-calibration-value").setValue (35, juce::sendNotificationSync);
        check (workspace.getSettings ().measuredFullScaleVolts == 35, "Calibration UI spans the recipe's permitted range without assuming a voltage");
        settle (workspace);
        viewport.setViewPosition (0, 0); snapshot (workspace, "waveform-workspace-layers");
        viewport.setViewPosition (0, viewport.getViewedComponent ()->getHeight ()); snapshot (workspace, "waveform-workspace-layer-bank");
        const auto retainedLayers { juce::JSON::toString (toJson (workspace.getSettings ())) };
        control<juce::ComboBox> (workspace, "design-mode").setSelectedId (1, juce::sendNotificationSync);
        check (std::abs (workspace.getSettings ().amplitude - 0.65) < 1e-9 && workspace.getSettings ().phaseDegrees == 45,
               "Switching modes restores the edited audio design");
        control<juce::ComboBox> (workspace, "design-mode").setSelectedId (2, juce::sendNotificationSync);
        check (juce::JSON::toString (toJson (workspace.getSettings ())) == retainedCv, "Mode switching preserves CV duration and edited curve");
        control<juce::ComboBox> (workspace, "design-mode").setSelectedId (3, juce::sendNotificationSync);
        check (juce::JSON::toString (toJson (workspace.getSettings ())) == retainedLayers, "Mode switching preserves individual voice edits");
        control<juce::TextEditor> (workspace, "design-name").setText (".", false);
        click (workspace, "Export package...");
        check (control<juce::Label> (workspace, "design-status").getText ().contains ("usable design name"), "Invalid export name is rejected before opening a chooser or writing files");
        check (! find (workspace, "design-open-export")->isVisible (), "Open exported folder stays hidden until a successful explicit export");
        click (workspace, "Back to preset");
        check (closeCount == 1, "Back button delegates navigation without changing a preset");
        std::cout << "PASS: actual waveform workspace controls, background visual preview, modes, duration matching, envelope/steps/drawing, layers, calibration and safe export entry\n";
    }
};

void testWaveformWorkspace ()
{
    WaveformWorkspaceTestAccess::run ();
    WaveformWorkspaceTestAccess::auditionAndExpandedPreview ();
    WaveformWorkspaceTestAccess::assignmentWorkflow ();
}
