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
    presetSession.init (rootPropertiesVT);

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
    for (auto* component : std::initializer_list<juce::Component*> { &designerPresetLabel, &designerPresetName, &designerSaveState, &designerSave, &designerFolder })
        addChildComponent (component);
    designerPresetLabel.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    Theme::bindColour (designerPresetLabel, juce::Label::textColourId, [] { return Theme::accent; });
    designerPresetName.setFont (juce::FontOptions (16.0f));
    designerPresetName.setInputRestrictions (12, " !\"#$%^&'()#+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~");
    designerPresetName.setSelectAllWhenFocused (true);
    designerPresetName.setTooltip ("The A8 preset name, shared with Samples. Save writes the selected preset slot.");
    designerPresetName.onTextChange = [this] ()
    {
        if (! presetSession.snapshot ()) return;
        PresetProperties preset (presetSession.getEdit (), PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        preset.setName (designerPresetName.getText (), false);
    };
    designerSave.onClick = [this] ()
    {
        if (presetSession.snapshot ()) assimil8orEditorComponent.savePreset ();
        updateSharedPresetHeader ();
    };
    designerFolder.onClick = [this] () { currentFolderComponent.selectRootFolder (); };
    waveformWorkspace.onGetAssignmentContext = [this] () { return presetSession.snapshot (); };
    waveformWorkspace.onApplyAssignment = [this] (const WaveformWorkspace::AssignmentContext& context, const WaveformDesign::AssignmentResult& generated)
    {
        const auto result { presetSession.apply (context, generated.editedPreset) };
        updateSharedPresetHeader ();
        return result;
    };
    waveformWorkspace.onClose = [this] () { showWaveformWorkspace (false); };
    assimil8orEditorComponent.onRecallWaveform = [this] (int channel, int zone)
    {
        showWaveformWorkspace (true);
        waveformWorkspace.recallAssigned (channel, zone);
    };
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
    startTimerHz (5);
}

void MainComponent::showWaveformWorkspace (bool show)
{
    if (showingDesigner != show)
    {
        // Reparent the actual list, not a second selector/document. Slot changes
        // in either workspace use its existing unsaved-preset confirmation.
        if (show)
        {
            presetListEditorSplitter.setComponents (nullptr, &assimil8orEditorComponent);
            addAndMakeVisible (presetListComponent);
        }
        else
            presetListEditorSplitter.setComponents (&presetListComponent, &assimil8orEditorComponent);
    }
    showingDesigner = show;
    if (show)
    {
        waveformWorkspace.setInitialFolder (juce::File (appProperties.getMostRecentFolder ()));
        waveformWorkspace.refreshAssignmentContext ();
    }
    currentFolderComponent.setVisible (true);
    topAndBottomSplitter.setVisible (! show);
    bottomStatusWindow.setVisible (! show);
    waveformWorkspace.setVisible (show);
    for (auto* component : std::initializer_list<juce::Component*> { &designerPresetLabel, &designerPresetName, &designerSaveState, &designerSave, &designerFolder })
        component->setVisible (show);
    updateSharedPresetHeader ();
    resized ();
    if (onWorkspaceChanged != nullptr)
        onWorkspaceChanged (show);
}

void MainComponent::updateSharedPresetHeader ()
{
    PresetProperties preset (presetSession.getEdit (), PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
    if (! preset.isValid ()) return;
    const auto bound { presetSession.snapshot ().has_value () };
    designerPresetLabel.setText ("Preset " + juce::String (preset.getId ()), juce::dontSendNotification);
    if (designerPresetName.getText () != preset.getName ()) designerPresetName.setText (preset.getName (), false);
    designerPresetName.setEnabled (bound);
    designerSave.setEnabled (bound && presetSession.isDirty ());
    designerSaveState.setText (! bound ? "Select a preset slot" : presetSession.isDirty () ? "Unsaved changes" : "Saved / unchanged", juce::dontSendNotification);
    const auto dirty { presetSession.isDirty () };
    Theme::bindColour (designerSaveState, juce::Label::textColourId, [dirty] { return dirty ? Theme::accent : Theme::muted; });
    if (displayedRevision != presetSession.getRevision ())
    {
        displayedRevision = presetSession.getRevision ();
        if (showingDesigner) waveformWorkspace.refreshAssignmentContext ();
    }
}

void MainComponent::timerCallback () { if (showingDesigner) updateSharedPresetHeader (); }

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
    auto localBounds { getLocalBounds () };
    currentFolderComponent.setBounds (localBounds.removeFromTop (30));
    bottomStatusWindow.setBounds (localBounds.removeFromBottom (toolWindowHeight));
    localBounds.reduce (3, 3);
    topAndBottomSplitter.setBounds (localBounds);
    if (guiProperties.isValid ()) saveLayoutChanges ();
    midiConfigComponent.setBounds (localBounds);
    if (showingDesigner)
    {
        auto designerBounds { getLocalBounds ().withTrimmedTop (34).reduced (6, 0) };
        presetListComponent.setBounds (designerBounds.removeFromLeft (165));
        designerBounds.removeFromLeft (8);
        auto header { designerBounds.removeFromTop (34).reduced (4, 3) };
        designerPresetLabel.setBounds (header.removeFromLeft (92));
        designerPresetName.setBounds (header.removeFromLeft (180));
        header.removeFromLeft (8);
        designerFolder.setBounds (header.removeFromRight (118));
        header.removeFromRight (8);
        designerSave.setBounds (header.removeFromRight (76));
        designerSaveState.setBounds (header);
        waveformWorkspace.setBounds (designerBounds);
    }
}
