#include "ZoneEditor.h"
#include "../../ModernTheme.h"
#include "../../A8NamePreview.h"
#include "Waveform/WaveformPresentation.h"
#include "FormatHelpers.h"
#include "ParameterToolTipData.h"
#include "SampleManager/SampleManagerProperties.h"
#include "../../../SystemServices.h"
#include "../../../Assimil8or/Preset/ChannelProperties.h"
#include "../../../Assimil8or/Preset/PresetProperties.h"
#include "../../../Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "../../../Assimil8or/Audio/SampleLoopSimulation.h"
#include "../../../Assimil8or/Audio/PlaybackPitch.h"
#include "oolib/Debug/DebugLog.h"
#include "oolib/Debug/DumpStack.h"
#include "oolib/GUI/ErrorHelpers.h"
#include "oolib/Properties/PersistentRootProperties.h"
#include "oolib/Properties/RuntimeRootProperties.h"

namespace
{
    void editZoneBoundary (ZoneProperties& zone, juce::int64 frames, ZoneSampleRanges::Marker marker,
                           double value, ZoneSampleRanges::Mode mode)
    {
        ChannelProperties channel (zone.getValueTree ().getParent (), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        const auto allow { channel.getAllowLoopOutsideSample () };
        ZoneSampleRanges::apply (zone, ZoneSampleRanges::edit (ZoneSampleRanges::read (zone), frames, marker, value, mode, false, allow), frames, true, allow);
    }
}

ZoneEditor::ZoneEditor ()
{
    {
        PresetProperties minPresetProperties (ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::MinParameterPresetType),
                                              PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        ChannelProperties minChannelProperties (minPresetProperties.getChannelVT (0), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        minZoneProperties.wrap (minChannelProperties.getZoneVT (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
    }
    {
        PresetProperties maxPresetProperties (ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::MaxParameterPresetType),
                                              PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        ChannelProperties maxChannelProperties (maxPresetProperties.getChannelVT (0), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        maxZoneProperties.wrap (maxChannelProperties.getZoneVT (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
    }

    auto setupLabel = [this] (juce::Label& label, juce::String text, float fontSize, juce::Justification justification)
    {
        label.setBorderSize ({ 0, 0, 0, 0 });
        label.setJustificationType (justification);
        Theme::bindColour (label, juce::Label::textColourId, [] { return Theme::muted; });
        label.setFont (label.getFont ().withHeight (fontSize));
        label.setText (text, juce::NotificationType::dontSendNotification);
        addAndMakeVisible (label);
    };

    toolsButton.setButtonText ("Zone tools");
    toolsButton.setComponentID ("zoneTools");
    toolsButton.setTooltip ("Zone Tools");
    toolsButton.onClick = [this] ()
    {
        jassert (displayToolsMenu != nullptr);
        displayToolsMenu (zoneProperties.getId () - 1);
    };
    addAndMakeVisible (toolsButton);
    copyNextButton.setTooltip ("Copy this sample and its settings to the next zone, then select it. Existing content requires confirmation.");
    continueNextButton.setTooltip ("Copy to the next zone starting at this zone's end, with the same duration clamped to the file. Then select the next slice.");
    copyNextButton.onClick = [this] () { if (copyToNext != nullptr) copyToNext (false); };
    continueNextButton.onClick = [this] () { if (copyToNext != nullptr) copyToNext (true); };
    addAndMakeVisible (copyNextButton);
    addAndMakeVisible (continueNextButton);
    updateNextButtons ();

    addAndMakeVisible (loopPointsView);
    loopPointsView.onContextMenu = [this] (bool start)
    {
        auto menu { getLoopPointsMenu (start) };
        if (menu.getNumItems () > 0) menu.showMenuAsync ({});
    };

    setActiveSamplePoints (AudioPlayerProperties::SamplePointsSelector::SamplePoints, true);

    auto setupPlayButton = [this] (juce::TextButton& playButton, juce::String text, AudioPlayerProperties::PlayState playState)
    {
        playButton.setButtonText (text);
        playButton.setEnabled (false);
        playButton.onClick = [this, &playButton, playState] ()
        {
            cancelAutoLoop (false); // A manual transport decision consumes any pending automatic start.
            if (hasCvAuditionSource () || ! playButton.isEnabled ()) { updateAuditionControls (); return; }
            // Read the current transport, not a label that may still be waiting
            // for a queued UI refresh after another zone started or stopped.
            const auto state { audioPlayerProperties.getPlayState () };
            const bool simulationStop { state == AudioPlayerProperties::PlayState::sampleIntoLoop &&
                (playButton.getToggleState () ||
                 (audioPlayerProperties.getSimulationPhase () == AudioPlayerProperties::SimulationPhase::loop) == (&playButton == &loopPlayButton)) };
            if (isCurrentAuditionSource () &&
                (simulationStop || state == playState))
            {
                audioPlayerProperties.setPlayState (AudioPlayerProperties::PlayState::stop, false);
            }
            else
            {
                audioPlayerProperties.setPlayState (AudioPlayerProperties::PlayState::stop, false);
                audioPlayerProperties.setSamplePointsSelector (samplePointsSelector, false);
                audioPlayerProperties.setSampleSource (parentChannelIndex, zoneIndex, false);
                // starting
                audioPlayerProperties.setPlayState (playState, false);
            }
            updatePlaybackDisplay ();
        };
        addAndMakeVisible (playButton);
    };
    loopPlayButton.setTooltip ("Plays the currently selected SOURCE in looping mode");
    setupPlayButton (loopPlayButton, "LOOP", AudioPlayerProperties::PlayState::loop);

    oneShotPlayButton.setTooltip ("Plays the currently selected SOURCE in one shot mode");
    setupPlayButton (oneShotPlayButton, "ONCE", AudioPlayerProperties::PlayState::play);
    autoLoopButton.setComponentID ("sampleAutoLoop");
    autoLoopButton.setTooltip ("Automatically loop SAMPLE after your next explicit sample import, drop, or browser selection. Never auditions CV, never uses LOOP markers, and does not start on preset/folder changes. Session only; off by default. STOP stays stopped until another sample is loaded.");
    autoLoopButton.onClick = [this]
    {
        if (audioPlayerProperties.isValid ())
            audioPlayerProperties.setAutoLoopEnabled (autoLoopButton.getToggleState (), true);
    };
    addAndMakeVisible (autoLoopButton);
    importSamplesButton.setComponentID ("zoneImportWav");
    importSamplesButton.setTooltip ("Choose WAV files anywhere on your computer. Copies are assigned to this preset's zones; the current root folder and original files stay unchanged. Multiple files fill consecutive zones.");
    importSamplesButton.onClick = [this] { importSamples (); };
    addAndMakeVisible (importSamplesButton);
    chooseSampleFiles = [this] (juce::File folder, std::function<void (juce::StringArray)> complete)
    {
        sampleChooser = std::make_unique<juce::FileChooser> ("Import WAV files into the current preset (originals are kept)", folder, "*.wav;*.WAV");
        sampleChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                                   juce::FileBrowserComponent::canSelectMultipleItems,
            [safe = juce::Component::SafePointer<ZoneEditor> (this), selectedFiles = std::move (complete)] (const juce::FileChooser& chooser)
            {
                if (safe == nullptr) return;
                juce::StringArray files;
                for (const auto& url : chooser.getURLResults ())
                {
                    if (! url.isLocalFile () || ! url.getLocalFile ().existsAsFile ()) { files.clear (); break; }
                    files.add (url.getLocalFile ().getFullPathName ());
                }
                safe->sampleChooser.reset ();
                selectedFiles (files);
            });
    };
    setupLabel (cvAuditionNotice, "CV sample\nSpeaker audition disabled", 10.0f, juce::Justification::centred);
    Theme::bindColour (cvAuditionNotice, juce::Label::textColourId, [] { return Theme::warning; });
    cvAuditionNotice.setTooltip ("This sample or its stereo partner is marked as control voltage (CV). Speaker/headphone audition is blocked. The waveform and preset remain editable for Assimil8or hardware.");
    cvAuditionNotice.setVisible (false);
    setupZoneComponents ();
    for (auto* label : { &sampleDurationLabel, &loopDurationLabel })
    {
        label->setFont (juce::FontOptions (11.0f));
        label->setBorderSize ({ 0, 2, 0, 2 });
        label->setJustificationType (juce::Justification::centredRight);
        label->setTooltip ("Region length in minutes:seconds at channel PITCH + zone PITCH OFFSET, capped at the sampler's playback-rate limit. Excludes audition speed and external CV.");
        addAndMakeVisible (label);
    }
    Theme::bindColour (sampleDurationLabel, juce::Label::textColourId, [] { return WaveformPresentation::markerColours[1]; });
    Theme::bindColour (loopDurationLabel, juce::Label::textColourId, [] { return WaveformPresentation::markerColours[3]; });
    sampleDurationLabel.addMouseListener (&selectSamplePointsClickListener, false);
    loopDurationLabel.addMouseListener (&selectLoopPointsClickListener, false);
    setEditComponentsEnabled (false);
}

bool ZoneEditor::isInterestedInFileDrag ([[maybe_unused]] const juce::StringArray& files)
{
    // we do this check in the fileDragEnter and fileDragMove handlers, presenting more info regarding the drop operation
    return true;
}

void ZoneEditor::setDropIndex (const juce::StringArray& files, int x, int y)
{
    if (files.size () == 1)
        dropIndex = -1;
    else if (getLocalBounds ().removeFromTop (getLocalBounds ().getHeight () / 2).contains (x, y))
        dropIndex = 0;
    else
        dropIndex = 1;
}

void ZoneEditor::filesDropped (const juce::StringArray& files, int x, int y)
{

    setDropIndex (files, x, y);
    if (supportedFile) handleSamplesInternal (dropIndex == 0 ? 0 : zoneIndex, files);
    resetDropInfo ();
    repaint ();
}

void ZoneEditor::resetDropInfo ()
{
    draggingFilesCount = 0;
    dropMsg = {};
    dropDetails = {};
}

void ZoneEditor::updateDropInfo (const juce::StringArray& files)
{
    supportedFile = true;
    for (auto& fileName : files)
    {
        auto draggedFile { juce::File (fileName) };
        if (! audioManager->isA8ManagerSupportedAudioFile (draggedFile))
            supportedFile = false;
    }
}

void ZoneEditor::fileDragEnter (const juce::StringArray& files, int x, int y)
{
    draggingFilesCount = files.size ();
    updateDropInfo (files);
    setDropIndex (files, x, y);
    repaint ();
}

void ZoneEditor::fileDragMove (const juce::StringArray& files, int x, int y)
{
    const auto prevDropIndex= dropIndex;
    setDropIndex (files, x, y);
    if (prevDropIndex != dropIndex)
    {
        updateDropInfo (files);
        repaint ();
    }
    repaint ();
}

void ZoneEditor::fileDragExit (const juce::StringArray&)
{
    resetDropInfo ();
    repaint ();
}

// TODO - can we move this to the EditManager, as it just calls editManager->assignSamples (parentChannelIndex, startingZoneIndex, files); in the ZoneEditor
bool ZoneEditor::handleSamplesInternal (int startingZoneIndex, juce::StringArray files)
{
    invalidateLoadContext ();
    audioPlayerProperties.setPlayState (AudioPlayerProperties::PlayState::stop, true);
    files.sort (true);
    // All selection/drop paths share this handler, so a failed import reports
    // its reason once and leaves the existing zone visible and intact.
    const auto assigned { editManager->assignSamples (parentChannelIndex, startingZoneIndex, files) };
    if (! assigned)
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Cannot assign sample",
            editManager->getLastAssignmentError ().isNotEmpty () ? editManager->getLastAssignmentError () : "The sample could not be assigned.");
    else if (audioPlayerProperties.getAutoLoopEnabled () && ! isStereoRightChannelMode &&
             startingZoneIndex <= zoneIndex && zoneIndex < startingZoneIndex + files.size () && isActiveLoadTarget ())
    {
        // Assignment may load synchronously today, or publish a ready buffer
        // later. In either case start only after the entire stereo edit settles.
        pendingAutoLoop = captureLoadContext ();
        queueAutoLoopAttempt ();
    }
    return assigned;
}

ZoneEditor::LoadContext ZoneEditor::captureLoadContext ()
{
    const auto zone { zoneProperties.getValueTree () };
    const auto preset { zone.getParent ().getParent () };
    return { zone, preset, preset.createCopy (), appProperties.getMostRecentFolder (), zoneProperties.getSample (), loadContextGeneration };
}

bool ZoneEditor::loadContextMatches (const LoadContext& context)
{
    return context.generation == loadContextGeneration && isActiveLoadTarget () &&
        zoneProperties.getValueTree () == context.zone && context.zone.getParent ().getParent () == context.preset &&
        appProperties.getMostRecentFolder () == context.folder && context.preset.isEquivalentTo (context.before);
}

void ZoneEditor::importSamples ()
{
    if (! zoneProperties.isValid () || editManager == nullptr || sampleChooser != nullptr || ! isActiveLoadTarget ()) return;
    const auto context { captureLoadContext () };
    chooseSampleFiles (juce::File (context.folder),
        [safe = juce::Component::SafePointer<ZoneEditor> (this), context] (juce::StringArray files)
        {
            if (safe == nullptr || files.isEmpty () || ! safe->loadContextMatches (context)) return;
            safe->handleSamplesInternal (safe->zoneIndex, files);
        });
}

void ZoneEditor::queueAutoLoopAttempt ()
{
    if (! pendingAutoLoop) return;
    const auto generation { pendingAutoLoop->generation };
    const juce::Component::SafePointer<ZoneEditor> safe (this);
    deferAutoLoop ([safe, generation]
    {
        if (safe != nullptr && safe->pendingAutoLoop && safe->pendingAutoLoop->generation == generation)
            safe->tryAutoLoop ();
    });
}

void ZoneEditor::tryAutoLoop ()
{
    if (! pendingAutoLoop) return;
    if (! audioPlayerProperties.getAutoLoopEnabled () || ! loadContextMatches (*pendingAutoLoop) ||
        isStereoRightChannelMode || parentChannelProperties.getChannelMode () == ChannelProperties::stereoRight || hasCvAuditionSource ())
    { pendingAutoLoop.reset (); return; }
    const auto ready = [] (SampleProperties& sample, const juce::String& expected)
    {
        const auto* buffer { sample.getAudioBufferPtr () };
        return sample.getStatus () == SampleStatus::exists && sample.getName () == expected && buffer != nullptr &&
            sample.getLengthInSamples () > 0 && buffer->getNumSamples () >= sample.getLengthInSamples () && buffer->getNumChannels () > 0;
    };
    if (! ready (sampleProperties, zoneProperties.getSample ())) return;
    if (nextChannelProperties.isValid () && nextChannelProperties.getChannelMode () == ChannelProperties::stereoRight)
    {
        ZoneProperties right (nextChannelProperties.getZoneVT (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        if (! ready (nextSampleProperties, right.getSample ())) return;
    }
    const auto range { resolvedRanges () };
    pendingAutoLoop.reset (); // A start is consumed once, never retried after STOP.
    if (range.sampleStart < 0 || range.sampleEnd <= range.sampleStart || range.sampleEnd > range.fileLength) return;
    audioPlayerProperties.setPlayState (AudioPlayerProperties::PlayState::stop, false);
    setActiveSamplePoints (AudioPlayerProperties::SamplePointsSelector::SamplePoints, false, false);
    audioPlayerProperties.setSamplePointsSelector (AudioPlayerProperties::SamplePointsSelector::SamplePoints, false);
    audioPlayerProperties.setSampleSource (parentChannelIndex, zoneIndex, false);
    autoLoopPlaying = true;
    audioPlayerProperties.setPlayState (AudioPlayerProperties::PlayState::loop, false);
    updatePlaybackDisplay ();
}

void ZoneEditor::cancelAutoLoop (bool stopPlayback)
{
    pendingAutoLoop.reset ();
    const auto shouldStop { autoLoopPlaying && audioPlayerProperties.isValid () && isCurrentAuditionSource () &&
                           audioPlayerProperties.getPlayState () == AudioPlayerProperties::PlayState::loop };
    autoLoopPlaying = false;
    if (stopPlayback && shouldStop) audioPlayerProperties.setPlayState (AudioPlayerProperties::PlayState::stop, true);
}

void ZoneEditor::invalidateLoadContext ()
{
    ++loadContextGeneration;
    cancelAutoLoop (true);
}

void ZoneEditor::visibilityChanged ()
{
    if (! isShowing ()) invalidateLoadContext ();
}

void ZoneEditor::setActiveSamplePoints (AudioPlayerProperties::SamplePointsSelector newSamplePointsSelector, bool forceSetup, bool publishToPlayer)
{
    const bool followingSimulation { publishToPlayer && isCurrentAuditionSource () &&
        audioPlayerProperties.getPlayState () == AudioPlayerProperties::PlayState::sampleIntoLoop };
    if (followingSimulation)
        newSamplePointsSelector = audioPlayerProperties.getSimulationPhase () == AudioPlayerProperties::SimulationPhase::loop
            ? AudioPlayerProperties::SamplePointsSelector::LoopPoints : AudioPlayerProperties::SamplePointsSelector::SamplePoints;
    if (publishToPlayer && ! followingSimulation && samplePointsSelector != newSamplePointsSelector)
        cancelAutoLoop (false);
    if (samplePointsSelector != newSamplePointsSelector || forceSetup)
    {
        samplePointsSelector = newSamplePointsSelector;
        activePointBackground = (samplePointsSelector == AudioPlayerProperties::SamplePointsSelector::SamplePoints ? &samplePointsBackground : &loopPointsBackground);
        updateLoopPointsView ();
        repaint ();
        if (onRegionSelected) onRegionSelected (isLoopSelected ());
    }
    // Each zone remembers its choice; only the visible editor may publish it
    // to the shared player. Re-publish even when its local choice is unchanged.
    if (publishToPlayer && ! followingSimulation && isShowing () && (audioPlayerProperties.getSamplePointsSelector () != samplePointsSelector ||
                         audioPlayerProperties.getSampleSource () != std::make_tuple (parentChannelIndex, zoneIndex)))
    {
        audioPlayerProperties.setPlayState (AudioPlayerProperties::PlayState::stop, true);
        audioPlayerProperties.setSamplePointsSelector (samplePointsSelector, false);
    }
}

void ZoneEditor::updateLoopPointsView ()
{
    updateDurations ();
    juce::int64 startSample { 0 };
    double numSamples { 0 };
    int side { 0 };
    const auto loopSelected { samplePointsSelector == AudioPlayerProperties::SamplePointsSelector::LoopPoints };
    if (sampleProperties.getStatus () == SampleStatus::exists)
    {
        const auto ranges { resolvedRanges () };
        if (! loopSelected)
        {
            startSample = ranges.sampleStart;
            if (startSample >= 0 && ranges.sampleEnd <= ranges.fileLength && ranges.sampleEnd > startSample)
                numSamples = static_cast<double> (ranges.sampleEnd - startSample);
        }
        else if (ranges.loopValid)
        {
            startSample = ranges.loopStart;
            numSamples = ranges.loopLength;
        }
        loopPointsView.setAudioBuffer (sampleProperties.getAudioBufferPtr ());
        side = zoneProperties.getSide ();
    }
    else
    {
        loopPointsView.setAudioBuffer (nullptr);
    }
    loopPointsView.setLoopPoints (startSample, numSamples, side, loopSelected);
    loopPointsView.repaint ();
    if (onSimulationAvailabilityChanged) onSimulationAvailabilityChanged ();
}

void ZoneEditor::updateDurations ()
{
    if (sampleProperties.getStatus () != SampleStatus::exists || ! zoneProperties.isValid ())
    {
        sampleDurationLabel.setText ("SAMPLE --:--", juce::dontSendNotification);
        loopDurationLabel.setText ("LOOP --:--", juce::dontSendNotification);
        return;
    }
    const auto ranges { resolvedRanges () };
    const auto rate { sampleProperties.getSampleRate () };
    const auto pitch { PlaybackPitch::effectiveSemitones (parentChannelProperties.getPitch (), zoneProperties.getPitchOffset (), rate) };
    sampleDurationLabel.setText ("SAMPLE " + WaveformPresentation::duration (
        static_cast<double> (ranges.sampleEnd - ranges.sampleStart), rate, pitch), juce::dontSendNotification);
    loopDurationLabel.setText (ranges.loopValid ? "LOOP " + WaveformPresentation::duration (
        ranges.loopLength, rate, pitch) : "LOOP --:--", juce::dontSendNotification);
}

juce::PopupMenu ZoneEditor::getSampleAdjustMenu (SampleMarker marker)
{
    return createBoundaryAdjustmentMenu ? createBoundaryAdjustmentMenu (static_cast<int> (marker)) : juce::PopupMenu {};
}

juce::PopupMenu ZoneEditor::getLoopPointsMenu (bool start)
{
    const auto marker { isLoopSelected () ? (start ? SampleMarker::loopStart : SampleMarker::loopEnd)
                                        : (start ? SampleMarker::sampleStart : SampleMarker::sampleEnd) };
    auto menu { getSampleAdjustMenu (marker) };
    if (menu.getNumItems () > 0)
    {
        juce::PopupMenu titled;
        titled.addSectionHeader (WaveformPresentation::markerNames[static_cast<size_t> (marker)]);
        for (juce::PopupMenu::MenuItemIterator items (menu); items.next ();)
            titled.addItem (items.getItem ());
        return titled;
    }
    return menu;
}

void ZoneEditor::setupZoneComponents ()
{
    juce::XmlDocument xmlDoc { BinaryData::Assimil8orToolTips_xml };
    auto xmlElement { xmlDoc.getDocumentElement (false) };
//     if (auto parseError { xmlDoc.getLastParseError () }; parseError != "")
//         juce::Logger::outputDebugString ("XML Parsing Error for Assimil8orToolTips_xml: " + parseError);
    // NOTE: this is a hard failure, which indicates there is a problem in the file the parameterPresetXml passed in
    jassert (xmlDoc.getLastParseError () == "");
    auto toolTipsVT { juce::ValueTree::fromXml (*xmlElement) };
    ParameterToolTipData parameterToolTipData (toolTipsVT, ParameterToolTipData::WrapperType::owner, ParameterToolTipData::EnableCallbacks::no);

    auto setupLabel = [this] (juce::Label& label, juce::String text, float fontSize, juce::Justification justification)
    {
        label.setBorderSize ({ 0, 0, 0, 0 });
        label.setJustificationType (justification);
        Theme::bindColour (label, juce::Label::textColourId, [] { return Theme::muted; });
        label.setFont (label.getFont ().withHeight (fontSize));
        label.setText (text, juce::NotificationType::dontSendNotification);
        addAndMakeVisible (label);
    };
    auto setupTextEditor = [this, &parameterToolTipData] (juce::TextEditor& textEditor, juce::Justification justification, int maxLen, juce::String validInputCharacters,
                                                          juce::String parameterName)
    {
        textEditor.setJustification (justification);
        textEditor.setIndents (2, 0);
        textEditor.setInputRestrictions (maxLen, validInputCharacters);
        textEditor.setTooltip (parameterToolTipData.getToolTip ("Zone", parameterName));
        addAndMakeVisible (textEditor);
    };

    // SAMPLE FILE SELECTOR LABEL
    setupLabel (sampleNameLabel, "FILE", 15.0, juce::Justification::centredLeft);

    // SAMPLE FILE SELECTOR
    Theme::bindColour (sampleNameSelectLabel, juce::Label::textColourId, [] { return Theme::text; });
    Theme::bindColour (sampleNameSelectLabel, juce::Label::backgroundColourId, [] { return Theme::field; });
    sampleNameSelectLabel.setOutline (levelOffsetTextEditor.findColour (juce::TextEditor::ColourIds::outlineColourId));
    sampleNameSelectLabel.setBorderSize ({ 0, 2, 0, 0 });
    sampleNameSelectLabel.setDialogTitle ("Select sample files to load...");
    sampleNameSelectLabel.canMultiSelect (true);
    sampleNameSelectLabel.onChooseFilesRequested = [this] { importSamples (); };
    sampleNameSelectLabel.onFilesSelected = [this] (const juce::StringArray& files)
    {
        handleSamplesInternal (zoneProperties.getId () - 1, files);
    };
    sampleNameSelectLabel.onPopupMenuCallback = [this] ()
    {
        auto editMenu { createSampleFileMenu () };
        if (editMenu.getNumItems () != 0) editMenu.showMenuAsync ({}, [] (int) {});
    };
    setupLabel (sampleNameSelectLabel, "", 12.5, juce::Justification::centredLeft);
    setupLabel (a8SelectNameLabel, "", 10.5, juce::Justification::centredLeft);
    setupLabel (a8ChannelNameLabel, "", 10.5, juce::Justification::centredLeft);
    a8SelectNameLabel.setTooltip (A8NamePreview::advice ());
    a8ChannelNameLabel.setTooltip (A8NamePreview::advice ());

    // AUDIO FILE CHANNEL SELECT BUTTONS
    auto setupChannelSelectButton = [this] (juce::TextButton& channelSelectButton, juce::String buttonText, int side)
    {
        Theme::bindColour (channelSelectButton, juce::TextButton::buttonOnColourId, [] { return Theme::accent; });
        Theme::bindColour (channelSelectButton, juce::TextButton::textColourOnId, [] { return Theme::field; });
        Theme::bindColour (channelSelectButton, juce::TextButton::buttonColourId, [] { return Theme::field; });
        Theme::bindColour (channelSelectButton, juce::TextButton::textColourOffId, [] { return Theme::text; });
        channelSelectButton.setButtonText (buttonText);
        channelSelectButton.setTooltip ("Use the " + juce::String (side == 0 ? "left" : "right") + " channel of this audio file for the zone");
        channelSelectButton.setEnabled (false);
        channelSelectButton.onClick = [this, side] ()
        {
            sideUiChanged (side);
            updateSideSelectButtons (side);
        };
        addAndMakeVisible (channelSelectButton);
    };
    setupChannelSelectButton (leftChannelSelectButton, "L", 0);
    setupChannelSelectButton (rightChannelSelectButton, "R", 1);

    selectSamplePointsClickListener.onClick = [this] () { setActiveSamplePoints (AudioPlayerProperties::SamplePointsSelector::SamplePoints, false); };
    // SAMPLE START
    sampleStartLabel.addMouseListener (&selectSamplePointsClickListener, false);
    setupLabel (sampleStartLabel, "SMPL START", 12.0, juce::Justification::centredRight);
    sampleStartTextEditor.getMinValueCallback = [this] { return static_cast<juce::int64> (boundaryLimits (ZoneSampleRanges::Marker::sampleStart).minimum); };
    sampleStartTextEditor.getMaxValueCallback = [this] { return static_cast<juce::int64> (boundaryLimits (ZoneSampleRanges::Marker::sampleStart).maximum); };
    sampleStartTextEditor.toStringCallback = [this] (juce::int64 value) { return juce::String (value); };
    sampleStartTextEditor.updateDataCallback = [this] (juce::int64 value)
    {
        sampleStartUiChanged (value);
    };
    sampleStartTextEditor.onDragCallback = [this] (double valueDelta)
    {
        const auto newValue { zoneProperties.getSampleStart ().value_or (0) + static_cast<juce::int64> (valueDelta) };
        sampleStartTextEditor.setValue (newValue);
    };
    sampleStartTextEditor.onPopupMenuCallback = [this] ()
    {
        auto adjustMenu { getSampleAdjustMenu (SampleMarker::sampleStart) };
        auto editMenu { createZoneEditMenu (adjustMenu, [this] (ZoneProperties& destZoneProperties, SampleProperties& destSampleProperties)
                                            {
                                                editZoneBoundary (destZoneProperties, destSampleProperties.getLengthInSamples (),
                                                    ZoneSampleRanges::Marker::sampleStart, resolvedRanges ().sampleStart, rangeMode ());
                                            },
                                            [this] () { sampleStartUiChanged (0); },
                                            [this] () { sampleStartUiChanged (uneditedZoneProperties.getSampleStart ().value_or (0)); },
                                            [] (ZoneProperties& destZoneProperties) { return destZoneProperties.getSample ().isNotEmpty (); },
                                            [] (ZoneProperties& destZoneProperties) { return destZoneProperties.getSample ().isNotEmpty (); }) };
        editMenu.showMenuAsync ({}, [this] (int) {});
    };
    sampleStartTextEditor.addMouseListener (&selectSamplePointsClickListener, false);
    setupTextEditor (sampleStartTextEditor, juce::Justification::centred, 0, "0123456789", "SampleStart");

    // SAMPLE END
    sampleEndLabel.addMouseListener (&selectSamplePointsClickListener, false);
    setupLabel (sampleEndLabel, "SMPL END", 12.0, juce::Justification::centredRight);
    sampleEndTextEditor.getMinValueCallback = [this] { return static_cast<juce::int64> (boundaryLimits (ZoneSampleRanges::Marker::sampleEnd).minimum); };
    sampleEndTextEditor.getMaxValueCallback = [this] { return static_cast<juce::int64> (boundaryLimits (ZoneSampleRanges::Marker::sampleEnd).maximum); };
    sampleEndTextEditor.toStringCallback = [this] (juce::int64 value) { return juce::String (value); };
    sampleEndTextEditor.updateDataCallback = [this] (juce::int64 value)
    {
        sampleEndUiChanged (value);
    };
    sampleEndTextEditor.onDragCallback = [this] (double valueDelta)
    {
        const auto newValue { zoneProperties.getSampleEnd ().value_or (sampleProperties.getLengthInSamples ()) + static_cast<juce::int64> (valueDelta) };
        sampleEndTextEditor.setValue (newValue);
    };
    sampleEndTextEditor.onPopupMenuCallback = [this] ()
    {
        auto adjustMenu { getSampleAdjustMenu (SampleMarker::sampleEnd) };

        auto editMenu { createZoneEditMenu (adjustMenu, [this] (ZoneProperties& destZoneProperties, SampleProperties& destSampleProperties)
                                            {
                                                editZoneBoundary (destZoneProperties, destSampleProperties.getLengthInSamples (),
                                                    ZoneSampleRanges::Marker::sampleEnd, resolvedRanges ().sampleEnd, rangeMode ());
                                            },
                                            [this] () { sampleEndUiChanged (sampleProperties.getLengthInSamples ()); },
                                            [this] () { sampleEndUiChanged (uneditedZoneProperties.getSampleEnd ().value_or (sampleProperties.getLengthInSamples ())); },
                                            [] (ZoneProperties& destZoneProperties) { return destZoneProperties.getSample ().isNotEmpty (); },
                                            [] (ZoneProperties& destZoneProperties) { return destZoneProperties.getSample ().isNotEmpty (); }) };
        editMenu.showMenuAsync ({}, [this] (int) {});
    };
    sampleEndTextEditor.addMouseListener (&selectSamplePointsClickListener, false);
    setupTextEditor (sampleEndTextEditor, juce::Justification::centred, 0, "0123456789", "SampleEnd");

    // LOOP START
    selectLoopPointsClickListener.onClick = [this] () { setActiveSamplePoints (AudioPlayerProperties::SamplePointsSelector::LoopPoints, false); };
    loopStartLabel.addMouseListener (&selectLoopPointsClickListener, false);
    setupLabel (loopStartLabel, "LOOP START", 12.0, juce::Justification::centredRight);
    loopStartTextEditor.getMinValueCallback = [this] { return static_cast<juce::int64> (boundaryLimits (ZoneSampleRanges::Marker::loopStart).minimum); };
    loopStartTextEditor.getMaxValueCallback = [this] { return static_cast<juce::int64> (boundaryLimits (ZoneSampleRanges::Marker::loopStart).maximum); };
    loopStartTextEditor.toStringCallback = [this] (juce::int64 value) { return juce::String (value); };
    loopStartTextEditor.updateDataCallback = [this] (juce::int64 value)
    {
        loopStartUiChanged (value);
    };
    loopStartTextEditor.onDragCallback = [this] (double valueDelta)
    {
        const auto newValue { resolvedRanges ().loopStart + static_cast<juce::int64> (valueDelta) };
        loopStartTextEditor.setValue (newValue);
    };
    loopStartTextEditor.onPopupMenuCallback = [this] ()
    {
        auto adjustMenu { getSampleAdjustMenu (SampleMarker::loopStart) };

        auto editMenu { createZoneEditMenu (adjustMenu , [this] (ZoneProperties& destZoneProperties, SampleProperties& destSampleProperties)
                                            {
                                                editZoneBoundary (destZoneProperties, destSampleProperties.getLengthInSamples (),
                                                    ZoneSampleRanges::Marker::loopStart, resolvedRanges ().loopStart, rangeMode ());
                                            },
                                            [this] () { loopStartUiChanged (resolvedRanges ().sampleStart); },
                                            [this] () { loopStartUiChanged (uneditedZoneProperties.getLoopStart ().value_or (resolvedRanges ().sampleStart)); },
                                            [] (ZoneProperties& destZoneProperties) { return destZoneProperties.getSample ().isNotEmpty (); },
                                            [] (ZoneProperties& destZoneProperties) { return destZoneProperties.getSample ().isNotEmpty (); }) };
        editMenu.showMenuAsync ({}, [this] (int) {});
    };
    loopStartTextEditor.addMouseListener (&selectLoopPointsClickListener, false);
    setupTextEditor (loopStartTextEditor, juce::Justification::centred, 0, "0123456789", "LoopStart");
    loopStartTextEditor.onFocusLost = [this]
    {
        // Merely visiting an automatic loop field is not an explicit edit.
        const auto value { loopStartTextEditor.getText ().getLargeIntValue () };
        if (value != resolvedRanges ().loopStart) loopStartTextEditor.setValue (value);
    };

    // LOOP LENGTH/END
    loopLengthLabel.addMouseListener (&selectLoopPointsClickListener, false);
    setupLabel (loopLengthLabel, "LOOP LENGTH", 12.0, juce::Justification::centredRight);
    loopLengthTextEditor.getMinValueCallback = [this]
    {
        const auto bounds { boundaryLimits (ZoneSampleRanges::Marker::loopEnd) };
        return bounds.editable ? bounds.minimum - (treatLoopLengthAsEndInUi ? 0.0 : static_cast<double> (resolvedRanges ().loopStart)) : 0.0;
    };
    loopLengthTextEditor.getMaxValueCallback = [this]
    {
        const auto bounds { boundaryLimits (ZoneSampleRanges::Marker::loopEnd) };
        return bounds.editable ? bounds.maximum - (treatLoopLengthAsEndInUi ? 0.0 : static_cast<double> (resolvedRanges ().loopStart)) : 0.0;
    };
    loopLengthTextEditor.snapValueCallback = [this] (double value)
    {
        const auto offset { treatLoopLengthAsEndInUi ? static_cast<double> (resolvedRanges ().loopStart) : 0.0 };
        return offset + snapLoopLength (value - offset);
    };
    loopLengthTextEditor.toStringCallback = [this] (double value)
    {
        auto loopLengthInputValue = [this, value] ()
        {
            if (treatLoopLengthAsEndInUi)
                return value - resolvedRanges ().loopStart;
            else
                return value;
        } ();
        return formatLoopLength (loopLengthInputValue);
    };
    loopLengthTextEditor.updateDataCallback = [this] (double value)
    {
        auto loopLengthInputValue = [this, value] ()
        {
            if (treatLoopLengthAsEndInUi)
                return value - resolvedRanges ().loopStart;
            else
                return value;
        } ();
        loopLengthUiChanged (loopLengthInputValue);
    };
    loopLengthTextEditor.onDragCallback = [this] (double valueDelta)
    {
        const auto ranges { resolvedRanges () };
        const auto newValue { ranges.loopLength + (treatLoopLengthAsEndInUi ? static_cast<double> (ranges.loopStart) : 0.0) + valueDelta };
        loopLengthTextEditor.setValue (newValue);
    };

    loopLengthTextEditor.onPopupMenuCallback = [this] ()
    {
        auto adjustMenu { getSampleAdjustMenu (SampleMarker::loopEnd) };
        auto editMenu { createZoneEditMenu (adjustMenu, [this] (ZoneProperties& destZoneProperties, SampleProperties& destSampleProperties)
                                            {
                                                const auto frames { destSampleProperties.getLengthInSamples () };
                                                ChannelProperties destinationChannel (destZoneProperties.getValueTree ().getParent (), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
                                                const auto destination { ZoneSampleRanges::resolve (ZoneSampleRanges::read (destZoneProperties), frames, destinationChannel.getAllowLoopOutsideSample ()) };
                                                editZoneBoundary (destZoneProperties, frames, ZoneSampleRanges::Marker::loopEnd,
                                                    destination.loopStart + resolvedRanges ().loopLength, rangeMode ());
                                            },
                                            [this] ()
                                            {
                                                const auto ranges { resolvedRanges () };
                                                loopLengthUiChanged (static_cast<double> (ranges.sampleEnd - ranges.loopStart));
                                            },
                                            [this] () { loopLengthUiChanged (uneditedZoneProperties.getLoopLength ().value_or (
                                                static_cast<double> (resolvedRanges ().sampleEnd - resolvedRanges ().loopStart))); },
                                            [] (ZoneProperties& destZoneProperties) { return destZoneProperties.getSample ().isNotEmpty (); },
                                            [] (ZoneProperties& destZoneProperties) { return destZoneProperties.getSample ().isNotEmpty (); }) };
        editMenu.showMenuAsync ({}, [this] (int) {});
    };
    loopLengthTextEditor.addMouseListener (&selectLoopPointsClickListener, false);
    setupTextEditor (loopLengthTextEditor, juce::Justification::centred, 0, ".0123456789", "LoopLength");
    loopLengthTextEditor.onFocusLost = [this]
    {
        const auto ranges { resolvedRanges () };
        if (loopLengthTextEditor.getText () != formatLoopLength (ranges.loopLength))
            loopLengthTextEditor.setValue (loopLengthTextEditor.getText ().getDoubleValue ());
    };

    // MIN VOLTAGE
    setupLabel (minVoltageLabel, "MIN VOLTAGE", 15.0, juce::Justification::centredRight);
    minVoltageTextEditor.getMinValueCallback = [this] { return minZoneProperties.getMinVoltage (); };
    minVoltageTextEditor.getMaxValueCallback = [this] { return maxZoneProperties.getMinVoltage (); };
    minVoltageTextEditor.highlightErrorCallback = [this] ()
    {
        ErrorHelpers::setColorIfError (minVoltageTextEditor, editManager->isMinVoltageInRange (parentChannelIndex, zoneIndex, minVoltageTextEditor.getText ().getDoubleValue ()));
    };
    minVoltageTextEditor.snapValueCallback = [this] (double value) { return editManager->clampMinVoltage (parentChannelIndex, zoneIndex, value); };
    minVoltageTextEditor.toStringCallback = [this] (double value) { return FormatHelpers::formatDouble (value, 2, true); };
    minVoltageTextEditor.updateDataCallback = [this] (double value) { minVoltageUiChanged (value); };
    minVoltageTextEditor.getIncrementCallback = [] () { return 0.01; };
    minVoltageTextEditor.onDragCallback = [this] (double valueDelta)
    {
        const auto newValue { zoneProperties.getMinVoltage () + valueDelta };
        minVoltageTextEditor.setValue (newValue);
    };
    minVoltageTextEditor.onPopupMenuCallback = [this] ()
    {
        if (zoneProperties.getSample ().isEmpty ())
            return;
        auto editMenu { createZoneEditMenu ({}, nullptr /* cloning across zones does not makes sense, as they have to be unique values */,
                                            [this] () { editManager->resetMinVoltage (parentChannelIndex, zoneIndex); },
                                            [this] () { zoneProperties.setMinVoltage (editManager->clampMinVoltage (parentChannelIndex, zoneIndex, uneditedZoneProperties.getMinVoltage ()), true); },
                                            [] (ZoneProperties&) { return false; },
                                            [] (ZoneProperties&) { return false; }) };
        editMenu.showMenuAsync ({}, [this] (int) {});
    };
    setupTextEditor (minVoltageTextEditor, juce::Justification::centred, 0, "+-.0123456789", "MinVoltage");

    // PITCH OFFSET
    pitchOffsetTextEditor.getMinValueCallback = [this] { return minZoneProperties.getPitchOffset (); };
    pitchOffsetTextEditor.getMaxValueCallback = [this] { return maxZoneProperties.getPitchOffset (); };
    pitchOffsetTextEditor.toStringCallback = [this] (double value) { return FormatHelpers::formatDouble (value, 2, true); };
    pitchOffsetTextEditor.updateDataCallback = [this] (double value) { pitchOffsetUiChanged (value); };
    pitchOffsetTextEditor.getIncrementCallback = [] () { return 0.01; };
    pitchOffsetTextEditor.onDragCallback = [this] (double valueDelta)
    {
        const auto newValue { zoneProperties.getPitchOffset () + valueDelta };
        pitchOffsetTextEditor.setValue (newValue);
    };
    pitchOffsetTextEditor.onPopupMenuCallback = [this] ()
    {
        auto editMenu { createZoneEditMenu ({}, [this] (ZoneProperties& destZoneProperties, SampleProperties&) { destZoneProperties.setPitchOffset (zoneProperties.getPitchOffset (), false); },
                                            [this] () { zoneProperties.setPitchOffset (0, true); },
                                            [this] () { zoneProperties.setPitchOffset (uneditedZoneProperties.getPitchOffset (), true); },
                                            [] (ZoneProperties& destZoneProperties) { return destZoneProperties.getSample ().isNotEmpty (); },
                                            [] (ZoneProperties& destZoneProperties) { return destZoneProperties.getSample ().isNotEmpty (); }) };
        editMenu.showMenuAsync ({}, [this] (int) {});
    };

    setupLabel (pitchOffsetLabel, "PITCH OFFSET", 15.0, juce::Justification::centredRight);
    setupTextEditor (pitchOffsetTextEditor, juce::Justification::centred, 0, "+-.0123456789", "PitchOffset");

    // LEVEL OFFSET
    setupLabel (levelOffsetLabel, "LEVEL OFFSET", 15.0, juce::Justification::centredRight);
    levelOffsetTextEditor.getMinValueCallback = [this] { return minZoneProperties.getLevelOffset (); };
    levelOffsetTextEditor.getMaxValueCallback = [this] { return maxZoneProperties.getLevelOffset (); };
    levelOffsetTextEditor.toStringCallback = [this] (double value) { return FormatHelpers::formatDouble (value, 1, true); };
    levelOffsetTextEditor.updateDataCallback = [this] (double value) { levelOffsetUiChanged (value); };
    levelOffsetTextEditor.getIncrementCallback = [] () { return 0.1; };
    levelOffsetTextEditor.onDragCallback = [this] (double valueDelta)
    {
        const auto newValue { zoneProperties.getLevelOffset () + valueDelta };
        levelOffsetTextEditor.setValue (newValue);
    };
    levelOffsetTextEditor.onPopupMenuCallback = [this] ()
    {
        auto editMenu { createZoneEditMenu ({}, [this] (ZoneProperties& destZoneProperties, SampleProperties&) { destZoneProperties.setLevelOffset (zoneProperties.getLevelOffset (), false); },
                                            [this] () { zoneProperties.setLevelOffset (0, true); },
                                            [this] () { zoneProperties.setLevelOffset (uneditedZoneProperties.getLevelOffset (), true); },
                                            [] (ZoneProperties& destZoneProperties) { return destZoneProperties.getSample ().isNotEmpty (); },
                                            [] (ZoneProperties& destZoneProperties) { return destZoneProperties.getSample ().isNotEmpty (); }) };
        editMenu.showMenuAsync ({}, [this] (int) {});
    };
    setupTextEditor (levelOffsetTextEditor, juce::Justification::centred, 0, "+-.0123456789", "LevelOffset");
    const std::array<juce::Label*, 4> markerLabels { &sampleStartLabel, &sampleEndLabel, &loopStartLabel, &loopLengthLabel };
    for (size_t marker { 0 }; marker < markerLabels.size (); ++marker)
        Theme::bindColour (*markerLabels[marker], juce::Label::textColourId, [marker] { return WaveformPresentation::markerColours[marker]; });
}

void ZoneEditor::init (juce::ValueTree zonePropertiesVT, juce::ValueTree uneditedZonePropertiesVT, juce::ValueTree rootPropertiesVT)
{
    invalidateLoadContext ();
    //DebugLog ("ZoneEditor[" + juce::String (zoneProperties.getId ()) + "]", "init");
    PersistentRootProperties persistentRootProperties (rootPropertiesVT, PersistentRootProperties::WrapperType::client, PersistentRootProperties::EnableCallbacks::no);
    RuntimeRootProperties runtimeRootProperties (rootPropertiesVT, RuntimeRootProperties::WrapperType::client, RuntimeRootProperties::EnableCallbacks::no);

    appProperties.wrap (persistentRootProperties.getValueTree (), AppProperties::WrapperType::client, AppProperties::EnableCallbacks::yes);
    appProperties.onMostRecentFolderChange = [this] (juce::String) { invalidateLoadContext (); };
    appProperties.onMostRecentFileChange = [this] (juce::String) { invalidateLoadContext (); };
    SystemServices systemServices { runtimeRootProperties.getValueTree (), SystemServices::WrapperType::client, SystemServices::EnableCallbacks::yes };
    audioManager = systemServices.getAudioManager ();
    sampleNameSelectLabel.setFileFilter (audioManager->getFileTypesList ());
    editManager = systemServices.getEditManager ();

    audioPlayerProperties.wrap (runtimeRootProperties.getValueTree (), AudioPlayerProperties::WrapperType::client, AudioPlayerProperties::EnableCallbacks::yes);
    audioPlayerProperties.onPlayStateChange = [this] (AudioPlayerProperties::PlayState state)
    {
        pendingAutoLoop.reset ();
        if (state != AudioPlayerProperties::PlayState::loop) autoLoopPlaying = false;
        queuePlaybackDisplayUpdate ();
    };
    audioPlayerProperties.onSampleSourceChanged = [this] (auto)
    {
        ++loadContextGeneration; // Ancestor channel visibility changes do not notify every zone child.
        pendingAutoLoop.reset ();
        if (! isCurrentAuditionSource ()) autoLoopPlaying = false;
        queuePlaybackDisplayUpdate ();
    };
    audioPlayerProperties.onSimulationPhaseChange = [this] (AudioPlayerProperties::SimulationPhase) { queuePlaybackDisplayUpdate (); };
    audioPlayerProperties.onAutoLoopEnabledChange = [this] (bool enabled)
    {
        autoLoopButton.setToggleState (enabled, juce::dontSendNotification);
        if (! enabled) cancelAutoLoop (true);
        updatePlaybackDisplay ();
    };
    autoLoopButton.setToggleState (audioPlayerProperties.getAutoLoopEnabled (), juce::dontSendNotification);

    zoneProperties.wrap (zonePropertiesVT, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::yes);
    uneditedZoneProperties.wrap (uneditedZonePropertiesVT, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
    zoneIndex = zoneProperties.getId () - 1;
    jassert (ChannelProperties::isChannelPropertiesVT (zoneProperties.getValueTree ().getParent ()));
    parentChannelProperties.wrap (zoneProperties.getValueTree ().getParent (), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::yes);
    parentChannelIndex = parentChannelProperties.getId () - 1;
    parentChannelProperties.onChannelModeChange = [this] (int) { updateAuditionControls (); };
    parentChannelProperties.onPitchChange = [this] (double) { updateDurations (); };
    parentChannelProperties.onAllowLoopOutsideSampleChange = [this] (bool) { updateSamplePositionInfo (); updateAuditionControls (); };

    SampleManagerProperties sampleManagerProperties (runtimeRootProperties.getValueTree (), SampleManagerProperties::WrapperType::client, SampleManagerProperties::EnableCallbacks::no);
    sampleProperties.wrap (sampleManagerProperties.getSamplePropertiesVT (parentChannelIndex, zoneIndex), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::yes);
    sampleProperties.onIsCvChange = [this] (bool cv) { if (cv) cancelAutoLoop (true); updateAuditionControls (); };
    sampleProperties.onAudioBufferPtrChange = [this] (AudioBufferType*) { queueAutoLoopAttempt (); };
    PresetProperties auditionPreset (parentChannelProperties.getValueTree ().getParent (), PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
    auto bindAdjacent = [&] (int channelIndex, ChannelProperties& channel, SampleProperties& sample)
    {
        if (channelIndex >= 0 && channelIndex < 8)
        {
            channel.wrap (auditionPreset.getChannelVT (channelIndex), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::yes);
            sample.wrap (sampleManagerProperties.getSamplePropertiesVT (channelIndex, zoneIndex), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::yes);
            channel.onChannelModeChange = [this] (int) { updateAuditionControls (); };
            sample.onIsCvChange = [this] (bool cv) { if (cv && hasCvAuditionSource ()) cancelAutoLoop (true); updateAuditionControls (); };
            sample.onStatusChange = [this] (SampleStatus) { queueAutoLoopAttempt (); };
            sample.onAudioBufferPtrChange = [this] (AudioBufferType*) { queueAutoLoopAttempt (); };
        }
        else
        {
            channel.enableCallbacks (false); channel.release ();
            sample.enableCallbacks (false); sample.release ();
        }
    };
    bindAdjacent (parentChannelIndex - 1, previousChannelProperties, previousSampleProperties);
    bindAdjacent (parentChannelIndex + 1, nextChannelProperties, nextSampleProperties);
    sampleProperties.onSampleRateChange = [this] (double) { updateDurations (); };
    sampleProperties.onStatusChange = [this] (SampleStatus status)
    {
        if (status == SampleStatus::exists)
        {
            //DebugLog ("ZoneEditor", "sample Status exists");
            // when we receive this callback, it means all of the other sample data is updated too
            setEditComponentsEnabled (true);
            updateLoopPointsView ();
            updateSamplePositionInfo ();
            updateSampleFileInfo (zoneProperties.getSample ());
            updateSideSelectButtons (zoneProperties.getSide ());
        }
        else if (status == SampleStatus::uninitialized)
        {
            //DebugLog ("ZoneEditor", "sample Status uninitialized");
            // this means the sample has been unloaded
            setEditComponentsEnabled (false);
            oneShotPlayButton.setEnabled (false);
            loopPlayButton.setEnabled (false);
            updateLoopPointsView ();
            updateSamplePositionInfo ();
            updateSideSelectButtons (0);
        }
        else if (status == SampleStatus::doesNotExist)
        {
            //DebugLog ("ZoneEditor", "sample Status doesNotExist");
            // obviously none of the sample data can be used
            setEditComponentsEnabled (false);
            oneShotPlayButton.setEnabled (false);
            loopPlayButton.setEnabled (false);
            updateLoopPointsView ();
            updateSamplePositionInfo ();
            updateSampleFileInfo (zoneProperties.getSample ());
            updateSideSelectButtons (0);
        }
        else if (status == SampleStatus::wrongFormat)
        {
            //DebugLog ("ZoneEditor", "sample Status wrongFormat");
            // we should not be able to load a sample of the wrong format, but if an already loaded sample is changed outside of the app, this could happen
            setEditComponentsEnabled (false);
            oneShotPlayButton.setEnabled (false);
            loopPlayButton.setEnabled (false);
            updateLoopPointsView ();
            updateSamplePositionInfo ();
            updateSampleFileInfo (zoneProperties.getSample ());
            updateSideSelectButtons (0);
        }
        updateAuditionControls ();
        if (status == SampleStatus::exists) queueAutoLoopAttempt ();
        else if (status == SampleStatus::doesNotExist || status == SampleStatus::wrongFormat) cancelAutoLoop (true);
    };

    setupZonePropertiesCallbacks ();

    levelOffsetDataChanged (zoneProperties.getLevelOffset ());
    loopLengthDataChanged (zoneProperties.getLoopLength ());
    loopStartDataChanged (zoneProperties.getLoopStart ());
    minVoltageDataChanged (zoneProperties.getMinVoltage ());
    pitchOffsetDataChanged (zoneProperties.getPitchOffset ());
    sampleDataChanged (zoneProperties.getSample ());
    sampleStartDataChanged (zoneProperties.getSampleStart ());
    sampleEndDataChanged (zoneProperties.getSampleEnd ());
    sideDataChanged (zoneProperties.getSide ());

    setLoopLengthIsEnd (parentChannelProperties.getLoopLengthIsEnd ());
    updateAuditionControls ();
}

bool ZoneEditor::hasCvAuditionSource ()
{
    if (sampleProperties.getIsCv ()) return true;
    const auto right { parentChannelProperties.getChannelMode () == ChannelProperties::ChannelMode::stereoRight };
    if (! right && nextChannelProperties.isValid () && nextChannelProperties.getChannelMode () == ChannelProperties::ChannelMode::stereoRight)
        return nextSampleProperties.isValid () && nextSampleProperties.getIsCv ();
    return right && previousChannelProperties.isValid () && previousChannelProperties.getChannelMode () != ChannelProperties::ChannelMode::stereoRight
           && previousSampleProperties.isValid () && previousSampleProperties.getIsCv ();
}

void ZoneEditor::updateAuditionControls ()
{
    const bool cv { hasCvAuditionSource () };
    const bool enabled { ! cv && ! isStereoRightChannelMode && sampleProperties.getStatus () == SampleStatus::exists };
    oneShotPlayButton.setEnabled (enabled);
    loopPlayButton.setEnabled (enabled);
    autoLoopButton.setEnabled (! isStereoRightChannelMode && ! cv);
    cvAuditionNotice.setVisible (cv);
    oneShotPlayButton.setTooltip (cv ? cvAuditionNotice.getTooltip () : "Plays the currently selected SOURCE in one shot mode");
    loopPlayButton.setTooltip (cv ? cvAuditionNotice.getTooltip () : "Plays the currently selected SOURCE in looping mode");
    updatePlaybackDisplay ();
    if (onSimulationAvailabilityChanged) onSimulationAvailabilityChanged ();
}

bool ZoneEditor::isCurrentAuditionSource ()
{
    return parentChannelIndex >= 0 && zoneIndex >= 0 &&
           audioPlayerProperties.getSampleSource () == std::make_tuple (parentChannelIndex, zoneIndex);
}

bool ZoneEditor::canStartSampleIntoLoop ()
{
    if (parentChannelIndex < 0 || zoneIndex < 0 || isStereoRightChannelMode || hasCvAuditionSource () ||
        sampleProperties.getStatus () != SampleStatus::exists || sampleProperties.getAudioBufferPtr () == nullptr)
        return false;
    const auto frames { std::min (sampleProperties.getLengthInSamples (),
        static_cast<juce::int64> (sampleProperties.getAudioBufferPtr ()->getNumSamples ())) };
    return SampleLoopSimulation::resolve (zoneProperties, frames).has_value ();
}

void ZoneEditor::startSampleIntoLoop ()
{
    cancelAutoLoop (false);
    if (! canStartSampleIntoLoop ()) return;
    audioPlayerProperties.setPlayState (AudioPlayerProperties::PlayState::stop, false);
    audioPlayerProperties.setSampleSource (parentChannelIndex, zoneIndex, false);
    const auto ranges { resolvedRanges () };
    const bool startsWithinLoop { ranges.sampleStart >= ranges.loopStart };
    audioPlayerProperties.setSimulationPhase (startsWithinLoop ? AudioPlayerProperties::SimulationPhase::loop
                                                             : AudioPlayerProperties::SimulationPhase::sample, false);
    audioPlayerProperties.setSamplePointsSelector (startsWithinLoop ? AudioPlayerProperties::SamplePointsSelector::LoopPoints
                                                                  : AudioPlayerProperties::SamplePointsSelector::SamplePoints, false);
    audioPlayerProperties.setPlayState (AudioPlayerProperties::PlayState::sampleIntoLoop, false);
    updatePlaybackDisplay ();
}

void ZoneEditor::queuePlaybackDisplayUpdate ()
{
    const juce::Component::SafePointer<ZoneEditor> safe (this);
    deferPlaybackDisplayUpdate ([safe] ()
    {
        if (safe != nullptr) safe->updatePlaybackDisplay ();
    });
}

void ZoneEditor::updatePlaybackDisplay ()
{
    using State = AudioPlayerProperties::PlayState;
    const auto state { audioPlayerProperties.getPlayState () };
    const bool active { isCurrentAuditionSource () && oneShotPlayButton.isEnabled () && ! hasCvAuditionSource () };
    const bool simulation { active && state == State::sampleIntoLoop };
    const bool loopPhase { simulation && audioPlayerProperties.getSimulationPhase () == AudioPlayerProperties::SimulationPhase::loop };
    if (simulation)
        setActiveSamplePoints (loopPhase ? AudioPlayerProperties::SamplePointsSelector::LoopPoints
                                       : AudioPlayerProperties::SamplePointsSelector::SamplePoints, false, false);
    const bool onceActive { active && (state == State::play || (simulation && ! loopPhase)) };
    const bool loopActive { active && (state == State::loop || loopPhase) };
    oneShotPlayButton.setButtonText (onceActive ? "STOP" : "ONCE");
    loopPlayButton.setButtonText (loopActive ? "STOP" : "LOOP");
    oneShotPlayButton.setToggleState (onceActive, juce::dontSendNotification);
    loopPlayButton.setToggleState (loopActive, juce::dontSendNotification);
}

void ZoneEditor::setLoopLengthIsEnd (bool newLoopLengthIsEnd)
{
    treatLoopLengthAsEndInUi = newLoopLengthIsEnd;
    if (treatLoopLengthAsEndInUi)
    {
        loopLengthLabel.setText ("LOOP END", juce::NotificationType::dontSendNotification);
        loopLengthTextEditor.setInputRestrictions (0, ".0123456789");
    }
    else
    {
        loopLengthLabel.setText ("LOOP LENGTH", juce::NotificationType::dontSendNotification);
        loopLengthTextEditor.setInputRestrictions (0, ".0123456789");
    }
    // reformat the UI string
    loopLengthDataChanged (zoneProperties.getLoopLength ());
}

void ZoneEditor::updateNextButtons ()
{
    const auto canCopy { zoneIndex >= 0 && zoneIndex < 7 && ! isStereoRightChannelMode && zoneProperties.isValid () && zoneProperties.getSample ().isNotEmpty () };
    copyNextButton.setEnabled (canCopy);
    continueNextButton.setEnabled (canCopy);
}

void ZoneEditor::setStereoRightChannelMode (bool newStereoRightChannelMode)
{
    isStereoRightChannelMode = newStereoRightChannelMode;
    updateNextButtons ();

    updateAuditionControls ();
    toolsButton.setEnabled (! isStereoRightChannelMode);
    setEditComponentsEnabled (sampleProperties.getStatus () == SampleStatus::exists);
    //leftChannelSelectButton.setEnabled (! isStereoRightChannelMode); // can still edit in stereo/right channel mode
    //rightChannelSelectButton.setEnabled (! isStereoRightChannelMode); // can still edit in stereo/right channel mode
    //sampleNameSelectLabel.setEnabled (! isStereoRightChannelMode); // can still edit in stereo/right channel mode
}

void ZoneEditor::receiveSampleLoadRequest (juce::File sampleFile)
{
    handleSamplesInternal (zoneProperties.getId () - 1, { sampleFile.getFullPathName () });
}

void ZoneEditor::setupZonePropertiesCallbacks ()
{
    zoneProperties.onIdChange = [this] ([[maybe_unused]] int id) { jassertfalse; /* I don't think this should change while we are editing */ };
    zoneProperties.onLevelOffsetChange = [this] (double levelOffset) { levelOffsetDataChanged (levelOffset); };
    zoneProperties.onLoopLengthChange = [this] (std::optional<double> loopLength) { loopLengthDataChanged (loopLength); };
    zoneProperties.onLoopStartChange = [this] (std::optional <juce::int64> loopStart) { loopStartDataChanged (loopStart); };
    zoneProperties.onMinVoltageChange = [this] (double minVoltage) { minVoltageDataChanged (minVoltage); };
    zoneProperties.onPitchOffsetChange = [this] (double pitchOffset) { pitchOffsetDataChanged (pitchOffset); };
    zoneProperties.onSampleChange = [this] (juce::String sample) { sampleDataChanged (sample); };
    zoneProperties.onSampleStartChange = [this] (std::optional <juce::int64> sampleStart) { sampleStartDataChanged (sampleStart); };
    zoneProperties.onSampleEndChange = [this] (std::optional <juce::int64> sampleEnd) { sampleEndDataChanged (sampleEnd); };
    zoneProperties.onSideChange = [this] (int side) { sideDataChanged (side); };
}

void ZoneEditor::paint ([[maybe_unused]] juce::Graphics& g)
{
    // draw area to indicate active sample points (sample or loop)
    g.setColour (juce::Colours::grey.withAlpha (0.3f));
    g.fillRoundedRectangle (activePointBackground->toFloat (), 0.5f);
}

void ZoneEditor::paintOverChildren (juce::Graphics& g)
{
    // Keep the active region's outline visible above the opaque waveform view.
    g.setColour (Theme::muted);
    g.drawRoundedRectangle (activePointBackground->toFloat (), 0.5f, 1.f);

    const std::array<juce::Component*, 4> markerFields { &sampleStartTextEditor, &sampleEndTextEditor, &loopStartTextEditor, &loopLengthTextEditor };
    for (size_t marker { 0 }; marker < markerFields.size (); ++marker)
    {
        g.setColour (WaveformPresentation::markerColours[marker]);
        const auto bounds { markerFields[marker]->getBounds () };
        g.fillRect (bounds.getX () + 1, bounds.getCentreY () - 5, 3, 10);
    }

    juce::Colour fillColor { juce::Colours::white };
    float activeAlpha { 0.7f };
    float nonActiveAlpha { 0.2f };
    if (draggingFilesCount > 0)
    {
        auto localBounds { getLocalBounds () };
        if (supportedFile)
        {
            if (dropIndex == -1 || zoneProperties.getId () == 1)
            {
                g.setColour (fillColor.withAlpha (activeAlpha));
                g.fillRect (localBounds);
                g.setFont (20.0f);
                g.setColour (Theme::muted);
                g.drawText ("Start on Zone " + juce::String (zoneProperties.getId ()), localBounds, juce::Justification::centred, false);
            }
            else
            {
                g.setColour (fillColor.withAlpha (dropIndex == 0 ? activeAlpha : nonActiveAlpha));
                const auto topHalfBounds { localBounds.removeFromTop (localBounds.getHeight () / 2) };
                g.fillRect (topHalfBounds);
                g.setColour (fillColor.withAlpha (dropIndex == 1 ? activeAlpha : nonActiveAlpha));
                g.fillRect (localBounds);

                g.setFont (20.0f);
                g.setColour (Theme::muted);
                if (dropIndex == 0)
                    g.drawText ("Start on Zone 1", topHalfBounds, juce::Justification::centred, false);
                else
                    g.drawText ("Start on Zone " + juce::String (zoneProperties.getId ()), localBounds, juce::Justification::centred, false);
            }
        }
        else
        {
            g.setColour (fillColor.withAlpha (activeAlpha));
            g.fillRect (localBounds);
            g.setFont (20.0f);
            g.setColour (juce::Colours::red);
            localBounds.reduce (5, 0);
            g.drawFittedText (draggingFilesCount == 1 ? "Unsupported file type" : "One, or more, unsupported file types", localBounds, juce::Justification::centred, 10);
        }
    }
}

void ZoneEditor::resized ()
{
    copyNextButton.setBounds (10, getHeight () - 78, getWidth () - 20, 22);
    continueNextButton.setBounds (10, getHeight () - 52, getWidth () - 20, 22);
    const auto xOffset { 10 };
    const auto width { 160 };
    const auto interParameterYOffset { 1 };
    const auto spaceBetweenLabelAndInput { 3 };
    auto scaleWidth = [width] (float scaleAmount) { return static_cast<int> (width * scaleAmount); };

    jassert (displayToolsMenu != nullptr);
    toolsButton.setBounds (getWidth () - 5 - 92, getHeight () - 5 - 20, 92, 20);

    const auto sampleNameLabelScale { 0.156f };
    const auto sampleNameInputScale { 1.f - sampleNameLabelScale };
    sampleNameLabel.setBounds (xOffset, 5, scaleWidth (sampleNameLabelScale), 30);
    sampleNameSelectLabel.setBounds (sampleNameLabel.getRight () + spaceBetweenLabelAndInput, 5,
                                     scaleWidth (sampleNameInputScale) - spaceBetweenLabelAndInput + 1 - 26, 30);

    leftChannelSelectButton.setBounds (sampleNameSelectLabel.getRight () + 2, sampleNameSelectLabel.getY (), 24, 14);
    rightChannelSelectButton.setBounds (sampleNameSelectLabel.getRight () + 2, leftChannelSelectButton.getBottom () + 2, 24, 14);

    const auto loopPointsViewHeight { 82 }; // trace plus a separate transport row
    const auto samplePointLabelScale { 0.45f };
    const auto samplePointInputScale { 1.f - samplePointLabelScale };
    a8SelectNameLabel.setBounds (xOffset, sampleNameSelectLabel.getBottom () + 1, width, 13);
    a8ChannelNameLabel.setBounds (xOffset, a8SelectNameLabel.getBottom (), width, 13);
    importSamplesButton.setBounds (xOffset, a8ChannelNameLabel.getBottom () + 3, width, 20);
    sampleStartLabel.setBounds (xOffset, importSamplesButton.getBottom () + 5, scaleWidth (samplePointLabelScale), 20);
    sampleStartTextEditor.setBounds (sampleStartLabel.getRight () + spaceBetweenLabelAndInput, sampleStartLabel.getY (), scaleWidth (samplePointInputScale) - spaceBetweenLabelAndInput, 20);
    sampleEndLabel.setBounds (xOffset, sampleStartLabel.getBottom () + interParameterYOffset, scaleWidth (samplePointLabelScale), 20);
    sampleEndTextEditor.setBounds (sampleEndLabel.getRight () + spaceBetweenLabelAndInput, sampleEndLabel.getY (), scaleWidth (samplePointInputScale) - spaceBetweenLabelAndInput, 20);
    sampleDurationLabel.setBounds (xOffset, sampleEndTextEditor.getBottom () + 1, width, 14);
    autoLoopButton.setBounds (xOffset, sampleDurationLabel.getBottom () + 1, width, 20);
    auto loopPointsViewBounds { juce::Rectangle<int> { xOffset, autoLoopButton.getBottom () + interParameterYOffset, width + 1, loopPointsViewHeight } };
    samplePointsBackground = juce::Rectangle<int>::leftTopRightBottom (sampleStartLabel.getX (), sampleStartLabel.getY () - 1,
        sampleEndTextEditor.getRight () + 1, loopPointsViewBounds.getBottom () + 1);
    loopPointsView.setBounds (loopPointsViewBounds.withTrimmedBottom (24));

    loopStartLabel.setBounds (xOffset, loopPointsViewBounds.getBottom () + interParameterYOffset, scaleWidth (samplePointLabelScale), 20);
    loopStartTextEditor.setBounds (loopStartLabel.getRight () + spaceBetweenLabelAndInput, loopStartLabel.getY (), scaleWidth (samplePointInputScale) - spaceBetweenLabelAndInput, 20);
    loopLengthLabel.setBounds (xOffset, loopStartLabel.getBottom () + interParameterYOffset, scaleWidth (samplePointLabelScale), 20);
    loopLengthTextEditor.setBounds (loopLengthLabel.getRight () + spaceBetweenLabelAndInput, loopLengthLabel.getY (), scaleWidth (samplePointInputScale) - spaceBetweenLabelAndInput, 20);
    loopDurationLabel.setBounds (xOffset, loopLengthTextEditor.getBottom () + 1, width, 14);
    loopPointsBackground = juce::Rectangle<int>::leftTopRightBottom (loopStartLabel.getX (), loopPointsView.getY () - 1,
                                                                   loopLengthTextEditor.getRight () + 1, loopDurationLabel.getBottom () + 1);

    oneShotPlayButton.setBounds (xOffset + 2, loopPointsView.getBottom () + 2, 76, 20);
    loopPlayButton.setBounds (xOffset + 82, loopPointsView.getBottom () + 2, 76, 20);

    const auto otherLabelScale { 0.66f };
    const auto otherInputScale { 1.f - otherLabelScale };
    minVoltageLabel.setBounds (xOffset, loopDurationLabel.getBottom () + 5, scaleWidth (otherLabelScale), 20);
    minVoltageTextEditor.setBounds (minVoltageLabel.getRight () + spaceBetweenLabelAndInput, minVoltageLabel.getY (), scaleWidth (otherInputScale) - spaceBetweenLabelAndInput, 20);

    pitchOffsetLabel.setBounds (xOffset, minVoltageTextEditor.getBottom () + 3, scaleWidth (otherLabelScale), 20);
    pitchOffsetTextEditor.setBounds (pitchOffsetLabel.getRight () + spaceBetweenLabelAndInput, pitchOffsetLabel.getY (), scaleWidth (otherInputScale) - spaceBetweenLabelAndInput, 20);

    levelOffsetLabel.setBounds (xOffset, pitchOffsetLabel.getBottom () + 3, scaleWidth (otherLabelScale), 20);
    levelOffsetTextEditor.setBounds (levelOffsetLabel.getRight () + spaceBetweenLabelAndInput, levelOffsetLabel.getY (), scaleWidth (otherInputScale) - spaceBetweenLabelAndInput, 20);
    cvAuditionNotice.setBounds (xOffset, levelOffsetTextEditor.getBottom () + 3, width, 28);
}

void ZoneEditor::setEditComponentsEnabled (bool enabled)
{
    const auto boundariesEnabled { enabled && ! isStereoRightChannelMode && sampleProperties.getLengthInSamples () >= 4 };
    sampleStartTextEditor.setEnabled (boundariesEnabled);
    sampleEndTextEditor.setEnabled (boundariesEnabled);
    loopStartTextEditor.setEnabled (boundariesEnabled);
    loopLengthTextEditor.setEnabled (boundariesEnabled);
    minVoltageTextEditor.setEnabled (enabled && ! isStereoRightChannelMode);
    pitchOffsetTextEditor.setEnabled (enabled && ! isStereoRightChannelMode);
    levelOffsetTextEditor.setEnabled (enabled && ! isStereoRightChannelMode);
}

double ZoneEditor::snapLoopLength (double rawValue)
{
    if (rawValue < 2048)
    {
        if (rawValue < 4.0)
            return 4.0;

        auto snapToResolution = [] (double number, double resolution) { return std::round (number / resolution) * resolution; };
        const auto wholeValue { static_cast<uint32_t> (rawValue) };
        const auto fractionalValue { rawValue - static_cast<double> (wholeValue) };

        auto getFractionalSize = [] (uint32_t wholeValue)
        {
            auto calculateFractionalSize = [] (int numberOfBits) { return 1.0 / (1 << numberOfBits); };
            if (wholeValue > 1024)
                return calculateFractionalSize (1);
            else if (wholeValue > 512)
                return calculateFractionalSize (2);
            else if (wholeValue > 256)
                return calculateFractionalSize (3);
            else if (wholeValue > 128)
                return calculateFractionalSize (4);
            else if (wholeValue >= 4)
                return calculateFractionalSize (5);
            else
            {
                jassertfalse;
                return 0.0;
            }
        };
        const auto snappedFractionalValue { snapToResolution (fractionalValue, getFractionalSize (wholeValue)) };
        return static_cast<double> (wholeValue) + snappedFractionalValue;
    }
    else
    {
        return static_cast<uint32_t> (rawValue);
    }
}

juce::PopupMenu ZoneEditor::createSampleFileMenu ()
{
    if (zoneProperties.getSample ().isEmpty ()) return {};
    // The preset editor captures the destination snapshot while this menu is
    // created, so a delayed popup action cannot rename a newly selected file.
    auto menu { createSampleFileActions ? createSampleFileActions () : juce::PopupMenu {} };
    if (isStereoRightChannelMode) return menu;
    if (menu.getNumItems () != 0) menu.addSeparator ();
    return createZoneEditMenu (std::move (menu), [this] (ZoneProperties& destination, SampleProperties&)
        {
            const auto file { juce::File (appProperties.getMostRecentFolder ()).getChildFile (zoneProperties.getSample ()) };
            handleSamplesInternal (destination.getId () - 1, { file.getFullPathName () });
        }, nullptr, [this] () { handleSamplesInternal (zoneIndex, { uneditedZoneProperties.getSample () }); },
        [this] (ZoneProperties& destination) { return destination.getId () - 1 <= editManager->getNumUsedZones (parentChannelIndex); },
        [] (ZoneProperties&) { return true; });
}

juce::PopupMenu ZoneEditor::createZoneEditMenu (juce::PopupMenu existingPopupMenu, std::function <void (ZoneProperties&, SampleProperties&)> setter, std::function <void ()> resetter, std::function <void ()> reverter,
                                                std::function<bool (ZoneProperties&)> canCloneCallback, std::function<bool (ZoneProperties&)> canCloneToAllCallback)
{
    // you can pass in a nullptr for one of the callbacks, to disable that item, but at least one of these should be valid, if not the caller should just not be trying to display an edit menu
    jassert (setter != nullptr || resetter != nullptr);
    jassert (reverter != nullptr);
    jassert (canCloneCallback != nullptr);
    jassert (canCloneToAllCallback != nullptr);
    juce::PopupMenu editMenu (existingPopupMenu);
    if (setter != nullptr)
    {
        juce::PopupMenu cloneMenu;
        for (auto destZoneIndex { 0 }; destZoneIndex < 8; ++destZoneIndex)
        {
            // TODO - the actual clone code should be in the EditManager too
            if (destZoneIndex != zoneIndex)
            {
                auto canCloneToDestZone { true };
                editManager->forZones (parentChannelIndex, { destZoneIndex }, [this, canCloneCallback, &canCloneToDestZone] (juce::ValueTree zonePropertiesVT, juce::ValueTree sampleZonePropertiesVT)
                {
                    ZoneProperties destZoneProperties (zonePropertiesVT, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                    canCloneToDestZone = canCloneCallback (destZoneProperties);
                });
                if (canCloneToDestZone)
                {
                    cloneMenu.addItem ("To Zone " + juce::String (destZoneIndex + 1), true, false, [this, destZoneIndex, setter] ()
                    {
                        editManager->forZones (parentChannelIndex, { destZoneIndex }, [this, setter] (juce::ValueTree zonePropertiesVT, juce::ValueTree sampleZonePropertiesVT)
                        {
                            ZoneProperties destZoneProperties (zonePropertiesVT, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                            SampleProperties destSampleProperties (sampleZonePropertiesVT, SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
                            setter (destZoneProperties, destSampleProperties);
                        });
                    });
                }
            }
        }
        std::vector<int> zoneIndexList;
        // build list of other zones
        for (auto destZoneIndex { 0 }; destZoneIndex < 8; ++destZoneIndex)
            if (destZoneIndex != zoneIndex)
                zoneIndexList.emplace_back (destZoneIndex);
        auto canCloneDestZoneCount { 0 };
        editManager->forZones (parentChannelIndex, zoneIndexList, [this, canCloneToAllCallback, &canCloneDestZoneCount] (juce::ValueTree zonePropertiesVT, juce::ValueTree sampleZonePropertiesVT)
        {
            ZoneProperties destZoneProperties (zonePropertiesVT, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            if (canCloneToAllCallback (destZoneProperties))
                ++canCloneDestZoneCount;
        });
        if (canCloneDestZoneCount > 0)
        {
            cloneMenu.addItem ("To All", true, false, [this, setter, zoneIndexList] ()
            {
                // clone to other zones
                editManager->forZones (parentChannelIndex, zoneIndexList, [this, setter] (juce::ValueTree zonePropertiesVT, juce::ValueTree sampleZonePropertiesVT)
                {
                    ZoneProperties destZoneProperties (zonePropertiesVT, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                    SampleProperties destSampleProperties (sampleZonePropertiesVT, SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
                    setter (destZoneProperties, destSampleProperties);
                });
            });
        }
        editMenu.addSubMenu ("Clone", cloneMenu, true);
    }

    if (resetter != nullptr)
        editMenu.addItem ("Default", true, false, [this, resetter] () { resetter (); });

    editMenu.addItem ("Revert", true, false, [this, reverter] () { reverter (); });

    return editMenu;
}

juce::String ZoneEditor::formatLoopLength (double loopLength)
{
    const auto fractional { loopLength < 2048.0 || loopLength != std::floor (loopLength) };
    if (treatLoopLengthAsEndInUi)
        loopLength += resolvedRanges ().loopStart;

    // value >= 2048 - no decimals
    // value < 2048 - 3 decimal places
    // 1024.000 < value < 2047.000 - 0.500 increment
    //  512.000 < value < 1024.000 - 0.250 increment (0.25, 0.5, 0.75, 1.000)
    //  256.000 < value <  512.000 - 0.125 increment (0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1.000)
    //  128.000 < value <  256.000 - 0.063 increment (0.063, 0.125, 0.188, 0.250, 0.313, 0.375, 0.438, 0.500, 0.563, 0.625, 0.688, 0.750, 0.813, 0.875, 0.938, 1.000)
    //    0.000 < value <  128.000 - 0.031 increment  (0.031, 0.063, 0.094, 0.125, 0.156, 0.188, 0.219, 0.250, 0.281, 0.313, 0.344, 0.375, 0.406, 0.438, 0.469, 0.500,
    //                                                 0.531, 0.563, 0.594, 0.625, 0.656, 0.688, 0.719, 0.750, 0.781, 0.813, 0.844, 0.875, 0.906, 0.938, 0.969, 1.000)

    if (fractional)
        return FormatHelpers::formatDouble (loopLength, 3, false);
    else
        return juce::String (static_cast<juce::int64> (loopLength));
}

ZoneSampleRanges::Mode ZoneEditor::rangeMode () const
{
    return treatLoopLengthAsEndInUi ? ZoneSampleRanges::Mode::end : ZoneSampleRanges::Mode::length;
}

ZoneSampleRanges::Resolved ZoneEditor::resolvedRanges ()
{
    return ZoneSampleRanges::resolve (ZoneSampleRanges::read (zoneProperties), sampleProperties.getLengthInSamples (), parentChannelProperties.getAllowLoopOutsideSample ());
}

ZoneSampleRanges::Limits ZoneEditor::boundaryLimits (ZoneSampleRanges::Marker marker)
{
    return ZoneSampleRanges::limits (ZoneSampleRanges::read (zoneProperties), sampleProperties.getLengthInSamples (), marker, rangeMode (), false,
                                     parentChannelProperties.getAllowLoopOutsideSample ());
}

void ZoneEditor::editBoundary (ZoneSampleRanges::Marker marker, double value)
{
    const auto frames { sampleProperties.getLengthInSamples () };
    const auto allow { parentChannelProperties.getAllowLoopOutsideSample () };
    ZoneSampleRanges::apply (zoneProperties,
        ZoneSampleRanges::edit (ZoneSampleRanges::read (zoneProperties), frames, marker, value, rangeMode (), false, allow), frames, false, allow);
    updateSamplePositionInfo ();
}

void ZoneEditor::levelOffsetDataChanged (double levelOffset)
{
    levelOffsetTextEditor.setText (FormatHelpers::formatDouble (levelOffset, 1, true));
}

void ZoneEditor::levelOffsetUiChanged (double levelOffset)
{
    zoneProperties.setLevelOffset (levelOffset, false);
}

void ZoneEditor::loopLengthDataChanged (std::optional<double>)
{
    updateSamplePositionInfo ();
}

void ZoneEditor::loopLengthUiChanged (double loopLength)
{
    editBoundary (ZoneSampleRanges::Marker::loopEnd, resolvedRanges ().loopStart + loopLength);
}

void ZoneEditor::loopStartDataChanged (std::optional<juce::int64>)
{
    updateSamplePositionInfo ();
}

void ZoneEditor::loopStartUiChanged (juce::int64 loopStart)
{
    editBoundary (ZoneSampleRanges::Marker::loopStart, static_cast<double> (loopStart));
}

void ZoneEditor::minVoltageDataChanged (double minVoltage)
{
    minVoltageTextEditor.setText (FormatHelpers::formatDouble (minVoltage, 2, true));
}

void ZoneEditor::minVoltageUiChanged (double minVoltage)
{
    zoneProperties.setMinVoltage (minVoltage, false);
}

void ZoneEditor::pitchOffsetDataChanged (double pitchOffset)
{
    pitchOffsetTextEditor.setText (FormatHelpers::formatDouble (pitchOffset, 2, true));
    updateDurations ();
}

void ZoneEditor::pitchOffsetUiChanged (double pitchOffset)
{
    zoneProperties.setPitchOffset (pitchOffset, false);
    updateDurations ();
}

void ZoneEditor::updateSampleFileInfo (juce::String sample)
{
    jassert (! sample.isEmpty ());
    auto textColor { Theme::text };
    if (sampleProperties.getStatus () == SampleStatus::exists)
    {
        updateSamplePositionInfo ();
    }
    else
    {
        textColor = Theme::error;
    }
    loopPointsView.repaint ();
    const auto valid { textColor != Theme::error };
    Theme::bindColour (sampleNameSelectLabel, juce::Label::textColourId, [valid] { return valid ? Theme::text : Theme::error; });
}

void ZoneEditor::updateSamplePositionInfo ()
{
    const auto ranges { resolvedRanges () };
    const auto known { sampleProperties.getStatus () != SampleStatus::uninitialized };
    sampleStartTextEditor.setText (juce::String (ranges.sampleStart));
    sampleEndTextEditor.setText (known ? juce::String (ranges.sampleEnd) : "0");
    loopStartTextEditor.setText (juce::String (ranges.loopStart));
    loopLengthTextEditor.setText (known ? formatLoopLength (ranges.loopLength) : "0");
    updateLoopPointsView ();
}

void ZoneEditor::updateSideSelectButtons (int side)
{
    auto disableButtons = [this] ()
    {
        leftChannelSelectButton.setEnabled (false);
        rightChannelSelectButton.setEnabled (false);
        leftChannelSelectButton.setToggleState (false, juce::NotificationType::dontSendNotification);
        rightChannelSelectButton.setToggleState (false, juce::NotificationType::dontSendNotification);
    };
    if (sampleProperties.getStatus () != SampleStatus::exists)
    {
        disableButtons ();
    }
    else if (sampleProperties.getNumChannels () == 1)
    {
        disableButtons ();
    }
    else
    {
        leftChannelSelectButton.setEnabled (true);
        rightChannelSelectButton.setEnabled (true);
        leftChannelSelectButton.setToggleState (side == 0, juce::NotificationType::dontSendNotification);
        rightChannelSelectButton.setToggleState (side == 1, juce::NotificationType::dontSendNotification);
    }
}

void ZoneEditor::sampleDataChanged (juce::String sample)
{
    if (autoLoopPlaying || (pendingAutoLoop && pendingAutoLoop->sample != sample))
        cancelAutoLoop (true);
    updateNextButtons ();
    //DebugLog ("ZoneEditor", "ZoneEditor[" + juce::String (zoneProperties.getId ()) + "]::sampleDataChanged: '" + sample + "'");
    sampleNameSelectLabel.setText (sample, juce::NotificationType::dontSendNotification);
    const auto preview { A8NamePreview::fromFilename (sample) };
    a8SelectNameLabel.setText (sample.isEmpty () ? juce::String {} : "A8 Select: " + preview.select, juce::dontSendNotification);
    a8ChannelNameLabel.setText (sample.isEmpty () ? juce::String {} : "A8 Channels: " + preview.channel, juce::dontSendNotification);
}

void ZoneEditor::sampleUiChanged (juce::String sample)
{
    zoneProperties.setSample (sample, false);
}

void ZoneEditor::sampleStartDataChanged (std::optional<juce::int64>)
{
    updateSamplePositionInfo ();
}

void ZoneEditor::sampleStartUiChanged (juce::int64 sampleStart)
{
    editBoundary (ZoneSampleRanges::Marker::sampleStart, static_cast<double> (sampleStart));
}

void ZoneEditor::sampleEndDataChanged (std::optional<juce::int64>)
{
    updateSamplePositionInfo ();
}

void ZoneEditor::sampleEndUiChanged (juce::int64 sampleEnd)
{
    editBoundary (ZoneSampleRanges::Marker::sampleEnd, static_cast<double> (sampleEnd));
}

void ZoneEditor::sideDataChanged (int side)
{
    if (sampleProperties.getStatus () == SampleStatus::exists && sampleProperties.getNumChannels () == 1 && side == 1)
    {
        side = 0;
        zoneProperties.setSide (0, false);
    }
    updateSideSelectButtons (side);
    updateLoopPointsView ();
}

void ZoneEditor::sideUiChanged (int side)
{
    zoneProperties.setSide (side, false);
    updateLoopPointsView ();
}
