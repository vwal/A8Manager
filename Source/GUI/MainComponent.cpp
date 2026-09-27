#include "MainComponent.h"
#include "ModernTheme.h"
#include "../SystemServices.h"
#include "../Assimil8or/Audio/AudioPlayer.h"
#include "oolib/Properties/PersistentRootProperties.h"
#include "oolib/Properties/RuntimeRootProperties.h"

const auto toolWindowHeight { 30 };

//  +----------------+-------------+-----------------------------------+
//  |Current Path                                                      |
//  +----------------+-------------+-----------------------------------+
//  | ..             | Preset 1    |                                   |
//  | folderX        | Preset 2    |                                   |
//  | folder34       | Preset 3    |                                   |
//  | fileAbc        | Preset 4    |                                   |
//  | fileElif       | Preset ...  |                                   |
// ...              ...           ...                                 ...
//  |                | Preset 199  |                                   |
//  +----------------+-------------+-----------------------------------+
//  | Type | Fix | Message (X items)                                   |
//  +------+-----+-----------------------------------------------------+
//  |      |     |                                                     |
//  |      |     |                                                     |
//  |      |     |                                                     |
//  |      |     |                                                     |
//  |      |     |                                                     |
//  +------+-----+-----------------------------------------------------+
//  |  Tool Bar                                                        |
//  +------------------------------------------------------------------+

MainComponent::MainComponent (juce::ValueTree rootPropertiesVT)
{
    setSize (1117, 609);

    PersistentRootProperties persistentRootProperties (rootPropertiesVT, PersistentRootProperties::WrapperType::client, PersistentRootProperties::EnableCallbacks::no);
    guiProperties.wrap (persistentRootProperties.getValueTree (), GuiProperties::WrapperType::client, GuiProperties::EnableCallbacks::no);
    appProperties.wrap (persistentRootProperties.getValueTree (), AppProperties::WrapperType::client, AppProperties::EnableCallbacks::no);

    fileViewComponent.overwritePresetOrCancel = [this] (std::function<void ()> overwriteFunction, std::function<void ()> cancelFunction)
    {
        assimil8orEditorComponent.overwritePresetOrCancel (overwriteFunction, cancelFunction);
    };
    currentFolderComponent.overwritePresetOrCancel = fileViewComponent.overwritePresetOrCancel;
    fileViewComponent.onSelectRootFolder = [this] () { currentFolderComponent.selectRootFolder (); };
    presetListComponent.overwritePresetOrCancel = [this] (std::function<void ()> overwriteFunction, std::function<void ()> cancelFunction)
    {
        assimil8orEditorComponent.overwritePresetOrCancel (overwriteFunction, cancelFunction);
    };

    assimil8orEditorComponent.init (rootPropertiesVT);
    assimil8orValidatorComponent.init (rootPropertiesVT);
    fileViewComponent.init (rootPropertiesVT);
    presetListComponent.init (rootPropertiesVT);
    bottomStatusWindow.init (rootPropertiesVT);
    currentFolderComponent.init (rootPropertiesVT);
    midiConfigComponent.init (rootPropertiesVT);

    presetListEditorSplitter.setComponents (&presetListComponent, &assimil8orEditorComponent);
    presetListEditorSplitter.setHorizontalSplit (false);

    folderBrowserEditorSplitter.setComponents (&fileViewComponent, &presetListEditorSplitter);
    folderBrowserEditorSplitter.setHorizontalSplit (false);

    topAndBottomSplitter.setComponents (&folderBrowserEditorSplitter, &assimil8orValidatorComponent);
    topAndBottomSplitter.setHorizontalSplit (true);

    presetListEditorSplitter.onLayoutChange = [this] () { saveLayoutChanges (); };
    folderBrowserEditorSplitter.onLayoutChange = [this] () { saveLayoutChanges (); };
    topAndBottomSplitter.onLayoutChange = [this] () { saveLayoutChanges (); };

    restoreLayout ();

    addAndMakeVisible (currentFolderComponent);
    addAndMakeVisible (topAndBottomSplitter);
    addChildComponent (midiConfigComponent);
    addAndMakeVisible (bottomStatusWindow);
    addChildComponent (waveformWorkspace);
    waveformWorkspace.onClose = [this] () { showWaveformWorkspace (false); };
    waveformWorkspace.onMatchDuration = [this] (int region) { return assimil8orEditorComponent.getSelectedDuration (region); };
    waveformWorkspace.onOpenExportedFolder = [this] (juce::File folder)
    {
        showWaveformWorkspace (false);
        currentFolderComponent.requestRootFolder (folder);
    };
    RuntimeRootProperties runtime (rootPropertiesVT, RuntimeRootProperties::WrapperType::client, RuntimeRootProperties::EnableCallbacks::no);
    SystemServices services (runtime.getValueTree (), SystemServices::WrapperType::client, SystemServices::EnableCallbacks::no);
    if (auto* player { services.getAudioPlayer () }; player != nullptr)
    {
        // AudioPlayer belongs to the application and outlives this workspace.
        waveformWorkspace.onAuditionPayload = [player] (WaveformAudition::PayloadPtr payload) { player->setWaveformAuditionPayload (std::move (payload)); };
        waveformWorkspace.onStartAudition = [player] () { return player->startWaveformAudition (); };
        waveformWorkspace.onStopAudition = [player] () { player->stopWaveformAudition (); };
        waveformWorkspace.onAuditionMonitorChange = [player] (double decibels, double semitones) { return player->setWaveformMonitor (decibels, semitones); };
        waveformWorkspace.isAuditionActive = [player] () { return player->isWaveformAuditionActive (); };
        waveformWorkspace.onAudioSettings = [player] () { player->showAudioSettings (); };
    }

    fileViewComponent.onAudioFileSelected = [this] (juce::File audioFile) { assimil8orEditorComponent.receiveSampleLoadRequest (audioFile); };
}

void MainComponent::showWaveformWorkspace (bool show)
{
    if (show)
        waveformWorkspace.setInitialFolder (juce::File (appProperties.getMostRecentFolder ()));
    currentFolderComponent.setVisible (! show);
    topAndBottomSplitter.setVisible (! show);
    bottomStatusWindow.setVisible (! show);
    waveformWorkspace.setVisible (show);
    if (onWorkspaceChanged != nullptr)
        onWorkspaceChanged (show);
}

void MainComponent::restoreLayout ()
{
    const auto [pane1Size, pane2Size, pane3Size] { guiProperties.getPaneSizes () };
    presetListEditorSplitter.setSplitOffset (pane1Size);
    folderBrowserEditorSplitter.setSplitOffset (pane2Size);
    topAndBottomSplitter.setSplitOffset (pane3Size);
}

void MainComponent::saveLayoutChanges ()
{
    // Preserve a useful parameter-grid size; the outer viewport handles small windows.
    if (folderBrowserEditorSplitter.getWidth () > 1000)
    {
        folderBrowserEditorSplitter.setSplitOffset (std::clamp (folderBrowserEditorSplitter.getSplitOffset (), 120,
            juce::jmax (120, folderBrowserEditorSplitter.getWidth () - 940)));
        presetListEditorSplitter.setSplitOffset (std::clamp (presetListEditorSplitter.getSplitOffset (), 120,
            juce::jmax (120, presetListEditorSplitter.getWidth () - 810)));
    }
    if (topAndBottomSplitter.getHeight () >= 700)
        topAndBottomSplitter.setSplitOffset (std::clamp (topAndBottomSplitter.getSplitOffset (), 620, topAndBottomSplitter.getHeight () - 80));
    const auto splitter1Size { presetListEditorSplitter.getSplitOffset () };
    const auto splitter2Size { folderBrowserEditorSplitter.getSplitOffset () };
    const auto splitter3Size { topAndBottomSplitter.getSplitOffset () };
    guiProperties.setPaneSizes (splitter1Size, splitter2Size, splitter3Size, false);
}

void MainComponent::paint ([[maybe_unused]] juce::Graphics& g)
{
    g.fillAll (Theme::background);
}

void MainComponent::resized ()
{
    waveformWorkspace.setBounds (getLocalBounds ());
    auto localBounds { getLocalBounds () };
    currentFolderComponent.setBounds (localBounds.removeFromTop (30));
    bottomStatusWindow.setBounds (localBounds.removeFromBottom (toolWindowHeight));
    localBounds.reduce (3, 3);
    topAndBottomSplitter.setBounds (localBounds);
    if (guiProperties.isValid ()) saveLayoutChanges ();
    midiConfigComponent.setBounds (localBounds);
}
