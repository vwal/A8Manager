#include "CurrentFolderComponent.h"
#include "oolib/Properties/PersistentRootProperties.h"
#include "oolib/Properties/RuntimeRootProperties.h"

CurrentFolderComponent::CurrentFolderComponent ()
{
    addAndMakeVisible (currentFolderAndProgressLabel);
}

void CurrentFolderComponent::init (juce::ValueTree rootPropertiesVT)
{
    PersistentRootProperties persistentRootProperties (rootPropertiesVT, PersistentRootProperties::WrapperType::client, PersistentRootProperties::EnableCallbacks::no);
    appProperties.wrap (persistentRootProperties.getValueTree (), AppProperties::WrapperType::client, AppProperties::EnableCallbacks::yes);
    appProperties.onMostRecentFolderChange = [this] (juce::String folderName)
    {
        currentFolderAndProgressLabel.setText (getFolderAndProgressString (folderName, directoryDataProperties.getProgress ()), juce::NotificationType::dontSendNotification);
    };
    RuntimeRootProperties runtimeRootProperties (rootPropertiesVT, RuntimeRootProperties::WrapperType::client, RuntimeRootProperties::EnableCallbacks::no);
    directoryDataProperties.wrap (runtimeRootProperties.getValueTree (), DirectoryDataProperties::WrapperType::client, DirectoryDataProperties::EnableCallbacks::yes);
    directoryDataProperties.onProgressChange = [this] (juce::String progressString)
    {
        currentFolderAndProgressLabel.setText (getFolderAndProgressString (appProperties.getMostRecentFolder (), progressString), juce::NotificationType::dontSendNotification);
    };

    currentFolderAndProgressLabel.setText (getFolderAndProgressString (appProperties.getMostRecentFolder (), directoryDataProperties.getProgress ()), juce::NotificationType::dontSendNotification);
}

void CurrentFolderComponent::selectRootFolder ()
{
    if (! appProperties.isValid () || fileChooser != nullptr || folderChangePending)
        return;

    fileChooser = std::make_unique<juce::FileChooser> ("Select a root folder to scan (confirm to start scanning)",
                                                     juce::File (appProperties.getMostRecentFolder ()), "");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safeThis = juce::Component::SafePointer<CurrentFolderComponent> (this)] (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;
            const auto results { chooser.getURLResults () };
            safeThis->fileChooser.reset ();
            if (results.size () == 1 && results [0].isLocalFile ())
                safeThis->requestRootFolder (results [0].getLocalFile ());
        });
}

void CurrentFolderComponent::requestRootFolder (juce::File folder)
{
    if (! appProperties.isValid () || folderChangePending || ! folder.isDirectory () ||
        folder == juce::File (appProperties.getMostRecentFolder ()))
        return;

    folderChangePending = true;
    const auto safeThis { juce::Component::SafePointer<CurrentFolderComponent> (this) };
    auto completeSelection = [safeThis, folder] ()
    {
        if (safeThis == nullptr)
            return;
        safeThis->folderChangePending = false;
        // A destination can disappear while the unsaved-preset warning is open.
        if (folder.isDirectory ())
            safeThis->appProperties.setMostRecentFolder (folder.getFullPathName ());
    };
    auto cancelSelection = [safeThis] ()
    {
        if (safeThis == nullptr)
            return;
        safeThis->folderChangePending = false;
    };
    if (overwritePresetOrCancel != nullptr)
        overwritePresetOrCancel (completeSelection, cancelSelection);
    else
        completeSelection ();
}

juce::String CurrentFolderComponent::getFolderAndProgressString (juce::String folder, juce::String progress)
{
    if (! progress.isEmpty ())
        return folder + " (" + progress + ")";
    else
        return folder;
}

void CurrentFolderComponent::resized ()
{
    currentFolderAndProgressLabel.setBounds (getLocalBounds ());
}
