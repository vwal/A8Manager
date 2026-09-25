#pragma once

#include <JuceHeader.h>
#include "../AppProperties.h"
#include "oolib/Directory/DirectoryDataProperties.h"

class CurrentFolderComponent : public juce::Component
{
public:
    CurrentFolderComponent ();

    void init (juce::ValueTree rootPropertiesVT);
    void selectRootFolder ();
    // Called only with a completed selection, never while browsing the chooser.
    void requestRootFolder (juce::File folder);

    std::function<void (std::function<void ()>, std::function<void ()>)> overwritePresetOrCancel;

private:
    AppProperties appProperties;
    DirectoryDataProperties directoryDataProperties;

    juce::Label currentFolderAndProgressLabel;
    bool folderChangePending { false };
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::String getFolderAndProgressString (juce::String folder, juce::String);

    void resized () override;
};
