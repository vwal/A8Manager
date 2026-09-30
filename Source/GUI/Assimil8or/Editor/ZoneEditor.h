#pragma once

#include <JuceHeader.h>
#include "EditManager.h"
#include "LoopPoints/LoopPointsView.h"
#include "SampleManager/SampleProperties.h"
#include "../../../AppProperties.h"
#include "../../../Assimil8or/Audio/AudioManager.h"
#include "../../../Assimil8or/Audio/AudioPlayerProperties.h"
#include "../../../Assimil8or/Preset/ZoneProperties.h"
#include "../../../Assimil8or/Preset/ZoneSampleRanges.h"
#include "../../DragValueEditor.h"
#include "../../SelectableFileLabel.h"

class ZoneEditor : public juce::Component,
                   public juce::FileDragAndDropTarget
{
public:
    ZoneEditor ();
    ~ZoneEditor () = default;

    void init (juce::ValueTree zonePropertiesVT, juce::ValueTree uneditedZonePropertiesVT, juce::ValueTree rootPropertiesVT);
    // TODO - can we move this to the EditManager, as it just calls editManager->assignSamples (parentChannelIndex, startingZoneIndex, files); in the ZoneEditor
    void receiveSampleLoadRequest (juce::File sampleFile);
    // TODO - is there a VTW that could manage this setting?
    void setLoopLengthIsEnd (bool loopLengthIsEnd);
    void setStereoRightChannelMode (bool isStereoRightChannelMode);
    void selectLoop (bool loop) { setActiveSamplePoints (loop ? AudioPlayerProperties::SamplePointsSelector::LoopPoints : AudioPlayerProperties::SamplePointsSelector::SamplePoints, false); }
    bool isLoopSelected () const { return samplePointsSelector == AudioPlayerProperties::SamplePointsSelector::LoopPoints; }
    std::function<void (bool)> onRegionSelected;
    std::function<juce::PopupMenu (int marker)> createBoundaryAdjustmentMenu;
    bool canStartSampleIntoLoop ();
    void startSampleIntoLoop ();
    std::function<void ()> onSimulationAvailabilityChanged;

    // TODO - can we make this local, since we should be able to access the edits through the EditManager
    std::function<void (int zoneIndex)> displayToolsMenu;
    std::function<void (bool continueSlice)> copyToNext;
    std::function<juce::PopupMenu ()> createSampleFileActions;
    juce::Rectangle<int> getSampleFileBounds () const { return sampleNameLabel.getBounds ().getUnion (sampleNameSelectLabel.getBounds ()).getUnion (importSamplesButton.getBounds ()); }

private:
    void lookAndFeelChanged () override { sampleNameSelectLabel.setOutline (Theme::border); repaint (); }
    friend struct ZoneEditorTestAccess;
    friend struct AudioAuditTestAccess;
    friend struct CvAuditionTestAccess;
    friend struct SimulationUiTestAccess;
    friend struct SampleRenameUiTestAccess;
    friend struct ZoneSampleRangesTestAccess;
    friend struct AutoLoopUiTestAccess;
    class ClickListener : public juce::MouseListener
    {
    public:
        std::function<void ()> onClick;
    private:
        void mouseDown (const juce::MouseEvent&) override
        {
            if (onClick != nullptr)
                onClick ();
        }
    };

    AppProperties appProperties;
    AudioPlayerProperties audioPlayerProperties;
    ZoneProperties zoneProperties;
    ZoneProperties uneditedZoneProperties;
    ZoneProperties minZoneProperties;
    ZoneProperties maxZoneProperties;
    SampleProperties sampleProperties;
    SampleProperties previousSampleProperties, nextSampleProperties;
    ChannelProperties previousChannelProperties, nextChannelProperties;
    // TODO - I want to remove ChannelProperties!
    ChannelProperties parentChannelProperties;
    // TODO - I think we might be able to get rid of currentSampleFileName too, but I am not sure yet
    juce::String currentSampleFileName;
    EditManager* editManager { nullptr };
    AudioManager* audioManager { nullptr };
    int zoneIndex { -1 };
    int parentChannelIndex { -1 };
    bool isStereoRightChannelMode { false };

    // Loop Length is always stored as loop length, but the UI can be toggled to display it, and take input for it, as if it is Loop End
    bool treatLoopLengthAsEndInUi { false };

    int draggingFilesCount { 0 };
    bool supportedFile { false };
    juce::String dropMsg;
    juce::StringArray dropDetails;

    int dropIndex { 0 };

    LoopPointsView loopPointsView;
    juce::TextButton oneShotPlayButton;
    juce::TextButton loopPlayButton;
    juce::ToggleButton autoLoopButton { "Auto Loop" };
    juce::TextButton importSamplesButton { "Import WAV..." };
    struct LoadContext
    {
        juce::ValueTree zone, preset, before;
        juce::String folder, sample;
        unsigned int generation;
    };
    unsigned int loadContextGeneration { 0 };
    std::optional<LoadContext> pendingAutoLoop;
    bool autoLoopPlaying { false };
    std::unique_ptr<juce::FileChooser> sampleChooser;
    std::function<bool ()> isActiveLoadTarget = [this] { return isShowing (); };
    std::function<void (std::function<void ()>)> deferAutoLoop = [] (std::function<void ()> callback)
        { juce::MessageManager::callAsync (std::move (callback)); };
    std::function<void (juce::File, std::function<void (juce::StringArray)>)> chooseSampleFiles;
    LoadContext captureLoadContext ();
    bool loadContextMatches (const LoadContext&);
    void importSamples ();
    void queueAutoLoopAttempt ();
    void tryAutoLoop ();
    void cancelAutoLoop (bool stopPlayback);
    void invalidateLoadContext ();
    void visibilityChanged () override;
    juce::Label cvAuditionNotice;
    bool hasCvAuditionSource ();
    void updateAuditionControls ();
    bool isCurrentAuditionSource ();
    void updatePlaybackDisplay ();
    void queuePlaybackDisplayUpdate ();
    std::function<void (std::function<void ()>)> deferPlaybackDisplayUpdate = [] (std::function<void ()> callback)
    {
        juce::MessageManager::callAsync (std::move (callback));
    };
    juce::TextButton toolsButton;
    juce::TextButton copyNextButton { "Copy > next" };
    juce::TextButton continueNextButton { "Continue > next" };
    void updateNextButtons ();
    juce::Rectangle<int> samplePointsBackground;
    juce::Rectangle<int> loopPointsBackground;
    juce::Rectangle<int>* activePointBackground { &samplePointsBackground };

    juce::Label levelOffsetLabel;
    DragValueEditorDouble levelOffsetTextEditor; // double
    juce::Label loopLengthLabel;
    DragValueEditorDouble loopLengthTextEditor; // double
    juce::Label loopStartLabel;
    DragValueEditorInt64 loopStartTextEditor; // int
    juce::Label minVoltageLabel;
    DragValueEditorDouble minVoltageTextEditor; // double
    juce::Label pitchOffsetLabel;
    DragValueEditorDouble pitchOffsetTextEditor; // double
    juce::TextButton leftChannelSelectButton;
    juce::TextButton rightChannelSelectButton;
    juce::Label sampleNameLabel;
    SelectableFileLabel sampleNameSelectLabel; // selectable read-only filename
    juce::Label a8SelectNameLabel, a8ChannelNameLabel;
    juce::PopupMenu createSampleFileMenu ();
    juce::Label sampleEndLabel;
    DragValueEditorInt64 sampleEndTextEditor; // int
    juce::Label sampleStartLabel;
    juce::Label sampleDurationLabel, loopDurationLabel;
    void updateDurations ();
    DragValueEditorInt64 sampleStartTextEditor; // int

    ClickListener selectSamplePointsClickListener;
    ClickListener selectLoopPointsClickListener;

    AudioPlayerProperties::SamplePointsSelector samplePointsSelector { AudioPlayerProperties::SamplePointsSelector::SamplePoints };

    void setEditComponentsEnabled (bool enabled);
    juce::PopupMenu createZoneEditMenu (juce::PopupMenu existingPopupMenu, std::function <void (ZoneProperties&, SampleProperties&)> setter, std::function <void ()> resetter, std::function <void ()> reverter,
                                        std::function<bool (ZoneProperties&)> canCloneToZoneCallback, std::function<bool (ZoneProperties&)> canCloneToAllCallback);
    juce::String formatLoopLength (double loopLength);
    enum class SampleMarker { sampleStart, sampleEnd, loopStart, loopEnd };
    juce::PopupMenu getSampleAdjustMenu (SampleMarker marker);
    juce::PopupMenu getLoopPointsMenu (bool start);
    bool handleSamplesInternal (int zoneIndex, juce::StringArray files);
    void setActiveSamplePoints (AudioPlayerProperties::SamplePointsSelector samplePointsSelector, bool forceSetup, bool publishToPlayer = true);
    void setupZoneComponents ();
    void setupZonePropertiesCallbacks ();
    double snapLoopLength (double rawValue);
    ZoneSampleRanges::Mode rangeMode () const;
    ZoneSampleRanges::Resolved resolvedRanges ();
    ZoneSampleRanges::Limits boundaryLimits (ZoneSampleRanges::Marker marker);
    void editBoundary (ZoneSampleRanges::Marker marker, double value);
    void updateLoopPointsView ();
    void updateSampleFileInfo (juce::String sample);
    void updateSamplePositionInfo ();
    void updateSideSelectButtons (int side);

    void levelOffsetDataChanged (double levelOffset);
    void levelOffsetUiChanged (double levelOffset);
    void loopLengthDataChanged (std::optional<double> loopLength);
    void loopLengthUiChanged (double loopLength);
    void loopStartDataChanged (std::optional <juce::int64> loopStart);
    void loopStartUiChanged (juce::int64 loopStart);
    void minVoltageDataChanged (double minVoltage);
    void minVoltageUiChanged (double minVoltage);
    void pitchOffsetDataChanged (double pitchOffset);
    void pitchOffsetUiChanged (double pitchOffset);
    void resetDropInfo ();
    void sampleDataChanged (juce::String sample);
    void sampleUiChanged (juce::String sample);
    void sampleStartDataChanged (std::optional <juce::int64> sampleStart);
    void sampleStartUiChanged (juce::int64 sampleStart);
    void sampleEndDataChanged (std::optional <juce::int64> sampleEnd);
    void sampleEndUiChanged (juce::int64 sampleEnd);
    void sideDataChanged (int side);
    void sideUiChanged (int side);
    void setDropIndex (const juce::StringArray& files, int x, int y);
    void updateDropInfo (const juce::StringArray& files);

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray& files, int, int) override;
    void fileDragMove (const juce::StringArray& files, int, int) override;
    void fileDragExit (const juce::StringArray& files) override;

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void resized () override;
};
