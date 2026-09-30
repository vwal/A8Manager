#include "MainComponent.h"
#include "ModernTheme.h"
#include "../SystemServices.h"
#include "../Assimil8or/Audio/AudioPlayer.h"
#include "../Assimil8or/PresetFolderCopy.h"
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
    designerSaveState.setName ("designer-preset-save-pending");
    designerSaveState.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    designerSaveState.setBorderSize ({ 0, 8, 0, 8 });
    designerSaveState.setMinimumHorizontalScale (1.0f);
    designerSaveState.setJustificationType (juce::Justification::centred);
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
    designerSave.onClick = [this] () { saveDesignerPreset (); };
    designerFolder.onClick = [this] () { currentFolderComponent.selectRootFolder (); };
    waveformWorkspace.onGetAssignmentContext = [this] () { return presetSession.snapshot (); };
    waveformWorkspace.onApplyAssignment = [this] (const WaveformWorkspace::AssignmentContext& context, const WaveformDesign::AssignmentResult& generated)
    {
        const auto result { presetSession.apply (context, generated.editedPreset) };
        updateSharedPresetHeader ();
        return result;
    };
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
        waveformWorkspace.isAuditionPausedForRange = [player] () { return player->isWaveformAuditionPausedForRange (); };
    }

    fileViewComponent.onAudioFileSelected = [this] (juce::File audioFile) { assimil8orEditorComponent.receiveSampleLoadRequest (audioFile); };
    startTimerHz (5);
}

MainComponent::~MainComponent ()
{
    stopTimer ();
    // Let an in-flight copy finish before member teardown. The worker only owns
    // a detached snapshot; its posted completion uses a Component::SafePointer.
    if (designerSaveThread.joinable ()) designerSaveThread.join ();
}

void MainComponent::saveDesignerPreset ()
{
    if (designerSaveBusy) return;
    const auto source { presetSession.snapshot () };
    if (! source) return;
    if (PresetFolderCopy::isNamedPresetFolder (source->folder, source->preset))
    {
        const auto result { assimil8orEditorComponent.savePreset () };
        waveformWorkspace.showPresetSaveStatus (result.wasOk () ? "Saved preset in " + source->folder.getFullPathName ()
            : result.getErrorMessage (), result.failed ());
        updateSharedPresetHeader ();
        return;
    }

    if (designerSaveThread.joinable ()) designerSaveThread.join ();
    designerSaveBusy = true;
    updateSharedPresetHeader ();
    waveformWorkspace.showPresetSaveStatus ("Saving a self-contained A8 preset folder... Original files stay in place.");
    auto safe = juce::Component::SafePointer<MainComponent> (this);
    try
    {
        // Large sample copies must not block interaction or audio. Never pass a
        // live ValueTree or read UI/session state from this worker.
        designerSaveThread = std::thread ([safe, capturedSource = *source]
        {
            juce::File folder;
            auto result { juce::Result::fail ("The preset folder could not be created.") };
            try { result = PresetFolderCopy::createOrUpdate (capturedSource.folder, capturedSource.preset, folder); }
            catch (...) { result = juce::Result::fail ("Unexpected failure while creating the preset folder."); }
            juce::MessageManager::callAsync ([safe, capturedSource, result, folder]
            {
                if (safe != nullptr) safe->completeDesignerPresetSave (capturedSource, result, folder);
            });
        });
    }
    catch (...)
    {
        completeDesignerPresetSave (*source, juce::Result::fail ("Could not start the preset folder copy. Please try Save again."), {});
    }
}

void MainComponent::completeDesignerPresetSave (const PresetEditSession::Snapshot& source, const juce::Result& result, juce::File savedFolder)
{
    designerSaveBusy = false;
    if (result.failed ())
    {
        const auto message { result.getErrorMessage () + " The current preset has not been saved by this operation; your edits and original files remain in place." };
        waveformWorkspace.showPresetSaveStatus (message, true);
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Preset folder save failed", message);
    }
    else if (! presetSession.matches (source))
    {
        const auto message { "Saved the captured preset in " + savedFolder.getFullPathName ()
            + ". The preset or folder changed during the copy, so your current edits were not saved or replaced. Original files remain in place." };
        waveformWorkspace.showPresetSaveStatus (message, true);
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Preset copy saved; current edits unchanged", message);
    }
    else
    {
        // Only a successful copy of the still-current snapshot may mark the
        // source document saved. A failed original save keeps its dirty baseline.
        const auto sourceResult { assimil8orEditorComponent.savePreset () };
        waveformWorkspace.showPresetSaveStatus (sourceResult.wasOk ()
            ? "Saved A8 preset folder: " + savedFolder.getFullPathName () + ". Original samples and working folder preserved."
            : "Created " + savedFolder.getFullPathName () + ", but saving the original preset failed: " + sourceResult.getErrorMessage (), sourceResult.failed ());
    }
    updateSharedPresetHeader ();
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
    const auto source { presetSession.snapshot () };
    const auto bound { source.has_value () };
    const auto needsPortableCopy { source && ! PresetFolderCopy::isNamedPresetFolder (source->folder, source->preset) };
    const auto dirty { presetSession.isDirty () };
    designerPresetLabel.setText ("Preset " + juce::String (preset.getId ()), juce::dontSendNotification);
    if (designerPresetName.getText () != preset.getName ()) designerPresetName.setText (preset.getName (), false);
    designerPresetName.setEnabled (bound);
    designerSave.setEnabled (bound && ! designerSaveBusy && (dirty || needsPortableCopy));
    designerSave.setTooltip (needsPortableCopy
        ? "Save the preset and create or refresh its self-contained PRnn - name folder for the SD card, even when there are no unsaved edits. Original samples and the working folder are preserved."
        : "Save changes in this already-open named preset folder. No additional nested folder is created.");
    designerSaveState.setText (designerSaveBusy ? (dirty ? "SAVE IS PENDING (saving...)" : "Saving A8 folder...")
        : dirty ? "SAVE IS PENDING" : ! bound ? "Select a preset slot"
        : needsPortableCopy ? "Saved / refresh copy" : "Saved / unchanged", juce::dontSendNotification);
    designerSaveState.setTooltip (dirty
        ? (designerSaveBusy ? "The A8 preset folder is being saved. Preset edits remain unsaved until the copy and original preset save both succeed."
                           : "This preset has changes that have not been saved. Click SAVE to preserve them; generating or assigning a waveform alone does not save the preset.")
        : (designerSaveBusy ? "Refreshing the self-contained A8 preset folder. The working preset has no unsaved changes."
                           : "The working preset has no unsaved changes."));
    Theme::bindColour (designerSaveState, juce::Label::textColourId, [dirty]
        { return dirty ? (Theme::isLight () ? juce::Colours::white : Theme::field) : Theme::muted; });
    Theme::bindColour (designerSaveState, juce::Label::backgroundColourId, [dirty]
        { return dirty ? Theme::warning : juce::Colours::transparentBlack; });
    Theme::bindColour (designerSaveState, juce::Label::outlineColourId, [dirty]
        { return dirty ? Theme::warning : juce::Colours::transparentBlack; });
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
