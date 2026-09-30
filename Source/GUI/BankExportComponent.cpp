#include "BankExportComponent.h"
#include <exception>
#include <map>
#include <set>

namespace
{
    juce::String slotName (int slot) { return "P" + juce::String (slot).paddedLeft ('0', 3); }
    constexpr int includeColumn { 1 }, sourceColumn { 2 }, nameColumn { 3 }, originalColumn { 4 }, targetColumn { 5 };
}

BankExportComponent::BankExportComponent (juce::File folder, std::function<void (juce::File)> openBank)
    : initialFolder (std::move (folder)), parentFolder (initialFolder), onOpenBank (std::move (openBank))
{
    setLookAndFeel (&look);
    title.setText ("Save / Export Bank", juce::dontSendNotification);
    title.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    explanation.setText ("Copy saved presets and their referenced files into one new, flat bank folder.\n"
                         "Original folders and files are always kept. Existing destinations are never overwritten.", juce::dontSendNotification);
    explanation.setFont (juce::FontOptions (15.0f));
    explanation.setMinimumHorizontalScale (1.0f);
    sourcesLabel.setText ("Source folders", juce::dontSendNotification);
    nameLabel.setText ("New bank name", juce::dontSendNotification);
    parentLabel.setText ("Create inside", juce::dontSendNotification);
    for (auto* label : { &title, &explanation, &sourcesLabel, &selectionLabel, &nameLabel, &parentLabel })
    {
        label->setBorderSize ({});
        addAndMakeVisible (*label);
    }
    sourceFolders.setTooltip ("Only the listed folders are read. Add each source folder explicitly; subfolders are not scanned.");
    sourceFolders.setComponentID ("bankSourceFolders");
    addFolderButton.onClick = [this] { chooseSourceFolder (); };
    rescanButton.onClick = [this] { if (activity == Activity::idle) beginDiscovery (folders); };
    selectAllButton.onClick = [this]
    {
        if (activity != Activity::idle) return;
        for (auto& row : rows) row.selected = true;
        table.updateContent ();
        refreshControls ();
    };
    selectNoneButton.onClick = [this]
    {
        if (activity != Activity::idle) return;
        for (auto& row : rows) row.selected = false;
        table.updateContent ();
        refreshControls ();
    };
    table.getHeader ().addColumn ("Copy", includeColumn, 52, 52, 52, juce::TableHeaderComponent::visible);
    table.getHeader ().addColumn ("Source folder", sourceColumn, 230, 130, 700, juce::TableHeaderComponent::visible);
    table.getHeader ().addColumn ("Saved preset name", nameColumn, 300, 160, 900, juce::TableHeaderComponent::visible);
    table.getHeader ().addColumn ("From", originalColumn, 76, 76, 76, juce::TableHeaderComponent::visible);
    table.getHeader ().addColumn ("Bank slot", targetColumn, 90, 90, 90, juce::TableHeaderComponent::visible);
    table.setRowHeight (34);
    table.setHeaderHeight (30);
    table.setMultipleSelectionEnabled (false);
    table.setComponentID ("bankPresetTable");
    bankName.setText ("New bank", false);
    bankName.setFont (juce::FontOptions (16.0f));
    bankName.setComponentID ("bankName");
    bankName.setTooltip ("A new hardware-safe folder name, at most 31 characters. Names are checked, never silently truncated.");
    bankName.onTextChange = [this] { refreshControls (); };
    destinationParent.setReadOnly (true);
    destinationParent.setFont (juce::FontOptions (15.0f));
    destinationParent.setComponentID ("bankDestinationParent");
    destinationParent.setText (parentFolder.getFullPathName (), false);
    chooseParentButton.onClick = [this] { chooseDestinationParent (); };
    openAfterExport.setToggleState (false, juce::dontSendNotification);
    openAfterExport.setTooltip ("Optional: switch Samples and Designer to the exported flat folder after success. Unchecked leaves your current working folder open.");
    openAfterExport.setComponentID ("bankOpenAfterExport");
    exportButton.setComponentID ("bankExport");
    exportButton.onClick = [this] { beginExport (); };
    revealButton.onClick = [this] { if (exportedFolder.isDirectory ()) exportedFolder.revealToUser (); };
    closeButton.onClick = [this] { cancelOrClose (); };
    status.setReadOnly (true);
    status.setMultiLine (true, true);
    status.setScrollbarsShown (true);
    status.setCaretVisible (false);
    status.setFont (juce::FontOptions (15.0f));
    status.setIndents (8, 6);
    status.setComponentID ("bankExportStatus");
    status.setTooltip ("Export validation, result and full output path. Select text to copy it.");
    progressBar.setPercentageDisplay (false);
    for (auto* component : std::initializer_list<juce::Component*> { &sourceFolders, &addFolderButton, &rescanButton,
             &selectAllButton, &selectNoneButton, &table, &bankName, &destinationParent, &chooseParentButton,
             &openAfterExport, &status, &progressBar, &exportButton, &revealButton, &closeButton })
        addAndMakeVisible (*component);

    chooseFolder = [this] (const juce::String& prompt, const juce::File& start, std::function<void (juce::File)> complete)
    {
        chooser = std::make_unique<juce::FileChooser> (prompt, start, "");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safe = juce::Component::SafePointer<BankExportComponent> (this), completeSelection = std::move (complete)] (const juce::FileChooser& dialog)
            {
                const auto chosen { dialog.getResult () };
                if (safe == nullptr) return;
                safe->chooser.reset ();
                completeSelection (chosen);
            });
    };
    discoverPresets = [] (const juce::File& source, std::vector<PresetBankExport::Candidate>& found, PresetBankExport::Cancel cancelled)
        { return PresetBankExport::discover (source, found, std::move (cancelled)); };
    writeBank = [] (const std::vector<PresetBankExport::Entry>& entries, const juce::File& destination,
                   PresetBankExport::Report& report, PresetBankExport::Cancel cancelled, PresetBankExport::Progress progress)
        { return PresetBankExport::exportBank (entries, destination, report, std::move (cancelled), std::move (progress)); };
    setSize (960, 720);
    lookAndFeelChanged ();
    addFolder (initialFolder);
}

BankExportComponent::~BankExportComponent ()
{
    stopTimer ();
    ++chooserGeneration;
    chooser.reset ();
    if (job) job->cancelled.store (true);
    // The worker captures detached values/functions only. Cancellation asks the
    // backend to discard its own staging folder, never any original source.
    if (worker.joinable ()) worker.join ();
    table.setModel (nullptr);
    setLookAndFeel (nullptr);
}

void BankExportComponent::show (juce::File folder, std::function<void (juce::File)> openBank)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new BankExportComponent (std::move (folder), std::move (openBank)));
    options.dialogTitle = "Save / Export Bank";
    options.dialogBackgroundColour = Theme::background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    if (auto* dialog { options.launchAsync () }) dialog->setResizeLimits (740, 640, 1800, 1400);
}

int BankExportComponent::getNumRows () { return static_cast<int> (rows.size ()); }

void BankExportComponent::paintRowBackground (juce::Graphics& g, int row, int width, int height, bool selected)
{
    g.fillAll (selected ? Theme::accent.withAlpha (0.12f).overlaidWith (Theme::field.withAlpha (0.85f))
                       : (row % 2 == 0 ? Theme::field : Theme::panel));
    g.setColour (Theme::border.withAlpha (0.45f));
    g.drawHorizontalLine (height - 1, 0.0f, static_cast<float> (width));
}

void BankExportComponent::paintCell (juce::Graphics& g, int index, int column, int width, int height, bool)
{
    if (index < 0 || index >= getNumRows ()) return;
    const auto& row { rows[static_cast<size_t> (index)] };
    juce::String value;
    if (column == sourceColumn) value = row.source.presetFile.getParentDirectory ().getFileName ();
    else if (column == nameColumn) value = row.source.name.isEmpty () ? "(unnamed preset)" : row.source.name;
    else if (column == originalColumn) value = slotName (row.source.slot);
    g.setColour (row.selected ? Theme::text : Theme::muted);
    g.setFont (column == originalColumn ? Theme::numericFont (14.0f) : juce::Font (juce::FontOptions (15.0f)));
    g.drawText (value, 7, 0, width - 14, height, juce::Justification::centredLeft, true);
}

juce::Component* BankExportComponent::refreshComponentForCell (int index, int column, bool, juce::Component* existing)
{
    if (index < 0 || index >= getNumRows ()) { delete existing; return nullptr; }
    auto& row { rows[static_cast<size_t> (index)] };
    if (column == includeColumn)
    {
        auto* button { dynamic_cast<juce::ToggleButton*> (existing) };
        if (button == nullptr) { delete existing; button = new juce::ToggleButton (); }
        button->setToggleState (row.selected, juce::dontSendNotification);
        button->setTooltip ("Include " + row.source.presetFile.getFullPathName ());
        button->setEnabled (activity == Activity::idle);
        button->onClick = [safe = juce::Component::SafePointer<BankExportComponent> (this), index, button]
        {
            if (safe == nullptr || safe->activity != Activity::idle || index >= safe->getNumRows ()) return;
            safe->rows[static_cast<size_t> (index)].selected = button->getToggleState ();
            safe->refreshControls ();
        };
        return button;
    }
    if (column == targetColumn)
    {
        auto* editor { dynamic_cast<juce::TextEditor*> (existing) };
        if (editor == nullptr) { delete existing; editor = new juce::TextEditor (); }
        editor->onTextChange = nullptr;
        editor->setText (row.targetSlot, false);
        editor->setFont (Theme::numericFont (15.0f));
        editor->setJustification (juce::Justification::centred);
        editor->setSelectAllWhenFocused (true);
        editor->setTooltip ("Destination bank slot 1-199. Resolve duplicate slots explicitly; original preset filenames are not changed.");
        editor->setEnabled (activity == Activity::idle && row.selected);
        editor->onTextChange = [safe = juce::Component::SafePointer<BankExportComponent> (this), index, editor]
        {
            if (safe == nullptr || safe->activity != Activity::idle || index >= safe->getNumRows ()) return;
            safe->rows[static_cast<size_t> (index)].targetSlot = editor->getText ();
            safe->refreshControls ();
        };
        return editor;
    }
    delete existing;
    return nullptr;
}

juce::String BankExportComponent::getCellTooltip (int index, int)
{
    return index >= 0 && index < getNumRows () ? rows[static_cast<size_t> (index)].source.presetFile.getFullPathName () : juce::String {};
}

void BankExportComponent::addFolder (juce::File folder)
{
    if (activity != Activity::idle) return;
    if (! folder.isDirectory ()) { setStatus ("Choose an existing source folder containing saved prstNNN.yml presets.", true); refreshControls (true); return; }
    auto sources { folders };
    if (std::find (sources.begin (), sources.end (), folder) != sources.end ())
    { setStatus ("That source folder is already listed. Use Rescan to refresh its saved presets."); return; }
    sources.push_back (std::move (folder));
    beginDiscovery (std::move (sources));
}

void BankExportComponent::beginDiscovery (std::vector<juce::File> sources)
{
    if (activity != Activity::idle || sources.empty ()) return;
    activity = Activity::discovering;
    job = std::make_shared<Job> ();
    job->folders = std::move (sources);
    const auto state { job };
    const auto discover { discoverPresets };
    setStatus ("Reading saved presets in the listed folders only...");
    refreshControls (true);
    try
    {
        worker = std::thread ([state, discover]
        {
            try
            {
                for (const auto& folder : state->folders)
                {
                    if (state->cancelled.load ()) { state->result = juce::Result::fail ("Folder scan cancelled."); break; }
                    std::vector<PresetBankExport::Candidate> found;
                    state->result = discover (folder, found, [state] { return state->cancelled.load (); });
                    if (state->result.failed ()) break;
                    state->candidates.insert (state->candidates.end (), found.begin (), found.end ());
                }
            }
            catch (const std::exception& e) { state->result = juce::Result::fail (e.what ()); }
            catch (...) { state->result = juce::Result::fail ("Unable to read the selected source folders."); }
            state->done.store (true);
        });
    }
    catch (...)
    {
        state->result = juce::Result::fail ("Unable to start the folder scan. Please try again.");
        state->done.store (true);
    }
    startTimerHz (20);
}

void BankExportComponent::chooseSourceFolder ()
{
    if (activity != Activity::idle) return;
    activity = Activity::choosingFolder;
    const auto generation { ++chooserGeneration };
    refreshControls (true);
    chooseFolder ("Add a folder of saved presets (this folder only)", folders.empty () ? initialFolder : folders.back (),
        [safe = juce::Component::SafePointer<BankExportComponent> (this), generation] (juce::File folder)
        {
            if (safe == nullptr || generation != safe->chooserGeneration) return;
            safe->activity = Activity::idle;
            if (folder != juce::File {}) safe->addFolder (std::move (folder));
            else safe->refreshControls ();
        });
}

void BankExportComponent::chooseDestinationParent ()
{
    if (activity != Activity::idle) return;
    activity = Activity::choosingFolder;
    const auto generation { ++chooserGeneration };
    refreshControls (true);
    chooseFolder ("Choose parent folder for the new bank", parentFolder,
        [safe = juce::Component::SafePointer<BankExportComponent> (this), generation] (juce::File folder)
        {
            if (safe == nullptr || generation != safe->chooserGeneration) return;
            safe->activity = Activity::idle;
            if (folder.isDirectory ())
            {
                safe->parentFolder = folder;
                safe->destinationParent.setText (folder.getFullPathName (), false);
            }
            safe->refreshControls ();
        });
}

std::vector<PresetBankExport::Entry> BankExportComponent::selectedEntries () const
{
    std::vector<PresetBankExport::Entry> entries;
    for (const auto& row : rows)
        if (row.selected) entries.push_back ({ row.source.presetFile, row.targetSlot.getIntValue () });
    return entries;
}

juce::Result BankExportComponent::validateControls () const
{
    std::set<int> slots;
    for (const auto& row : rows)
    {
        if (! row.selected) continue;
        const auto slot { row.targetSlot.getIntValue () };
        if (row.targetSlot.isEmpty () || row.targetSlot.length () > 3 || ! row.targetSlot.containsOnly ("0123456789") || slot < 1 || slot > 199)
            return juce::Result::fail ("Enter a bank slot from 1 to 199 for each selected preset.");
        if (! slots.insert (slot).second)
            return juce::Result::fail ("Destination slot " + slotName (slot) + " is selected more than once. Change a Bank slot or uncheck a preset; no slots are reassigned automatically.");
    }
    if (slots.empty ()) return juce::Result::fail ("Select at least one saved preset to export. Add folder... can combine presets from other folders.");
    const auto name { bankName.getText () };
    if (name.isEmpty () || name != name.trim () || name.length () > 31 || name == "." || name == ".." || juce::File::createLegalFileName (name) != name)
        return juce::Result::fail ("Enter a bank folder name of 1-31 characters without path separators, reserved characters, or leading/trailing spaces.");
    if (! parentFolder.isDirectory ()) return juce::Result::fail ("Choose an existing destination parent folder.");
    return PresetBankExport::validateDestination (parentFolder.getChildFile (name));
}

void BankExportComponent::beginExport ()
{
    if (activity != Activity::idle) return;
    const auto valid { validateControls () };
    if (valid.failed ()) { setStatus (valid.getErrorMessage (), true); refreshControls (true); return; }
    const auto entries { selectedEntries () };
    const auto destination { parentFolder.getChildFile (bankName.getText ()) };
    const auto write { writeBank };
    openWhenDone = openAfterExport.getToggleState ();
    activity = Activity::exporting;
    job = std::make_shared<Job> ();
    const auto state { job };
    setStatus ("Validating saved presets and preparing a new bank. Original files remain untouched.");
    refreshControls (true);
    try
    {
        worker = std::thread ([state, entries, destination, write]
        {
            try
            {
                state->result = write (entries, destination, state->report, [state] { return state->cancelled.load (); },
                    [state] (double progress, const juce::String& message)
                    {
                        const std::lock_guard<std::mutex> lock { state->progressMutex };
                        state->progress = progress;
                        state->message = message;
                    });
            }
            catch (const std::exception& e) { state->result = juce::Result::fail (e.what ()); }
            catch (...) { state->result = juce::Result::fail ("The bank export could not be completed."); }
            state->done.store (true);
        });
    }
    catch (...)
    {
        state->result = juce::Result::fail ("Unable to start the export worker. No files were changed; please try again.");
        state->done.store (true);
    }
    startTimerHz (20);
}

void BankExportComponent::cancelOrClose ()
{
    if (job && ! job->done.load ())
    {
        job->cancelled.store (true);
        closeButton.setEnabled (false);
        setStatus ("Cancelling... Original files are unchanged; unfinished export files will be cleaned up.");
        return;
    }
    ++chooserGeneration;
    chooser.reset ();
    if (auto* dialog { findParentComponentOfClass<juce::DialogWindow> () }) dialog->exitModalState (0);
}

void BankExportComponent::timerCallback ()
{
    if (! job) return;
    if (! job->done.load ())
    {
        const std::lock_guard<std::mutex> lock { job->progressMutex };
        progressValue = job->progress;
        if (! job->cancelled.load () && job->message.isNotEmpty ()) setStatus (job->message);
        return;
    }
    stopTimer ();
    if (worker.joinable ()) worker.join ();
    const auto completed { job };
    job.reset ();
    const auto wasExport { activity == Activity::exporting };
    activity = Activity::idle;
    progressValue = 0.0;
    if (completed->result.failed ())
    {
        setStatus (completed->result.getErrorMessage (), ! completed->cancelled.load ());
        refreshControls (true);
        return;
    }
    if (! wasExport)
    {
        std::map<juce::String, Row> previous;
        for (const auto& row : rows) previous.emplace (row.source.presetFile.getFullPathName (), row);
        rows.clear ();
        for (const auto& candidate : completed->candidates)
        {
            const auto found { previous.find (candidate.presetFile.getFullPathName ()) };
            rows.push_back ({ candidate, found == previous.end () ? true : found->second.selected,
                             found == previous.end () ? juce::String (candidate.slot) : found->second.targetSlot });
        }
        folders = completed->folders;
        sourceFolders.clear (juce::dontSendNotification);
        for (size_t index { 0 }; index < folders.size (); ++index)
            sourceFolders.addItem (folders[index].getFullPathName (), static_cast<int> (index) + 1);
        sourceFolders.setSelectedId (static_cast<int> (folders.size ()), juce::dontSendNotification);
        table.updateContent ();
        refreshControls ();
        return;
    }
    exportedFolder = completed->report.folder;
    auto summary { "Bank exported: " + exportedFolder.getFullPathName () + "\n" +
        juce::String (completed->report.presetCount) + " presets, " + juce::String (completed->report.waveCount) + " WAV files, " +
        juce::String (completed->report.midiCount) + " MIDI setups. Original files are unchanged." };
    if (completed->report.renamedWaves > 0)
        summary += "\n" + juce::String (completed->report.renamedWaves) + " conflicting WAV filenames safely renamed in the new bank.";
    if (! completed->report.warnings.isEmpty ()) summary += "\n" + completed->report.warnings.joinIntoString ("\n");
    setStatus (summary);
    refreshControls (true);
    if (openWhenDone && onOpenBank)
    {
        const auto open { onOpenBank };
        const auto destination { exportedFolder };
        if (auto* dialog { findParentComponentOfClass<juce::DialogWindow> () })
        {
            dialog->exitModalState (0);
            juce::MessageManager::callAsync ([open, destination] { open (destination); });
        }
        else open (destination);
    }
}

void BankExportComponent::refreshControls (bool preserveStatus)
{
    const auto idle { activity == Activity::idle };
    for (auto* component : std::initializer_list<juce::Component*> { &sourceFolders, &addFolderButton, &rescanButton,
             &selectAllButton, &selectNoneButton, &table, &bankName, &chooseParentButton, &openAfterExport })
        component->setEnabled (idle);
    rescanButton.setEnabled (idle && ! folders.empty ());
    table.updateContent ();
    table.repaint ();
    const auto valid { validateControls () };
    exportButton.setEnabled (idle && valid.wasOk ());
    revealButton.setEnabled (idle && exportedFolder.isDirectory ());
    closeButton.setEnabled (true);
    closeButton.setButtonText (job ? "Cancel" : "Close");
    progressBar.setVisible (job != nullptr);
    selectionLabel.setText (juce::String (selectedEntries ().size ()) + " selected / " + juce::String (rows.size ()) + " saved presets",
                            juce::dontSendNotification);
    if (idle && ! preserveStatus)
        setStatus (valid.failed () ? valid.getErrorMessage ()
                                 : "Ready to copy into " + parentFolder.getChildFile (bankName.getText ()).getFullPathName () +
                                   "\nOnly selected presets and their referenced files are included. No original files are moved or deleted.", valid.failed ());
}

void BankExportComponent::setStatus (const juce::String& message, bool error)
{
    statusIsError = error;
    status.setText (message, false);
    status.applyColourToAllText (error ? Theme::error : Theme::text, true);
}

void BankExportComponent::paint (juce::Graphics& g) { g.fillAll (Theme::background); }

void BankExportComponent::lookAndFeelChanged ()
{
    title.setColour (juce::Label::textColourId, Theme::accent);
    explanation.setColour (juce::Label::textColourId, Theme::muted);
    table.setColour (juce::ListBox::backgroundColourId, Theme::field);
    table.setColour (juce::ListBox::outlineColourId, Theme::border);
    table.getHeader ().setColour (juce::TableHeaderComponent::backgroundColourId, Theme::panel);
    table.getHeader ().setColour (juce::TableHeaderComponent::textColourId, Theme::text);
    table.getHeader ().setColour (juce::TableHeaderComponent::outlineColourId, Theme::border);
    for (auto* editor : { &bankName, &destinationParent, &status })
        editor->applyColourToAllText (editor == &status && statusIsError ? Theme::error : Theme::text, false);
    table.repaint ();
    repaint ();
}

void BankExportComponent::resized ()
{
    auto area { getLocalBounds ().reduced (20) };
    title.setBounds (area.removeFromTop (32));
    area.removeFromTop (6);
    explanation.setBounds (area.removeFromTop (48));
    area.removeFromTop (12);
    auto sources { area.removeFromTop (30) };
    sourcesLabel.setBounds (sources.removeFromLeft (112));
    rescanButton.setBounds (sources.removeFromRight (80));
    sources.removeFromRight (8);
    addFolderButton.setBounds (sources.removeFromRight (116));
    sources.removeFromRight (8);
    sourceFolders.setBounds (sources);
    area.removeFromTop (8);
    auto selection { area.removeFromTop (28) };
    selectNoneButton.setBounds (selection.removeFromRight (100));
    selection.removeFromRight (8);
    selectAllButton.setBounds (selection.removeFromRight (90));
    selectionLabel.setBounds (selection);
    area.removeFromTop (8);
    auto footer { area.removeFromBottom (252) };
    table.setBounds (area);
    const auto flexible { juce::jmax (290, table.getWidth () - 52 - 76 - 90 - 18) };
    table.getHeader ().setColumnWidth (sourceColumn, juce::roundToInt (flexible * 0.43));
    table.getHeader ().setColumnWidth (nameColumn, flexible - table.getHeader ().getColumnWidth (sourceColumn));
    // Header notifications are asynchronous; keep live cell editors and the
    // scrollable width in sync during a resize, before the next message tick.
    table.setMinimumContentWidth (table.getHeader ().getTotalWidth ());
    table.updateContent ();
    footer.removeFromTop (12);
    auto nameRow { footer.removeFromTop (30) };
    nameLabel.setBounds (nameRow.removeFromLeft (126));
    bankName.setBounds (nameRow);
    footer.removeFromTop (8);
    auto destination { footer.removeFromTop (30) };
    parentLabel.setBounds (destination.removeFromLeft (126));
    chooseParentButton.setBounds (destination.removeFromRight (100));
    destination.removeFromRight (8);
    destinationParent.setBounds (destination);
    footer.removeFromTop (6);
    openAfterExport.setBounds (footer.removeFromTop (28));
    footer.removeFromTop (6);
    status.setBounds (footer.removeFromTop (70));
    footer.removeFromTop (4);
    progressBar.setBounds (footer.removeFromTop (8));
    footer.removeFromTop (8);
    auto buttons { footer.removeFromTop (32) };
    closeButton.setBounds (buttons.removeFromRight (84));
    buttons.removeFromRight (10);
    exportButton.setBounds (buttons.removeFromRight (162));
    revealButton.setBounds (buttons.removeFromLeft (180));
}
