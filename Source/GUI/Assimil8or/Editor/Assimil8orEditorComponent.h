#pragma once

#include <JuceHeader.h>
#include "ChannelEditor.h"
#include "CvInputComboBox.h"
#include "EditManager.h"
#include "../../GuiControlProperties.h"
#include "../../../AppProperties.h"
#include "../../../Assimil8or/Audio/AudioPlayerProperties.h"
#include "../../../Assimil8or/Preset/PresetProperties.h"
#include "../../DragValueEditor.h"
#include "../../PresetEditSession.h"
#include "oolib/Debug/DebugLog.h"
#include "oolib/Properties/RuntimeRootProperties.h"

class WindowDecorator : public juce::Component
{
public:
private:
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::border);
        g.drawLine ({ getLocalBounds ().getTopLeft ().toFloat (),getLocalBounds ().getTopRight ().toFloat () });
    }
};

class Assimil8orEditorComponent : public juce::Component,
                                  public juce::Timer
{
public:
    Assimil8orEditorComponent ();
    ~Assimil8orEditorComponent () = default;

    void init (juce::ValueTree rootPropertiesVT);
    void receiveSampleLoadRequest (juce::File sampleFile);
    void overwritePresetOrCancel (std::function<void ()> overwriteFunction, std::function<void ()> cancelFunction);
    std::optional<double> getSelectedDuration (int region);
    void savePreset ();
    void recallSelectedWaveform ();
    bool canRecallSelectedWaveform ();
    std::function<void (int channel, int zone)> onRecallWaveform;

private:
    friend struct StereoChannelUiTestAccess;
    friend struct ChannelPurgeUiTestAccess;
    RuntimeRootProperties runtimeRootProperties;
    AppProperties appProperties;
    AudioPlayerProperties audioPlayerProperties;
    GuiControlProperties guiControlProperties;
    PresetProperties presetProperties;
    PresetProperties unEditedPresetProperties;
    PresetProperties defaultPresetProperties;
    PresetProperties minPresetProperties;
    PresetProperties maxPresetProperties;
    ChannelProperties defaultChannelProperties;
    ChannelProperties copyBufferChannelProperties;
    ZoneProperties copyBufferZoneProperties;
    bool copyBufferHasData { false };
    bool channelEditorsInitialized { false };
    EditManager* editManager { nullptr };
    PresetEditSession channelActionSession;
    unsigned purgeConfirmation { 0 };
    std::function<void (const juce::String&, const juce::String&, std::function<void (bool)>)> confirmChannelPurge;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Label titleLabel;
    juce::TextButton saveButton;
    juce::TextButton toolsButton;

    TabbedComponentWithChangeCallback channelTabs { juce::TabbedButtonBar::Orientation::TabsAtTop };
    WindowDecorator windowDecorator;

    // Preset Parameters
    juce::TextEditor nameEditor;
    juce::Label midiSetupLabel;
    CustomComboBox midiSetupComboBox;

    juce::Label data2AsCvLabel;
    CvInputGlobalComboBox data2AsCvComboBox;
    juce::Label xfadeGroupsLabel;
    struct XfadeGroupControls
    {
        juce::Label xfadeGroupLabel;
        juce::Label xfadeCvLabel;
        CvInputGlobalComboBox xfadeCvComboBox;
        juce::Label xfadeWidthLabel;
        DragValueEditorDouble xfadeWidthEditor;
    };
    enum XfadeGroupIndex
    {
        groupA,
        groupB,
        groupC,
        groupD,
        numberOfGroups
    };
    std::array<XfadeGroupControls, 4> xfadeGroups;
    std::array<ChannelEditor, 8> channelEditors;
    std::array<ChannelProperties, 8> channelProperties;

    void displayToolsMenu ();
    juce::PopupMenu createPresetToolsMenu ();
    void displayChannelToolsMenu (int channelIndex);
    juce::PopupMenu createChannelToolsMenu (int channelIndex);
    void addChannelDefaultMenuItem (juce::PopupMenu& menu, int channelIndex);
    void addChannelPurgeMenuItem (juce::PopupMenu& menu, int channelIndex);
    void synchronizeStereoZones (int sourceChannel, int zoneIndex);
    void synchronizeAllStereoZones ();
    void explodeChannel (int channelIndex, int explodeCount);
    void exportPresetSettings ();
    void exportPresetSettingsAndSamples ();
    juce::String formatXfadeWidthString (double width);
    void importPresetSettings ();
    void importPresetSettingsAndSamples ();
    juce::PopupMenu createChannelCloneMenu (int channelIndex,   std::function <void (ChannelProperties&)> setter);
    bool isChannelActive (int channelIndex);
    void revertPreset ();
    void setPresetToDefaults ();
    void setupPresetComponents ();
    void setupPresetPropertiesCallbacks ();
    void updateAllChannelTabNames ();
    void updateChannelTabName (int channelIndex);

    // Preset callbacks
    void idDataChanged (int id); // tracks when a new preset has been loaded
    void midiSetupDataChanged (int midiSetupId);
    void midiSetupUiChanged (int midiSetupId);
    void nameDataChanged (juce::String name);
    void nameUiChanged (juce::String name);
    void data2AsCvDataChanged (juce::String data2AsCvString);
    void data2AsCvUiChanged (juce::String data2AsCvString);
    void xfadeCvDataChanged (int group, juce::String data2AsCvString);
    void xfadeCvUiChanged (int group, juce::String data2AsCvString);
    void xfadeWidthDataChanged (int group, double);
    void xfadeWidthUiChanged (int group, double);

    void timerCallback () override;
    void resized () override;
    void paint (juce::Graphics& g) override;
};
