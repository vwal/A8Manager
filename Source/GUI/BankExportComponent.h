#pragma once

#include <JuceHeader.h>
#include "../Assimil8or/PresetBankExport.h"
#include "ModernTheme.h"
#include <atomic>
#include <mutex>
#include <thread>

// A detached, copy-only export of saved presets. The caller resolves unsaved
// working changes before opening this dialog; this component never saves them.
class BankExportComponent : public juce::Component, private juce::TableListBoxModel, private juce::Timer
{
public:
    BankExportComponent (juce::File initialFolder, std::function<void (juce::File)> onOpenBank = {});
    ~BankExportComponent () override;
    static void show (juce::File initialFolder, std::function<void (juce::File)> onOpenBank = {});

private:
    friend struct BankExportUiTestAccess;
    enum class Activity { idle, choosingFolder, discovering, exporting };
    struct Row
    {
        PresetBankExport::Candidate source;
        bool selected { true };
        juce::String targetSlot;
    };
    struct Job
    {
        std::atomic<bool> cancelled { false }, done { false };
        juce::Result result { juce::Result::ok () };
        std::vector<PresetBankExport::Candidate> candidates;
        std::vector<juce::File> folders;
        PresetBankExport::Report report;
        std::mutex progressMutex;
        double progress { -1.0 };
        juce::String message;
    };
    ModernLookAndFeel look;
    juce::Label title, explanation, sourcesLabel, selectionLabel, nameLabel, parentLabel;
    juce::ComboBox sourceFolders;
    juce::TableListBox table { "Saved presets to copy", this };
    juce::TextEditor bankName, destinationParent, status;
    juce::TextButton addFolderButton { "Add folder..." }, rescanButton { "Rescan" };
    juce::TextButton selectAllButton { "Select all" }, selectNoneButton { "Select none" };
    juce::TextButton chooseParentButton { "Choose..." }, exportButton { "Export new bank" };
    juce::TextButton revealButton { "Show exported folder" }, closeButton { "Close" };
    juce::ToggleButton openAfterExport { "Open bank after export" };
    double progressValue { 0.0 };
    juce::ProgressBar progressBar { progressValue };
    std::vector<Row> rows;
    std::vector<juce::File> folders;
    juce::File initialFolder, parentFolder, exportedFolder;
    std::function<void (juce::File)> onOpenBank;
    Activity activity { Activity::idle };
    bool statusIsError { false }, openWhenDone { false };
    unsigned int chooserGeneration { 0 };
    std::unique_ptr<juce::FileChooser> chooser;
    std::shared_ptr<Job> job;
    std::thread worker;

    // Seams retain the actual detached-worker lifecycle in regression tests.
    std::function<void (const juce::String&, const juce::File&, std::function<void (juce::File)>)> chooseFolder;
    std::function<juce::Result (const juce::File&, std::vector<PresetBankExport::Candidate>&,
                               PresetBankExport::Cancel)> discoverPresets;
    std::function<juce::Result (const std::vector<PresetBankExport::Entry>&, const juce::File&,
                               PresetBankExport::Report&, PresetBankExport::Cancel, PresetBankExport::Progress)> writeBank;

    int getNumRows () override;
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
    juce::Component* refreshComponentForCell (int row, int column, bool selected, juce::Component* existing) override;
    juce::String getCellTooltip (int row, int column) override;
    void addFolder (juce::File);
    void beginDiscovery (std::vector<juce::File>);
    void chooseSourceFolder ();
    void chooseDestinationParent ();
    void beginExport ();
    void cancelOrClose ();
    void refreshControls (bool preserveStatus = false);
    void setStatus (const juce::String&, bool error = false);
    juce::Result validateControls () const;
    std::vector<PresetBankExport::Entry> selectedEntries () const;
    void timerCallback () override;
    void paint (juce::Graphics&) override;
    void resized () override;
    void lookAndFeelChanged () override;
};
