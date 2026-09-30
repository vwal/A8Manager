#pragma once

#include <JuceHeader.h>
#include "CurrentFolderComponent.h"
#include "GuiProperties.h"
#include "BottomStatusWindow.h"
#include "Assimil8or/Editor/Assimil8orEditorComponent.h"
#include "Assimil8or/FileView/FileViewComponent.h"
#include "Assimil8or/MidiConfig/MidiConfigComponent.h"
#include "Assimil8or/PresetList/PresetListComponent.h"
#include "Assimil8or/Validator/Assimil8orValidatorComponent.h"
#include "oolib/GUI/SplitWindowComponent.h"
#include "ModernTheme.h"
#include "WaveformWorkspace.h"
#include "PresetEditSession.h"
#include <thread>

class WorkspaceSplitter : public SplitWindowComponent
{
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::border);
        if (getHorizontalSplit ())
            g.fillRect (4, getSplitOffset (), getWidth () - 8, 3);
        else
            g.fillRect (getSplitOffset (), 4, 3, getHeight () - 8);
    }
};

class MainComponent : public juce::Component, private juce::Timer
{
public:
    MainComponent (juce::ValueTree rootPropertiesVT);
    ~MainComponent () override;

    void showWaveformWorkspace (bool show);
    std::function<void (bool)> onWorkspaceChanged;

private:
    Assimil8orEditorComponent assimil8orEditorComponent;
    Assimil8orValidatorComponent assimil8orValidatorComponent;
    GuiProperties guiProperties;
    CurrentFolderComponent currentFolderComponent;
    MidiConfigComponent midiConfigComponent;
    FileViewComponent fileViewComponent;
    PresetListComponent presetListComponent;
    WorkspaceSplitter topAndBottomSplitter;
    WorkspaceSplitter presetListEditorSplitter;
    WorkspaceSplitter folderBrowserEditorSplitter;
    BottomStatusWindow bottomStatusWindow;
    WaveformWorkspace waveformWorkspace;
    AppProperties appProperties;
    PresetEditSession presetSession;
    juce::Label designerPresetLabel, designerSaveState;
    juce::TextEditor designerPresetName;
    juce::TextButton designerSave { "SAVE" }, designerFolder { "Root folder..." };
    bool showingDesigner { false };
    bool designerSaveBusy { false };
    std::thread designerSaveThread;
    uint64_t displayedRevision { 0 };

    juce::TooltipWindow tooltipWindow;

    void restoreLayout ();
    void saveLayoutChanges ();
    void updateSharedPresetHeader ();
    void saveDesignerPreset ();
    void completeDesignerPresetSave (const PresetEditSession::Snapshot& source, const juce::Result& result, juce::File savedFolder);
    void timerCallback () override;

    void resized () override;
    void paint (juce::Graphics& g) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
