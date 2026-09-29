#include "PresetListComponent.h"
#include "../../ModernTheme.h"
#include "../../../Assimil8or/Assimil8orPreset.h"
#include "../../../Assimil8or/FileTypeHelpers.h"
#include "../../../Assimil8or/PresetManagerProperties.h"
#include "../../../Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "oolib/Debug/DebugLog.h"
#include "oolib/Properties/PersistentRootProperties.h"
#include "oolib/Properties/RuntimeRootProperties.h"
#include "oolib/Debug/WatchDogTimer.h"

#define LOG_PRESET_LIST 0
#if LOG_PRESET_LIST
#define LogPresetList(text) DebugLog ("PresetListComponent", text);
#else
#define LogPresetList(text) ;
#endif

PresetListComponent::PresetListComponent ()
{
    showAllPresets.setToggleState (true, juce::NotificationType::dontSendNotification);
    showAllPresets.setButtonText ("Show All");
    showAllPresets.setTooltip ("Show all Presets, Show only existing presets");
    showAllPresets.onClick = [this] ()
    {
        requestedShowAllPresets.store (showAllPresets.getToggleState ());
        requestPresetCheck ();
    };
    addAndMakeVisible (showAllPresets);
    addAndMakeVisible (presetListBox);

    checkPresetsThread.onThreadLoop = [this] ()
    {
        checkPresets (requestedShowAllPresets.load ());
        return false;
    };
}

void PresetListComponent::init (juce::ValueTree rootPropertiesVT)
{
    LogPresetList ("PresetListComponent::init");
    PersistentRootProperties persistentRootProperties (rootPropertiesVT, PersistentRootProperties::WrapperType::client, PersistentRootProperties::EnableCallbacks::no);
    appProperties.wrap (persistentRootProperties.getValueTree (), AppProperties::WrapperType::client, AppProperties::EnableCallbacks::no);

    RuntimeRootProperties runtimeRootProperties (rootPropertiesVT, RuntimeRootProperties::WrapperType::client, RuntimeRootProperties::EnableCallbacks::no);
    directoryDataProperties.wrap (runtimeRootProperties.getValueTree (), DirectoryDataProperties::WrapperType::client, DirectoryDataProperties::EnableCallbacks::yes);
    presetFileTypeId = directoryDataProperties.getFileTypeId (FileTypeHelpers::kPresetFileTypeName);
    directoryDataProperties.onRootScanComplete = [this] ()
    {
        LogPresetList ("PresetListComponent::init - directoryDataProperties.onRootScanComplete");
        requestPresetCheck ();
    };
//     directoryDataProperties.onStatusChange = [this] (DirectoryDataProperties::ScanStatus status)
//     {
//         switch (status)
//         {
//             case DirectoryDataProperties::ScanStatus::empty:
//             {
//             }
//             break;
//             case DirectoryDataProperties::ScanStatus::scanning:
//             {
//             }
//             break;
//             case DirectoryDataProperties::ScanStatus::canceled:
//             {
//             }
//             break;
//             case DirectoryDataProperties::ScanStatus::done:
//             {
//                 checkPresetsThread.startThread ();
//             }
//             break;
//         }
//     };
    PresetManagerProperties presetManagerProperties (runtimeRootProperties.getValueTree (), PresetManagerProperties::WrapperType::owner, PresetManagerProperties::EnableCallbacks::no);
    unEditedPresetProperties.wrap (presetManagerProperties.getPreset ("unedited"), PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::yes);
    presetProperties.wrap (presetManagerProperties.getPreset ("edit"), PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::yes);

    checkPresetsThread.start ();
}

void PresetListComponent::requestPresetCheck ()
{
    if (! checkPresetsThread.isThreadRunning ())
    {
        LogPresetList ("PresetListComponent::requestPresetCheck - starting thread");
        checkPresetsThread.start ();
    }
    else
    {
        LogPresetList ("PresetListComponent::requestPresetCheck - starting timer");
        startTimer (1);
    }
}

void PresetListComponent::forEachPresetFile (std::function<bool (juce::File presetFile, int index)> presetFileCallback)
{
    jassert (presetFileCallback != nullptr);

    auto inPresetList { false };
    ValueTreeHelpers::forEachChild (directoryDataProperties.getRootFolderVT (), [this, presetFileCallback, &inPresetList] (juce::ValueTree child)
    {
        if (FileProperties::isFileVT (child))
        {
            FileProperties fileProperties (child, FileProperties::WrapperType::client, FileProperties::EnableCallbacks::no);
            if (fileProperties.getType () == presetFileTypeId)
            {
                inPresetList = true;
                const auto fileToCheck { juce::File (fileProperties.getName ()) };
                const auto presetIndex { FileTypeHelpers::getPresetNumberFromName (fileToCheck) - 1 };
                if (presetIndex < 0 || presetIndex >= kMaxPresets)
                    return false;
                if (! presetFileCallback (fileToCheck, presetIndex))
                    return false;
            }
            else
            {
                // if the entry is not a preset file, but we were processing preset files, then we are done
                if (inPresetList)
                    return false;
            }
        }
        return true;
    });
}

void PresetListComponent::checkPresets (bool showAll)
{
    WatchdogTimer timer;
    timer.start (100000);

    // this runs on the check presets thread, so we work from a detached snapshot of the live tree
    const auto rootFolderSnapshotVT { ValueTreeHelpers::getMessageThreadSnapshot (directoryDataProperties.getRootFolderVT ()) };
    if (! rootFolderSnapshotVT.isValid ())
        return;
    FolderProperties rootFolder (rootFolderSnapshotVT, FolderProperties::WrapperType::client, FolderProperties::EnableCallbacks::no);
    const auto scannedFolder { juce::File (rootFolder.getName ()) };
    PresetInfoList newPresetInfoList;

    // clear preset info list
    for (auto curPresetInfoIndex { 0 }; curPresetInfoIndex < newPresetInfoList.size (); ++curPresetInfoIndex)
        newPresetInfoList [curPresetInfoIndex] = { curPresetInfoIndex + 1, false, "" };

    auto newNumPresets { showAll ? kMaxPresets : 0 };
    auto inPresetList { false };
    std::array<bool, kMaxPresets> seenSlots {};
    ValueTreeHelpers::forEachChild (rootFolderSnapshotVT, [this, &inPresetList, &newNumPresets, &newPresetInfoList, &seenSlots, showAll] (juce::ValueTree child)
    {
        if (FileProperties::isFileVT (child))
        {
            FileProperties fileProperties (child, FileProperties::WrapperType::client, FileProperties::EnableCallbacks::no);
            if (fileProperties.getType () == presetFileTypeId)
            {
                inPresetList = true;
                const auto fileToCheck { juce::File (fileProperties.getName ()) };
                const auto presetIndex { FileTypeHelpers::getPresetNumberFromName (fileToCheck) - 1 };

                if (presetIndex < 0 || presetIndex >= kMaxPresets || seenSlots[static_cast<size_t> (presetIndex)])
                    return true;
                seenSlots[static_cast<size_t> (presetIndex)] = true;
                juce::ValueTree parsed;
                const auto readResult { PresetFileOperations::read (fileToCheck, parsed) };
                const auto presetName { readResult.wasOk () ? parsed.getProperty (PresetProperties::NamePropertyId).toString () : "(invalid preset)" };

                if (showAll)
                    newPresetInfoList [presetIndex] = { presetIndex + 1 , true, presetName };
                else
                {
                    newPresetInfoList [newNumPresets] = { presetIndex + 1, true, presetName };
                    ++newNumPresets;
                }
            }
            else
            {
                // if the entry is not a preset file, but we had started processing preset files, then we are done, because the files are sorted by type
                if (inPresetList)
                    return false;
            }
        }
        return true; // keep looking
    });

    juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<PresetListComponent> (this),
                                      scannedFolder,
                                      newNumPresets,
                                      newPresetInfoList = std::move (newPresetInfoList)] () mutable
    {
        if (safeThis == nullptr)
            return;
        // A scan of the previous root must not rebind the editor after navigation.
        if (scannedFolder != juce::File (safeThis->appProperties.getMostRecentFolder ()))
            return;

        const auto newFolder { scannedFolder != safeThis->previousFolder };
        safeThis->currentFolder = scannedFolder;
        safeThis->numPresets = newNumPresets;
        safeThis->presetInfoList = std::move (newPresetInfoList);
        safeThis->presetListBox.updateContent ();
        if (newFolder)
        {
            safeThis->presetListBox.scrollToEnsureRowIsOnscreen (0);
            safeThis->loadFirstPreset ();
        }
        else
        {
            safeThis->lastSelectedPresetIndex = PresetFileOperations::rowForSlot (safeThis->presetInfoList, safeThis->numPresets, safeThis->selectedPresetNumber);
            safeThis->presetListBox.deselectAllRows ();
            if (safeThis->lastSelectedPresetIndex >= 0)
                safeThis->presetListBox.selectRow (safeThis->lastSelectedPresetIndex, false, true);
        }
        safeThis->presetListBox.repaint ();
        safeThis->previousFolder = scannedFolder;
    });

    //juce::Logger::outputDebugString ("PresetListComponent::checkPresets - elapsed time: " + juce::String (timer.getElapsedTime ()));
}

void PresetListComponent::loadFirstPreset ()
{
    // Navigation already passed the dirty guard. Bind an empty preset to the new
    // root first, so a malformed first file cannot leave Save targeting the old root.
    loadDefault (1);
    selectedPresetNumber = 1;
    lastSelectedPresetIndex = PresetFileOperations::rowForSlot (presetInfoList, numPresets, 1);
    appProperties.addRecentlyUsedFile (getPresetFile (1).getFullPathName ());
    for (auto row { 0 }; row < numPresets; ++row)
    {
        const auto [number, exists, name] { presetInfoList[static_cast<size_t> (row)] };
        if (exists) { selectPreset (number); return; }
    }
    presetListBox.deselectAllRows ();
    if (lastSelectedPresetIndex >= 0) presetListBox.selectRow (lastSelectedPresetIndex, false, true);
}

void PresetListComponent::loadDefault (int presetNumber)
{
    PresetProperties::copyTreeProperties (ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType),
                                          presetProperties.getValueTree ());
    // set the ID, since the default that was just loaded always has Id 1
    presetProperties.setId (presetNumber, false);
    PresetProperties::copyTreeProperties (presetProperties.getValueTree (), unEditedPresetProperties.getValueTree ());
}

bool PresetListComponent::loadPresetFile (juce::File presetFile, juce::ValueTree presetPropertiesVT)
{
    juce::ValueTree tree;
    const auto result { PresetFileOperations::read (presetFile, tree) };
    if (result.failed ())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Preset load failed", result.getErrorMessage ());
        return false;
    }
    tree.setProperty (PresetProperties::IdPropertyId, FileTypeHelpers::getPresetNumberFromName (presetFile), nullptr);
    PresetProperties::copyTreeProperties (tree, presetPropertiesVT);
    return true;
}

bool PresetListComponent::loadPreset (juce::File presetFile)
{
    if (! loadPresetFile (presetFile, unEditedPresetProperties.getValueTree ())) return false;
    PresetProperties::copyTreeProperties (ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType),
                                          presetProperties.getValueTree ());
    PresetProperties::copyTreeProperties (unEditedPresetProperties.getValueTree (), presetProperties.getValueTree ());
    return true;
}

void PresetListComponent::selectPreset (int presetNumber)
{
    if (presetNumber < 1 || presetNumber > kMaxPresets) return;
    const auto file { getPresetFile (presetNumber) };
    if (file.exists ()) { if (! loadPreset (file)) return; }
    else loadDefault (presetNumber);
    selectedPresetNumber = presetNumber;
    lastSelectedPresetIndex = PresetFileOperations::rowForSlot (presetInfoList, numPresets, presetNumber);
    presetListBox.deselectAllRows ();
    if (lastSelectedPresetIndex >= 0)
    {
        presetListBox.selectRow (lastSelectedPresetIndex, false, true);
        presetListBox.scrollToEnsureRowIsOnscreen (lastSelectedPresetIndex);
    }
    appProperties.addRecentlyUsedFile (file.getFullPathName ());
}

void PresetListComponent::resized ()
{
    auto localBounds { getLocalBounds () };
    auto toolRow { localBounds.removeFromTop (25) };
    showAllPresets.setBounds (toolRow.removeFromLeft (100));
    presetListBox.setBounds (localBounds);
}

int PresetListComponent::getNumRows ()
{
    return numPresets;
}

void PresetListComponent::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (row >= 0 && row < numPresets)
    {
        juce::Colour textColor;
        juce::Colour rowColor;
        if (rowIsSelected)
        {
            rowColor = Theme::accent.withAlpha (0.16f);
            textColor = Theme::accent;
        }
        else
        {
            rowColor = Theme::field;
            textColor = Theme::text;
        }
        auto [presetNumber, thisPresetExists, presetName] { presetInfoList [row] };
        if (thisPresetExists)
        {

        }
        else
        {
            presetName = "(preset)";
            textColor = textColor.withAlpha (0.5f);
        }
        g.setColour (rowColor);
        g.fillRect (0, 0, width, height);
        g.setColour (textColor);
        g.drawText ("  " + juce::String (presetNumber) + "-" + presetName, juce::Rectangle<float>{ 0.0f, 0.0f, (float) width, (float) height }, juce::Justification::centredLeft, true);
    }
}

void PresetListComponent::timerCallback ()
{
    LogPresetList ("PresetListComponent::timerCallback - enter");
    if (! checkPresetsThread.isThreadRunning ())
    {
        LogPresetList ("PresetListComponent::timerCallback - starting thread, stopping timer");
        checkPresetsThread.start ();
        stopTimer ();
    }
    LogPresetList ("PresetListComponent::timerCallback - enter");
}

void PresetListComponent::movePresetUp (int row)
{
    if (row >= 0 && row < numPresets)
    {
        const auto number { std::get<0> (presetInfoList[static_cast<size_t> (row)]) };
        if (number > 1) swapPresets (number, number - 1);
    }
}

void PresetListComponent::movePresetDown (int row)
{
    if (row >= 0 && row < numPresets)
    {
        const auto number { std::get<0> (presetInfoList[static_cast<size_t> (row)]) };
        if (number < kMaxPresets) swapPresets (number, number + 1);
    }
}

void PresetListComponent::swapPresets (int fromSlot, int toSlot)
{
    const auto folder { currentFolder };
    auto move = [safe = juce::Component::SafePointer<PresetListComponent> (this), folder, fromSlot, toSlot] ()
    {
        if (safe == nullptr || safe->currentFolder != folder) return;
        const auto result { PresetFileOperations::swap (folder, fromSlot, toSlot) };
        if (result.failed ())
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Preset move failed", result.getErrorMessage ());
        else
        {
            const auto selected { PresetFileOperations::slotAfterSwap (safe->selectedPresetNumber, fromSlot, toSlot) };
            if (selected != safe->selectedPresetNumber) safe->selectPreset (selected);
            safe->directoryDataProperties.triggerStartScan (false);
            safe->requestPresetCheck ();
        }
    };
    if ((selectedPresetNumber == fromSlot || selectedPresetNumber == toSlot) && overwritePresetOrCancel != nullptr)
        overwritePresetOrCancel (move, [] () {});
    else move ();
}

juce::String PresetListComponent::getTooltipForRow (int row)
{
    if (row < 0 || row >= numPresets) return {};
    auto [presetNumber, thisPresetExists, presetName] { presetInfoList [row] };
    return "Preset " + juce::String (presetNumber);
}

void PresetListComponent::copyPreset (int presetNumber)
{
    loadPresetFile (getPresetFile (presetNumber), copyBufferPresetProperties.getValueTree ());
}

void PresetListComponent::pastePreset (int presetNumber)
{
    if (presetNumber < 1 || presetNumber > kMaxPresets) return;
    const auto destination { getPresetFile (presetNumber) };
    const auto folder { currentFolder };
    const auto pasted { copyBufferPresetProperties.getValueTree ().createCopy () };
    auto doPaste = [safe = juce::Component::SafePointer<PresetListComponent> (this), presetNumber, destination, folder, pasted] ()
    {
        if (safe == nullptr || safe->currentFolder != folder) return;
        Assimil8orPreset assimil8orPreset;
        const auto result { assimil8orPreset.write (destination, pasted) };
        if (result.failed ())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Paste failed", result.getErrorMessage ());
            return;
        }
        if (presetNumber == safe->selectedPresetNumber) safe->selectPreset (presetNumber);
        safe->directoryDataProperties.triggerStartScan (false);
        safe->requestPresetCheck ();
    };
    auto confirm = [destination, doPaste] ()
    {
        if (PresetFileOperations::needsOverwriteConfirmation (destination))
            juce::AlertWindow::showOkCancelBox (juce::AlertWindow::WarningIcon, "OVERWRITE PRESET", "Are you sure you want to overwrite '" + destination.getFileName () + "'?", "YES", "NO", nullptr,
                juce::ModalCallbackFunction::create ([doPaste] (int option) { if (option != 0) doPaste (); }));
        else doPaste ();
    };
    if (presetNumber == selectedPresetNumber && overwritePresetOrCancel != nullptr)
        overwritePresetOrCancel (confirm, [] () {});
    else confirm ();
}

void PresetListComponent::deletePreset (int presetNumber)
{
    if (presetNumber < 1 || presetNumber > kMaxPresets) return;
    const auto folder { currentFolder };
    const auto presetFile { getPresetFile (presetNumber) };
    const auto safe { juce::Component::SafePointer<PresetListComponent> (this) };
    auto confirm = [safe, folder, presetNumber, presetFile] ()
    {
        if (safe == nullptr || safe->currentFolder != folder) return;
        juce::AlertWindow::showOkCancelBox (juce::AlertWindow::WarningIcon, "DELETE PRESET", "Move '" + presetFile.getFileName () + "' to the Trash?", "YES", "NO", nullptr,
        juce::ModalCallbackFunction::create ([safe, folder, presetNumber, presetFile] (int option)
        {
            if (option == 0 || safe == nullptr || safe->currentFolder != folder)
                return;
            if (! presetFile.moveToTrash ())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Delete failed", "Unable to move '" + presetFile.getFileName () + "' to the Trash.");
                return;
            }
            if (presetNumber == safe->selectedPresetNumber) safe->selectPreset (presetNumber);
            safe->directoryDataProperties.triggerStartScan (false);
            safe->requestPresetCheck ();
        }));
    };
    if (presetNumber == selectedPresetNumber && overwritePresetOrCancel != nullptr)
        overwritePresetOrCancel (confirm, [] () {});
    else confirm ();
}

juce::File PresetListComponent::getPresetFile (int presetNumber)
{
    return currentFolder.getChildFile (FileTypeHelpers::getPresetFileName (presetNumber)).withFileExtension (".yml");
}

void PresetListComponent::listBoxItemClicked (int row, [[maybe_unused]] const juce::MouseEvent& me)
{
    if (row < 0 || row >= numPresets) return;
    if (me.mods.isPopupMenu ())
    {
        if (row != lastSelectedPresetIndex)
            presetListBox.selectRow (lastSelectedPresetIndex, true, true);

        auto [presetNumber, thisPresetExists, presetName] { presetInfoList [row] };
        if (! thisPresetExists)
            presetName = "(preset)";

        auto* popupMenuLnF { new ModernLookAndFeel };
        juce::PopupMenu pm;
        pm.setLookAndFeel (popupMenuLnF);
        pm.addSectionHeader (juce::String (presetNumber) + " - " + presetName);
        pm.addSeparator ();
        pm.addItem ("Copy", thisPresetExists, false, [this, presetNumber = presetNumber] () { copyPreset (presetNumber); });
        pm.addItem ("Paste", copyBufferPresetProperties.getName ().isNotEmpty (), false, [this, presetNumber = presetNumber] () { pastePreset (presetNumber); });
        pm.addItem ("Delete", thisPresetExists, false, [this, presetNumber = presetNumber] () { deletePreset (presetNumber); });
        {
            juce::PopupMenu moveMenu;
            moveMenu.addItem ("Up", presetNumber > 1, false, [this, presetNumber] () { swapPresets (presetNumber, presetNumber - 1); });
            moveMenu.addItem ("Down", presetNumber < kMaxPresets, false, [this, presetNumber] () { swapPresets (presetNumber, presetNumber + 1); });
            pm.addSubMenu ("Move", moveMenu, thisPresetExists);
        }
        pm.showMenuAsync ({}, [this, popupMenuLnF] (int) { delete popupMenuLnF; });
    }
    else
    {
        // don't reload the currently loaded preset
        const auto requestedPresetNumber { std::get<0> (presetInfoList[static_cast<size_t> (row)]) };
        if (requestedPresetNumber == selectedPresetNumber)
            return;

        auto completeSelection = [safe = juce::Component::SafePointer<PresetListComponent> (this), requestedPresetNumber, folder = currentFolder] ()
        {
            if (safe != nullptr && safe->currentFolder == folder) safe->selectPreset (requestedPresetNumber);
        };

        if (overwritePresetOrCancel != nullptr)
        {
            presetListBox.selectRow (lastSelectedPresetIndex, false, true);
            overwritePresetOrCancel (completeSelection, [this] () {});
        }
        else
        {
            completeSelection ();
        }
    }
}
