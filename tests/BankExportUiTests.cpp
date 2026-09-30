#include "GUI/BankExportComponent.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace
{
    void check (bool condition, const char* message) { if (! condition) throw std::runtime_error (message); }
    void snapshot (juce::Component& component, const juce::String& name)
    {
        const auto path { juce::SystemStats::getEnvironmentVariable ("A8MANAGER_TEST_ARTIFACTS", {}) };
        if (path.isEmpty ()) return;
        const juce::File folder { path };
        check (folder.createDirectory ().wasOk (), "Create bank UI artifact folder");
        auto stream { folder.getChildFile (name + ".png").createOutputStream () };
        check (stream && stream->setPosition (0)
               && juce::PNGImageFormat ().writeImageToStream (component.createComponentSnapshot (component.getLocalBounds ()), *stream)
               && stream->truncate ().wasOk (), "Render actual bank export dialog");
    }
}

struct BankExportUiTestAccess
{
    static void finish (BankExportComponent& component)
    {
        for (int attempt { 0 }; attempt < 1000 && component.job; ++attempt)
        {
            component.timerCallback ();
            if (component.job) std::this_thread::sleep_for (std::chrono::milliseconds (2));
        }
        check (! component.job && ! component.worker.joinable () && component.activity == BankExportComponent::Activity::idle,
               "Detached bank work finishes and is joined before returning the UI to idle");
    }

    static void run ()
    {
        const auto previousLight { Theme::isLight () };
        struct Restore { bool light; ~Restore () { Theme::setAppearance (light); } } restore { previousLight };
        const auto root { juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("a8-bank-ui-" + juce::Uuid ().toString ()) };
        check (root.createDirectory ().wasOk (), "Create owned bank UI fixture");
        struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { root };
        const auto first { root.getChildFile ("P01 - First" ) }, second { root.getChildFile ("P02 - Other" ) };
        check (first.createDirectory ().wasOk () && second.createDirectory ().wasOk (), "Create explicit source folders");
        const auto firstPreset { first.getChildFile ("prst001.yml") }, secondPreset { second.getChildFile ("prst001.yml") };
        const juce::String firstText { "Preset 1:\n  Name: First saved\n" }, secondText { "Preset 1:\n  Name: Second saved\n" };
        check (firstPreset.replaceWithText (firstText) && secondPreset.replaceWithText (secondText), "Create saved source presets");
        juce::MemoryBlock firstOriginal, secondOriginal;
        check (firstPreset.loadFileAsData (firstOriginal) && secondPreset.loadFileAsData (secondOriginal),
               "Capture the exact saved source bytes, including JUCE's on-disk line endings");
        const auto nested { first.getChildFile ("not-selected") };
        check (nested.createDirectory ().wasOk () && nested.getChildFile ("prst002.yml").replaceWithText ("Preset 2:\n  Name: Do not discover\n"),
               "Create a nested preset that must not be discovered");

        juce::File opened;
        auto openCalls { 0 };
        BankExportComponent component (first, [&] (juce::File folder) { opened = folder; ++openCalls; });
        finish (component);
        check (component.rows.size () == 1 && component.folders.size () == 1
               && component.rows[0].source.presetFile == firstPreset && component.rows[0].selected,
               "Initial discovery includes immediate saved presets only, selected by default");
        check (! component.openAfterExport.getToggleState () && component.exportButton.isEnabled ()
               && component.explanation.getText ().contains ("always kept") && component.explanation.getText ().contains ("never overwritten"),
               "Copy-only behavior is explicit and opening the bank is off by default");
        component.addFolder (first);
        check (! component.job && component.folders.size () == 1 && component.status.getText ().contains ("already listed"),
               "Adding the same source does not create duplicate entries or a new scan");

        std::function<void (juce::File)> chooseResult;
        auto chooserCalls { 0 };
        component.chooseFolder = [&] (const juce::String& prompt, const juce::File&, std::function<void (juce::File)> done)
        {
            check (prompt.contains ("folder"), "Folder chooser explains what is being selected");
            ++chooserCalls;
            chooseResult = std::move (done);
        };
        component.chooseSourceFolder ();
        check (chooserCalls == 1 && ! component.exportButton.isEnabled (), "An open source chooser freezes export controls");
        chooseResult ({});
        check (component.activity == BankExportComponent::Activity::idle && component.rows.size () == 1,
               "Cancelling Add folder leaves the source list intact");
        component.chooseSourceFolder ();
        chooseResult (second);
        finish (component);
        check (component.rows.size () == 2 && component.folders.size () == 2 && ! component.exportButton.isEnabled ()
               && component.status.getText ().contains ("P001") && component.status.getText ().contains ("more than once"),
               "Duplicate slots across source folders require an explicit user decision");
        auto writes { 0 };
        const auto productionWrite { component.writeBank };
        component.writeBank = [&] (const std::vector<PresetBankExport::Entry>&, const juce::File&,
                                  PresetBankExport::Report&, PresetBankExport::Cancel, PresetBankExport::Progress)
        { ++writes; return juce::Result::fail ("should not write"); };
        component.beginExport ();
        check (writes == 0 && ! component.job, "Duplicate-slot validation cannot be bypassed by calling Export directly");
        snapshot (component, "bank-export-slot-conflict");

        auto* slotEditor { dynamic_cast<juce::TextEditor*> (component.table.getCellComponent (5, 1)) };
        check (slotEditor != nullptr, "Destination slots use actual editable table cells");
        slotEditor->setText ("2", false);
        slotEditor->onTextChange ();
        check (component.rows[1].targetSlot == "2" && component.exportButton.isEnabled (),
               "Editing a destination slot resolves a conflict without changing either source slot");
        check (component.rows[0].source.slot == 1 && component.rows[1].source.slot == 1, "Source slot identifiers are never reassigned");
        component.rows[1].targetSlot = "4294967298";
        component.refreshControls ();
        check (! component.exportButton.isEnabled (), "Oversized slot strings cannot wrap into a valid hardware slot");
        component.rows[1].targetSlot = "2";
        component.refreshControls ();
        component.selectNoneButton.onClick ();
        check (component.selectedEntries ().empty () && ! component.exportButton.isEnabled (), "An empty selection disables export");
        component.selectAllButton.onClick ();
        check (component.selectedEntries ().size () == 2 && component.exportButton.isEnabled (), "Select all preserves explicit target slots");
        component.rescanButton.onClick ();
        finish (component);
        check (component.rows[1].targetSlot == "2" && component.rows.size () == 2, "Rescan preserves destination edits without recursive discovery");

        component.chooseDestinationParent ();
        chooseResult (root);
        check (component.parentFolder == root && component.destinationParent.getText () == root.getFullPathName (),
               "Destination parent is chosen independently of all source folders");
        for (const auto& name : { juce::String ("../unsafe"), juce::String::repeatedString ("x", 32), juce::String ("P01 - First") })
        {
            component.bankName.setText (name, false);
            component.refreshControls ();
            check (! component.exportButton.isEnabled (), "Unsafe, overlong and already-existing destination folders are refused");
        }
        component.bankName.setText ("Performance bank", false);
        component.refreshControls ();
        for (const auto light : { false, true })
        {
            Theme::setAppearance (light);
            Theme::refreshComponentTree (component);
            component.setSize (960, 720);
            for (auto* child : component.getChildren ())
                check (component.getLocalBounds ().contains (child->getBounds ()), "Normal bank dialog controls stay inside the content area");
            snapshot (component, light ? "bank-export-light" : "bank-export-dark");
            component.setSize (740, 640);
            for (auto* child : component.getChildren ())
                check (component.getLocalBounds ().contains (child->getBounds ()), "Compact bank dialog retains all controls");
            check (component.table.getHeight () >= 160 && component.status.getHeight () >= 70, "Compact view retains usable preset and status areas");
            for (int index { 0 }; index < 2; ++index)
            {
                auto* compactSlot { dynamic_cast<juce::TextEditor*> (component.table.getCellComponent (5, index)) };
                check (compactSlot != nullptr && compactSlot->getText () == juce::String (index + 1)
                       && component.table.getLocalBounds ().contains (component.table.getLocalArea (compactSlot, compactSlot->getLocalBounds ())),
                       "Both editable destination slots stay visible immediately after compact resize, without an asynchronous layout tick");
            }
            check (! component.table.getViewport ()->getHorizontalScrollBar ().isVisible (),
                   "Compact preset columns fit without hiding Bank slot behind a horizontal scrollbar");
            snapshot (component, light ? "bank-export-compact-light" : "bank-export-compact-dark");
        }
        component.setSize (960, 720);
        component.writeBank = productionWrite;
        component.beginExport ();
        check (! component.addFolderButton.isEnabled () && ! component.bankName.isEnabled () && component.closeButton.getButtonText () == "Cancel",
               "In-flight export freezes the captured request and exposes cancellation");
        finish (component);
        check (component.exportedFolder == root.getChildFile ("Performance bank") && component.exportedFolder.isDirectory ()
               && component.revealButton.isEnabled () && openCalls == 0 && component.status.getText ().contains ("Original files are unchanged"),
               "Successful export shows its folder and summary without switching the working folder by default");
        juce::MemoryBlock firstAfter, secondAfter;
        check (firstPreset.loadFileAsData (firstAfter) && secondPreset.loadFileAsData (secondAfter)
               && firstAfter == firstOriginal && secondAfter == secondOriginal,
               "Real multi-folder export keeps both original preset files byte-for-byte unchanged");
        check (component.exportedFolder.getChildFile ("prst001.yml").existsAsFile ()
               && component.exportedFolder.getChildFile ("prst002.yml").existsAsFile (),
               "Real multi-folder export writes both explicitly chosen flat bank slots");
        snapshot (component, "bank-export-success");
        check (! component.exportButton.isEnabled (), "A completed bank is not overwritten by a second click");

        component.bankName.setText ("Open bank", false);
        component.openAfterExport.setToggleState (true, juce::dontSendNotification);
        component.refreshControls ();
        component.beginExport ();
        finish (component);
        check (openCalls == 1 && opened == root.getChildFile ("Open bank"), "Opt-in opens exactly the successfully exported bank");

        std::atomic<bool> workerEntered { false }, cancellationObserved { false };
        component.writeBank = [&] (const std::vector<PresetBankExport::Entry>& entries, const juce::File& destination,
                                  PresetBankExport::Report&, PresetBankExport::Cancel cancelled, PresetBankExport::Progress progress)
        {
            check (entries.size () == 2 && destination.getFileName () == "Cancel bank", "The worker receives detached selected entries and destination");
            workerEntered.store (true);
            progress (0.25, "Copying referenced WAVs...");
            while (! cancelled ()) std::this_thread::sleep_for (std::chrono::milliseconds (1));
            cancellationObserved.store (true);
            return juce::Result::fail ("Bank export cancelled; original files were not changed.");
        };
        component.bankName.setText ("Cancel bank", false);
        component.refreshControls ();
        component.beginExport ();
        component.cancelOrClose ();
        finish (component);
        check (workerEntered.load () && cancellationObserved.load () && ! root.getChildFile ("Cancel bank").exists () && openCalls == 1
               && component.status.getText ().contains ("cancelled"), "Cancel cooperatively ends the worker and never opens a failed or partial bank");

        auto closing { std::make_unique<BankExportComponent> (first) };
        finish (*closing);
        closing->writeBank = [] (const std::vector<PresetBankExport::Entry>&, const juce::File&, PresetBankExport::Report&,
                                PresetBankExport::Cancel cancelled, PresetBankExport::Progress)
        {
            while (! cancelled ()) std::this_thread::sleep_for (std::chrono::milliseconds (1));
            return juce::Result::fail ("Cancelled by window destruction");
        };
        closing->beginExport ();
        closing.reset (); // Must cancel and join, never leave an untracked worker with UI references.
        auto choosing { std::make_unique<BankExportComponent> (first) };
        finish (*choosing);
        choosing->chooseFolder = [&] (const juce::String&, const juce::File&, std::function<void (juce::File)> done) { chooseResult = std::move (done); };
        choosing->chooseSourceFolder ();
        choosing.reset ();
        chooseResult (second); // SafePointer guards a delayed native chooser completion.
    }
};

void testBankExportUi ()
{
    BankExportUiTestAccess::run ();
    std::cout << "PASS: bank export UI, explicit slot conflicts, non-destructive defaults, opt-in opening and cancellable worker lifecycle\n";
}
