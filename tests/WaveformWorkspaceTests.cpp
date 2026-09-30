#include "GUI/WaveformWorkspace.h"
#include "GUI/ModernTheme.h"
#include "GUI/A8NamePreview.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "Assimil8or/Preset/PresetProperties.h"
#include "Assimil8or/Audio/RawCycleImport.h"
#include "Assimil8or/Audio/WaveformDesignRecall.h"
#include "Assimil8or/Audio/CvSampleSafety.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unordered_map>

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
        check (button (workspace, "Back to preset") == nullptr,
               "Designer navigation uses the shared Samples tab without a duplicate Back action");
        check (find (workspace, "design-audio-settings") == nullptr && button (workspace, "Audio settings...") == nullptr,
               "Designer uses the shared top-right Audio Settings instead of a duplicate local action");
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
        check (auditionBounds.getRight () == levelBounds.getRight (),
               "Audition remains right-aligned over Monitor level after removing the duplicate settings action");
    }

    void checkFooterLayout (juce::Component& workspace)
    {
        const auto boundsFor = [&] (const char* name)
        {
            auto* component { &control<juce::Component> (workspace, name) };
            // Include each field's caption, not only the named combobox.
            if (dynamic_cast<juce::ComboBox*> (component) != nullptr) component = component->getParentComponent ();
            return workspace.getLocalArea (component, component->getLocalBounds ());
        };
        std::vector<juce::Rectangle<int>> controls;
        for (const auto* name : { "design-name", "design-name-preview", "design-name-advice", "design-assignment-heading", "design-package-heading", "design-target-channel", "design-target-zone",
                                  "design-export-slot", "design-assign", "design-recall", "design-export", "design-open-export", "design-test-output" })
        {
            const auto bounds { boundsFor (name) };
            check (workspace.getLocalBounds ().contains (bounds) && bounds.getWidth () >= 80 && bounds.getHeight () >= 20,
                   "Footer controls and complete field captions remain within the workspace at every supported test width");
            for (const auto& previous : controls)
                check (! bounds.intersects (previous), "Footer headings, fields and actions never overlap");
            controls.push_back (bounds);
        }
        const auto assignment { boundsFor ("design-assignment-heading") }, package { boundsFor ("design-package-heading") };
        check (assignment.getY () == package.getY () && assignment.getRight () <= package.getX (),
               "Current preset heading occupies the left group and separate package heading occupies the right group");
        for (const auto* name : { "design-target-channel", "design-target-zone", "design-assign", "design-recall" })
        {
            const auto bounds { boundsFor (name) };
            check (bounds.getX () >= assignment.getX () && bounds.getRight () <= assignment.getRight () && bounds.getY () >= assignment.getBottom (),
                   "Assignment targets, Generate and Recall stay beneath the current-preset heading");
        }
        for (const auto* name : { "design-export-slot", "design-export" })
        {
            const auto bounds { boundsFor (name) };
            check (bounds.getX () >= package.getX () && bounds.getRight () <= package.getRight () && bounds.getY () >= package.getBottom (),
                   "Package preset and Export remain together beneath the separate-package heading");
        }
        const auto assign { boundsFor ("design-assign") }, recall { boundsFor ("design-recall") };
        const auto open { boundsFor ("design-open-export") }, test { boundsFor ("design-test-output") };
        check (boundsFor ("design-target-channel").getBottom () <= assign.getY () && boundsFor ("design-export").getBottom () <= test.getY (),
               "Target fields and package export occupy the row above Generate, Recall, Open and Test output");
        check (assign.getY () == recall.getY () && assign.getY () == open.getY () && assign.getY () == test.getY ()
               && assign.getRight () <= recall.getX () && recall.getRight () <= open.getX () && open.getRight () <= test.getX (),
               "Lower actions run left-to-right as Generate, Recall, Open in Sample workspace and Test output");
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
    static void namePreviewWorkflow ()
    {
        const auto example { A8NamePreview::fromFilename ("test-waveform-0123456789ab-01.wav") };
        check (example.select == "test-w" && example.channel == "test-wavef...01", "Module filename preview preserves identifying prefix and voice tail without .wav");
        const auto confirmed { A8NamePreview::fromFilename ("1234567890abcdefg-a4e6352772f0-01.wav") };
        check (confirmed.select == "123456" && confirmed.channel == "1234567890...01",
               "Filename previews match the hardware-confirmed Select and Channels displays");
        const auto shortName { A8NamePreview::fromFilename ("Bass.WAV") };
        check (shortName.select == "Bass" && shortName.channel == "Bass", "Short names and uppercase WAV suffix remain readable without artificial truncation");
        check (A8NamePreview::fromFilename ("abcdefghij.wav").channel == "abcdefghij"
               && A8NamePreview::fromFilename ("abcdefghijk.wav").channel == "abcdefghij...jk"
               && A8NamePreview::fromFilename ("abcdefghijklm.wav").channel == "abcdefghij...lm"
               && A8NamePreview::fromFilename ("tone.v1.wav").channel == "tone.v1"
               && A8NamePreview::fromFilename ({}).select.isEmpty (), "Filename preview handles threshold, embedded dots and empty names");
        const auto generatedShort { A8NamePreview::fromGeneratedPrefix ("test", 1) };
        const auto generatedLong { A8NamePreview::fromGeneratedPrefix ("1234567890abc", 8) };
        check (generatedShort.select == "test" && generatedShort.channel == "test...01"
               && generatedLong.select == "123456" && generatedLong.channel == "1234567890...08"
               && A8NamePreview::fromGeneratedPrefix ("test-", 1).channel == "test-...01"
               && A8NamePreview::fromGeneratedPrefix ({}, 1).select.isEmpty ()
               && A8NamePreview::fromGeneratedPrefix ("test", 9).channel.isEmpty (),
               "Generated previews use only the friendly prefix and valid automatic voice number, preserving user-entered hyphens");
        WaveformWorkspace workspace;
        workspace.setSize (975, 732);
        auto& name { control<juce::TextEditor> (workspace, "design-name") };
        auto& preview { control<juce::Label> (workspace, "design-name-preview") };
        auto& advice { control<juce::Label> (workspace, "design-name-advice") };
        check (preview.getText ().contains ("New-Wa"), "Initial design name has an immediate device preview");
        const auto before { juce::JSON::toString (WaveformDesign::toJson (workspace.getSettings ())) };
        name.setText ("test waveform", false);
        check (name.onTextChange != nullptr, "Typing is wired to the filename preview");
        name.onTextChange ();
        const auto plannedFilename = [&] { return preview.getTooltip ().fromFirstOccurrenceOf (": ", false, false).upToFirstOccurrenceOf (".wav", true, false); };
        const auto filename { plannedFilename () };
        const auto id { filename.dropLastCharacters (7).getLastCharacters (12) };
        check (preview.getText () == "A8 Select: test-w    A8 Channels: test-wavef...01"
               && filename == WaveformDesign::assignmentWaveName ("test waveform", id, 1)
               && WaveformDesign::isAssignmentIdValid (id) && ! preview.getTooltip ().containsChar ('?')
               && advice.getText ().contains ("first 6-10") && preview.getTooltip ().contains ("not editable")
               && preview.getTooltip ().contains ("automatic voice number") && preview.getTooltip ().contains ("user-controlled name portion")
               && preview.getTooltip ().contains ("hardware display can reveal"),
               "Typing previews the name portion while the tooltip explains and gives the real reserved filename");
        name.onTextChange ();
        check (plannedFilename () == filename, "Refreshing the preview keeps its reserved identifier stable");
        name.setText ("adf", false); name.onTextChange ();
        const auto shortFilename { WaveformDesign::assignmentWaveName ("adf", id, 1) };
        check (plannedFilename () == shortFilename && preview.getText () == "A8 Select: adf    A8 Channels: adf...01"
               && ! preview.getText ().containsChar ('?'),
               "Short generated-name previews omit automatic separators and identifiers while retaining the actual filename in the tooltip");
        name.setText ("test", false); name.onTextChange ();
        check (preview.getText () == "A8 Select: test    A8 Channels: test...01", "A four-character generated name has no automatic dash in either preview");
        name.setText ("1234567890abc", false); name.onTextChange ();
        check (preview.getText () == "A8 Select: 123456    A8 Channels: 1234567890...01", "Long generated names retain the established six/ten-character prefix widths");
        name.setText ("test-", false); name.onTextChange ();
        check (preview.getText () == "A8 Select: test-    A8 Channels: test-...01", "A genuinely user-entered hyphen is not stripped from the prefix");
        name.setText ("test waveform", false); name.onTextChange ();
        check (plannedFilename () == filename, "Editing a name and restoring it does not consume a generation identifier");
        check (juce::JSON::toString (WaveformDesign::toJson (workspace.getSettings ())) == before,
               "Filename guidance changes no waveform settings");
        for (const auto width : { 975, 1117, 1400 })
        {
            workspace.setSize (width, 732);
            for (auto* label : { &preview, &advice })
                check (workspace.getLocalBounds ().contains (label->getBounds ())
                       && juce::GlyphArrangement::getStringWidth (label->getFont (), label->getText ()) <= label->getWidth (),
                       "Device-name previews and naming advice fit compact and wide layouts without squeezing text");
            check (! preview.getBounds ().intersects (advice.getBounds ()), "Name preview and advice never overlap");
            check (advice.getBottom () <= name.getY () && advice.getRight () == name.getRight ()
                   && advice.getJustificationType () == juce::Justification::centredRight
                   && preview.getY () >= name.getBottom (),
                   "A single right-aligned naming hint sits above Design name, with live device previews below it");
        }
        name.setText ("Changed silently", false);
        control<juce::ComboBox> (workspace, "design-preset").setSelectedId (2, juce::sendNotificationSync);
        check (preview.getText ().contains ("Change"), "Loading a fresh design preset resynchronizes name guidance");
        name.setText ({}, false); name.onTextChange ();
        check (preview.getText () == "A8 Select: --    A8 Channels: --", "Empty names use the explicit -- placeholder, not a generated filename");
    }

    static void testOutputEntry ()
    {
        using namespace WaveformDesign;
        auto stopped { 0 }, opened { 0 }, cleared { 0 }, applied { 0 };
        WaveformWorkspace workspace;
        workspace.setSize (975, 732);
        const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory) };
        workspace.setInitialFolder (folder);
        control<juce::ComboBox> (workspace, "design-export-slot").setSelectedId (47, juce::sendNotificationSync);
        control<juce::TextEditor> (workspace, "design-name").setText ("Timing check", false);
        control<juce::TextEditor> (workspace, "design-name").onTextChange ();
        workspace.onStopAudition = [&] { ++stopped; };
        workspace.onAuditionPayload = [&] (WaveformAudition::PayloadPtr payload) { if (! payload) ++cleared; };
        workspace.onApplyAssignment = [&] (const auto&, const auto&) { ++applied; return juce::Result::ok (); };
        const auto before { juce::JSON::toString (toJson (workspace.getSettings ())) };
        workspace.launchTestOutput = [&] (const Settings& design, juce::File parent, const juce::String& name, int slot)
        {
            check (stopped == 1 && cleared == 1, "Entering hardware tests stops and clears computer audition before opening");
            check (juce::JSON::toString (toJson (design)) == before && parent == folder && name == "Timing check" && slot == 47,
                   "Hardware test output gets the current design, folder and separate package slot");
            ++opened;
        };
        click (workspace, "Test output...");
        check (opened == 1 && applied == 0 && juce::JSON::toString (toJson (workspace.getSettings ())) == before,
               "Entering hardware tests neither assigns a preset nor edits the design");
        auto& open { control<juce::Button> (workspace, "design-open-export") };
        open.setVisible (true);
        for (const auto width : { 975, 1117, 1400 })
        {
            workspace.setSize (width, 732);
            workspace.resized ();
            checkFooterLayout (workspace);
            if (width == 975) snapshot (workspace, "waveform-workspace-footer-compact");
            if (width == 1400) snapshot (workspace, "waveform-workspace-footer-large");
        }
        snapshot (workspace, "waveform-workspace-test-output-entry");
    }

    static void presetSaveStatus ()
    {
        WaveformWorkspace workspace;
        workspace.setSize (975, 732);
        settle (workspace);
        const auto before { juce::JSON::toString (WaveformDesign::toJson (workspace.getSettings ())) };
        auto& status { control<juce::Label> (workspace, "design-status") };
        const juce::String success { "A8 folder copy created: /test/card/koe-01. Working folder unchanged." };
        workspace.showPresetSaveStatus (success, false);
        check (status.getText () == success && status.getTooltip () == success
               && status.findColour (juce::Label::textColourId) == Theme::muted,
               "Successful preset copy reports the complete destination in visible status and its tooltip");
        const juce::String warning { "A8 folder copy created, but the selected preset changed. Its newer edits remain unsaved." };
        workspace.showPresetSaveStatus (warning, true);
        check (status.getText () == warning && status.getTooltip () == warning
               && status.findColour (juce::Label::textColourId) == Theme::warning,
               "A stale or failed original save is visibly distinguished from successful save completion");
        check (juce::JSON::toString (WaveformDesign::toJson (workspace.getSettings ())) == before,
               "Preset copy/save status never changes the waveform recipe");
    }

    static void recallWorkflow ()
    {
        using namespace WaveformDesign;
        const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-workspace-recall", "", false) };
        check (folder.createDirectory ().wasOk (), "Create owned recall workflow folder");
        struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { folder };
        auto live { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType).createCopy () };
        live.setProperty (PresetProperties::IdPropertyId, 17, nullptr);
        auto audio { startingPoint (Mode::oscillator, Shape::pulse) };
        audio.phaseDegrees = 87; audio.pulseWidth = 0.27; audio.harmonics = 13; audio.fold = 0.2;
        AssignmentResult assigned;
        check (prepareAssignment (audio, folder, "Recall me", live, 2, 0, assigned).wasOk (), "Generate real assigned waveform to recall");
        live = assigned.editedPreset;
        auto revision { std::uint64_t { 1 } };
        auto context = [&] () -> std::optional<WaveformWorkspace::AssignmentContext> { return WaveformWorkspace::AssignmentContext { folder, live.createCopy (), revision }; };
        std::function<void (bool)> answer;
        int prompts { 0 };
        juce::String message;
        WaveformWorkspace workspace;
        workspace.setSize (975, 732);
        workspace.onGetAssignmentContext = context;
        workspace.confirmRecall = [&] (const juce::String& text, std::function<void (bool)> callback) { ++prompts; message = text; answer = std::move (callback); };
        workspace.refreshAssignmentContext ();
        auto& target { control<juce::ComboBox> (workspace, "design-target-channel") };
        auto& zone { control<juce::ComboBox> (workspace, "design-target-zone") };
        auto& recall { control<juce::Button> (workspace, "design-recall") };
        check (! recall.isEnabled (), "Recall is unavailable for the suggested empty destination");
        target.setSelectedId (3, juce::sendNotificationSync);
        check (recall.isEnabled (), "An occupied target exposes recall without changing preset data");
        const auto before { juce::JSON::toString (toJson (workspace.getSettings ())) };
        click (workspace, "Recall assigned...");
        check (prompts == 1 && message.contains ("replaced") && juce::JSON::toString (toJson (workspace.getSettings ())) == before,
               "Recall confirms replacement before changing design settings");
        answer (false); answer (true);
        check (juce::JSON::toString (toJson (workspace.getSettings ())) == before, "Cancel is single-use and preserves the design");
        click (workspace, "Recall assigned...");
        ++revision;
        answer (true);
        check (juce::JSON::toString (toJson (workspace.getSettings ())) == before, "Changed preset invalidates a pending recall");
        click (workspace, "Recall assigned...");
        control<juce::Slider> (workspace, "design-phase-value").setValue (25, juce::sendNotificationSync);
        answer (true);
        check (workspace.getSettings ().phaseDegrees == 25, "Edits made during recall confirmation are not discarded");
        const auto liveBefore { live.createCopy () };
        const auto filesBefore { folder.findChildFiles (juce::File::findFiles, false).size () };
        click (workspace, "Recall assigned...");
        answer (true);
        check (juce::JSON::toString (toJson (workspace.getSettings ())) == juce::JSON::toString (toJson (audio)) &&
               target.getSelectedId () == 3 && zone.getSelectedId () == 1,
               "Recall restores every audio shaping setting and keeps its original destination");
        check (control<juce::TextEditor> (workspace, "design-name").getText () == "Recall me"
               && control<juce::Label> (workspace, "design-name-preview").getText ().contains ("A8 Select: Recall"),
               "Recipe recall immediately refreshes the name preview without requiring another typed character");
        check (live.isEquivalentTo (liveBefore) && folder.findChildFiles (juce::File::findFiles, false).size () == filesBefore,
               "Recall does not assign, save, or generate any preset/WAV files");
        click (workspace, "Recall assigned...");
        auto changedRecipe { audio }; changedRecipe.phaseDegrees = 42;
        check (assigned.recipe.replaceWithText (juce::JSON::toString (toJson (changedRecipe))), "Alter owned recipe during confirmation");
        answer (true);
        check (workspace.getSettings ().phaseDegrees == 87, "Externally changed recipe is not silently recalled after confirmation");

        auto cv { startingPoint (Mode::modulation, Shape::triangle) }; cv.durationSeconds = 0.02; cv.offset = 0.25;
        AssignmentResult cvAssigned;
        check (prepareAssignment (cv, folder, "Recall CV", live, 3, 0, cvAssigned).wasOk (), "Generate assigned CV recall fixture");
        live = cvAssigned.editedPreset; ++revision;
        workspace.recallAssigned (3, 0);
        answer (true);
        settle (workspace);
        check (juce::JSON::toString (toJson (workspace.getSettings ())) == juce::JSON::toString (toJson (cv)) &&
               ! control<juce::Button> (workspace, "design-audition").isEnabled (),
               "CV recall restores its mode and shaping without enabling speaker audition");

        auto bank { startingPoint (Mode::layers, Shape::saw) }; spreadVoices (bank, 3, 19, 137, 0.6);
        bank.voices[1].detuneCents = 9; bank.voices[2].pan = 0.31;
        AssignmentResult bankAssigned;
        check (prepareAssignment (bank, folder, "Recall bank", live, 4, 0, bankAssigned).wasOk (), "Generate assigned bank recall fixture");
        live = bankAssigned.editedPreset; ++revision;
        workspace.recallAssigned (5, 0); // Select the second voice, not its master.
        check (message.contains ("entire bank"), "Bank recall explains that it restores all voices");
        answer (true);
        check (juce::JSON::toString (toJson (workspace.getSettings ())) == juce::JSON::toString (toJson (bank)) && target.getSelectedId () == 5,
               "Recalling a bank follower restores all voices and selects the original bank's first channel");
        check (std::abs (control<juce::Slider> (workspace, "design-detune-spread-value").getValue () - 19) < 1.0e-7
               && std::abs (control<juce::Slider> (workspace, "design-phase-spread-value").getValue () - 137) < 1.0e-7
               && std::abs (control<juce::Slider> (workspace, "design-pan-spread-value").getValue () - 0.6) < 1.0e-7
               && control<juce::Label> (workspace, "design-spread-status").getText ().contains ("Custom"),
               "Legacy bank recall derives accurate master spreads and marks custom positions without altering its recipe");
        const auto bankBefore { juce::JSON::toString (toJson (workspace.getSettings ())) };
        ZoneProperties missing (live.getChild (0).getChild (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        missing.setSample ("ordinary.wav", false); ++revision;
        const auto promptsBefore { prompts };
        workspace.recallAssigned (0, 0);
        check (prompts == promptsBefore && juce::JSON::toString (toJson (workspace.getSettings ())) == bankBefore &&
               control<juce::Label> (workspace, "design-status").getText ().contains ("Could not recall"),
               "Missing/unrelated WAV recipe gives an explanation and preserves the design");
        auto doomed { std::make_unique<WaveformWorkspace> () };
        doomed->onGetAssignmentContext = context;
        doomed->confirmRecall = [&] (const juce::String&, std::function<void (bool)> callback) { answer = std::move (callback); };
        doomed->recallAssigned (5, 0);
        doomed.reset ();
        answer (true); // SafePointer must discard late native-modal callbacks.
        snapshot (workspace, "waveform-workspace-recalled-bank");
        std::cout << "PASS: saved audio/CV/bank recall, destinations, non-mutating confirmation, stale rejection and lifetime safety\n";
    }

    static void signalWarningWorkflow ()
    {
        WaveformWorkspace workspace;
        workspace.setSize (1000, 760);
        workspace.updateAuditionVisibility (true);
        bool active { false };
        int starts {}, prompts {}, inspections {};
        auto signalStatus { AuditionSignalCheck::Status::warning };
        std::function<void (bool)> answer;
        workspace.isAuditionActive = [&] { return active; };
        workspace.onStopAudition = [&] { active = false; };
        workspace.onStartAudition = [&] { ++starts; active = true; return juce::Result::ok (); };
        workspace.onAuditionPayload = [] (auto) {};
        workspace.onAuditionMonitorChange = [] (double, double) { return juce::Result::ok (); };
        workspace.inspectAuditionSignal = [&] (auto, double)
        {
            ++inspections;
            AuditionSignalCheck::Report report;
            report.status = signalStatus;
            report.reasons.add ("Synthetic high-confidence DC test warning");
            return report;
        };
        workspace.confirmAuditionWarning = [&] (const juce::String&, std::function<void (bool)> callback)
        { ++prompts; answer = std::move (callback); };
        settle (workspace);
        click (workspace, "Start audition");
        check (prompts == 1 && starts == 0 && ! active, "Signal warning holds Designer silent before Start");
        auto oldAnswer { answer };
        click (workspace, "Stop audition");
        oldAnswer (true);
        check (starts == 0, "STOP cancels a pending warning's playback intent");
        click (workspace, "Start audition"); answer (false);
        check (prompts == 2 && starts == 0, "Cancel never starts a suspicious audition");
        click (workspace, "Start audition");
        oldAnswer = answer;
        control<juce::Slider> (workspace, "design-monitor-level-value").setValue (-24, juce::sendNotificationSync);
        oldAnswer (true);
        check (starts == 0, "A level change invalidates warning confirmation");
        click (workspace, "Start audition"); answer (true);
        check (starts == 1 && active, "Explicit approval starts the unchanged audition");
        const auto approvedPrompts { prompts }, cachedInspections { inspections };
        click (workspace, "Stop audition"); click (workspace, "Start audition");
        check (starts == 2 && prompts == approvedPrompts && inspections == cachedInspections,
               "Unchanged repeated playback reuses inspection and approval without more prompts");
        control<juce::Slider> (workspace, "design-drive-value").setValue (0.2, juce::sendNotificationSync);
        settle (workspace);
        check (! active && starts == 2 && prompts == approvedPrompts + 1,
               "A suspicious live design edit is held before publishing its new payload");
        oldAnswer = answer;
        workspace.updateAuditionVisibility (false);
        oldAnswer (true);
        check (! active && starts == 2, "Leaving the Designer invalidates a live-edit warning");
        workspace.updateAuditionVisibility (true);
        signalStatus = AuditionSignalCheck::Status::blocked;
        control<juce::Slider> (workspace, "design-drive-value").setValue (0.3, juce::sendNotificationSync);
        settle (workspace);
        click (workspace, "Start audition");
        check (starts == 2 && ! active && prompts == approvedPrompts + 1,
               "Invalid signal data is blocked, not offered an unsafe override");
        std::cout << "PASS: Designer preflight warning, exact approval cache, stale/STOP/visibility rejection and live-edit gate\n";
    }

    static void layerConversionWorkflow ()
    {
        using namespace WaveformDesign;
        const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-workspace-layer-conversion", "", false) };
        check (folder.createDirectory ().wasOk (), "Create owned layer-conversion fixture");
        struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { folder };
        const auto live { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType).createCopy () };
        WaveformWorkspace workspace;
        workspace.setSize (1000, 760);
        workspace.updateAuditionVisibility (true);
        int prompts {}, starts {}, applications {};
        bool playing { false };
        WaveformAudition::PayloadPtr payload;
        std::function<void (bool)> answer;
        workspace.onGetAssignmentContext = [&] () -> std::optional<WaveformWorkspace::AssignmentContext>
        { return WaveformWorkspace::AssignmentContext { folder, live.createCopy (), 1 }; };
        workspace.onApplyAssignment = [&] (const auto&, const auto&) { ++applications; return juce::Result::ok (); };
        workspace.onAuditionPayload = [&] (auto value) { payload = std::move (value); };
        workspace.onStartAudition = [&] { ++starts; playing = true; return juce::Result::ok (); };
        workspace.onStopAudition = [&] { playing = false; };
        workspace.isAuditionActive = [&] { return playing; };
        workspace.confirmLayerConversion = [&] (const juce::String& message, std::function<void (bool)> callback)
        {
            check (message.contains ("Replace the current Layer Bank") && message.contains ("Preset values and WAV files are not changed"),
                   "Replacing a cached bank explains what is replaced and what stays unchanged");
            ++prompts; answer = std::move (callback);
        };
        workspace.refreshAssignmentContext ();
        auto& mode { control<juce::ComboBox> (workspace, "design-mode") };
        auto& phase { control<juce::Slider> (workspace, "design-phase-value") };
        auto& name { control<juce::TextEditor> (workspace, "design-name") };
        auto& convert { control<juce::Button> (workspace, "design-create-layers") };
        const auto state = [&] { return juce::JSON::toString (toJson (workspace.getSettings ())); };
        const auto expectedBank = [] (Settings source)
        {
            source.mode = Mode::layers;
            spreadVoices (source, 7, 24, 300, 0.8);
            return source;
        };
        name.setText ("My cycle", false);
        control<juce::ComboBox> (workspace, "design-shape").setSelectedId (static_cast<int> (Shape::pulse) + 1, juce::sendNotificationSync);
        control<juce::ComboBox> (workspace, "design-rate").setSelectedId (2, juce::sendNotificationSync);
        control<juce::ComboBox> (workspace, "design-frames").setSelectedId (1024, juce::sendNotificationSync);
        control<juce::ComboBox> (workspace, "design-playback").setSelectedId (1, juce::sendNotificationSync);
        phase.setValue (37, juce::sendNotificationSync);
        for (const auto& setting : { std::pair { "design-width-value", 33.0 }, { "design-harmonics-value", 17.0 },
                                    { "design-brightness-value", 59.0 }, { "design-drive-value", 12.0 },
                                    { "design-fold-value", 9.0 }, { "design-amplitude-value", 45.0 } })
            control<juce::Slider> (workspace, setting.first).setValue (setting.second, juce::sendNotificationSync);
        const auto source { workspace.getSettings () };
        const auto original { state () };
        check (convert.isVisible () && convert.isEnabled () && convert.getBounds ().getRight () <= find (workspace, "design-expand-preview")->getX ()
               && convert.getBottom () <= find (workspace, "design-summary")->getY (),
               "Create layer bank fits beneath the compact preview without overlapping expansion or summary");
        settle (workspace);
        snapshot (workspace, "waveform-workspace-create-layers");
        click (workspace, "Start audition");
        check (playing && starts == 1, "The source cycle is auditioning before explicit conversion");
        click (workspace, "Create layer bank...");
        check (prompts == 0 && ! workspace.hasPendingFileOperation () && ! playing && starts == 1 && ! payload,
               "First conversion is immediate, stops audition, and invalidates its old payload");
        check (state () == juce::JSON::toString (toJson (expectedBank (source))) && name.getText () == "My cycle",
               "Conversion preserves all edited source settings and name, changing only mode and seven-voice spread");
        auto& target { control<juce::ComboBox> (workspace, "design-target-channel") };
        check (target.isItemEnabled (1) && target.isItemEnabled (2) && ! target.isItemEnabled (3),
               "Seven-voice conversion recomputes destinations to require seven consecutive channels");
        auto& viewport { control<juce::Viewport> (workspace, "design-controls") };
        check (viewport.getViewPositionY () > 0 && viewport.getLocalBounds ().intersects (viewport.getLocalArea (find (workspace, "design-voices"), find (workspace, "design-voices")->getLocalBounds ())),
               "Conversion scrolls to the Layer Bank voice controls");
        settle (workspace);
        check (payload && payload->getSettings ().mode == Mode::layers && payload->getSettings ().voiceCount == 7 && ! playing && starts == 1,
               "New audition payload includes the full bank without automatically starting playback");
        snapshot (workspace, "waveform-workspace-converted-bank");
        Theme::setAppearance (true); Theme::refreshComponentTree (workspace);
        snapshot (workspace, "waveform-workspace-converted-bank-light");
        Theme::setAppearance (false); Theme::refreshComponentTree (workspace);
        check (! convert.isVisible () && ! convert.isEnabled (), "Bank mode cannot recursively convert itself");
        control<juce::Slider> (workspace, "design-voices-value").setValue (3, juce::sendNotificationSync);
        control<juce::Slider> (workspace, "design-voice-2-0-value").setValue (13, juce::sendNotificationSync);
        const auto customBank { state () };
        mode.setSelectedId (1, juce::sendNotificationSync);
        check (state () == original, "Returning to Audio Cycle restores the original waveform and all shaping exactly");
        click (workspace, "Create layer bank...");
        check (prompts == 1 && workspace.hasPendingFileOperation () && ! convert.isEnabled ()
               && ! button (workspace, "Load recipe / cycle WAV...")->isEnabled ()
               && ! find (workspace, "design-assign")->isEnabled (), "Existing bank asks before replacement and blocks overlapping file operations");
        convert.onClick ();
        check (prompts == 1, "Repeated invocation cannot stack replacement prompts");
        const auto canceled { answer }; canceled (false); canceled (true);
        check (state () == original && ! workspace.hasPendingFileOperation (), "Canceled replacement is single-use and leaves the source unchanged");
        mode.setSelectedId (3, juce::sendNotificationSync);
        check (state () == customBank, "Cancel also preserves every edit in the prior Layer Bank");
        mode.setSelectedId (1, juce::sendNotificationSync);

        click (workspace, "Create layer bank..."); phase.setValue (43, juce::sendNotificationSync);
        const auto edited { state () }; answer (true);
        check (state () == edited && ! workspace.hasPendingFileOperation (), "A stale confirmation cannot replace a newly edited design");
        click (workspace, "Create layer bank..."); name.setText ("Renamed cycle", false); answer (true);
        check (state () == edited && name.getText () == "Renamed cycle", "Renaming during confirmation invalidates conversion");
        click (workspace, "Create layer bank..."); mode.setSelectedId (2, juce::sendNotificationSync);
        const auto cv { state () }; answer (true); convert.onClick ();
        check (state () == cv && ! convert.isVisible () && ! convert.isEnabled (), "Mode changes reject stale conversion and CV cannot become auditionable audio");
        mode.setSelectedId (1, juce::sendNotificationSync);
        click (workspace, "Create layer bank..."); workspace.updateAuditionVisibility (false); answer (true);
        check (state () == edited, "Leaving the workspace rejects a pending conversion");
        workspace.updateAuditionVisibility (true);
        const auto latestSource { workspace.getSettings () };
        click (workspace, "Create layer bank..."); answer (true);
        check (state () == juce::JSON::toString (toJson (expectedBank (latestSource))),
               "Confirmed replacement initializes a fresh spread without restoring the previous bank");
        mode.setSelectedId (1, juce::sendNotificationSync);
        check (state () == edited, "Confirmed replacement retains the latest source cycle");
        check (applications == 0 && starts == 1 && folder.findChildFiles (juce::File::findFiles, false).isEmpty (),
               "Design conversion and confirmations never assign a preset or create files");

        // Exercise the same action with actual raw WAV data, not a synthesized
        // starting point or a generated WAV with a pre-existing recipe.
        const auto raw { folder.getChildFile ("Imported.wav") };
        {
            std::unique_ptr<juce::OutputStream> stream { raw.createOutputStream () };
            auto writer { juce::WavAudioFormat ().createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (48000).withNumChannels (1).withBitsPerSample (24)) };
            juce::AudioBuffer<float> audio (1, 128);
            for (int i { 0 }; i < 128; ++i)
                audio.setSample (0, i, static_cast<float> (0.4 * std::sin (juce::MathConstants<double>::twoPi * i / 128.0)
                                                       + 0.1 * std::sin (juce::MathConstants<double>::twoPi * 3 * i / 128.0)));
            check (writer && writer->writeFromAudioSampleBuffer (audio, 0, 128) && writer->flush (), "Write raw source cycle");
        }
        juce::MemoryBlock rawBefore, rawAfter;
        check (raw.loadFileAsData (rawBefore), "Snapshot original imported WAV");
        workspace.confirmRawImport = [] (const juce::String&, std::function<void (bool)> callback) { callback (true); };
        workspace.importSingleCycle (raw);
        phase.setValue (25, juce::sendNotificationSync);
        control<juce::Slider> (workspace, "design-harmonics-value").setValue (11, juce::sendNotificationSync);
        const auto imported { workspace.getSettings () };
        click (workspace, "Create layer bank..."); answer (true);
        const auto bank { workspace.getSettings () };
        check (imported.shape == Shape::imported && bank.importedCycle == imported.importedCycle && ! bank.importedCycle.empty ()
               && state () == juce::JSON::toString (toJson (expectedBank (imported))),
               "Imported source frames, name and edited shaping survive explicit conversion despite an existing bank");
        Render rendered;
        check (render (bank, rendered).wasOk () && rendered.voices.size () == 7 && rendered.frames == imported.cycleFrames,
               "Converted imported cycle renders all seven voices at the retained cycle length");
        AssignmentResult assignment;
        check (prepareAssignment (bank, folder, "Imported bank", live, 0, 0, assignment).wasOk (), "Converted bank assigns through the real seven-channel exporter");
        WaveformDesignRecall::RecalledDesign recalled;
        const auto loaded { WaveformDesignRecall::loadRecipe (assignment.recipe, recalled) };
        if (loaded.failed ()) std::cerr << loaded.getErrorMessage () << '\n';
        // Recipes serialize doubles to decimal JSON; source preservation must
        // tolerate decimal roundoff, but remain well below one PCM24 step.
        check (loaded.wasOk () && recalled.settings.importedCycle.size () == imported.importedCycle.size ()
               && recalled.settings.voiceCount == 7 && recalled.settings.mode == Mode::layers && recalled.settings.shape == Shape::imported,
               "Saved converted bank recipe recalls all voices and embeds the source cycle");
        double sourceError { 0 }, renderError { 0 };
        for (size_t frame { 0 }; frame < imported.importedCycle.size (); ++frame)
            sourceError = std::max (sourceError, std::abs (recalled.settings.importedCycle[frame] - imported.importedCycle[frame]));
        check (sourceError < 1.0e-14, "Saved imported cycle differs only by decimal JSON roundoff");
        Render recalledAudio;
        check (render (recalled.settings, recalledAudio).wasOk () && recalledAudio.voices.size () == rendered.voices.size ()
               && recalledAudio.frames == rendered.frames, "Recalled bank renders with the original dimensions");
        for (size_t voice { 0 }; voice < rendered.voices.size (); ++voice)
            for (int frame { 0 }; frame < rendered.frames; ++frame)
                renderError = std::max (renderError, std::abs (static_cast<double> (rendered.voices[voice].getSample (0, frame))
                                                           - recalledAudio.voices[voice].getSample (0, frame)));
        check (renderError <= 1.0 / 8388608.0, "Saved/recall bank render remains within one PCM24 step for every voice");
        std::cout << "Converted bank round-trip max error: source=" << sourceError << ", rendered=" << renderError << '\n';
        for (int channel { 0 }; channel < 7; ++channel)
        {
            const auto sample { assignment.editedPreset.getChild (channel).getChild (0).getProperty (ZoneProperties::SamplePropertyId).toString () };
            check (sample.isNotEmpty () && folder.getChildFile (sample).existsAsFile (), "Every converted voice is assigned to its own existing WAV");
        }
        check (raw.loadFileAsData (rawAfter) && rawAfter == rawBefore && applications == 0, "Conversion and export leave the original WAV and live preset untouched");
        auto doomed { std::make_unique<WaveformWorkspace> () };
        std::function<void (bool)> late;
        doomed->confirmLayerConversion = [&] (const juce::String&, auto callback) { late = std::move (callback); };
        control<juce::ComboBox> (*doomed, "design-mode").setSelectedId (3, juce::sendNotificationSync);
        control<juce::ComboBox> (*doomed, "design-mode").setSelectedId (1, juce::sendNotificationSync);
        click (*doomed, "Create layer bank...");
        check (late != nullptr, "Destruction fixture reaches conversion confirmation");
        doomed.reset (); late (true);
        std::cout << "PASS: explicit waveform-to-layer conversion, source/cache preservation, imported-cycle export/recall, target refresh and stale/cancel/lifetime guards\n";
    }

    static void rawImportWorkflow ()
    {
        using namespace WaveformDesign;
        const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-workspace-raw-import", "", false) };
        check (folder.createDirectory ().wasOk (), "Create owned raw-cycle workspace fixture");
        struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { folder };
        auto writeWave = [&] (const juce::File& file, int frames, int channels, bool cv = false, float offset = 0.0f)
        {
            auto fileStream { file.createOutputStream () };
            check (fileStream && fileStream->setPosition (0) && fileStream->truncate ().wasOk (), "Create a complete owned raw WAV fixture");
            std::unique_ptr<juce::OutputStream> stream { std::move (fileStream) };
            auto options { juce::AudioFormatWriterOptions {}.withSampleRate (48000).withNumChannels (channels).withBitsPerSample (24) };
            if (cv)
            {
                const auto metadata { CvSampleSafety::exportMetadata (true) };
                std::unordered_map<juce::String, juce::String> values;
                for (int index { 0 }; index < metadata.size (); ++index)
                    values.emplace (metadata.getAllKeys ()[index], metadata.getAllValues ()[index]);
                options = options.withMetadataValues (values);
            }
            auto writer { juce::WavAudioFormat ().createWriterFor (stream, options) };
            juce::AudioBuffer<float> samples (channels, frames);
            for (int channel { 0 }; channel < channels; ++channel)
                for (int frame { 0 }; frame < frames; ++frame)
                    samples.setSample (channel, frame, offset + 0.45f * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * frame / frames + channel)));
            check (writer && writer->writeFromAudioSampleBuffer (samples, 0, frames) && writer->flush (), "Write bounded raw audio/CV fixture samples");
        };
        const auto mono { folder.getChildFile ("Raw cycle.wav") }, stereo { folder.getChildFile ("Stereo cycle.wav") };
        const auto altered { folder.getChildFile ("Altered cycle.wav") }, cv { folder.getChildFile ("CV cycle.wav") };
        const auto tooShort { folder.getChildFile ("Too short.wav") }, tooLong { folder.getChildFile ("Too long.wav") };
        writeWave (mono, 96, 1); writeWave (stereo, 96, 2); writeWave (altered, 96, 1);
        writeWave (cv, 96, 1, true); writeWave (tooShort, 3, 1); writeWave (tooLong, 8193, 1);
        juce::MemoryBlock originalMono, originalStereo;
        check (mono.loadFileAsData (originalMono) && stereo.loadFileAsData (originalStereo), "Snapshot raw sources before any UI import");
        const auto originalFileCount { folder.findChildFiles (juce::File::findFiles, false).size () };
        auto live { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType).createCopy () };
        live.setProperty (PresetProperties::IdPropertyId, 19, nullptr);
        ZoneProperties rawZone (live.getChild (2).getChild (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        rawZone.setSample (mono.getFileName (), false);
        const auto initialPreset { live.createCopy () };
        std::uint64_t revision { 1 };
        int rawPrompts { 0 }, stereoPrompts { 0 }, recipePrompts { 0 }, starts { 0 }, applications { 0 };
        std::function<void (bool)> answer, recallAnswer;
        std::function<void (int)> choose;
        WaveformWorkspace workspace;
        workspace.setSize (1000, 760);
        workspace.onGetAssignmentContext = [&] () -> std::optional<WaveformWorkspace::AssignmentContext>
        { return WaveformWorkspace::AssignmentContext { folder, live.createCopy (), revision }; };
        workspace.onApplyAssignment = [&] (const auto&, const auto&) { ++applications; return juce::Result::ok (); };
        workspace.onStartAudition = [&] { ++starts; return juce::Result::ok (); };
        workspace.isAuditionActive = [] { return false; };
        workspace.confirmRawImport = [&] (const juce::String&, std::function<void (bool)> callback)
        { ++rawPrompts; answer = std::move (callback); };
        workspace.chooseRawChannel = [&] (const juce::String&, std::function<void (int)> callback)
        { ++stereoPrompts; choose = std::move (callback); };
        workspace.confirmRecall = [&] (const juce::String&, std::function<void (bool)> callback)
        { ++recipePrompts; recallAnswer = std::move (callback); };
        workspace.refreshAssignmentContext ();
        auto& name { control<juce::TextEditor> (workspace, "design-name") };
        auto& phase { control<juce::Slider> (workspace, "design-phase-value") };
        auto& mode { control<juce::ComboBox> (workspace, "design-mode") };
        auto& shape { control<juce::ComboBox> (workspace, "design-shape") };
        const auto state = [&] { return juce::JSON::toString (toJson (workspace.getSettings ())); };
        const auto initial { state () };
        workspace.importSingleCycle (mono);
        check (rawPrompts == 1 && stereoPrompts == 0 && workspace.hasPendingFileOperation () && state () == initial,
               "Raw mono import confirms replacement before changing the design and blocks competing file work");
        const auto cancelled { answer };
        cancelled (false); cancelled (true);
        check (! workspace.hasPendingFileOperation () && state () == initial && live.isEquivalentTo (initialPreset),
               "Raw import cancellation is single-use and preserves the current design and preset");

        workspace.importSingleCycle (mono);
        phase.setValue (37, juce::sendNotificationSync);
        const auto editedDesign { state () };
        answer (true);
        check (! workspace.hasPendingFileOperation () && state () == editedDesign,
               "Editing a shaping value during import confirmation cannot be overwritten by the old request");
        workspace.importSingleCycle (mono);
        name.setText ("Name edited during import", false); name.onTextChange ();
        answer (true);
        check (! workspace.hasPendingFileOperation () && state () == editedDesign && name.getText () == "Name edited during import",
               "Editing only the design name also invalidates a pending raw import");
        workspace.recallAssigned (2, 0);
        ++revision;
        answer (true);
        check (! workspace.hasPendingFileOperation () && state () == editedDesign,
               "Changing the shared preset revision rejects a pending raw import without replacing the design");
        workspace.importSingleCycle (altered);
        writeWave (altered, 96, 1, false, 0.12f);
        answer (true);
        check (! workspace.hasPendingFileOperation () && state () == editedDesign,
               "A source WAV changed during confirmation is not silently imported with different samples");

        for (const auto& invalid : { tooShort, tooLong, cv, folder.getChildFile ("missing.wav") })
        {
            const auto beforePrompts { rawPrompts }, beforeStereo { stereoPrompts };
            workspace.importSingleCycle (invalid);
            check (rawPrompts == beforePrompts && stereoPrompts == beforeStereo && ! workspace.hasPendingFileOperation () && state () == editedDesign,
                   "Invalid, missing, overlong and known-CV sources are refused before any replacement confirmation");
        }
        mode.setSelectedId (2, juce::sendNotificationSync);
        workspace.importSingleCycle (mono); answer (true);
        Settings expectedMono;
        check (RawCycleImport::load (mono, RawCycleImport::StereoChannel::unspecified, expectedMono).wasOk (), "Load reference raw mono through the production import backend");
        check (workspace.getSettings ().mode == Mode::oscillator && workspace.getSettings ().shape == Shape::imported
               && workspace.getSettings ().importedCycle == expectedMono.importedCycle && workspace.getSettings ().cycleFrames == 128
               && shape.getSelectedId () == static_cast<int> (Shape::imported) + 1 && shape.isItemEnabled (static_cast<int> (Shape::imported) + 1),
               "Raw import from CV switches to Audio Cycle, retains every source frame, and exposes the imported shape");
        check (name.getText () == mono.getFileNameWithoutExtension (), "Imported raw cycle suggests its source basename as the editable design name");
        const auto importedPoints { workspace.getSettings ().importedCycle };
        control<juce::Slider> (workspace, "design-drive-value").setValue (40, juce::sendNotificationSync);
        check (std::abs (workspace.getSettings ().drive - 0.4) < 1.0e-7 && workspace.getSettings ().importedCycle == importedPoints,
               "Imported cycles support normal shaping controls without altering their stored source samples");
        shape.setSelectedId (static_cast<int> (Shape::sine) + 1, juce::sendNotificationSync);
        shape.setSelectedId (static_cast<int> (Shape::imported) + 1, juce::sendNotificationSync);
        check (workspace.getSettings ().shape == Shape::imported && workspace.getSettings ().importedCycle == importedPoints,
               "The shape chooser can leave and return to the imported source without losing it");
        Settings restored;
        check (fromJson (toJson (workspace.getSettings ()), restored).wasOk () && restored.importedCycle == importedPoints,
               "The edited GUI design embeds its imported cycle in a reloadable recipe");
        settle (workspace);
        check (starts == 0 && applications == 0 && live.isEquivalentTo (initialPreset)
               && folder.findChildFiles (juce::File::findFiles, false).size () == originalFileCount,
               "Importing and shaping never auto-audition, assign, save, or create files");
        snapshot (workspace, "waveform-workspace-imported-cycle");

        const auto beforeStereo { state () };
        const auto beforeRawPrompts { rawPrompts };
        workspace.importSingleCycle (stereo);
        check (stereoPrompts == 1 && rawPrompts == beforeRawPrompts && workspace.hasPendingFileOperation (),
               "Stereo source asks for an explicit side before asking to replace the design");
        const auto cancelledChoice { choose };
        cancelledChoice (0); cancelledChoice (1);
        check (! workspace.hasPendingFileOperation () && state () == beforeStereo && rawPrompts == beforeRawPrompts,
               "Canceling stereo choice is single-use and never advances to a replacement confirmation");
        workspace.importSingleCycle (stereo);
        phase.setValue (51, juce::sendNotificationSync);
        const auto choiceEdit { state () };
        choose (2);
        check (! workspace.hasPendingFileOperation () && rawPrompts == beforeRawPrompts && state () == choiceEdit,
               "A design edited while stereo choice is open cannot be replaced by that stale choice");
        for (int selection { 1 }; selection <= 3; ++selection)
        {
            workspace.importSingleCycle (stereo); choose (selection); answer (true);
            Settings expected;
            const auto channel { selection == 1 ? RawCycleImport::StereoChannel::left : selection == 2 ? RawCycleImport::StereoChannel::right : RawCycleImport::StereoChannel::average };
            check (RawCycleImport::load (stereo, channel, expected).wasOk () && workspace.getSettings ().importedCycle == expected.importedCycle,
                   "The actual stereo prompt maps Left, Right and Average to the selected production import channel");
        }
        mode.setSelectedId (3, juce::sendNotificationSync);
        control<juce::Slider> (workspace, "design-voices-value").setValue (3, juce::sendNotificationSync);
        workspace.importSingleCycle (mono); answer (true);
        check (workspace.getSettings ().mode == Mode::layers && workspace.getSettings ().shape == Shape::imported
               && workspace.getSettings ().voiceCount == 7 && workspace.getSettings ().importedCycle == expectedMono.importedCycle
               && workspace.getSettings ().voices[0].detuneCents != workspace.getSettings ().voices[6].detuneCents,
               "Raw import in Layer Bank keeps bank mode and establishes the default seven-voice spread around the source cycle");

        mode.setSelectedId (1, juce::sendNotificationSync);
        const auto beforeAssignedPrompts { rawPrompts };
        workspace.recallAssigned (2, 0);
        check (rawPrompts == beforeAssignedPrompts + 1 && recipePrompts == 0,
               "Recall assigned offers raw import when the assigned WAV has no generated recipe");
        answer (true);
        check (workspace.getSettings ().shape == Shape::imported && workspace.getSettings ().importedCycle == expectedMono.importedCycle
               && control<juce::ComboBox> (workspace, "design-target-channel").getSelectedId () == 3
               && control<juce::ComboBox> (workspace, "design-target-zone").getSelectedId () == 1
               && live.isEquivalentTo (initialPreset), "Assigned raw import keeps the source destination and does not mutate its preset");

        auto savedDesign { startingPoint (Mode::oscillator, Shape::pulse) };
        savedDesign.cycleFrames = 128;
        AssignmentResult generated;
        check (prepareAssignment (savedDesign, folder, "Strict recipe", initialPreset, 3, 0, generated).wasOk (), "Create actual generated WAV for strict recall regression");
        live = generated.editedPreset; ++revision;
        const auto rawBeforeRecipe { rawPrompts };
        workspace.recallAssigned (3, 0);
        check (recipePrompts == 1 && rawPrompts == rawBeforeRecipe, "A generated assigned WAV still takes the strict saved-recipe recall path");
        recallAnswer (false);
        const auto beforeCorruptRecipe { state () };
        check (generated.recipe.replaceWithText ("{broken recipe"), "Corrupt only the owned generated-recipe fixture");
        workspace.recallAssigned (3, 0);
        check (recipePrompts == 1 && rawPrompts == rawBeforeRecipe && state () == beforeCorruptRecipe && ! workspace.hasPendingFileOperation (),
               "A present but corrupt generated recipe is rejected instead of silently falling back to raw import");
        juce::MemoryBlock monoAfter, stereoAfter;
        check (mono.loadFileAsData (monoAfter) && stereo.loadFileAsData (stereoAfter) && monoAfter == originalMono && stereoAfter == originalStereo
               && starts == 0 && applications == 0, "All raw import paths preserve source WAV bytes and never start audition or apply a preset");
        auto doomed { std::make_unique<WaveformWorkspace> () };
        std::function<void (bool)> lateAnswer;
        doomed->confirmRawImport = [&] (const juce::String&, std::function<void (bool)> callback) { lateAnswer = std::move (callback); };
        doomed->importSingleCycle (mono);
        check (lateAnswer != nullptr, "Destroyed-workspace fixture reaches raw replacement confirmation");
        doomed.reset (); lateAnswer (true);
        std::cout << "PASS: raw-cycle workspace import, explicit stereo choice, stale-state guards, source/preset preservation, imported shaping/recipe, bank spread and strict generated recall\n";
    }

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
        check (! workspace.hasPendingFileOperation (), "Visual rendering alone is not a pending file operation");
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
        auto& designName { control<juce::TextEditor> (workspace, "design-name") };
        auto& namePreview { control<juce::Label> (workspace, "design-name-preview") };
        auto plannedFilename = [&] { return namePreview.getTooltip ().fromFirstOccurrenceOf (": ", false, false).upToFirstOccurrenceOf (".wav", true, false); };
        designName.setText ("Shared design", false); designName.onTextChange ();
        const auto approvedFilename { plannedFilename () };
        const auto approvedId { approvedFilename.dropLastCharacters (7).getLastCharacters (12) };
        const auto recipeBeforeContext { juce::JSON::toString (toJson (workspace.getSettings ())) };
        ++revision;
        workspace.refreshAssignmentContext ();
        check (target.getSelectedId () == 2 && juce::JSON::toString (toJson (workspace.getSettings ())) == recipeBeforeContext,
               "Refreshing an edited preset retains the chosen target and current waveform design");
        click (workspace, "Generate & Assign...");
        check (workspace.hasPendingFileOperation (), "An open assignment confirmation blocks competing file operations");
        check (prompts == 1 && promptText.contains ("channel 2, zone 1") && promptText.contains ("channel-wide") &&
               promptText.contains ("other zones") && promptText.contains ("CV range") && promptText.contains ("Save"),
               "Assignment confirms the exact destination, channel-wide impact, CV split and unsaved result");
        check (applications == 0 && folder.findChildFiles (juce::File::findFiles, false).isEmpty (), "Confirmation happens before generating files or changing the preset");
        answer (false); answer (true);
        check (! workspace.hasPendingFileOperation () && plannedFilename () == approvedFilename,
               "Cancel releases the file-operation guard and keeps the same reserved filename");
        check (applications == 0 && live.isEquivalentTo (original) && folder.findChildFiles (juce::File::findFiles, false).isEmpty (),
               "Canceled assignment is single-use and leaves files and existing unsaved edits untouched");
        click (workspace, "Generate & Assign...");
        ++revision;
        answer (true);
        check (applications == 0 && folder.findChildFiles (juce::File::findFiles, false).isEmpty () &&
               control<juce::Label> (workspace, "design-status").getText ().contains ("confirmation"),
               "A stale confirmation is rejected before starting generation");
        check (! workspace.hasPendingFileOperation () && plannedFilename () == approvedFilename,
               "A stale confirmation does not consume the filename identifier");
        click (workspace, "Generate & Assign...");
        designName.setText ("Renamed while confirming", false); designName.onTextChange ();
        answer (true);
        check (workspace.hasPendingFileOperation (), "The file-operation guard remains active during generation and before apply");
        designName.setText ("Renamed during generation", false); designName.onTextChange ();
        check (plannedFilename () == assignmentWaveName (designName.getText (), approvedId, 1),
               "Typing while generation is pending retains the current identifier without changing the captured request");
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
        check (! workspace.hasPendingFileOperation () && plannedFilename () != assignmentWaveName (designName.getText (), approvedId, 1),
               "A successful assignment releases the guard and reserves a new identifier for the next output");
        for (const auto& file : createdFiles) check (file.existsAsFile (), "Successful assignment retains every generated file");
        check (! folder.getChildFile ("prst007.yml").exists () && live.getProperty (PresetProperties::NamePropertyId) == original.getProperty (PresetProperties::NamePropertyId),
               "Assignment preserves an unsaved preset name and never writes its YAML automatically");
        PresetProperties assignedPreset (live, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        ChannelProperties assignedChannel (assignedPreset.getChannelVT (1), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        ZoneProperties assignedZone (assignedChannel.getZoneVT (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        check (assignedZone.getSample () == approvedFilename && folder.getChildFile (assignedZone.getSample ()).existsAsFile (),
               "Asynchronous assignment uses exactly the name and identifier previewed when confirmation opened");
        Settings savedDesign;
        check (fromJson (juce::JSON::parse (recipe.loadFileAsString ()), savedDesign).wasOk () && savedDesign.phaseDegrees == 79.0 &&
               juce::JSON::toString (toJson (savedDesign)) == recipeBeforeContext,
               "Saved assignment recipe represents the complete approved design snapshot");
        const auto successfulPreset { live.createCopy () };
        const auto successfulFiles { folder.findChildFiles (juce::File::findFiles, false).size () };
        const auto retryFilename { plannedFilename () };
        click (workspace, "Generate & Assign...");
        answer (true);
        ++revision; // Change after worker dispatch but before the UI commit.
        waitForAssignment ();
        check (applications == 1 && live.isEquivalentTo (successfulPreset) && folder.findChildFiles (juce::File::findFiles, false).size () == successfulFiles,
               "Post-render stale assignment cleans up only its new files and retains the live preset and prior generation");
        check (plannedFilename () == retryFilename && ! workspace.hasPendingFileOperation (),
               "Failed live apply retains the unused reserved filename for retry and releases the guard");

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

        // The reported case: CH 1/2 is a stereo pair, and the suggested CH 3
        // must receive the generated sample, without touching either partner.
        live = original.createCopy ();
        PresetProperties stereoPreset (live, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        ChannelProperties stereoLeft (stereoPreset.getChannelVT (0), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        ChannelProperties stereoRight (stereoPreset.getChannelVT (1), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        stereoRight.setChannelMode (ChannelProperties::stereoRight, false);
        for (auto channel : { stereoLeft.getValueTree (), stereoRight.getValueTree () })
        {
            ZoneProperties stereoZone (channel.getChild (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            stereoZone.setSample ("existing-stereo.wav", false);
        }
        const auto leftBefore { stereoLeft.getValueTree ().createCopy () }, rightBefore { stereoRight.getValueTree ().createCopy () };
        ++revision;
        control<juce::ComboBox> (workspace, "design-mode").setSelectedId (1, juce::sendNotificationSync);
        workspace.refreshAssignmentContext ();
        check (target.getSelectedId () == 3 && ! target.isItemEnabled (1) && ! target.isItemEnabled (2),
               "Stereo CH 1/2 suggests the independent empty CH 3");
        click (workspace, "Generate & Assign...");
        check (promptText.contains ("channel 3, zone 1"), "Stereo-neighbor assignment confirms the suggested channel");
        answer (true);
        waitForAssignment ();
        PresetProperties stereoAssigned (live, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        ChannelProperties thirdAssigned (stereoAssigned.getChannelVT (2), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        ZoneProperties thirdZone (thirdAssigned.getZoneVT (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        check (applications == 2 && thirdZone.getSample ().isNotEmpty () && folder.getChildFile (thirdZone.getSample ()).existsAsFile (),
               "Generate & Assign fills CH 3 beside an existing stereo pair");
        check (stereoAssigned.getChannelVT (0).isEquivalentTo (leftBefore) && stereoAssigned.getChannelVT (1).isEquivalentTo (rightBefore),
               "Assigning the suggested CH 3 preserves the complete stereo pair");
        check (! target.getText ().contains ("empty") && control<juce::Label> (workspace, "design-status").getText ().contains ("preset 7, CH 3, zone 1"),
               "Successful assignment visibly identifies its destination and no longer lists CH 3 as empty");
        check (control<juce::Label> (workspace, "design-assignment-heading").getText ().contains ("CURRENT PRESET") &&
               control<juce::Label> (workspace, "design-package-heading").getText ().contains ("SEPARATE PACKAGE") &&
               control<juce::Label> (workspace, "design-package-heading").getText ().contains ("new folder") &&
               control<juce::Button> (workspace, "design-export").getTooltip ().contains ("CH 1") &&
               control<juce::Button> (workspace, "design-export").getTooltip ().contains ("Target channel/zone fields are NOT used"),
               "Persistent headings and package tooltip distinguish assignment from separate export");
        workspace.setSize (975, 732); // Actual minimum space beside the shared preset sidebar/header.
        settle (workspace);
        for (const auto* name : { "design-assign", "design-recall", "design-export", "design-target-channel", "design-target-zone", "design-export-slot", "design-assignment-heading", "design-package-heading" })
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

    static void rangePauseWorkflow ()
    {
        // Exercise the actual transport together with the actual workspace:
        // range pause is engine state, not a UI flag that can restart audio
        // after a device change or another player's takeover.
        WaveformAudition engine;
        engine.prepareToPlay (48000.0);
        int starts { 0 };
        WaveformWorkspace workspace;
        workspace.setSize (975, 732);
        workspace.onAuditionPayload = [&] (auto payload) { engine.setPayload (std::move (payload)); };
        workspace.onStartAudition = [&] { ++starts; return engine.start (); };
        workspace.onStopAudition = [&] { engine.setPlaying (false); };
        workspace.onAuditionMonitorChange = [&] (double db, double semitones)
        {
            engine.setMonitorGain (juce::Decibels::decibelsToGain (db));
            return engine.setTransposeSemitones (semitones);
        };
        workspace.isAuditionActive = [&] { return engine.isActive (); };
        workspace.isAuditionPausedForRange = [&] { return engine.isPausedForRange (); };
        auto& transpose { control<juce::Slider> (workspace, "design-monitor-transpose-value") };
        auto& level { control<juce::Slider> (workspace, "design-monitor-level-value") };
        auto& frames { control<juce::ComboBox> (workspace, "design-frames") };
        auto& audition { control<juce::Button> (workspace, "design-audition") };
        auto& hint { control<juce::Label> (workspace, "design-audition-hint") };
        auto render = [&]
        {
            juce::AudioBuffer<float> output (2, 4096);
            output.clear ();
            engine.process ({ &output, 0, output.getNumSamples () });
            workspace.timerCallback ();
        };
        auto pause = [&]
        {
            transpose.setValue (-48, juce::sendNotificationSync);
            render ();
            check (engine.isPausedForRange () && ! engine.isActive () && audition.isEnabled ()
                   && audition.getButtonText () == "Stop audition" && hint.getText ().contains ("paused"),
                   "Inaudible transpose pauses real playback but keeps Stop available after the fade");
        };
        settle (workspace);
        check (transpose.getMinimum () == -48 && transpose.getMaximum () == 72,
               "The 48 kHz monitor exposes hardware pitch headroom without narrowing to its current audible range");
        click (workspace, "Start audition");
        control<juce::ComboBox> (workspace, "design-shape").setSelectedId (3, juce::sendNotificationSync);
        settle (workspace);
        check (engine.isActive () && starts == 1, "In-range shape changes preserve existing live audition behavior");
        render ();
        pause ();
        snapshot (workspace, "waveform-workspace-range-paused");
        check (hint.getTooltip ().contains ("20"), "Range explanation includes the engine's frequency limits");
        level.setValue (-25, juce::sendNotificationSync);
        check (engine.isPausedForRange (), "Level adjustment does not cancel or resume a range pause");
        transpose.setValue (0, juce::sendNotificationSync);
        check (engine.isActive () && ! engine.isPausedForRange () && starts == 1,
               "Returning to range resumes without a new UI Start request");
        render ();

        pause ();
        frames.setSelectedId (8192, juce::sendNotificationSync);
        settle (workspace);
        check (! hint.getTooltip ().contains ("At 0.01 st steps"), "Replacing a paused render discards the previous design's cached transpose limits");
        transpose.setValue (24, juce::sendNotificationSync);
        check (engine.isActive (), "Transpose resumes the replacement long cycle in its own valid range");
        frames.setSelectedId (512, juce::sendNotificationSync);
        settle (workspace);
        transpose.setValue (0, juce::sendNotificationSync);
        frames.setSelectedId (8192, juce::sendNotificationSync);
        settle (workspace);
        render ();
        check (engine.isPausedForRange () && hint.getText ().contains ("paused"),
               "A live cycle-length edit can pause audition and explains why");
        frames.setSelectedId (512, juce::sendNotificationSync);
        settle (workspace);
        check (engine.isPausedForRange () && ! engine.isActive (), "A new valid render alone does not resume paused audition");
        level.setValue (-26, juce::sendNotificationSync);
        check (engine.isPausedForRange () && ! engine.isActive (), "Level-only adjustment cannot resume a newly valid render");
        transpose.setValue (1, juce::sendNotificationSync);
        check (engine.isActive () && ! engine.isPausedForRange (), "A deliberate valid transpose gesture resumes the latest render");

        pause ();
        control<juce::Slider> (workspace, "design-phase-value").setValue (120, juce::sendNotificationSync);
        std::this_thread::sleep_for (std::chrono::milliseconds (160));
        workspace.timerCallback (); // Stop while a newer paused render is queued/in flight.
        click (workspace, "Stop audition");
        settle (workspace);
        transpose.setValue (0, juce::sendNotificationSync);
        check (! engine.isActive () && ! engine.isPausedForRange () && audition.getButtonText () == "Start audition",
               "Stop cancels range-resume intent even after the monitor is fully silent");
        transpose.setValue (-48, juce::sendNotificationSync);
        click (workspace, "Start audition");
        transpose.setValue (0, juce::sendNotificationSync);
        check (! engine.isActive () && ! engine.isPausedForRange (), "Failed out-of-range Start never arms a delayed automatic start");

        auto cancelWith = [&] (auto action)
        {
            click (workspace, "Start audition");
            render ();
            pause ();
            action ();
            settle (workspace);
            transpose.setValue (0, juce::sendNotificationSync);
            check (! engine.isPausedForRange () && ! engine.isActive () && ! hint.getText ().contains ("Audition paused"),
                   "Source, navigation and device stop actions cancel pause intent and clear the paused notice");
        };
        cancelWith ([&] { control<juce::ComboBox> (workspace, "design-shape").setSelectedId (2, juce::sendNotificationSync); });
        cancelWith ([&] { control<juce::ComboBox> (workspace, "design-preset").setSelectedId (3, juce::sendNotificationSync); });
        cancelWith ([&] { engine.prepareToPlay (44100.0); });
        cancelWith ([&]
        {
            workspace.updateAuditionVisibility (false);
            workspace.updateAuditionVisibility (true);
        });
        cancelWith ([&]
        {
            control<juce::ComboBox> (workspace, "design-mode").setSelectedId (2, juce::sendNotificationSync);
            control<juce::ComboBox> (workspace, "design-mode").setSelectedId (1, juce::sendNotificationSync);
        });
        control<juce::ComboBox> (workspace, "design-mode").setSelectedId (3, juce::sendNotificationSync);
        settle (workspace);
        cancelWith ([&] { click (workspace, "Supersaw (7 voices)"); });
        std::cout << "PASS: real waveform transpose range pause/resume, live cycle changes, retained Stop and lifecycle cancellation\n";
    }

    static void layerSpreadWorkflow ()
    {
        using namespace WaveformDesign;
        const auto near = [] (double a, double b) { return std::abs (a - b) < 1.0e-7; };
        WaveformAudition engine;
        engine.prepareToPlay (48000.0);
        int starts { 0 }, publications { 0 };
        WaveformAudition::PayloadPtr audible;
        WaveformWorkspace workspace;
        workspace.setSize (1160, 800);
        workspace.onAuditionPayload = [&] (auto payload)
        {
            audible = payload;
            if (payload) ++publications;
            engine.setPayload (std::move (payload));
        };
        workspace.onStartAudition = [&] { ++starts; return engine.start (); };
        workspace.onStopAudition = [&] { engine.setPlaying (false); };
        workspace.onAuditionMonitorChange = [&] (double db, double semitones)
        {
            engine.setMonitorGain (juce::Decibels::decibelsToGain (db));
            return engine.setTransposeSemitones (semitones);
        };
        workspace.isAuditionActive = [&] { return engine.isActive (); };
        workspace.isAuditionPausedForRange = [&] { return engine.isPausedForRange (); };
        auto& mode { control<juce::ComboBox> (workspace, "design-mode") };
        mode.setSelectedId (3, juce::sendNotificationSync);
        auto& voices { control<juce::Slider> (workspace, "design-voices-value") };
        auto& detune { control<juce::Slider> (workspace, "design-detune-spread-value") };
        auto& phase { control<juce::Slider> (workspace, "design-phase-spread-value") };
        auto& pan { control<juce::Slider> (workspace, "design-pan-spread-value") };
        auto& transpose { control<juce::Slider> (workspace, "design-monitor-transpose-value") };
        auto& spreadStatus { control<juce::Label> (workspace, "design-spread-status") };
        const auto editVoice = [&] (int index, int column, double value)
        {
            control<juce::Slider> (workspace, "design-voice-" + juce::String (index) + "-" + juce::String (column) + "-value")
                .setValue (value, juce::sendNotificationSync);
        };
        auto render = [&]
        {
            juce::AudioBuffer<float> output (2, 4096);
            output.clear ();
            engine.process ({ &output, 0, output.getNumSamples () });
            workspace.timerCallback ();
        };
        check (button (workspace, "Apply voice spread") == nullptr && phase.getMinimum () == -360 && phase.getMaximum () == 360,
               "Layer spreads are live controls with signed phase, without an Apply action");
        check (voices.getValue () == 7 && near (detune.getValue (), 24) && near (phase.getValue (), 300) && near (pan.getValue (), 0.8),
               "A fresh Layer Bank displays the actual seven-voice defaults");

        editVoice (1, 1, -125);
        editVoice (1, 3, 0.17);
        editVoice (3, 2, -0.41);
        editVoice (8, 0, 1900); editVoice (8, 1, -359); editVoice (8, 2, 0.97); editVoice (8, 3, 0.29);
        auto before { workspace.getSettings () };
        detune.setValue (48, juce::sendNotificationSync);
        for (size_t i { 0 }; i < before.voices.size (); ++i)
        {
            const auto& old { before.voices[i] };
            const auto current { workspace.getSettings ().voices[i] };
            check (near (current.detuneCents, i < 7 ? -48.0 + 16.0 * i : old.detuneCents)
                   && near (current.phaseDegrees, old.phaseDegrees) && near (current.pan, old.pan) && near (current.level, old.level),
                   "Live detune redistributes only active detunes, preserving other properties and inactive voices");
        }
        before = workspace.getSettings ();
        phase.setValue (-180, juce::sendNotificationSync);
        for (size_t i { 0 }; i < before.voices.size (); ++i)
        {
            const auto& old { before.voices[i] };
            const auto current { workspace.getSettings ().voices[i] };
            check (near (current.phaseDegrees, i < 7 ? -30.0 * i : old.phaseDegrees)
                   && near (current.detuneCents, old.detuneCents) && near (current.pan, old.pan) && near (current.level, old.level),
                   "Live signed phase spread preserves detune, pan, manual gains and inactive voices");
        }
        before = workspace.getSettings ();
        pan.setValue (0.5, juce::sendNotificationSync);
        for (size_t i { 0 }; i < before.voices.size (); ++i)
        {
            const auto& old { before.voices[i] };
            const auto current { workspace.getSettings ().voices[i] };
            check (near (current.pan, i < 7 ? -0.5 + static_cast<double> (i) / 6.0 : old.pan)
                   && near (current.detuneCents, old.detuneCents) && near (current.phaseDegrees, old.phaseDegrees) && near (current.level, old.level),
                   "Live pan spread preserves detune, phase, manual gains and inactive voices");
        }
        editVoice (4, 0, 155); editVoice (2, 1, -315); editVoice (7, 2, -0.91);
        check (near (detune.getValue (), 155) && near (phase.getValue (), -315) && near (pan.getValue (), 0.91)
               && spreadStatus.getText ().contains ("Custom"),
               "Irregular active voices show their actual extents and a Custom indication");
        auto retained { workspace.getSettings () };
        voices.setValue (3, juce::sendNotificationSync);
        retained.voiceCount = 3;
        check (juce::JSON::toString (toJson (workspace.getSettings ())) == juce::JSON::toString (toJson (retained))
               && near (detune.getValue (), 48) && near (phase.getValue (), -315) && near (pan.getValue (), 0.5),
               "Voice-count changes retain every voice and summarize only the active subset");
        voices.setValue (1, juce::sendNotificationSync);
        check (! detune.isEnabled () && ! phase.isEnabled () && ! pan.isEnabled (),
               "Single-voice banks disable spreads instead of suggesting a nonexistent distribution");
        voices.setValue (8, juce::sendNotificationSync);
        retained.voiceCount = 8;
        check (juce::JSON::toString (toJson (workspace.getSettings ())) == juce::JSON::toString (toJson (retained))
               && near (detune.getValue (), 1900) && near (phase.getValue (), -359) && near (pan.getValue (), 0.97),
               "Restoring an inactive voice restores its manual settings and includes it in spread summaries");
        const auto bankBeforeModeSwitch { juce::JSON::toString (toJson (workspace.getSettings ())) };
        mode.setSelectedId (1, juce::sendNotificationSync);
        mode.setSelectedId (3, juce::sendNotificationSync);
        check (juce::JSON::toString (toJson (workspace.getSettings ())) == bankBeforeModeSwitch
               && near (detune.getValue (), 1900) && near (phase.getValue (), -359) && near (pan.getValue (), 0.97)
               && spreadStatus.getText ().contains ("Custom"),
               "Returning to Layer Bank restores custom voices and truthful spread controls without redistributing them");
        control<juce::ComboBox> (workspace, "design-preset").setSelectedId (1, juce::sendNotificationSync);
        check (workspace.getSettings ().shape == Shape::sine && voices.getValue () == 7
               && near (detune.getValue (), 24) && near (phase.getValue (), 300) && near (pan.getValue (), 0.8),
               "A fresh waveform preset resets both voices and spread controls");
        detune.setValue (130, juce::sendNotificationSync);
        phase.setValue (-210, juce::sendNotificationSync);
        pan.setValue (0.37, juce::sendNotificationSync);
        voices.setValue (4, juce::sendNotificationSync);
        click (workspace, "Supersaw (7 voices)");
        auto expected { startingPoint (Mode::layers, Shape::saw) };
        spreadVoices (expected, 7, 24, 300, 0.8);
        check (juce::JSON::toString (toJson (workspace.getSettings ())) == juce::JSON::toString (toJson (expected))
               && voices.getValue () == 7 && near (detune.getValue (), 24) && near (phase.getValue (), 300) && near (pan.getValue (), 0.8)
               && ! spreadStatus.getText ().contains ("Custom"),
               "Supersaw restores its complete seven-voice recipe and every master control");

        settle (workspace);
        workspace.setSize (975, 732);
        auto& viewport { control<juce::Viewport> (workspace, "design-controls") };
        viewport.setViewPosition (0, spreadStatus.getParentComponent ()->getY ());
        snapshot (workspace, "waveform-workspace-spread-controls-compact");
        workspace.setSize (1160, 800);
        viewport.setViewPosition (0, 0);
        click (workspace, "Start audition");
        render ();
        const auto started { starts }, published { publications };
        for (int event { 0 }; event < 70; ++event)
        {
            phase.setValue (-90 + event * 3, juce::sendNotificationSync);
            workspace.timerCallback ();
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        }
        check (publications - published >= 3 && engine.isActive () && starts == started,
               "Rapid master-spread dragging publishes live audition snapshots before release without restarting");
        settle (workspace);
        render ();
        check (audible && near (audible->getSettings ().voices[6].phaseDegrees, 117) && engine.isActive () && starts == started,
               "The audible bank catches up to the last master-spread value");
        click (workspace, "Stop audition");
        render ();
        detune.setValue (30, juce::sendNotificationSync);
        settle (workspace); render ();
        check (! engine.isActive () && ! engine.isPausedForRange () && starts == started,
               "Editing a stopped bank's master controls does not start audition");
        click (workspace, "Start audition"); render ();
        transpose.setValue (-48, juce::sendNotificationSync); render ();
        check (engine.isPausedForRange () && ! engine.isActive (), "Fixture enters a genuine bank frequency pause");
        phase.setValue (-120, juce::sendNotificationSync);
        settle (workspace); render ();
        check (engine.isPausedForRange () && ! engine.isActive () && starts == started + 1,
               "Master-spread rendering does not resume an audition frequency pause");
        click (workspace, "Stop audition");
        control<juce::ComboBox> (workspace, "design-frames").setSelectedId (8192, juce::sendNotificationSync);
        transpose.setValue (transpose.getMaximum (), juce::sendNotificationSync);
        settle (workspace);
        click (workspace, "Start audition"); render ();
        check (engine.isActive (), "Long-cycle bank can audition at its current hardware transpose ceiling");
        detune.setValue (300, juce::sendNotificationSync);
        check (near (transpose.getMaximum (), 69) && near (transpose.getValue (), 69),
               "Live master detune reserves A8 pitch headroom and immediately clamps the displayed transpose");
        settle (workspace); render ();
        check (! engine.isActive () && ! engine.isPausedForRange () && starts == started + 2
               && control<juce::Label> (workspace, "design-audition-hint").getText ().contains ("Audition stopped"),
               "A master-detune ceiling clamp stops safely and never restarts on render completion");
        snapshot (workspace, "waveform-workspace-live-layer-spread");
        std::cout << "PASS: live layer spreads, truthful custom summaries, preset resets, retained voices and audition safety\n";
    }

    static void hardwareTransposeRangeWorkflow ()
    {
        WaveformAudition engine;
        engine.prepareToPlay (48000.0);
        int starts { 0 };
        double forwardedTranspose { 0 };
        WaveformWorkspace workspace;
        workspace.setSize (975, 732);
        workspace.onAuditionPayload = [&] (auto payload) { engine.setPayload (std::move (payload)); };
        workspace.onStartAudition = [&] { ++starts; return engine.start (); };
        workspace.onStopAudition = [&] { engine.setPlaying (false); };
        workspace.onAuditionMonitorChange = [&] (double db, double semitones)
        {
            forwardedTranspose = semitones;
            engine.setMonitorGain (juce::Decibels::decibelsToGain (db));
            return engine.setTransposeSemitones (semitones);
        };
        workspace.isAuditionActive = [&] { return engine.isActive (); };
        workspace.isAuditionPausedForRange = [&] { return engine.isPausedForRange (); };
        auto& transpose { control<juce::Slider> (workspace, "design-monitor-transpose-value") };
        auto& rate { control<juce::ComboBox> (workspace, "design-rate") };
        auto& frames { control<juce::ComboBox> (workspace, "design-frames") };
        auto& mode { control<juce::ComboBox> (workspace, "design-mode") };
        auto& audition { control<juce::Button> (workspace, "design-audition") };
        auto& hint { control<juce::Label> (workspace, "design-audition-hint") };
        auto render = [&]
        {
            juce::AudioBuffer<float> output (2, 4096);
            output.clear ();
            engine.process ({ &output, 0, output.getNumSamples () });
            workspace.timerCallback ();
        };
        auto stopped = [&]
        {
            settle (workspace);
            render ();
            check (! engine.isActive () && ! engine.isPausedForRange () && audition.getButtonText () == "Start audition",
                   "A hardware-range clamp stops audition and cancels automatic-resume intent");
        };
        frames.setSelectedId (8192, juce::sendNotificationSync);
        settle (workspace);
        check (transpose.getMinimum () == -48 && transpose.getMaximum () == 72 && transpose.getInterval () == 0.01,
               "48 kHz source exposes -48 through +72 semitones at hundredth-semitone precision");
        transpose.setValue (72, juce::sendNotificationSync);
        check (! engine.isActive (), "Selecting extended hardware headroom does not start idle audition");
        click (workspace, "Start audition");
        render ();
        check (engine.isActive () && starts == 1, "An 8192-frame 48 kHz cycle really auditions at +72 semitones");

        rate.setSelectedId (2, juce::sendNotificationSync);
        check (transpose.getMaximum () == 60 && transpose.getValue () == 60,
               "Changing to 96 kHz immediately clamps the displayed monitor transpose to +60");
        stopped ();
        check (hint.getText ().contains ("Transpose reduced to +60.00") && hint.getText ().contains ("Audition stopped"),
               "The clamp explanation survives the replacement render and explains that Start is required");
        snapshot (workspace, "waveform-workspace-transpose-clamped");
        rate.setSelectedId (1, juce::sendNotificationSync);
        stopped ();
        check (transpose.getMaximum () == 72 && transpose.getValue () == 60 && starts == 1,
               "Restoring the 48 kHz range does not restore the old high value or restart playback");
        transpose.setValue (72, juce::sendNotificationSync);
        check (! engine.isActive (), "A deliberate transpose edit after a range clamp still requires explicit Start");
        click (workspace, "Start audition");
        render ();
        transpose.setValue (36, juce::sendNotificationSync);
        rate.setSelectedId (2, juce::sendNotificationSync);
        settle (workspace);
        render ();
        check (transpose.getMaximum () == 60 && transpose.getValue () == 36 && engine.isActive () && starts == 2,
               "A rate edit retaining an already-valid transpose continues live without a fresh Start");

        rate.setSelectedId (1, juce::sendNotificationSync);
        transpose.setValue (72, juce::sendNotificationSync);
        check (transpose.getMaximum () == 72 && forwardedTranspose == 36 && engine.isActive () && ! engine.isPausedForRange ()
               && starts == 2 && ! hint.getText ().contains ("unavailable"),
               "Newly available headroom is not sent to the older restrictive payload while its replacement is pending");
        settle (workspace);
        render ();
        check (forwardedTranspose == 72 && engine.isActive () && ! engine.isPausedForRange () && starts == 2,
               "The newly rendered 48 kHz payload applies the pending high transpose without a false stop or restart");
        transpose.setValue (36, juce::sendNotificationSync);
        rate.setSelectedId (2, juce::sendNotificationSync);
        settle (workspace);
        render ();

        transpose.setValue (-48, juce::sendNotificationSync);
        render ();
        rate.setSelectedId (1, juce::sendNotificationSync);
        settle (workspace);
        render ();
        check (transpose.getMaximum () == 72 && engine.isPausedForRange () && ! engine.isActive (),
               "Increasing hardware headroom cannot consume a monitor-frequency pause");
        click (workspace, "Stop audition");

        mode.setSelectedId (3, juce::sendNotificationSync);
        rate.setSelectedId (2, juce::sendNotificationSync);
        frames.setSelectedId (8192, juce::sendNotificationSync);
        auto& voices { control<juce::Slider> (workspace, "design-voices-value") };
        auto& firstDetune { control<juce::Slider> (workspace, "design-voice-1-0-value") };
        auto& secondDetune { control<juce::Slider> (workspace, "design-voice-2-0-value") };
        voices.setValue (2, juce::sendNotificationSync);
        firstDetune.setValue (-700, juce::sendNotificationSync);
        secondDetune.setValue (350, juce::sendNotificationSync);
        settle (workspace);
        check (std::abs (transpose.getMaximum () - 56.5) < 1.0e-9,
               ("The highest positive active voice detune reserves bank headroom at 96 kHz: actual "
                + juce::String (transpose.getMaximum (), 12) + ", rate " + juce::String (workspace.getSettings ().sampleRate, 0)
                + ", detunes " + juce::String (workspace.getSettings ().voices[0].detuneCents, 12) + ", "
                + juce::String (workspace.getSettings ().voices[1].detuneCents, 12)).toRawUTF8 ());
        transpose.setValue (56.5, juce::sendNotificationSync);
        click (workspace, "Start audition");
        render ();
        check (engine.isActive (), "A detuned bank auditions at its hardware-aware common transpose ceiling");
        secondDetune.setValue (350.1, juce::sendNotificationSync);
        check (std::abs (transpose.getMaximum () - 56.49) < 1.0e-9 && std::abs (transpose.getValue () - 56.49) < 1.0e-9,
               "Fractional positive detune rounds the UI ceiling inward before clamping the current value");
        stopped ();
        secondDetune.setValue (-100, juce::sendNotificationSync);
        stopped ();
        check (transpose.getMaximum () == 60, "Negative-only bank detuning does not extend the nominal source-rate ceiling");
        voices.setValue (1, juce::sendNotificationSync);
        secondDetune.setValue (1200, juce::sendNotificationSync);
        check (transpose.getMaximum () == 60, "Inactive bank voices do not consume transpose headroom");
        voices.setValue (2, juce::sendNotificationSync);
        check (transpose.getMaximum () == 48 && transpose.getValue () == 48,
               "Enabling a higher-detuned voice immediately reserves its hardware headroom");
        rate.setSelectedId (1, juce::sendNotificationSync);
        check (transpose.getMaximum () == 60, "The same +12-semitone bank offset is deducted from the 48 kHz ceiling");
        secondDetune.setValue (-100, juce::sendNotificationSync);
        check (transpose.getMaximum () == 72, "Negative-only bank headroom remains capped at +72 for 48 kHz");

        rate.setSelectedId (2, juce::sendNotificationSync);
        secondDetune.setValue (350, juce::sendNotificationSync);
        mode.setSelectedId (1, juce::sendNotificationSync);
        settle (workspace);
        transpose.setValue (72, juce::sendNotificationSync);
        click (workspace, "Start audition");
        render ();
        mode.setSelectedId (3, juce::sendNotificationSync);
        check (std::abs (transpose.getMaximum () - 56.5) < 1.0e-9 && std::abs (transpose.getValue () - 56.5) < 1.0e-9,
               "Restoring a cached 96 kHz bank recomputes and clamps its own detune-aware range");
        stopped ();
        mode.setSelectedId (2, juce::sendNotificationSync);
        const auto startsBeforeCv { starts };
        audition.onClick (); // A queued action must retain the CV safety guard.
        check (! transpose.isEnabled () && ! audition.isEnabled () && starts == startsBeforeCv && ! engine.isActive (),
               "The extended range never enables CV monitoring or a stale CV audition request");
        mode.setSelectedId (1, juce::sendNotificationSync);
        stopped ();
        check (transpose.getMaximum () == 72, "Returning to the cached audio source restores its hardware ceiling without playback");

        frames.setSelectedId (64, juce::sendNotificationSync);
        rate.setSelectedId (2, juce::sendNotificationSync);
        settle (workspace);
        transpose.setValue (0, juce::sendNotificationSync);
        click (workspace, "Start audition");
        render ();
        const auto startsBeforePendingTranspose { starts };
        rate.setSelectedId (1, juce::sendNotificationSync);
        transpose.setValue (48, juce::sendNotificationSync);
        check (std::abs (forwardedTranspose) < 1.0e-9 && engine.isActive () && ! engine.isPausedForRange (),
               ("A pending 48 kHz transpose gesture cannot apply an ultrasonic 24 kHz pitch to the old 96 kHz short cycle: forwarded "
                + juce::String (forwardedTranspose, 16) + ", active " + juce::String (static_cast<int> (engine.isActive ()))
                + ", paused " + juce::String (static_cast<int> (engine.isPausedForRange ()))).toRawUTF8 ());
        settle (workspace);
        render ();
        check (workspace.getSettings ().sampleRate == 48000 && forwardedTranspose == 48
               && engine.isActive () && ! engine.isPausedForRange () && starts == startsBeforePendingTranspose,
               "Publishing the 48 kHz short cycle applies its valid 12 kHz gesture without a false pause or fresh Start");
        rate.setSelectedId (2, juce::sendNotificationSync);
        transpose.setValue (0, juce::sendNotificationSync);
        check (forwardedTranspose == 48 && engine.isActive () && ! engine.isPausedForRange (),
               "The reverse pending source change retains the old safe pitch until its requested transpose can be applied coherently");
        settle (workspace);
        render ();
        check (workspace.getSettings ().sampleRate == 96000 && std::abs (forwardedTranspose) < 1.0e-9
               && engine.isActive () && ! engine.isPausedForRange () && starts == startsBeforePendingTranspose,
               "A deliberate pending transpose gesture avoids a transient range pause when the new source invalidates the previous pitch");
        rate.setSelectedId (1, juce::sendNotificationSync);
        transpose.setValue (48, juce::sendNotificationSync);
        click (workspace, "Stop audition");
        stopped ();
        check (starts == startsBeforePendingTranspose && transpose.getValue () == 48,
               "Explicit Stop cancels a deferred transpose gesture even when its matching render arrives later");
        transpose.setValue (0, juce::sendNotificationSync);
        check (! engine.isActive () && ! engine.isPausedForRange (),
               "Later monitor edits cannot revive the explicitly stopped pending gesture");
        click (workspace, "Start audition");
        render ();
        transpose.setValue (72, juce::sendNotificationSync);
        render ();
        check (transpose.getMaximum () == 72 && engine.isPausedForRange () && ! engine.isActive () && audition.getButtonText () == "Stop audition",
               "The independent monitor-frequency guard still pauses an ultrasonic short cycle within hardware headroom");
        transpose.setValue (0, juce::sendNotificationSync);
        check (engine.isActive () && ! engine.isPausedForRange (), "A safe transpose gesture can resume that frequency pause");
        std::cout << "PASS: hardware-aware UI transpose ranges, sample-rate and bank headroom, safe clamps, cached modes and independent monitor guard\n";
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
            int starts { 0 }, stops { 0 }, publications { 0 };
            double monitorDb { 999 }, transpose { 999 };
        } host;
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
                if (host.failStart) return juce::Result::fail ("No output device selected - use the top-right Audio Settings.");
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
            workspace.timerCallback (); // Samples navigation interrupts a submitted, newer edit.
            workspace.updateAuditionVisibility (false);
            check (! host.active && ! host.payload && ! popup->isVisible (),
                   "Leaving the designer stops/clears the host and closes the visual popup");
            settle (workspace);
            workspace.updateAuditionVisibility (true);
            check (! host.active && host.payload, "Returning after an in-flight edit prepares audition without automatically starting it");
            click (workspace, "Start audition");
            check (host.active && host.payload && host.payload->getSettings ().phaseDegrees == 223,
                   "Returning after an in-flight edit restores the current design and explicit Start readiness without another edit");
            workspace.updateAuditionVisibility (false);
            workspace.timerCallback ();
            workspace.updateAuditionVisibility (true);
            check (! host.active && host.payload, "Returning to a cached design still requires an explicit audition start");
            click (workspace, "Start audition");
            check (host.active && host.payload->getSettings ().phaseDegrees == 223,
                   "Leaving and returning with an up-to-date cache retains immediate explicit Start readiness");
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
        int matchCalls { 0 };
        workspace.onMatchDuration = [&] (int selection) -> std::optional<double>
        {
            ++matchCalls;
            return selection == 1 ? std::optional<double> (3.25) : std::nullopt;
        };
        settle (workspace);
        check (workspace.getSettings ().mode == Mode::oscillator && validate (workspace.getSettings ()).wasOk (), "Audio-cycle workspace starts with a valid design");
        check (control<juce::Label> (workspace, "design-summary").getText ().contains ("Base"), "Actual background render publishes duration/base note");
        auto& harmonics { control<juce::Slider> (workspace, "design-harmonics-value") };
        const auto beforeHarmonics { juce::JSON::toString (toJson (workspace.getSettings ())) };
        const auto originalHarmonics { harmonics.getValue () };
        check (harmonics.getMinimum () == 1 && harmonics.getMaximum () == 1024 && harmonics.getInterval () == 1,
               "Nonlinear harmonics gesture preserves the full integer recipe range");
        struct HarmonicAnchor { double value, position; };
        const HarmonicAnchor anchors[] {
            { 1, 0.0 }, { 2, 0.011908994948754494 }, { 3, 0.023288522850766418 },
            { 4, 0.034183666074970635 }, { 5, 0.04463398342557704 },
            { 20, 0.1649536086491215 }, { 50, 0.3082136978991229 },
            { 100, 0.44619530682634206 }, { 200, 0.6013643910250979 },
            { 300, 0.6972848746240374 }, { 512, 0.8274140228064236 }, { 1024, 1.0 }
        };
        for (const auto& anchor : anchors)
        {
            check (std::abs (harmonics.valueToProportionOfLength (anchor.value) - anchor.position) < 1.0e-10
                   && std::abs (harmonics.proportionOfLengthToValue (anchor.position) - anchor.value) < 1.0e-10,
                   "The whole harmonic range follows the offset-logarithmic curve, including its exact endpoints");
        }
        auto previous { 0.0 };
        for (int step { 0 }; step <= 1000; ++step)
        {
            const auto position { step / 1000.0 };
            const auto value { harmonics.proportionOfLengthToValue (position) };
            check (std::isfinite (value) && value > previous && value >= 1 && value <= 1024
                   && std::abs (harmonics.valueToProportionOfLength (value) - position) < 1.0e-10,
                   "Harmonic mapping is bounded, continuous, monotonic and invertible over its whole travel");
            previous = value;
        }
        const auto lowerTravel { harmonics.valueToProportionOfLength (200) };
        check (harmonics.valueToProportionOfLength (5) > 0.04 && harmonics.valueToProportionOfLength (5) < 0.05,
               "Harmonics one through five use less than five percent of travel, without a separate coarse segment");
        check (lowerTravel > 0.60 && lowerTravel < 0.61 && 1.0 - lowerTravel > 0.39 && 1.0 - lowerTravel < 0.40,
               "Harmonics one through 200 receive about sixty percent of travel while the upper range keeps about forty percent");
        for (const auto value : { 1, 2, 3, 4, 5, 20, 100, 200, 300, 511, 1024 })
        {
            harmonics.setValue (value, juce::sendNotificationSync);
            check (workspace.getSettings ().harmonics == value && harmonics.getValue () == value,
                   "Exact harmonic values still update the generation model without reinterpretation or clamping");
        }
        for (const auto value : { 1.2, 4.6, 99.8, 199.6, 511.2, 1023.8 })
        {
            harmonics.setValue (value, juce::sendNotificationSync);
            const auto expected { std::round (value) };
            check (harmonics.getValue () == expected && workspace.getSettings ().harmonics == static_cast<int> (expected),
                   "Fractional harmonic gestures still snap to integer values across the logarithmic range");
        }
        harmonics.setValue (originalHarmonics, juce::sendNotificationSync);
        check (juce::JSON::toString (toJson (workspace.getSettings ())) == beforeHarmonics,
               "Slider mapping alone does not alter any saved design parameters");
        control<juce::Slider> (workspace, "design-amplitude-value").setValue (65, juce::sendNotificationSync);
        control<juce::Slider> (workspace, "design-offset-value").setValue (-10, juce::sendNotificationSync);
        control<juce::Slider> (workspace, "design-phase-value").setValue (45, juce::sendNotificationSync);
        check (std::abs (workspace.getSettings ().amplitude - 0.65) < 1e-9 && std::abs (workspace.getSettings ().offset + 0.1) < 1e-9
               && workspace.getSettings ().phaseDegrees == 45, "Actual sliders update the generation model");
        check (! find (workspace, "design-width")->isVisible (), "Sine hides irrelevant pulse width");
        settle (workspace); snapshot (workspace, "waveform-workspace-audio");
        const auto beforeAppearance { juce::JSON::toString (toJson (workspace.getSettings ())) };
        Theme::setAppearance (true);
        Theme::refreshComponentTree (workspace);
        snapshot (workspace, "waveform-workspace-audio-light");
        Theme::setAppearance (false);
        Theme::refreshComponentTree (workspace);
        check (juce::JSON::toString (toJson (workspace.getSettings ())) == beforeAppearance,
               "Changing designer appearance does not alter its waveform settings");

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
        click (workspace, "Export new package...");
        check (control<juce::Label> (workspace, "design-status").getText ().contains ("usable design name"), "Invalid export name is rejected before opening a chooser or writing files");
        check (! find (workspace, "design-open-export")->isVisible (), "Open exported folder stays hidden until a successful explicit export");
        workspace.updateAuditionVisibility (false);
        workspace.updateAuditionVisibility (true);
        check (juce::JSON::toString (toJson (workspace.getSettings ())) == retainedLayers,
               "Leaving and returning through shared navigation does not change the design");
        std::cout << "PASS: actual waveform workspace controls, background visual preview, modes, duration matching, envelope/steps/drawing, layers, calibration and safe export entry\n";
    }
};

void testWaveformWorkspace ()
{
    WaveformWorkspaceTestAccess::namePreviewWorkflow ();
    WaveformWorkspaceTestAccess::testOutputEntry ();
    WaveformWorkspaceTestAccess::presetSaveStatus ();
    WaveformWorkspaceTestAccess::run ();
    WaveformWorkspaceTestAccess::auditionAndExpandedPreview ();
    WaveformWorkspaceTestAccess::rangePauseWorkflow ();
    WaveformWorkspaceTestAccess::layerSpreadWorkflow ();
    WaveformWorkspaceTestAccess::layerConversionWorkflow ();
    WaveformWorkspaceTestAccess::hardwareTransposeRangeWorkflow ();
    WaveformWorkspaceTestAccess::assignmentWorkflow ();
    WaveformWorkspaceTestAccess::recallWorkflow ();
    WaveformWorkspaceTestAccess::rawImportWorkflow ();
    WaveformWorkspaceTestAccess::signalWarningWorkflow ();
}
