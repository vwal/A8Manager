#include "Assimil8orValidatorComponent.h"
#include "../../ModernTheme.h"
#include "RenameDialogComponent.h"
#include "LocateFileComponent.h"
#include "../../../SystemServices.h"
#include "../../../Assimil8or/Audio/SafeAudioImport.h"
#include "../../../Assimil8or/Validator/ValidatorResultListProperties.h"
#include "oolib/Properties/RuntimeRootProperties.h"

Assimil8orValidatorComponent::Assimil8orValidatorComponent ()
{
    setOpaque (true);

    addAndMakeVisible (validatorToolWindow);

    validationResultsListBox.setClickingTogglesRowSelection (false);
    validationResultsListBox.setColour (juce::ListBox::outlineColourId, juce::Colours::grey);
    validationResultsListBox.setOutlineThickness (1);
    validationResultsListBox.getHeader ().addColumn ("Status", Columns::resultType, 60, 60, 60, juce::TableHeaderComponent::visible);
    validationResultsListBox.getHeader ().addColumn ("Fix", Columns::fix, 60, 60, 60, juce::TableHeaderComponent::visible);
    validationResultsListBox.getHeader ().addColumn ("Message", Columns::text, 100, 10, 3000, juce::TableHeaderComponent::visible);
    validationResultsListBox.getHeader ().setStretchToFitActive (true);
    addAndMakeVisible (validationResultsListBox);

    setupViewList ();
}

Assimil8orValidatorComponent::~Assimil8orValidatorComponent ()
{
    if (renameDialog != nullptr)
    {
        renameDialog->exitModalState (0);

        // we are shutting down: can't wait for the message manager
        // to eventually delete this
        delete renameDialog;
    }
    if (locateDialog!= nullptr)
    {
        locateDialog->exitModalState (0);

        // we are shutting down: can't wait for the message manager
        // to eventually delete this
        delete locateDialog;
    }
}

void Assimil8orValidatorComponent::init (juce::ValueTree rootPropertiesVT)
{
    RuntimeRootProperties runtimeRootProperties (rootPropertiesVT, RuntimeRootProperties::WrapperType::client, RuntimeRootProperties::EnableCallbacks::no);
    directoryDataProperties.wrap (runtimeRootProperties.getValueTree (), DirectoryDataProperties::WrapperType::client, DirectoryDataProperties::EnableCallbacks::yes);

    SystemServices systemServices { runtimeRootProperties.getValueTree (), SystemServices::WrapperType::client, SystemServices::EnableCallbacks::yes };
    audioManager = systemServices.getAudioManager ();

    validatorComponentProperties.wrap (runtimeRootProperties.getValueTree (), ValidatorComponentProperties::WrapperType::owner, ValidatorComponentProperties::EnableCallbacks::yes);
    auto updateFromViewChange = [this] ()
    {
        setupViewList ();
        validatorResultsQuickLookupList.clear ();
        buildQuickLookupList ();
        validationResultsListBox.updateContent ();
        updateHeader ();
    };
    validatorComponentProperties.onViewInfoChange = [this, updateFromViewChange] (bool)
    {
        updateFromViewChange ();
    };
    validatorComponentProperties.onViewWarningChange = [this, updateFromViewChange] (bool)
    {
        updateFromViewChange ();
    };
    validatorComponentProperties.onViewErrorChange = [this, updateFromViewChange] (bool)
    {
        updateFromViewChange ();
    };
    validatorComponentProperties.onConvertAllTrigger = [this] ()
    {
        autoConvertAll ();
    };
    validatorComponentProperties.onLocateAllTrigger = [this] ()
    {
        autoLocateAll ();
    };
    validatorComponentProperties.onRenameAllTrigger = [this] ()
    {
        autoRenameAll ();
    };

    validatorProperties.wrap (runtimeRootProperties.getValueTree (), ValidatorProperties::WrapperType::client, ValidatorProperties::EnableCallbacks::yes);
    validatorProperties.onScanStatusChanged = [this] (juce::String scanStatus)
    {
        localCopyOfValidatorResultsList = validatorProperties.getValidatorResultListVT ().createCopy ();
        juce::MessageManager::callAsync ([this, scanStatus] ()
        {
            updateListFromScan (scanStatus);
        });
    };
    localCopyOfValidatorResultsList = validatorProperties.getValidatorResultListVT ().createCopy ();
    updateListFromScan ("idle");

    validatorToolWindow.init (rootPropertiesVT);
}

void Assimil8orValidatorComponent::updateListFromScan (juce::String scanStatus)
{
    validatorResultsQuickLookupList.clear ();
    if (scanStatus == "idle")
        buildQuickLookupList ();

    validationResultsListBox.updateContent ();
    updateHeader ();
    // TODO - this is a crazy work around because when I am getting the initial list, a horizontal scroll bar is appearing.
    //        the only experiment that worked was doing this
    setSize (getWidth (), getHeight () + 1);
    setSize (getWidth (), getHeight () - 1);
}

void Assimil8orValidatorComponent::setupViewList ()
{
    viewList.clearQuick ();
    if (validatorComponentProperties.getViewInfo ())
        viewList.add (ValidatorResultProperties::ResultTypeInfo);
    if (validatorComponentProperties.getViewWarning ())
        viewList.add (ValidatorResultProperties::ResultTypeWarning);
    if (validatorComponentProperties.getViewError ())
        viewList.add (ValidatorResultProperties::ResultTypeError);
}

void Assimil8orValidatorComponent::updateHeader ()
{
    validationResultsListBox.getHeader ().setColumnName (Columns::text, "Message (" + juce::String (validatorResultsQuickLookupList.size ()) + " of " +
                                                                        juce::String (totalInfoItems + totalWarningItems + totalErrorItems) + " items | " +
                                                                        "Info: " + juce::String (totalInfoItems) +
                                                                        " | Warning : "+ juce::String (totalWarningItems) +
                                                                        " | Error : "+ juce::String (totalErrorItems) +")");
    validationResultsListBox.repaint ();
}

void Assimil8orValidatorComponent::buildQuickLookupList ()
{
    totalInfoItems = 0;
    totalWarningItems = 0;
    totalErrorItems = 0;
    renameFilesCount = 0;
    renameFoldersCount = 0;
    convertCount = 0;
    missingFileCount = 0;

    if (! localCopyOfValidatorResultsList.isValid ())
        return;

    ValidatorResultListProperties validatorResultListProperties (localCopyOfValidatorResultsList,
                                                                 ValidatorResultListProperties::WrapperType::client, ValidatorResultListProperties::EnableCallbacks::no);
    // iterate over the state message list, adding each one to the quick list
    validatorResultListProperties.forEachResult ([this] (juce::ValueTree validatorResultVT)
    {
        ValidatorResultProperties validatorResultProperties (validatorResultVT, ValidatorResultProperties::WrapperType::client, ValidatorResultProperties::EnableCallbacks::no);
        totalInfoItems += (validatorResultProperties.getType () == ValidatorResultProperties::ResultTypeInfo ? 1 : 0);
        totalWarningItems += (validatorResultProperties.getType () == ValidatorResultProperties::ResultTypeWarning? 1 : 0);
        totalErrorItems += (validatorResultProperties.getType () == ValidatorResultProperties::ResultTypeError ? 1 : 0);
        validatorResultProperties.forEachFixerEntry ([this, &validatorResultVT] (juce::ValueTree fixerEntryVT)
        {
            FixerEntryProperties fixerEntryProperties (validatorResultVT, FixerEntryProperties::WrapperType::client, FixerEntryProperties::EnableCallbacks::no);
            if (fixerEntryProperties.getType () == FixerEntryProperties::FixerTypeRenameFile)
                ++renameFilesCount;
            else if (fixerEntryProperties.getType () == FixerEntryProperties::FixerTypeRenameFolder)
                ++renameFoldersCount;
            else if (fixerEntryProperties.getType () == FixerEntryProperties::FixerTypeConvert)
                ++convertCount;
            else if (fixerEntryProperties.getType () == FixerEntryProperties::FixerTypeFileNotFound)
                ++missingFileCount;
            else
                jassertfalse;
            return true;
        });
        if (viewList.contains (validatorResultProperties.getType ()))
            validatorResultsQuickLookupList.emplace_back (validatorResultVT);
        return true;
    });
    validatorComponentProperties.enableConvertAll (convertCount, false);
    validatorComponentProperties.enableLocateAll (missingFileCount, false);
    validatorComponentProperties.enableRenameAll (renameFilesCount > 0 || renameFoldersCount > 0, false);
}

void Assimil8orValidatorComponent::paint ([[maybe_unused]] juce::Graphics& g)
{
    g.fillAll (Theme::panel);
}

juce::String Assimil8orValidatorComponent::getCellTooltip (int rowNumber, int columnId)
{
    if (columnId != Columns::text)
        return {};

    if (rowNumber >= validatorResultsQuickLookupList.size ())
        return {};

    ValidatorResultProperties validatorResultProperties (validatorResultsQuickLookupList [rowNumber],
                                                         ValidatorResultProperties::WrapperType::client, ValidatorResultProperties::EnableCallbacks::no);
    auto getPrefix = [this, &validatorResultProperties] () -> juce::String
    {
        if (! validatorResultProperties.getValueTree ().hasProperty ("fullFileName"))
            return {};

        juce::File file (validatorResultProperties.getValueTree ().getProperty ("fullFileName").toString ());
        return file.getParentDirectory ().getFileName () + file.getSeparatorString () + file.getFileName () + "\r\n";
    };
    return getPrefix () + validatorResultProperties.getText ();
}
void Assimil8orValidatorComponent::resized ()
{
    auto localBounds { getLocalBounds () };
    validatorToolWindow.setBounds (localBounds.removeFromTop (25));
    validationResultsListBox.setBounds (localBounds);
}

int Assimil8orValidatorComponent::getNumRows ()
{
    return (int) validatorResultsQuickLookupList.size ();
}

void Assimil8orValidatorComponent::paintRowBackground (juce::Graphics& g, int rowNumber, int /*width*/, int /*height*/, bool rowIsSelected)
{
    if (rowNumber >= validatorResultsQuickLookupList.size ())
        return;

    if (rowIsSelected)
    {
        g.fillAll (Theme::accent.withAlpha (0.16f));
    }
    else
    {
        auto unSelectedBackgroundColour { Theme::field };
        if (rowNumber % 2)
            unSelectedBackgroundColour = unSelectedBackgroundColour.interpolatedWith (juce::Colours::black, 0.1f);
        g.fillAll (unSelectedBackgroundColour);
    }
}

void Assimil8orValidatorComponent::paintCell (juce::Graphics& g, int rowNumber, int columnId, int width, int height, [[maybe_unused]] bool rowIsSelected)
{
    if (rowNumber < validatorResultsQuickLookupList.size ())
    {
        g.setColour (Theme::border);
        g.fillRect (width - 1, 0, 1, height);
        ValidatorResultProperties validatorResultProperties (validatorResultsQuickLookupList [rowNumber],
                                                             ValidatorResultProperties::WrapperType::client, ValidatorResultProperties::EnableCallbacks::no);
        auto textColor { Theme::text };
        if (validatorResultProperties.getType () == ValidatorResultProperties::ResultTypeWarning)
            textColor = Theme::warning;
        else if (validatorResultProperties.getType () == ValidatorResultProperties::ResultTypeError)
            textColor = Theme::error;

        juce::String outputText { "  " };
        switch (columnId)
        {
        case Columns::resultType:
        {
            outputText += validatorResultProperties.getType ();
        }
        break;
        case Columns::fix:
        {
            if (validatorResultProperties.getNumFixerEntries () != 0)
            {
                outputText += "Fix";
            }
        }
        break;
        case Columns::text:
        {
            outputText += validatorResultProperties.getText ();
        }
        break;
        }
        g.setColour (textColor);
        g.drawText (outputText, juce::Rectangle<float>{ 0.0f, 0.0f, (float) width, (float) height }, juce::Justification::centredLeft, true);
    }
}

juce::Component* Assimil8orValidatorComponent::refreshComponentForCell (int rowNumber, [[maybe_unused]] int columnId, bool rowIsSelected,
    juce::Component* existingComponentToUpdate)
{
    if (rowIsSelected)
    {
        jassert (rowNumber < validatorResultsQuickLookupList.size ());
    }
    else if (rowNumber < validatorResultsQuickLookupList.size ())
    {
        if (existingComponentToUpdate != nullptr)
            delete existingComponentToUpdate;
        return nullptr;
    }

    jassert (existingComponentToUpdate == nullptr);
    return nullptr;
}

void Assimil8orValidatorComponent::handleLocatedFiles (std::vector<std::tuple <juce::File, juce::File>>& locatedFiles)
{
    if (directoryDataProperties.getRootFolder () != locateRootFolder) return;
    std::vector<juce::File> copiedFiles;
    for (auto [sourceFile, destinationFile] : locatedFiles)
    {
        const auto copied { SafeAudioImport::copyNew (sourceFile, destinationFile) };
        if (copied.wasOk ())
        {
            copiedFiles.emplace_back (destinationFile);
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Copy Failed",
                copied.getErrorMessage ());
        }
    }
    // Neither input is guaranteed to be sorted; set_difference could keep an
    // already-copied destination and then try to overwrite it on the next pass.
    filesToLocate.erase (std::remove_if (filesToLocate.begin (), filesToLocate.end (), [&copiedFiles] (const juce::File& file)
    {
        return std::find (copiedFiles.begin (), copiedFiles.end (), file) != copiedFiles.end ();
    }), filesToLocate.end ());
    if (! copiedFiles.empty ()) directoryDataProperties.triggerStartScan (false);
    if (! filesToLocate.empty () && locateDialog != nullptr)
    {
        if (auto* content { dynamic_cast<LocateFileComponent*> (locateDialog->getContentComponent ()) }; content != nullptr)
            locateFilesInitialDirectory = content->getCurFolder ();
        triggerAsyncUpdate ();
    }
}

// handleAsyncUpdate handles displaying the locate dialog, and copying any missing files it can locate, and then redisplaying the dialog if there are more to be located
void Assimil8orValidatorComponent::handleAsyncUpdate ()
{
    if (filesToLocate.empty () || directoryDataProperties.getRootFolder () != locateRootFolder) return;
    juce::DialogWindow::LaunchOptions options;
    const juce::Component::SafePointer<Assimil8orValidatorComponent> safe { this };
    auto locateComponent { std::make_unique<LocateFileComponent> (filesToLocate, locateFilesInitialDirectory, [safe] (std::vector<std::tuple <juce::File, juce::File>> locatedFiles)
    {
        if (safe == nullptr) return;
        safe->handleLocatedFiles (locatedFiles);
        if (safe->locateDialog != nullptr) safe->locateDialog->exitModalState (0);
    },
    [safe] ()
    {
        if (safe != nullptr && safe->locateDialog != nullptr) safe->locateDialog->exitModalState (0);
    }) };
    options.content.setOwned (locateComponent.release ());

    juce::Rectangle<int> area (0, 0, 500, 400);

    options.content->setSize (area.getWidth (), area.getHeight ());
    options.dialogTitle = "Locate Missing Files";
    options.dialogBackgroundColour = Theme::panel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = true;
    options.componentToCentreAround = this->getParentComponent ();

    locateDialog = options.launchAsync ();
}

void Assimil8orValidatorComponent::rename (juce::File file, int maxLength)
{
    // bring up a dialog showing the old name, a field for typing the new name (length constrained), and an ok/cancel button
    juce::DialogWindow::LaunchOptions options;
    auto renameContent { std::make_unique<RenameDialogContent> (file, maxLength, [this] (bool wasRenamed)
    {
        if (wasRenamed)
            directoryDataProperties.triggerStartScan (false);
    }) };
    options.content.setOwned (renameContent.release ());

    juce::Rectangle<int> area (0, 0, 380, 110);

    options.content->setSize (area.getWidth (), area.getHeight ());
    options.dialogTitle = "Rename";
    options.dialogBackgroundColour = Theme::panel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = false;
    options.componentToCentreAround = this;

    renameDialog = options.launchAsync ();
}

void Assimil8orValidatorComponent::autoRename (juce::File fileToRename, bool doRescan)
{
    juce::File destination;
    auto result { SafeRename::automaticDestination (fileToRename, destination) };
    if (result.wasOk ()) result = SafeRename::apply (fileToRename, destination.getFileName ());
    if (result.failed ())
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Rename Failed", result.getErrorMessage ());
    else if (doRescan)
        directoryDataProperties.triggerStartScan (false);
}

void Assimil8orValidatorComponent::autoRenameAll ()
{
    if (validatorResultsQuickLookupList.empty ()) return;
    ValueTreeHelpers::forEachChildOfType (validatorResultsQuickLookupList [0].getParent (), ValidatorResultProperties::ValidatorResultTypeId, [this] (juce::ValueTree vrpVT)
    {
        ValidatorResultProperties validatorResultProperties (vrpVT, ValidatorResultProperties::WrapperType::client, ValidatorResultProperties::EnableCallbacks::no);
        validatorResultProperties.forEachFixerEntry ([this] (juce::ValueTree fixerEntryVT)
        {
            FixerEntryProperties fixerEntryProperties (fixerEntryVT, FixerEntryProperties::WrapperType::client, FixerEntryProperties::EnableCallbacks::no);
            if (fixerEntryProperties.getType () == FixerEntryProperties::FixerTypeRenameFile || fixerEntryProperties.getType () == FixerEntryProperties::FixerTypeRenameFolder)
            {
                auto file { juce::File (fixerEntryProperties.getFileName ()) };
                autoRename (file, false);
            }
            return true;
        });
        return true;
    });
    directoryDataProperties.triggerStartScan (false);
}

void Assimil8orValidatorComponent::autoConvertAll ()
{
    if (validatorResultsQuickLookupList.empty ()) return;
    ValueTreeHelpers::forEachChildOfType (validatorResultsQuickLookupList [0].getParent (), ValidatorResultProperties::ValidatorResultTypeId, [this] (juce::ValueTree vrpVT)
    {
        ValidatorResultProperties validatorResultProperties (vrpVT, ValidatorResultProperties::WrapperType::client, ValidatorResultProperties::EnableCallbacks::no);
        validatorResultProperties.forEachFixerEntry ([this] (juce::ValueTree fixerEntryVT)
        {
            FixerEntryProperties fixerEntryProperties (fixerEntryVT, FixerEntryProperties::WrapperType::client, FixerEntryProperties::EnableCallbacks::no);
            if (fixerEntryProperties.getType () == FixerEntryProperties::FixerTypeConvert)
            {
                auto file { juce::File (fixerEntryProperties.getFileName ()) };
                convert (file, false);
            }
            return true;
        });
        return true;
    });
    directoryDataProperties.triggerStartScan (false);
}

void Assimil8orValidatorComponent::autoLocateAll ()
{
    if (validatorResultsQuickLookupList.empty ()) return;
    // build list of files that need to be located
    filesToLocate.clear ();
    ValueTreeHelpers::forEachChildOfType (validatorResultsQuickLookupList [0].getParent (), ValidatorResultProperties::ValidatorResultTypeId, [this] (juce::ValueTree vrpVT)
    {
        ValidatorResultProperties validatorResultProperties (vrpVT, ValidatorResultProperties::WrapperType::client, ValidatorResultProperties::EnableCallbacks::no);
        validatorResultProperties.forEachFixerEntry ([this] (juce::ValueTree fixerEntryVT)
        {
            FixerEntryProperties fixerEntryProperties (fixerEntryVT, FixerEntryProperties::WrapperType::client, FixerEntryProperties::EnableCallbacks::no);
            if (fixerEntryProperties.getType () == FixerEntryProperties::FixerTypeFileNotFound)
            {
                auto file { juce::File (fixerEntryProperties.getFileName ()) };
                filesToLocate.emplace_back (file);
            }
            return true;
        });
        return true;
    });
    // handleAsyncUpdate handles displaying the locate dialog, and copying any missing files it can locate, and then redisplaying the dialog if there are more to be located
    locateRootFolder = directoryDataProperties.getRootFolder ();
    locateFilesInitialDirectory = locateRootFolder;
    triggerAsyncUpdate ();
}

void Assimil8orValidatorComponent::convert (juce::File file, bool reportSuccess)
{
    juce::File converted, backup;
    const auto result { audioManager ? SafeAudioImport::convertInPlace (*audioManager, file, converted, backup)
                                    : juce::Result::fail ("The audio service is unavailable.") };
    if (result.failed ())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Conversion Failed", result.getErrorMessage ());
        return;
    }
    directoryDataProperties.triggerStartScan (false);
    if (reportSuccess)
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon, "Conversion Complete",
            "Converted to " + converted.getFileName () + ". The original is recoverable at:\n" + backup.getFullPathName ());
}

void Assimil8orValidatorComponent::locate (juce::File file)
{
    // bring up a file browser to locate
    fileChooser.reset (new juce::FileChooser ("Please locate the missing file...", {}, {}));
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<Assimil8orValidatorComponent> (this), file, root = directoryDataProperties.getRootFolder ()] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || safe->directoryDataProperties.getRootFolder () != root) return;
        if (fc.getURLResults ().size () == 1 && fc.getURLResults () [0].isLocalFile ())
        {
            // copy selected file to missing file location
            const auto sourceFile { fc.getURLResults () [0].getLocalFile () };
            // TODO - this should probably be in a thread
            const auto copied { SafeAudioImport::copyNew (sourceFile, file) };
            if (copied.failed ())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Copy Failed",
                                                       copied.getErrorMessage ());
            }
            else safe->directoryDataProperties.triggerStartScan (false);
        }
    }, nullptr);
}

void Assimil8orValidatorComponent::cellClicked (int rowNumber, int columnId, const juce::MouseEvent&)
{
    if (rowNumber >= validatorResultsQuickLookupList.size ())
        return;

    const auto kMaxFileNameLength { 47 };
    const auto kMaxFolderNameLength { 31 };
    if (columnId == Columns::fix)
    {
        ValidatorResultProperties validatorResultProperties (validatorResultsQuickLookupList [rowNumber],
                                                             ValidatorResultProperties::WrapperType::client, ValidatorResultProperties::EnableCallbacks::no);
        if (validatorResultProperties.getNumFixerEntries () == 1)
        {
            // Handle one fix

            juce::ValueTree fixerEntryVT;
            // TODO - add an indexed getter, since using a for loop to get the first item seems like overkill
            validatorResultProperties.forEachFixerEntry ([this, &fixerEntryVT] (juce::ValueTree feVT)
            {
                fixerEntryVT = feVT;
                return false; // exit after the first one (since that is the one we want)
            });
            FixerEntryProperties fixerEntryProperties (fixerEntryVT, FixerEntryProperties::WrapperType::client, FixerEntryProperties::EnableCallbacks::no);

            // just do the fix
            if (fixerEntryProperties.getType () == FixerEntryProperties::FixerTypeRenameFile)
            {
                juce::PopupMenu pm;
                auto file { juce::File (fixerEntryProperties.getFileName ()) };
                pm.addItem (file.getParentDirectory ().getFileName () + file.getSeparatorString () + file.getFileName (), false, false, {});
                pm.addItem ("Rename", true, false, [this, kMaxFileNameLength, file = fixerEntryProperties.getFileName ()] () { rename (file, kMaxFileNameLength); });
                pm.addItem ("Auto Rename", true, false, [this, kMaxFileNameLength, file = fixerEntryProperties.getFileName ()] () { autoRename (juce::File (file), true); });
                pm.showMenuAsync ({}, [this] (int) {});
            }
            else if (fixerEntryProperties.getType () == FixerEntryProperties::FixerTypeRenameFolder)
            {
                rename (juce::File (fixerEntryProperties.getFileName ()), kMaxFolderNameLength);
            }
            else if (fixerEntryProperties.getType () == FixerEntryProperties::FixerTypeConvert)
            {
                convert (juce::File (fixerEntryProperties.getFileName ()));
            }
            else if (fixerEntryProperties.getType () == FixerEntryProperties::FixerTypeFileNotFound)
            {
                locate (juce::File (fixerEntryProperties.getFileName ()));
            }
            else
            {
                jassertfalse;
            }
        }
        else if (validatorResultProperties.getNumFixerEntries () > 0)
        {
            // Handle multiple fixes
            juce::PopupMenu pm;
            validatorResultProperties.forEachFixerEntry ([this, &pm, kMaxFileNameLength, kMaxFolderNameLength] (juce::ValueTree fixerEntryVT)
            {
                FixerEntryProperties fixerEntryProperties (fixerEntryVT, FixerEntryProperties::WrapperType::client, FixerEntryProperties::EnableCallbacks::no);
                auto file { juce::File (fixerEntryProperties.getFileName ()) };
                if (fixerEntryProperties.getType () == FixerEntryProperties::FixerTypeRenameFile)
                {
                    pm.addItem ("Rename " + file.getFileName (), true, false, [this, kMaxFileNameLength, file] () { rename (file, kMaxFileNameLength); });
                }
                else if (fixerEntryProperties.getType () == FixerEntryProperties::FixerTypeRenameFolder)
                {
                    pm.addItem ("Rename " + file.getFileName (), true, false, [this, kMaxFolderNameLength, file] () { rename (file, kMaxFolderNameLength); });
                }
                else if (fixerEntryProperties.getType () == FixerEntryProperties::FixerTypeConvert)
                {
                    pm.addItem ("Convert " + file.getFileName (), true, false, [this, file] () { convert (file); });
                }
                else if (fixerEntryProperties.getType () == FixerEntryProperties::FixerTypeFileNotFound)
                {
                    pm.addItem ("Find " + file.getFileName (), true, false, [this, file] () { locate (file); });
                }
                else
                {
                    jassertfalse;
                }

                return true;
            });
            pm.showMenuAsync ({}, [this] (int) {});
        }
    }
}
