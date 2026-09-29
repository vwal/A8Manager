#include "FileViewComponent.h"
#include "../../ModernTheme.h"
#include "../../../SystemServices.h"
#include "../../../Assimil8or/Assimil8orPreset.h"
#include "../../../Assimil8or/FileTypeHelpers.h"
#include "../../../Assimil8or/SafeRename.h"
#include "../../../Assimil8or/Audio/SafeAudioImport.h"
#include "oolib/Properties/PersistentRootProperties.h"
#include "oolib/Properties/RuntimeRootProperties.h"
#include "oolib/ValueTree/ValueTreeHelpers.h"
#include "oolib/Debug/WatchDogTimer.h"

#define LOG_FILE_VIEW 0
#if LOG_FILE_VIEW
#define LogFileView(text) juce::Logger::outputDebugString (text);
#else
#define LogFileView(text) ;
#endif

const auto kDialogTextEditorName { "foldername" };

FileViewComponent::FileViewComponent ()
{
    optionsButton.setButtonText ("OPTIONS");
    optionsButton.setTooltip ("Folder and File options");
    optionsButton.onClick = [this] ()
    {
        juce::PopupMenu optionsMenu;
        optionsMenu.addItem ("Select Root Folder", onSelectRootFolder != nullptr, false, [this] ()
        {
            if (onSelectRootFolder != nullptr)
                onSelectRootFolder ();
        });
        optionsMenu.addItem ("New Folder", true, false, [this] () { newFolder (); });
        optionsMenu.addItem ("Remove Unused Samples", true, false, [this] ()
        {
            juce::AlertWindow::showOkCancelBox (juce::AlertWindow::WarningIcon, "REMOVE UNUSED SAMPLES",
                                                "Unused Samples are samples that are not used by any presets in the current preset folder.\r\n\r\nAre you sure you want to delete the unused samples in'" + appProperties.getMostRecentFolder () + "'", "YES", "NO", nullptr,
                                                juce::ModalCallbackFunction::create ([this] (int option)
                                                                                    {
                                                                                        if (option == 0) // no
                                                                                            return;
                                                                                        deleteUnusedSamples ();
                                                                                    }));
        });
        optionsMenu.showMenuAsync ({}, [this] (int) {});
    };
    addAndMakeVisible (optionsButton);
    addAndMakeVisible (directoryContentsListBox);
    showAllFiles.setToggleState (false, juce::NotificationType::dontSendNotification);
    showAllFiles.setButtonText ("Show All");
    showAllFiles.setTooltip ("Show all files, or show just Assimil8or files");
    showAllFiles.onClick = [this] () { updateFromNewDataThread.start (); };
    addAndMakeVisible (showAllFiles);

    updateFromNewDataThread.onThreadLoop = [this] ()
    {
        updateFromNewData ();
        return false;
    };
}

void FileViewComponent::showRenameDialog (juce::File source, juce::String proposedName)
{
    const auto title { source.isDirectory () ? "RENAME FOLDER" : "RENAME FILE" };
    renameAlertWindow = std::make_unique<juce::AlertWindow> (title,
        "Enter the new name for '" + source.getFileName () + "'", juce::MessageBoxIconType::NoIcon);
    renameAlertWindow->addTextEditor (kDialogTextEditorName, proposedName.isEmpty () ? source.getFileName () : proposedName);
    renameAlertWindow->addButton ("RENAME", 1, juce::KeyPress (juce::KeyPress::returnKey));
    renameAlertWindow->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    renameAlertWindow->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<FileViewComponent> (this), source] (int result)
        {
            if (safe == nullptr || safe->renameAlertWindow == nullptr) return;
            const auto name { safe->renameAlertWindow->getTextEditorContents (kDialogTextEditorName) };
            safe->renameAlertWindow.reset ();
            if (result == 0) return;
            const auto renamed { SafeRename::apply (source, name) };
            if (renamed.failed ())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Rename failed", renamed.getErrorMessage (), {}, nullptr,
                    juce::ModalCallbackFunction::create ([safe, source, name] (int)
                    {
                        if (safe != nullptr) safe->showRenameDialog (source, name);
                    }));
            }
            else safe->directoryDataProperties.triggerStartScan (false);
        }));
}

void FileViewComponent::init (juce::ValueTree rootPropertiesVT)
{
    LogFileView ("FileViewComponent::init");
    PersistentRootProperties persistentRootProperties (rootPropertiesVT, PersistentRootProperties::WrapperType::client, PersistentRootProperties::EnableCallbacks::no);
    appProperties.wrap (persistentRootProperties.getValueTree (), AppProperties::WrapperType::client, AppProperties::EnableCallbacks::yes);

    RuntimeRootProperties runtimeRootProperties (rootPropertiesVT, RuntimeRootProperties::WrapperType::client, RuntimeRootProperties::EnableCallbacks::no);
    SystemServices systemServices { runtimeRootProperties.getValueTree (), SystemServices::WrapperType::client, SystemServices::EnableCallbacks::yes };
    audioManager = systemServices.getAudioManager ();

    directoryDataProperties.wrap (runtimeRootProperties.getValueTree (), DirectoryDataProperties::WrapperType::client, DirectoryDataProperties::EnableCallbacks::yes);
    audioFileTypeId = directoryDataProperties.getFileTypeId (FileTypeHelpers::kAudioFileTypeName);
    directoryDataProperties.onRootScanComplete = [this] ()
    {
        LogFileView ("FileViewComponent/onRootScanComplete");
        isRootFolder = juce::File (directoryDataProperties.getRootFolder ()).getParentDirectory () == juce::File (directoryDataProperties.getRootFolder ());
        updateFromNewDataThread.start ();
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
//                 isRootFolder = juce::File (directoryDataProperties.getRootFolder ()).getParentDirectory () == juce::File (directoryDataProperties.getRootFolder ());
//                 updateFromNewDataThread.start ();
//             }
//             break;
//         }
//     };

    updateFromNewDataThread.start ();
}

void FileViewComponent::updateFromNewData ()
{
    LogFileView ("FileViewComponent::updateFromNewData ()");
    WatchdogTimer timer;
    timer.start (10000);
    buildQuickLookupList ();
    juce::MessageManager::callAsync ([this] ()
    {
        directoryContentsListBox.updateContent ();
        directoryContentsListBox.repaint ();
    });
    //juce::Logger::outputDebugString ("FileViewComponent::updateFromNewData () - elapsed time: " + juce::String (timer.getElapsedTime ()));
}

void FileViewComponent::timerCallback ()
{
    const auto elapsedTime { juce::Time::currentTimeMillis () - curBlinkTime };
    if (elapsedTime > 1500)
    {
        doubleClickedRow = -1;
        curBlinkTime = 0;
        stopTimer ();
    }
    repaint ();
}

void FileViewComponent::buildQuickLookupList ()
{
    updateDirectoryListQuickLookupList->clear ();
    // this runs on the update from new data thread, so we work from a detached snapshot of the live tree
    const auto rootFolderSnapshotVT { ValueTreeHelpers::getMessageThreadSnapshot (directoryDataProperties.getRootFolderVT ()) };
    if (! rootFolderSnapshotVT.isValid ())
        return;
    ValueTreeHelpers::forEachChild (rootFolderSnapshotVT, [this] (juce::ValueTree child)
    {
        if (showAllFiles.getToggleState ())
        {
            updateDirectoryListQuickLookupList->emplace_back (child);
        }
        else if (FolderProperties::isFolderVT (child) ||
                 static_cast<int> (child.getProperty (FileProperties::TypePropertyId)) == audioFileTypeId)
        {
            updateDirectoryListQuickLookupList->emplace_back (child);
        }
        return true;
    });
    juce::ScopedLock sl (directoryListQuickLookupListLock);
    if (curDirectoryListQuickLookupList == &directoryListQuickLookupListA)
    {
        curDirectoryListQuickLookupList = &directoryListQuickLookupListB;
        updateDirectoryListQuickLookupList = &directoryListQuickLookupListA;
    }
    else
    {
        curDirectoryListQuickLookupList = &directoryListQuickLookupListA;
        updateDirectoryListQuickLookupList = &directoryListQuickLookupListB;
    }
}

void FileViewComponent::newFolder ()
{
    newAlertWindow = std::make_unique<juce::AlertWindow> ("NEW FOLDER", "Enter the name for new folder", juce::MessageBoxIconType::NoIcon);
    newAlertWindow->addTextEditor (kDialogTextEditorName, {}, {});
    newAlertWindow->addButton ("CREATE", 1, juce::KeyPress (juce::KeyPress::returnKey, 0, 0));
    newAlertWindow->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey, 0, 0));
    auto* textEdtitor { newAlertWindow->getTextEditor (kDialogTextEditorName) };
    auto* createButton { newAlertWindow->getButton ("CREATE") };
    auto* cancelButton { newAlertWindow->getButton ("CANCEL") };
    textEdtitor->setExplicitFocusOrder (1);
    createButton->setExplicitFocusOrder (2);
    cancelButton->setExplicitFocusOrder (3);
    newAlertWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int option)
    {
        newAlertWindow->exitModalState (option);
        newAlertWindow->setVisible (false);
        if (option == 1) // ok
        {
            auto newFolderName { newAlertWindow->getTextEditorContents (kDialogTextEditorName) };
            auto newFolder { juce::File (appProperties.getMostRecentFolder ()).getChildFile (newFolderName) };
            newFolder.createDirectory ();
            // TODO handle error
        }
        newAlertWindow.reset ();
    }));
}

int FileViewComponent::getNumRows ()
{
    juce::ScopedLock sl (directoryListQuickLookupListLock);
    return static_cast<int> (curDirectoryListQuickLookupList->size () + (isRootFolder ? 0 : 1));
}

juce::ValueTree FileViewComponent::getDirectoryEntryVT (int row)
{
    juce::ScopedLock sl (directoryListQuickLookupListLock);
    const auto quickLookupIndex { row - (isRootFolder ? 0 : 1) };
    return (*curDirectoryListQuickLookupList) [quickLookupIndex];
}

void FileViewComponent::paintListBoxItem (int row, juce::Graphics& g, int width, int height, [[maybe_unused]] bool rowIsSelected)
{
    if (row >= getNumRows ())
        return;

    if (rowIsSelected)
        lastSelectedRow = row;

    juce::Colour textColor { Theme::text };
    juce::String fileListItem;
    if (! isRootFolder && row == 0)
    {
        fileListItem = " >  ..";
    }
    else
    {
        const auto directoryEntryVT { getDirectoryEntryVT (row) };
        juce::String filePrefix;
        if (directoryEntryVT.getType ().toString () == "Folder")
        {
            filePrefix = "> ";
        }
        else if (static_cast<int> (directoryEntryVT.getProperty (FileProperties::TypePropertyId)) == audioFileTypeId)
        {
            filePrefix = "-  ";
            textColor = Theme::accent;
            if (curBlinkTime != 0 && doubleClickedRow == row)
            {
                filePrefix += "Loading ";
                textColor = textColor.brighter (0.7f);
            }
        }
        else
        {
            filePrefix = "   ";
            textColor = textColor.darker (0.4f);
        }
        auto file { juce::File (directoryEntryVT.getProperty ("name").toString ()) };
        fileListItem = " " + filePrefix + file.getFileName ();
    }

    g.fillAll (rowIsSelected ? Theme::accent.withAlpha (0.16f) : Theme::field);
    g.setColour (Theme::border);
    g.fillRect (width - 1, 0, 1, height);
    g.setColour (textColor);
    g.drawText (fileListItem, juce::Rectangle<float>{ 0.0f, 0.0f, (float) width, (float) height }, juce::Justification::centredLeft, true);
}

juce::String FileViewComponent::getTooltipForRow (int row)
{
    if (row >= getNumRows ())
        return {};

    if (! isRootFolder && row == 0)
        return juce::File (directoryDataProperties.getRootFolder ()).getParentDirectory ().getFullPathName ();
    else
    {
        const auto directoryEntryVT { getDirectoryEntryVT (row) };

        juce::String toolTip { juce::File (directoryEntryVT.getProperty ("name").toString ()).getFileName () };
        if (static_cast<int> (directoryEntryVT.getProperty (FileProperties::TypePropertyId)) == audioFileTypeId)
        {
            const auto sampleRate { static_cast<int> (directoryEntryVT.getProperty ("sampleRate")) };
            if (auto errorString { directoryEntryVT.getProperty ("error").toString () }; errorString != "")
                toolTip += juce::String ("\r") + "Error: " + errorString;
            toolTip += juce::String ("\r") + "DataType: " + directoryEntryVT.getProperty ("dataType").toString ();
            toolTip += juce::String ("\r") + "BitDepth: " + juce::String (static_cast<int> (directoryEntryVT.getProperty ("bitDepth")));
            toolTip += juce::String ("\r") + "Channels: " + juce::String (static_cast<int> (directoryEntryVT.getProperty ("numChannels")));
            toolTip += juce::String ("\r") + "SampleRate: " + juce::String (sampleRate);
            toolTip += juce::String ("\r") + "Length: " + juce::String (static_cast<double> (static_cast<juce::int64> (directoryEntryVT.getProperty ("lengthSamples"))) / sampleRate, 2);
        }
        return toolTip;
    }
}

void FileViewComponent::listBoxItemClicked (int row, [[maybe_unused]] const juce::MouseEvent& me)
{
    if (row >= getNumRows ())
        return;

    auto getEntryType = [this, row] ()
    {
        const auto directoryEntryVT { getDirectoryEntryVT (row) };
        return static_cast<int> (directoryEntryVT.getProperty (FileProperties::TypePropertyId));
    };
    auto isEntryAFolder = [this, row] ()
    {
        return FolderProperties::isFolderVT (getDirectoryEntryVT (row));
    };
    auto getEntryFile = [this, row] ()
    {
        const auto directoryEntryVT { getDirectoryEntryVT (row) };
        return juce::File (directoryEntryVT.getProperty ("name").toString ());
    };

    auto isUpFolder = [this, row] ()
    {
        return ! isRootFolder && row == 0;
    };

    if (me.mods.isPopupMenu ())
    {
        if (isUpFolder ())
            return;

        if (isEntryAFolder ())
        {
            auto directoryEntry { getEntryFile () };
            auto* popupMenuLnF { new ModernLookAndFeel };
            juce::PopupMenu pm;
            pm.setLookAndFeel (popupMenuLnF);
            pm.addSectionHeader (directoryEntry.getFileName ());
            pm.addSeparator ();
            pm.addItem ("Rename", true, false, [safe = juce::Component::SafePointer<FileViewComponent> (this), directoryEntry] ()
            {
                if (safe != nullptr) safe->showRenameDialog (directoryEntry);
            });
            pm.addItem ("Delete", true, false, [this, directoryEntry] ()
            {
                juce::AlertWindow::showOkCancelBox (juce::AlertWindow::WarningIcon, "DELETE FOLDER",
                                                    "Are you sure you want to delete the folder '" + directoryEntry.getFileName () + "'", "YES", "NO", nullptr,
                                                    juce::ModalCallbackFunction::create ([this, directoryEntry] (int option)
                                                                                         {
                                                                                             if (option == 0) // no
                                                                                                 return;
                                                                                             if (! directoryEntry.deleteFile ())
                                                                                             {
                                                                                                // TODO handle delete error
                                                                                             }
                                                                                         }));
            });
            pm.showMenuAsync ({}, [this, popupMenuLnF] (int) { delete popupMenuLnF; });
        }
        else if (getEntryType () == audioFileTypeId)
        {
            auto directoryEntry { getEntryFile () };
            auto* popupMenuLnF { new ModernLookAndFeel };
            juce::PopupMenu pm;
            pm.setLookAndFeel (popupMenuLnF);
            pm.addSectionHeader (directoryEntry.getFileName ());
            pm.addSeparator ();
            pm.addItem ("Rename", true, false, [safe = juce::Component::SafePointer<FileViewComponent> (this), directoryEntry] ()
            {
                if (safe != nullptr) safe->showRenameDialog (directoryEntry);
            });
            pm.addItem ("Delete", true, false, [this, directoryEntry] ()
            {
                juce::AlertWindow::showOkCancelBox (juce::AlertWindow::WarningIcon, "DELETE FILE",
                                                    "Are you sure you want to delete the file '" + directoryEntry.getFileName () + "'", "YES", "NO", nullptr,
                                                    juce::ModalCallbackFunction::create ([this, directoryEntry] (int option)
                                                                                            {
                                                                                                if (option == 0) // no
                                                                                                    return;
                                                                                                if (! directoryEntry.deleteFile ())
                                                                                                {
                                                                                                // TODO handle delete error
                                                                                                }
                                                                                            }));
            });
            if (getEntryType () == audioFileTypeId)
            {
                const auto directoryEntryVT { getDirectoryEntryVT (row) };
                if (static_cast<int> (directoryEntryVT.getProperty ("numChannels")) == 2)
                {
                    juce::PopupMenu stereoConvertMenu;
                    stereoConvertMenu.addItem ("To Mono", true, false, [this, directoryEntry] ()
                    {
                        // mix to mono
                        audioManager->mixStereoToMono (directoryEntry);
                    });
                    stereoConvertMenu.addItem ("Split", true, false, [this, directoryEntry] ()
                    {
                        // split into two mono L/R files
                        audioManager->splitStereoIntoTwoMono (directoryEntry);
                    });
                    pm.addSubMenu ("Stereo Convert", stereoConvertMenu, true);
                }
            }

            pm.showMenuAsync ({}, [this, popupMenuLnF] (int) { delete popupMenuLnF; });
        }
    }
    else
    {
        if (! isUpFolder () && ! isEntryAFolder ())
            return;

        auto completeSelection = [this, row, isUpFolder, getEntryFile] ()
        {
            if (isUpFolder ())
            {
                appProperties.setMostRecentFolder (juce::File (directoryDataProperties.getRootFolder ()).getParentDirectory ().getFullPathName ());
            }
            else
            {
                auto folder { getEntryFile () };
                appProperties.setMostRecentFolder (folder.getFullPathName ());
            }
        };

        if (overwritePresetOrCancel != nullptr)
        {
            auto cancelSelection = [this] ()
            {
                directoryContentsListBox.selectRow (lastSelectedRow, false, true);
            };

            overwritePresetOrCancel (completeSelection, cancelSelection);
        }
        else
        {
            completeSelection ();
        }
    }
}

void FileViewComponent::listBoxItemDoubleClicked (int row, [[maybe_unused]] const juce::MouseEvent& me)
{
    if (row >= getNumRows () || (! isRootFolder && row == 0))
        return;

    const auto directoryEntryVT { getDirectoryEntryVT (row) };
    if (FileProperties::isFileVT (directoryEntryVT) && static_cast<int> (directoryEntryVT.getProperty (FileProperties::TypePropertyId)) == audioFileTypeId)
    {
        doubleClickedRow = row;
        curBlinkTime = juce::Time::currentTimeMillis ();
        startTimer (125);
        if (onAudioFileSelected != nullptr)
            onAudioFileSelected (juce::File (directoryEntryVT.getProperty ("name").toString ()));
    }
}

void FileViewComponent::resized ()
{
    auto localBounds { getLocalBounds () };
    localBounds.reduce (3, 3);
    auto toolRow { localBounds.removeFromTop (25) };
    optionsButton.setBounds (toolRow.removeFromLeft (70));
    toolRow.removeFromLeft (5);
    showAllFiles.setBounds (toolRow);

    localBounds.removeFromTop (3);
    directoryContentsListBox.setBounds (localBounds);
}

void FileViewComponent::paintOverChildren (juce::Graphics& g)
{
    if (draggingFilesCount > 0)
    {
        auto localBounds { getLocalBounds () };
        juce::Colour fillColor { juce::Colours::white };
        float activeAlpha { 0.7f };
        g.setColour (fillColor.withAlpha (activeAlpha));
        g.fillRect (directoryContentsListBox.getBounds ());
        g.setColour (supportedFile ? juce::Colours::black : juce::Colours::red);
        localBounds.reduce (5, 0);
        g.drawFittedText (dropMsg, localBounds, juce::Justification::centred, 10);
    }
}

void FileViewComponent::deleteUnusedSamples ()
{
    // build list of files in the preset files
    std::set<juce::File> samplesInPresets;
    ValueTreeHelpers::forEachChild (directoryDataProperties.getRootFolderVT (), [this, &samplesInPresets] (juce::ValueTree child)
    {
        const auto name { child.getProperty ("name").toString () };
        auto file { juce::File (name) };
        if (FileTypeHelpers::isPresetFile (file))
        {
            const auto presetNumber { FileTypeHelpers::getPresetNumberFromName (file) };
            if (presetNumber < 1 || presetNumber > 199 || file.getFileNameWithoutExtension ().startsWith (FileTypeHelpers::kPresetFileNamePrefix) == false)
                return true;

            juce::StringArray fileContents;
            file.readLines (fileContents);
            Assimil8orPreset assimil8orPreset;
            assimil8orPreset.parse (fileContents);

            // TODO - how should we handle errors in the preset file
            if (auto presetErrorList { assimil8orPreset.getParseErrorsVT () }; presetErrorList.getNumChildren () > 0)
            {
                ValueTreeHelpers::forEachChildOfType (presetErrorList, "ParseError", [this] (juce::ValueTree childVT)
                {
                    const auto parseErrorType { childVT.getProperty ("type").toString () };
                    const auto parseErrorDescription { childVT.getProperty ("description").toString () };
                    return true;
                });
            }
            PresetProperties presetProperties (assimil8orPreset.getPresetVT (), PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
            if (presetProperties.isValid ())
            {
                presetProperties.forEachChannel ([this, &samplesInPresets, &file] (juce::ValueTree channelVT, int)
                {
                    ChannelProperties channelProperties (channelVT, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
                    channelProperties.forEachZone ([this, &samplesInPresets, &file] (juce::ValueTree zoneVT, int)
                    {
                        ZoneProperties zoneProperties (zoneVT, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                        const auto sampleFileName { zoneProperties.getSample () };
                        if (sampleFileName.isEmpty ())
                            return true;
                        const auto sampleFile { file.getParentDirectory ().getChildFile (sampleFileName) };
                        samplesInPresets.emplace (sampleFile);
                        return true;
                    });
                    return true;
                });
            }
            else
            {
                // TODO - how should we handle errors in the preset file
            }
        }
        return true;
    });

    // iterate over sample files, and remove any that aren't in the list
    ValueTreeHelpers::forEachChild (directoryDataProperties.getRootFolderVT (), [this, &samplesInPresets] (juce::ValueTree child)
    {
        const auto name { child.getProperty ("name").toString () };
        auto file { juce::File (name) };
        if (static_cast<int> (child.getProperty (FileProperties::TypePropertyId)) == audioFileTypeId)
        {
            if (samplesInPresets.find (file) == samplesInPresets.end ())
                file.moveToTrash ();
        }
        return true;
    });
}

void FileViewComponent::updateDropInfo (const juce::StringArray& files)
{
    supportedFile = true;
    for (auto& fileName : files)
    {
        auto draggedFile { juce::File (fileName) };
        if (! audioManager->isA8ManagerSupportedAudioFile (draggedFile))
            supportedFile = false;
    }
    if (supportedFile)
    {
        dropMsg = juce::String (draggingFilesCount) + " files to copy";
    }
    else
    {
        dropMsg = (draggingFilesCount == 1 ? "Unsupported file type" : "One, or more, unsupported file types");
    }
}

void FileViewComponent::resetDropInfo ()
{
    draggingFilesCount = 0;
    dropMsg = {};
}

void FileViewComponent::importSamples (const juce::StringArray& files)
{
    const juce::File folder { appProperties.getMostRecentFolder () };
    juce::StringArray failures;
    bool importedAny { false };
    for (const auto& fileName : files)
    {
        const juce::File source { fileName };
        // Files already in this folder are left alone, as before.
        if (source.getParentDirectory () == folder) continue;
        juce::File imported;
        const auto result { audioManager ? SafeAudioImport::importFile (*audioManager, source, folder, imported)
                                        : juce::Result::fail ("The audio service is unavailable.") };
        if (result.failed ()) failures.add (source.getFileName () + ": " + result.getErrorMessage ());
        else importedAny = true;
    }
    if (importedAny) directoryDataProperties.triggerStartScan (false);
    if (! failures.isEmpty ())
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Import Failed", failures.joinIntoString ("\n\n"));
}

bool FileViewComponent::isInterestedInFileDrag ([[maybe_unused]] const juce::StringArray& files)
{
    // we do this check in the fileDragEnter and fileDragMove handlers, presenting more info regarding the drop operation
    return true;
}

void FileViewComponent::filesDropped (const juce::StringArray& files, int /*x*/, int /*y*/)
{

    if (supportedFile)
        importSamples (files);
    resetDropInfo ();
    repaint ();
}

void FileViewComponent::fileDragEnter (const juce::StringArray& files, int /*x*/, int /*y*/)
{
    draggingFilesCount = files.size ();
    updateDropInfo (files);
    repaint ();
}

void FileViewComponent::fileDragMove (const juce::StringArray& files, int /*x*/, int /*y*/)
{
    updateDropInfo (files);
    repaint ();
}

void FileViewComponent::fileDragExit (const juce::StringArray&)
{
    resetDropInfo ();
    repaint ();
}
