#include "GUI/CurrentFolderComponent.h"
#include "oolib/Properties/PersistentRootProperties.h"
#include "oolib/Properties/RuntimeRootProperties.h"
#include <iostream>
#include <stdexcept>

void testRootFolderSelection ()
{
    auto check = [] (bool condition, const char* message)
    {
        if (! condition) throw std::runtime_error (message);
    };

    // Unique, empty temporary directories; no user sample folders or preferences.
    juce::TemporaryFile firstPath, secondPath, removedPath, regularFile;
    const auto first { firstPath.getFile () };
    const auto second { secondPath.getFile () };
    const auto removed { removedPath.getFile () };
    check (first.createDirectory ().wasOk () && second.createDirectory ().wasOk () &&
           removed.createDirectory ().wasOk () && regularFile.getFile ().create ().wasOk (), "Create test fixtures");

    juce::ValueTree root { "Root" };
    PersistentRootProperties persistent (root, PersistentRootProperties::WrapperType::owner, PersistentRootProperties::EnableCallbacks::no);
    RuntimeRootProperties runtime (root, RuntimeRootProperties::WrapperType::owner, RuntimeRootProperties::EnableCallbacks::no);
    AppProperties preferences;
    preferences.wrap (persistent.getValueTree (), AppProperties::WrapperType::owner, AppProperties::EnableCallbacks::yes);
    preferences.setMostRecentFolder (first.getFullPathName ());
    DirectoryDataProperties directory (runtime.getValueTree (), DirectoryDataProperties::WrapperType::owner, DirectoryDataProperties::EnableCallbacks::yes);
    directory.setRootFolder (first.getFullPathName (), false);
    auto scanRequests { 0 };
    directory.onStartScanChange = [&] () { ++scanRequests; };
    // Mirror Main.cpp's folder-change -> scanner notification wiring without
    // launching a background scan. Intermediate roots must never be published.
    preferences.onMostRecentFolderChange = [&] (juce::String folder)
    {
        directory.setRootFolder (folder, false);
        directory.triggerStartScan (true);
    };

    auto component { std::make_unique<CurrentFolderComponent> () };
    component->init (root);
    std::function<void ()> confirm, cancel;
    auto prompts { 0 };
    component->overwritePresetOrCancel = [&] (auto onConfirm, auto onCancel)
    {
        ++prompts;
        confirm = std::move (onConfirm);
        cancel = std::move (onCancel);
    };
    component->requestRootFolder ({}); // native chooser Cancel has no selection
    component->requestRootFolder (first);
    component->requestRootFolder (regularFile.getFile ());
    check (scanRequests == 0 && prompts == 0, "Cancelled, unchanged or non-directory selections must not scan or prompt");

    component->requestRootFolder (second);
    check (prompts == 1 && scanRequests == 0 && directory.getRootFolder () == first.getFullPathName (),
           "Selection must wait for the unsaved-edit decision");
    component->requestRootFolder (removed);
    check (prompts == 1, "Do not open overlapping confirmations");
    cancel ();
    check (scanRequests == 0 && preferences.getMostRecentFolder () == first.getFullPathName (), "Cancel must keep the current folder");

    component->requestRootFolder (second);
    confirm ();
    check (scanRequests == 1 && directory.getRootFolder () == second.getFullPathName (), "Confirm must scan only the final destination, once");
    component->requestRootFolder (second);
    check (prompts == 2 && scanRequests == 1, "Selecting the current root must not rescan");

    component->requestRootFolder (removed);
    check (removed.deleteFile (), "Remove empty temporary destination");
    confirm ();
    check (scanRequests == 1, "A destination removed during confirmation must not be scanned");
    component->requestRootFolder (removed);
    check (prompts == 3, "Missing folders must not prompt");

    component->overwritePresetOrCancel = nullptr;
    component->requestRootFolder (first);
    check (scanRequests == 2 && directory.getRootFolder () == first.getFullPathName (), "Clean selection must switch directly");

    component->overwritePresetOrCancel = [&] (auto onConfirm, auto onCancel)
    {
        confirm = std::move (onConfirm);
        cancel = std::move (onCancel);
    };
    component->requestRootFolder (second);
    component.reset ();
    confirm ();
    cancel ();
    check (scanRequests == 2, "Delayed callbacks must be harmless after the component is destroyed");
    std::cout << "PASS: confirmed root selection, scan notifications, cancel, unsaved-edit guard, invalid/same folders and callback lifetime\n";
}
