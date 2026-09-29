#include "GUI/HardwareTestOutputComponent.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace
{
    void check (bool condition, const char* message)
    {
        if (! condition) throw std::runtime_error (message);
    }

    void snapshot (juce::Component& component, const juce::String& name)
    {
        const auto path { juce::SystemStats::getEnvironmentVariable ("A8MANAGER_TEST_ARTIFACTS", {}) };
        if (path.isEmpty ()) return;
        const juce::File directory { path };
        check (directory.createDirectory ().wasOk (), "Create hardware-test UI artifact directory");
        auto output { directory.getChildFile (name + ".png").createOutputStream () };
        check (output != nullptr && output->setPosition (0), "Open hardware-test UI artifact");
        check (juce::PNGImageFormat ().writeImageToStream (component.createComponentSnapshot (component.getLocalBounds ()), *output),
               "Render hardware-test UI artifact");
        check (output->truncate ().wasOk (), "Truncate previous hardware-test UI artifact");
    }
}

struct HardwareTestOutputUiTestAccess
{
    static void finish (HardwareTestOutputComponent& component)
    {
        for (auto attempt { 0 }; attempt < 500 && component.activity == HardwareTestOutputComponent::Activity::exporting; ++attempt)
        {
            component.timerCallback ();
            if (component.activity == HardwareTestOutputComponent::Activity::exporting)
                std::this_thread::sleep_for (std::chrono::milliseconds (5));
        }
        check (component.activity == HardwareTestOutputComponent::Activity::idle && ! component.worker.joinable (),
               "Finished export is joined and controls return to idle");
    }

    static void acknowledge (HardwareTestOutputComponent& component)
    {
        component.safetyAcknowledged.setToggleState (true, juce::dontSendNotification);
        component.safetyAcknowledged.onClick ();
    }

    static void run ()
    {
        using namespace HardwareTestOutput;
        const auto initialLight { Theme::isLight () };
        struct Restore { bool light; ~Restore () { Theme::setAppearance (light); } } restore { initialLight };
        const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory) };
        auto design { WaveformDesign::startingPoint (WaveformDesign::Mode::oscillator, WaveformDesign::Shape::saw) };
        design.amplitude = 0.8;
        design.offset = 0.1;
        design.playback = WaveformDesign::Playback::loop;
        const auto original { juce::JSON::toString (WaveformDesign::toJson (design)) };
        HardwareTestOutputComponent component (design, folder, "Bench", 37);
        check (component.getWidth () == 840 && component.getHeight () == 720, "Hardware-test dialog leaves room for larger readable text");
        for (auto* editor : { &component.summary, &component.safety, &component.status, &component.outputName })
            check (editor->getFont ().getHeight () >= 16.0f, "Hardware-test body text uses a readable minimum font size");
        check (component.source.getSelectedId () == 1 && ! component.level.isEnabled () && ! component.safetyAcknowledged.getToggleState ()
               && ! component.exportButton.isEnabled (), "Current-design default never enables export before an explicit safety acknowledgment");
        check (component.preset.getValue () == 37 && component.duration.getValue () == 10 && component.level.getValue () == 10
               && component.outputName.getText () == "Bench test", "Defaults retain the requested slot with a visibly distinct package name");
        check (component.summary.getText ().contains ("full scale") && component.summary.getText ().contains ("25% cap does NOT apply")
               && component.summary.getText ().contains ("DC offsets") && component.summary.getText ().contains ("do NOT stop"),
               "Current-design summary discloses full-scale/DC risk and continuous playback beyond the END reference");
        check (component.summary.getText ().contains ("CH 2") && component.safety.getText ().contains ("not volts")
               && component.safety.getText ().contains ("Auto Trigger is OFF") && component.safety.getText ().contains ("both stereo mix outputs")
               && component.safety.getText ().contains ("On Assimil8or, disconnect"),
               "Reference routing and electrical/mix safety explicitly identify Assimil8or outputs");
        for (auto* child : component.getChildren ())
        {
            check (component.getLocalBounds ().contains (child->getBounds ()), "Dialog controls stay inside its fixed size");
            if (auto* button { dynamic_cast<juce::Button*> (child) })
                check (! button->getButtonText ().containsIgnoreCase ("play") && ! button->getButtonText ().containsIgnoreCase ("audition"),
                       "Export-only hardware-test UI exposes no play or audition action");
        }
        snapshot (component, "hardware-test-output-current-design");
        for (auto* editor : { &component.summary, &component.safety })
        {
            // getTextHeight() includes the top indent and is clamped to the
            // viewport height even for empty text; do not subtract padding twice.
            const auto viewportHeight { editor->getHeight () - editor->getBorder ().getTop () - editor->getBorder ().getBottom () };
            const auto glyphs { editor->getTextBounds ({ 0, editor->getTotalNumChars () }).getBounds () };
            if (editor->getTextHeight () > viewportHeight || ! editor->getLocalBounds ().reduced (2).contains (glyphs))
                throw std::runtime_error ((editor->getName () + " must fit without scrolling: text " + juce::String (editor->getTextHeight ()) +
                    "/" + juce::String (viewportHeight) + ", glyphs " + glyphs.toString ()).toStdString ());
        }
        design.amplitude = 0.02;
        design.offset = 0;
        check (juce::JSON::toString (WaveformDesign::toJson (component.settingsFromControls ().design)) == original,
               "The dialog retains an immutable design snapshot even if its caller changes settings");

        auto chooserCalls { 0 };
        std::function<void (juce::File)> selectedFolder;
        component.chooseFolder = [&] (const juce::File& initial, std::function<void (juce::File)> callback)
        {
            check (initial == folder, "Folder chooser begins at the supplied folder");
            ++chooserCalls;
            selectedFolder = std::move (callback);
        };
        component.beginExport ();
        check (chooserCalls == 0, "Calling an unchecked export action cannot bypass the safety acknowledgment");
        acknowledge (component);
        check (component.exportButton.isEnabled (), "A valid current design can export after acknowledgment");
        component.outputName.setText ("../unsafe", false);
        component.refreshControls (true);
        component.beginExport ();
        check (! component.exportButton.isEnabled () && chooserCalls == 0, "Invalid package names cannot reach folder selection");
        component.outputName.setText ("Bench test", false);
        component.source.setSelectedId (3, juce::sendNotificationSync);
        check (component.level.isEnabled () && ! component.safetyAcknowledged.getToggleState () && ! component.exportButton.isEnabled (),
               "Switching signal type resets the safety acknowledgment and enables built-in level control");
        check (component.duration.getMinimum () == 1 && component.duration.getMaximum () == 60
               && component.level.getMinimum () == 1 && component.level.getMaximum () == 25
               && component.preset.getMinimum () == 1 && component.preset.getMaximum () == 199,
               "Duration, built-in full-scale fraction and preset slots have the agreed bounds");
        check (component.summary.getText ().contains ("CV test") && component.summary.getText ().contains ("One Shot"),
               "Built-in CV summary identifies one-shot playback and signal type");

        for (const auto light : { false, true })
        {
            Theme::setAppearance (light);
            Theme::refreshComponentTree (component);
            for (auto* slider : { &component.duration, &component.level, &component.preset })
            {
                auto hasNumericField { false };
                for (auto* child : slider->getChildren ())
                    if (auto* label { dynamic_cast<juce::Label*> (child) })
                    {
                        hasNumericField = true;
                        check (label->getFont ().getHeight () >= 16.0f, "Numeric fields retain larger fonts after appearance changes");
                    }
                check (hasNumericField, "Each test setting has a visible numeric field");
            }
            snapshot (component, light ? "hardware-test-output-light" : "hardware-test-output-dark");
        }

        Settings observed;
        juce::String observedName;
        juce::File observedParent;
        auto exportCalls { 0 };
        component.writePackage = [&] (const Settings& settings, const juce::File& parent, const juce::String& name, ExportResult& result)
        {
            observed = settings;
            observedName = name;
            observedParent = parent;
            ++exportCalls;
            result.folder = parent.getChildFile (name);
            return juce::Result::ok ();
        };
        acknowledge (component);
        component.beginExport ();
        check (chooserCalls == 1 && component.activity == HardwareTestOutputComponent::Activity::choosingFolder
               && ! component.source.isEnabled () && ! component.duration.isEnabled () && ! component.level.isEnabled ()
               && ! component.preset.isEnabled () && ! component.outputName.isEnabled () && ! component.safetyAcknowledged.isEnabled (),
               "All request fields are locked from folder-chooser opening until export completion");
        component.beginExport ();
        check (chooserCalls == 1, "Repeated export attempts cannot open overlapping folder choosers");
        // Programmatic changes emulate an external state change after the native
        // chooser opened; the export must use the already-approved snapshot.
        component.duration.setValue (22, juce::dontSendNotification);
        component.level.setValue (19, juce::dontSendNotification);
        component.preset.setValue (88, juce::dontSendNotification);
        component.outputName.setText ("Changed after chooser", false);
        selectedFolder (folder);
        check (component.activity == HardwareTestOutputComponent::Activity::exporting && ! component.closeButton.isEnabled (),
               "The worker owns the detached request while UI close/edit controls remain disabled");
        selectedFolder (folder); // An asynchronous callback is single-use.
        finish (component);
        check (exportCalls == 1 && observed.signal == Signal::cvLevels && observed.durationSeconds == 10
               && std::abs (observed.level - 0.1) < 1.0e-9 && observed.presetNumber == 37
               && observedName == "Bench test" && observedParent == folder
               && juce::JSON::toString (WaveformDesign::toJson (observed.design)) == original,
               "Worker export uses only the settings, design, name and slot snapshotted before folder selection");
        check (component.status.getText ().contains (folder.getChildFile ("Bench test").getFullPathName ())
               && component.status.isReadOnly () && component.status.getText ().contains ("unchanged") && component.closeButton.isEnabled (),
               "Successful export leaves a selectable output path and does not claim to assign or audition it");

        const auto completedChoice { selectedFolder };
        component.beginExport ();
        completedChoice (folder);
        check (component.activity == HardwareTestOutputComponent::Activity::choosingFolder && exportCalls == 1,
               "A stale chooser callback cannot export a previous request while a newer chooser is open");
        selectedFolder ({});
        check (component.activity == HardwareTestOutputComponent::Activity::idle && exportCalls == 1
               && component.status.getText ().contains ("canceled"), "Canceling folder selection writes nothing and restores controls");

        component.writePackage = [] (const Settings&, const juce::File&, const juce::String&, ExportResult&) -> juce::Result
        { throw std::runtime_error ("Synthetic export failure"); };
        component.beginExport ();
        selectedFolder (folder);
        finish (component);
        check (component.statusIsError && component.status.getText ().contains ("Synthetic export failure"),
               "Worker exceptions are reported on the UI thread and remain visible after controls unlock");

        component.chooseFolder = [] (const juce::File&, std::function<void (juce::File)>) { throw std::runtime_error ("Synthetic chooser failure"); };
        component.beginExport ();
        check (component.activity == HardwareTestOutputComponent::Activity::idle && component.status.getText ().contains ("Synthetic chooser failure"),
               "Folder-chooser failures restore the dialog instead of leaving it busy");

        auto bank { WaveformDesign::startingPoint (WaveformDesign::Mode::layers, WaveformDesign::Shape::saw) };
        bank.voiceCount = 8;
        HardwareTestOutputComponent eight (bank, folder, "Eight voices", 1);
        acknowledge (eight);
        check (! eight.exportButton.isEnabled () && eight.validateControls ().failed () && eight.summary.getText ().contains ("no channel available"),
               "An eight-voice current design visibly rejects export because no sync-reference channel remains");
        eight.source.setSelectedId (2, juce::sendNotificationSync);
        acknowledge (eight);
        check (eight.exportButton.isEnabled () && eight.validateControls ().wasOk (), "Built-in tests remain available when the captured bank uses all eight channels");

        std::function<void (juce::File)> abandonedChoice;
        {
            auto closing { std::make_unique<HardwareTestOutputComponent> (design, folder, "Closing", 1) };
            closing->chooseFolder = [&] (const juce::File&, std::function<void (juce::File)> callback) { abandonedChoice = std::move (callback); };
            acknowledge (*closing);
            closing->beginExport ();
        }
        abandonedChoice (folder); // SafePointer must discard a closed chooser target.

        std::atomic<bool> workerFinished { false };
        {
            HardwareTestOutputComponent closing (design, folder, "Close during worker", 1);
            closing.chooseFolder = [&] (const juce::File&, std::function<void (juce::File)> callback) { callback (folder); };
            closing.writePackage = [&] (const Settings&, const juce::File&, const juce::String&, ExportResult&)
            {
                std::this_thread::sleep_for (std::chrono::milliseconds (20));
                workerFinished.store (true);
                return juce::Result::fail ("Synthetic shutdown test");
            };
            acknowledge (closing);
            closing.beginExport ();
        }
        check (workerFinished.load (), "Destroying the dialog joins its detached worker without touching destroyed controls");
    }
};

void testHardwareTestOutputUi ()
{
    HardwareTestOutputUiTestAccess::run ();
    std::cout << "PASS: export-only hardware-test dialog, explicit safety, frozen requests, separate reference routing and worker lifetime\n";
}
