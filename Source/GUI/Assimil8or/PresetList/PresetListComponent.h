#pragma once

#include <JuceHeader.h>
#include "../../../AppProperties.h"
#include "../../../Assimil8or/Preset/PresetProperties.h"
#include "../../../Assimil8or/PresetFileOperations.h"
#include "oolib/Directory/DirectoryDataProperties.h"
#include "oolib/Directory/DirectoryValueTree.h"
#include "oolib/Core/LambdaThread.h"

const auto kMaxPresets { 199 };
class PresetListComponent : public juce::Component,
                            private juce::ListBoxModel,
                            private juce::Timer
{
public:
    PresetListComponent ();
    ~PresetListComponent () = default;
    void init (juce::ValueTree rootPropertiesVT);

    std::function<void (std::function<void ()>, std::function<void ()>)> overwritePresetOrCancel;

private:
    friend struct PresetLoopRangeTestAccess;
    std::function<void (const juce::StringArray&)> notifyLoopRepairs;
    using PresetInfo = PresetFileOperations::PresetInfo;
    using PresetInfoList = PresetFileOperations::PresetInfoList;

    AppProperties appProperties;
    DirectoryDataProperties directoryDataProperties;
    // resolved in init (), once DirectoryValueTree has published the types Main registered
    int presetFileTypeId { DirectoryValueTree::unknownTypeId };
    PresetProperties presetProperties;
    PresetProperties unEditedPresetProperties;
    PresetProperties copyBufferPresetProperties;

    juce::ToggleButton showAllPresets { "Show All" };
    juce::ListBox presetListBox { {}, this };
    PresetInfoList presetInfoList {};
    int numPresets { kMaxPresets };
    juce::File currentFolder;
    juce::File previousFolder;
    int lastSelectedPresetIndex { -1 };
    int selectedPresetNumber { 1 };
    std::atomic<bool> requestedShowAllPresets { true };
    LambdaThread checkPresetsThread { "CheckPresetsThread", 100 };

    void copyPreset (int presetNumber);
    void checkPresets (bool showAll);
    void deletePreset (int presetNumber);
    juce::File getPresetFile (int presetNumber);
    void forEachPresetFile (std::function<bool (juce::File presetFile, int index)> presetFileCallback);
    bool loadPresetFile (juce::File presetFile, juce::ValueTree vt);
    void loadDefault (int presetNumber);
    void loadFirstPreset ();
    bool loadPreset (juce::File presetFile);
    void movePresetUp (int row);
    void movePresetDown (int row);
    void requestPresetCheck ();
    void swapPresets (int fromSlot, int toSlot);
    void selectPreset (int presetNumber);
    void pastePreset (int presetNumber);

    void resized () override;
    int getNumRows () override;
    juce::String getTooltipForRow (int row) override;
    void listBoxItemClicked (int row, const juce::MouseEvent& me) override;
    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void timerCallback () override;
};
