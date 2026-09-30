#include "Assimil8orEditorComponent.h"
#include "WaveformDuration.h"
#include "../../ModernTheme.h"
#include "ParameterToolTipData.h"
#include "../../../SystemServices.h"
#include "../../../Assimil8or/Assimil8orPreset.h"
#include "../../../Assimil8or/PresetFileOperations.h"
#include "../../../Assimil8or/PresetArchive.h"
#include "../../../Assimil8or/PresetManagerProperties.h"
#include "../../../Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "../../../Assimil8or/Preset/PresetHelpers.h"
#include "../../../Assimil8or/Preset/StereoChannelTools.h"
#include "../../../Assimil8or/Audio/WaveformDesignRecall.h"
#include "../../../Assimil8or/Audio/AudioPlayer.h"
#include "oolib/Debug/DebugLog.h"
#include "oolib/Debug/DumpStack.h"
#include "oolib/GUI/ErrorHelpers.h"
#include "oolib/Properties/PersistentRootProperties.h"
#include <algorithm>
#include <atomic>

namespace
{
    class SampleRenamePrompt final : public juce::AlertWindow
    {
    public:
        SampleRenamePrompt (const juce::String& title, const juce::String& message, const juce::String& initialName)
            : juce::AlertWindow (title, message, juce::AlertWindow::NoIcon)
        {
            setComponentID ("sample-rename-dialog");
            addTextEditor ("sample-rename-name", initialName, "New name (.wav is kept):");
            addButton ("CREATE COPY", 1, juce::KeyPress (juce::KeyPress::returnKey));
            addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            feedback.setComponentID ("sample-rename-character-count");
            feedback.setSize (520, 66);
            feedback.setFont (juce::FontOptions (13.0f));
            feedback.setMinimumHorizontalScale (1.0f);
            feedback.setJustificationType (juce::Justification::topLeft);
            addCustomComponent (&feedback);
            auto* input { getTextEditor ("sample-rename-name") };
            // Assigned designer files end in a generated ID and voice number.
            // Select the useful, visible beginning without locking that suffix.
            const auto stem { initialName.endsWithIgnoreCase (".wav") ? initialName.dropLastCharacters (4) : initialName };
            const auto prefixLength { stem.length () - 16 };
            const auto generatedSuffix { prefixLength > 0 && stem[prefixLength] == '-'
                && stem[stem.length () - 3] == '-'
                && stem.substring (prefixLength + 1, stem.length () - 3).containsOnly ("0123456789abcdefABCDEF")
                && stem.getLastCharacters (2).startsWithChar ('0')
                && stem.getLastCharacters (1).containsOnly ("12345678") };
            input->setSelectAllWhenFocused (! generatedSuffix);
            input->setHighlightedRegion ({ 0, generatedSuffix ? prefixLength : initialName.length () });
            input->onTextChange = [this] { updateNameFeedback (); };
            updateNameFeedback ();
        }

        ~SampleRenamePrompt () override { removeCustomComponent (0); }

    private:
        juce::Label feedback;

        void updateNameFeedback ()
        {
            const auto requested { getTextEditorContents ("sample-rename-name").trim () };
            const auto displayed { requested.endsWithIgnoreCase (".wav") ? requested : requested + ".wav" };
            juce::String filename;
            const auto valid { SampleRename::validateName (requested, filename) };
            feedback.setText (juce::String (displayed.length ()) + " / 47 characters including .wav\n"
                + (valid.wasOk () ? "Valid WAV filename. Existing files are never overwritten." : valid.getErrorMessage ())
                + "\nA8 shows the beginning: keep important words first.", juce::dontSendNotification);
            Theme::bindColour (feedback, juce::Label::textColourId, [error = valid.failed ()] { return error ? Theme::warning : Theme::text; });
            getButton ("CREATE COPY")->setEnabled (valid.wasOk ());
        }
    };
}

struct Assimil8orEditorComponent::StereoCollapseJob
{
    PresetEditSession::Snapshot source;
    StereoCollapse::Result prepared;
    juce::Result outcome { juce::Result::fail ("Stereo conversion did not finish.") };
    int selectedZone { 0 };
    bool committed { false };
    std::atomic<bool> deliveryFailed { false };

    ~StereoCollapseJob ()
    {
        if (! committed) StereoCollapse::cleanup (prepared);
    }
};

struct Assimil8orEditorComponent::SampleRenameJob
{
    PresetEditSession::Snapshot source;
    SampleRename::Result prepared;
    juce::Result outcome { juce::Result::fail ("The sample copy did not finish.") };
    bool committed { false };
    std::atomic<bool> deliveryFailed { false };

    ~SampleRenameJob ()
    {
        if (! committed) SampleRename::cleanup (prepared);
    }
};

std::optional<double> Assimil8orEditorComponent::getSelectedDuration (int region)
{
    auto channel { channelTabs.getCurrentTabIndex () };
    if (! channelEditorsInitialized || editManager == nullptr || channel < 0 || channel >= 8)
        return std::nullopt;
    // Right-side stereo controls follow the left/master zone's markers and pitch.
    if (channel > 0 && channelProperties [channel].getChannelMode () == ChannelProperties::ChannelMode::stereoRight)
        --channel;
    const auto zone { channelEditors [channel].getSelectedZoneIndex () };
    if (zone < 0 || zone >= 8)
        return std::nullopt;
    std::optional<double> result;
    editManager->forZones (channel, { zone }, [&] (juce::ValueTree zoneTree, juce::ValueTree sampleTree)
    {
        ZoneProperties zoneProperties (zoneTree, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        SampleProperties sampleProperties (sampleTree, SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
        result = WaveformDuration::selected (zoneProperties, sampleProperties, region, channelProperties[channel].getPitch ());
    });
    return result;
}
Assimil8orEditorComponent::Assimil8orEditorComponent ()
{
    setOpaque (true);
    confirmChannelPurge = [] (const juce::String& title, const juce::String& message, std::function<void (bool)> callback)
    {
        juce::AlertWindow::showOkCancelBox (juce::AlertWindow::WarningIcon, title, message, "Purge", "Cancel", nullptr,
            juce::ModalCallbackFunction::create ([completion = std::move (callback)] (int response) { completion (response == 1); }));
    };
    confirmStereoCollapse = [] (const juce::String& title, const juce::String& message, std::function<void (bool)> callback)
    {
        juce::AlertWindow::showOkCancelBox (juce::AlertWindow::WarningIcon, title, message, "Create mono", "Cancel", nullptr,
            juce::ModalCallbackFunction::create ([completion = std::move (callback)] (int response) { completion (response == 1); }));
    };
    notifyStereoCollapse = [] (bool error, const juce::String& title, const juce::String& message)
    {
        juce::AlertWindow::showMessageBoxAsync (error ? juce::AlertWindow::WarningIcon : juce::AlertWindow::InfoIcon, title, message);
    };
    dispatchStereoCollapse = [] (std::function<void ()> completion) { return juce::MessageManager::callAsync (std::move (completion)); };
    prepareStereoCollapse = [] (const juce::File& folder, const juce::ValueTree& preset, int channel,
                                 StereoCollapse::Mode mode, StereoCollapse::Result& result)
    {
        return StereoCollapse::prepare (folder, preset, channel, mode, result);
    };
    promptSampleRename = [this] (const juce::String& title, const juce::String& message, const juce::String& initialName,
                                std::function<void (std::optional<juce::String>)> completion)
    {
        sampleRenameAlert = createSampleRenamePrompt (title, message, initialName);
        auto safe = juce::Component::SafePointer<Assimil8orEditorComponent> (this);
        auto window = juce::Component::SafePointer<juce::AlertWindow> (sampleRenameAlert.get ());
        sampleRenameAlert->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window, completion = std::move (completion)] (int response)
        {
            if (safe == nullptr || window == nullptr || safe->sampleRenameAlert.get () != window.getComponent ()) return;
            const auto entered { window->getTextEditorContents ("sample-rename-name") };
            safe->sampleRenameAlert.reset ();
            completion (response == 1 ? std::optional<juce::String> { entered } : std::nullopt);
        }));
    };
    notifySampleRename = [] (bool error, const juce::String& title, const juce::String& message)
    {
        juce::AlertWindow::showMessageBoxAsync (error ? juce::AlertWindow::WarningIcon : juce::AlertWindow::InfoIcon, title, message);
    };
    dispatchSampleRename = [] (std::function<void ()> completion) { return juce::MessageManager::callAsync (std::move (completion)); };
    prepareSampleRename = [] (const juce::File& folder, const juce::ValueTree& preset, const juce::String& filename,
                              const juce::String& requestedName, SampleRename::Result& result)
    {
        return SampleRename::prepare (folder, preset, filename, requestedName, result);
    };

    auto setupButton = [this] (juce::TextButton& button, juce::String text, std::function<void ()> buttonFunction)
    {
        button.setButtonText (text);
        button.onClick = buttonFunction;
        addAndMakeVisible (button);
    };

    // Title : Preset X
    addAndMakeVisible (titleLabel);

    setupButton (saveButton, "SAVE", [this] () { savePreset ();  });
    saveButton.setTooltip ("Save the current Preset");
    saveButton.setEnabled (false);
    savePendingLabel.setName ("preset-save-pending");
    savePendingLabel.setText ("SAVE IS PENDING", juce::dontSendNotification);
    savePendingLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    savePendingLabel.setBorderSize ({ 0, 8, 0, 8 });
    savePendingLabel.setMinimumHorizontalScale (1.0f);
    savePendingLabel.setJustificationType (juce::Justification::centred);
    savePendingLabel.setTooltip ("This preset has changes that have not been saved. Click SAVE to preserve them. Changing channels does not save the preset.");
    Theme::bindColour (savePendingLabel, juce::Label::textColourId, [] { return Theme::isLight () ? juce::Colours::white : Theme::field; });
    Theme::bindColour (savePendingLabel, juce::Label::backgroundColourId, [] { return Theme::warning; });
    Theme::bindColour (savePendingLabel, juce::Label::outlineColourId, [] { return Theme::warning; });
    addChildComponent (savePendingLabel);

    for (auto curChannelIndex { 0 }; curChannelIndex < 8; ++curChannelIndex)
        channelTabs.addTab ("CH " + juce::String::charToString ('1' + curChannelIndex), Theme::panel, &channelEditors [curChannelIndex], false);
    addAndMakeVisible (channelTabs);
    channelTabs.onTabPopup = [this] (int channel) { displayChannelToolsMenu (channel); };

    // add this AFTER the Channels tabs, because it occupies some of the same space, and ends up behind the tabs if we add it before
    toolsButton.setButtonText ("Preset tools");
    toolsButton.setComponentID ("presetTools");
    toolsButton.setTooltip ("Preset Tools");
    toolsButton.onClick = [this] () { displayToolsMenu (); };
    addAndMakeVisible (toolsButton);

    minPresetProperties.wrap (ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::MinParameterPresetType),
                              PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
    maxPresetProperties.wrap (ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::MaxParameterPresetType),
                              PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
    defaultPresetProperties.wrap (ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType),
                                  PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
    defaultChannelProperties.wrap (defaultPresetProperties.getChannelVT (0), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);

    setupPresetComponents ();

    // NOTE: This must go last to overlay the entire window when activated
    //addChildComponent (midiConfigWindow);

    startTimer (250);
}

Assimil8orEditorComponent::~Assimil8orEditorComponent ()
{
    stopTimer ();
    ++stereoCollapseConfirmation;
    ++sampleRenameConfirmation;
    sampleRenameAlert.reset ();
    if (stereoCollapseThread.joinable ()) stereoCollapseThread.join ();
    if (sampleRenameThread.joinable ()) sampleRenameThread.join ();
    // A queued completion may outlive this editor. Remove only still-owned,
    // unapplied outputs now; its shared job also cleans up if delivery is lost.
    if (stereoCollapseJob && ! stereoCollapseJob->committed)
        StereoCollapse::cleanup (stereoCollapseJob->prepared);
    if (sampleRenameJob && ! sampleRenameJob->committed)
        SampleRename::cleanup (sampleRenameJob->prepared);
}

void Assimil8orEditorComponent::setupPresetComponents ()
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
        Theme::bindColour (label, juce::Label::textColourId, [] { return Theme::text; });
        label.setFont (label.getFont ().withPointHeight (fontSize));
        label.setMinimumHorizontalScale (1.0f);
        label.setText (text, juce::NotificationType::dontSendNotification);
        addAndMakeVisible (label);
    };

    titleLabel.setText ("Preset _ :", juce::NotificationType::dontSendNotification);

    // NAME EDITOR
    //  length
    //  valid characters
    //  names doe not have to be unique, as the preset number is unique
    nameEditor.setJustification (juce::Justification::centredLeft);
    nameEditor.setIndents (1, 0);
    nameEditor.onFocusLost = [this] () { nameUiChanged (nameEditor.getText ()); };
    nameEditor.onReturnKey = [this] () { nameUiChanged (nameEditor.getText ()); };
    nameEditor.onTextChange = [this] () { nameUiChanged (nameEditor.getText ()); };
    nameEditor.setInputRestrictions (12, " !\"#$%^&'()#+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~");
    nameEditor.setTooltip (parameterToolTipData.getToolTip ("Preset", "Name"));
    addAndMakeVisible (nameEditor);

    setupLabel (midiSetupLabel, "MIDI SETUP", 12.0, juce::Justification::centredLeft);
    for (auto midiSetupId { 0 }; midiSetupId < 9; ++midiSetupId)
        midiSetupComboBox.addItem (juce::String (midiSetupId + 1), midiSetupId + 1);
    midiSetupComboBox.onChange = [this] ()
    {
        midiSetupUiChanged (midiSetupComboBox.getSelectedItemIndex ());
    };
    midiSetupComboBox.onDragCallback = [this] (double valueDelta)
    {
        const auto scrollAmount { static_cast<int> (valueDelta) };
        presetProperties.setMidiSetup (std::clamp (midiSetupComboBox.getSelectedItemIndex () + scrollAmount, 0, midiSetupComboBox.getNumItems () - 1), true);
    };
    midiSetupComboBox.onPopupMenuCallback = [this] ()
    {
    };
    addAndMakeVisible (midiSetupComboBox);

    // used to cover the right channel controls for a stereo linked pair
    addAndMakeVisible (windowDecorator);

    // Data 2 CV
    data2AsCvLabel.setBorderSize ({ 1, 0, 1, 0 });
    data2AsCvLabel.setText ("Data2 As", juce::NotificationType::dontSendNotification);
    data2AsCvLabel.setTooltip (parameterToolTipData.getToolTip ("Preset", "Data2asCV"));
    addAndMakeVisible (data2AsCvLabel);
    data2AsCvComboBox.onChange = [this] ()
    {
        data2AsCvUiChanged (data2AsCvComboBox.getSelectedItemText ());
    };
    data2AsCvComboBox.onDragCallback = [this] (double valueDelta)
    {
        const auto scrollAmount { static_cast<int> (valueDelta) };
        const auto newCvInputComboBoxIndex { data2AsCvComboBox.getSelectedItemIndex () + scrollAmount };
        data2AsCvComboBox.setSelectedItemIndex (std::clamp (newCvInputComboBoxIndex, 0, data2AsCvComboBox.getNumItems () - 1));
        presetProperties.setData2AsCV (data2AsCvComboBox.getSelectedItemText (), false);
    };
    data2AsCvComboBox.onPopupMenuCallback = [this] ()
    {
        juce::PopupMenu editMenu;
        editMenu.addItem ("Default", true, false, [this] () { presetProperties.setData2AsCV (defaultPresetProperties.getData2AsCV (), true); });
        editMenu.addItem ("Revert", true, false, [this] () { presetProperties.setData2AsCV (unEditedPresetProperties.getData2AsCV (), true); });
        editMenu.showMenuAsync ({}, [this] (int) {});

    };
    data2AsCvComboBox.setTooltip (parameterToolTipData.getToolTip ("Preset", "Data2asCV"));
    addAndMakeVisible (data2AsCvComboBox);

    xfadeGroupsLabel.setText ("XFade:", juce::NotificationType::dontSendNotification);
    addAndMakeVisible (xfadeGroupsLabel);

    for (auto xfadeGroupIndex { 0 }; xfadeGroupIndex < XfadeGroupIndex::numberOfGroups; ++xfadeGroupIndex)
    {
        auto& xfadeGroup { xfadeGroups [xfadeGroupIndex] };

        xfadeGroup.xfadeGroupLabel.setBorderSize ({ 0, 0, 0, 0 });
        xfadeGroup.xfadeGroupLabel.setText (juce::String::charToString ('A' + xfadeGroupIndex) + ":", juce::NotificationType::dontSendNotification);
        addAndMakeVisible (xfadeGroup.xfadeGroupLabel);

        // Xfade Label
        xfadeGroup.xfadeCvLabel.setBorderSize ({ 0, 0, 0, 0 });
        Theme::bindColour (xfadeGroup.xfadeCvLabel, juce::Label::textColourId, [] { return Theme::muted; });
        xfadeGroup.xfadeCvLabel.setText ("CV", juce::NotificationType::dontSendNotification);
        addAndMakeVisible (xfadeGroup.xfadeCvLabel);

        // Xfade CV Input ComboBox
        xfadeGroup.xfadeCvComboBox.onChange = [this, xfadeGroupIndex] ()
        {
            xfadeCvUiChanged (xfadeGroupIndex, xfadeGroups [xfadeGroupIndex].xfadeCvComboBox.getSelectedItemText ());
        };
        xfadeGroup.xfadeCvComboBox.onDragCallback = [this, xfadeGroupIndex] (double valueDelta)
        {
            const auto scrollAmount { static_cast<int> (valueDelta) };
            auto& xfadeGroup { xfadeGroups [xfadeGroupIndex] };
            const auto newCvInputComboBoxIndex { xfadeGroup.xfadeCvComboBox.getSelectedItemIndex () + scrollAmount };
            xfadeGroup.xfadeCvComboBox.setSelectedItemIndex (std::clamp (newCvInputComboBoxIndex, 0, xfadeGroup.xfadeCvComboBox.getNumItems () - 1));
            xfadeCvUiChanged (xfadeGroupIndex, xfadeGroups [xfadeGroupIndex].xfadeCvComboBox.getSelectedItemText ());
        };
        xfadeGroup.xfadeCvComboBox.onPopupMenuCallback = [this, xfadeGroupIndex] ()
        {
            juce::PopupMenu editMenu;
            juce::PopupMenu cloneMenu;
            for (auto curGroupIndex { 0 }; curGroupIndex < 4; ++curGroupIndex)
            {
                if (xfadeGroupIndex != curGroupIndex)
                    cloneMenu.addItem ("To Group " + juce::String::charToString ('A' + curGroupIndex), true, false, [this, curGroupIndex, xfadeGroupIndex] ()
                    {
                        editManager->setXfadeCvValueByIndex (curGroupIndex, editManager->getXfadeCvValueByIndex (xfadeGroupIndex), true);
                    });
            }
            cloneMenu.addItem ("To All", true, false, [this, xfadeGroupIndex] ()
            {
                const auto value { editManager->getXfadeCvValueByIndex (xfadeGroupIndex) };
                presetProperties.setXfadeACV (value, true);
                presetProperties.setXfadeBCV (value, true);
                presetProperties.setXfadeCCV (value, true);
                presetProperties.setXfadeDCV (value, true);
            });
            editMenu.addSubMenu ("Clone", cloneMenu, true);
            editMenu.addItem ("Default", true, false, [this, xfadeGroupIndex] ()
            {
                auto getDefaultXfadeCvValueByIndex = [this] (int xfadeGroupIndex)
                {
                    if (xfadeGroupIndex == 0)
                        return defaultPresetProperties.getXfadeACV ();
                    else if (xfadeGroupIndex == 1)
                        return defaultPresetProperties.getXfadeBCV ();
                    else if (xfadeGroupIndex == 2)
                        return defaultPresetProperties.getXfadeCCV ();
                    else if (xfadeGroupIndex == 3)
                        return defaultPresetProperties.getXfadeDCV ();
                    jassertfalse;
                        return juce::String ("Off");
                };
                editManager->setXfadeCvValueByIndex (xfadeGroupIndex, getDefaultXfadeCvValueByIndex (xfadeGroupIndex), true);
            });
            editMenu.addItem ("Revert", true, false, [this, xfadeGroupIndex] ()
            {
                auto getUneditedXfadeCvValueByIndex = [this] (int xfadeGroupIndex)
                {
                    if (xfadeGroupIndex == 0)
                        return unEditedPresetProperties.getXfadeACV ();
                    else if (xfadeGroupIndex == 1)
                        return unEditedPresetProperties.getXfadeBCV ();
                    else if (xfadeGroupIndex == 2)
                        return unEditedPresetProperties.getXfadeCCV ();
                    else if (xfadeGroupIndex == 3)
                        return unEditedPresetProperties.getXfadeDCV ();
                    jassertfalse;
                    return juce::String ("Off");
                };
                editManager->setXfadeCvValueByIndex (xfadeGroupIndex, getUneditedXfadeCvValueByIndex (xfadeGroupIndex), true);
            });
            editMenu.showMenuAsync ({}, [this] (int) {});
        };
        xfadeGroup.xfadeCvComboBox.setTooltip (parameterToolTipData.getToolTip ("Preset", "Xfade" + juce::String::charToString ('A' + xfadeGroupIndex) + "CV"));
        addAndMakeVisible (xfadeGroup.xfadeCvComboBox);

        // Xfade Group Width Label
        xfadeGroup.xfadeWidthLabel.setBorderSize ({ 0, 0, 0, 0 });
        Theme::bindColour (xfadeGroup.xfadeWidthLabel, juce::Label::textColourId, [] { return Theme::muted; });
        xfadeGroup.xfadeWidthLabel.setText ("Width", juce::NotificationType::dontSendNotification);
        addAndMakeVisible (xfadeGroup.xfadeWidthLabel);

        // Xfade Group Width
        //      1 decimal place when above 1.0, 0.1 increment
        //      2 decimal places below 1.0, 0.01 increment
        //      10.V
        //      9.0V
        //      .99V
        xfadeGroup.xfadeWidthEditor.setJustification (juce::Justification::centred);
        xfadeGroup.xfadeWidthEditor.setIndents (0, 0);
        xfadeGroup.xfadeWidthEditor.setInputRestrictions (0, "+-.0123456789");
        xfadeGroup.xfadeWidthEditor.setTooltip (parameterToolTipData.getToolTip ("Preset", "Xfade" + juce::String::charToString ('A' + xfadeGroupIndex) + "Width"));
        xfadeGroup.xfadeWidthEditor.setPopupMenuEnabled (false);
        xfadeGroup.xfadeWidthEditor.getMinValueCallback = [this] () { return minPresetProperties.getXfadeAWidth (); };
        xfadeGroup.xfadeWidthEditor.getMaxValueCallback = [this] () { return maxPresetProperties.getXfadeAWidth (); };
        xfadeGroup.xfadeWidthEditor.toStringCallback = [this] (double value) { return formatXfadeWidthString (value); };
        xfadeGroup.xfadeWidthEditor.updateDataCallback = [this, xfadeGroupIndex] (double value) { xfadeWidthUiChanged (xfadeGroupIndex, value); };
        xfadeGroup.xfadeWidthEditor.getIncrementCallback = [] () { return 0.01; };
        xfadeGroup.xfadeWidthEditor.onDragCallback = [this, xfadeGroupIndex] (double valueDelta)
        {
            const auto newAmount { editManager->getXfadeGroupValueByIndex (xfadeGroupIndex) + valueDelta };
            // the min/max values for all of the XFade Group Widths are the same, so we can just use A
            auto width { std::clamp (newAmount, minPresetProperties.getXfadeAWidth (), maxPresetProperties.getXfadeAWidth ()) };
            editManager->setXfadeGroupValueByIndex (xfadeGroupIndex, width, true);
        };
        xfadeGroup.xfadeWidthEditor.onPopupMenuCallback = [this, xfadeGroupIndex] ()
        {
            juce::PopupMenu pm;
            juce::PopupMenu cloneMenu;
            for (auto curGroupIndex { 0 }; curGroupIndex < 4; ++curGroupIndex)
            {
                if (xfadeGroupIndex != curGroupIndex)
                    cloneMenu.addItem ("To Group " + juce::String::charToString ('A' + curGroupIndex), true, false, [this, curGroupIndex, xfadeGroupIndex] ()
                    {
                        editManager->setXfadeGroupValueByIndex (curGroupIndex, editManager->getXfadeGroupValueByIndex (xfadeGroupIndex), true);
                    });
            }
            cloneMenu.addItem ("To All", true, false, [this, xfadeGroupIndex] ()
            {
                const auto value { editManager->getXfadeGroupValueByIndex (xfadeGroupIndex) };
                presetProperties.setXfadeAWidth (value, true);
                presetProperties.setXfadeBWidth (value, true);
                presetProperties.setXfadeCWidth (value, true);
                presetProperties.setXfadeDWidth (value, true);
            });
            pm.addSubMenu ("Clone", cloneMenu, true);
            pm.addItem ("Default", true, false, [this, xfadeGroupIndex] ()
            {
                // the default values for all of the XFade Group Widths are the same, so we can just use A
                editManager->setXfadeGroupValueByIndex (xfadeGroupIndex, defaultPresetProperties.getXfadeAWidth (), true);
            });
            pm.addItem ("Revert", true, false, [this, xfadeGroupIndex]
            {
                auto getUneditedXfadeGroupValueByIndex = [this] (int xfadeGroupIndex)
                {
                    if (xfadeGroupIndex == 0)
                        return unEditedPresetProperties.getXfadeAWidth ();
                    else if (xfadeGroupIndex == 1)
                        return unEditedPresetProperties.getXfadeBWidth ();
                    else if (xfadeGroupIndex == 2)
                        return unEditedPresetProperties.getXfadeCWidth ();
                    else if (xfadeGroupIndex == 3)
                        return unEditedPresetProperties.getXfadeDWidth ();
                    jassertfalse;
                    return 0.0;
                };
                editManager->setXfadeGroupValueByIndex (xfadeGroupIndex, getUneditedXfadeGroupValueByIndex (xfadeGroupIndex), true);
            });
            pm.showMenuAsync ({}, [this] (int) {});
        };
        addAndMakeVisible (xfadeGroup.xfadeWidthEditor);
    }
}

juce::String Assimil8orEditorComponent::formatXfadeWidthString (double width)
    {
        const auto numDecimals { width >= 1.0 ? 1 : 2 };
        auto newText { FormatHelpers::formatDouble (width, numDecimals, false) };
        if (width == 10)
            newText = newText.trimCharactersAtEnd ("0");
        else if (width < 1.0)
            newText = newText.trimCharactersAtStart ("0");
        return newText += "V";
    };

juce::PopupMenu Assimil8orEditorComponent::createChannelCloneMenu (int channelIndex, std::function <void (ChannelProperties&)> setter)
{
    jassert (setter != nullptr);
    juce::PopupMenu cloneMenu;
    for (auto destChannelIndex { 0 }; destChannelIndex < 8; ++destChannelIndex)
    {
        if (destChannelIndex != channelIndex)
        {
            cloneMenu.addItem ("To Channel " + juce::String (destChannelIndex + 1), true, false, [this, destChannelIndex, setter] ()
            {
                editManager->forChannels ({ destChannelIndex }, [this, setter] (juce::ValueTree channelPropertiesVT)
                {
                    ChannelProperties destChannelProperties (channelPropertiesVT, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
                    setter (destChannelProperties);
                });
            });
        }
    }
    cloneMenu.addItem ("To All", true, false, [this, setter, channelIndex] ()
    {
        std::vector<int> channelIndexList;
        // build list of other channels
        for (auto destChannelIndex { 0 }; destChannelIndex < 8; ++destChannelIndex)
            if (destChannelIndex != channelIndex)
                channelIndexList.emplace_back (destChannelIndex);
        // clone to other channels
        editManager->forChannels (channelIndexList, [this, setter] (juce::ValueTree channelPropertiesVT)
        {
            ChannelProperties destChannelProperties (channelPropertiesVT, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            setter (destChannelProperties);
        });
    });
    return cloneMenu;
}

void Assimil8orEditorComponent::init (juce::ValueTree rootPropertiesVT)
{
    channelEditorsInitialized = false;
    PersistentRootProperties persistentRootProperties (rootPropertiesVT, PersistentRootProperties::WrapperType::client, PersistentRootProperties::EnableCallbacks::no);
    runtimeRootProperties.wrap (rootPropertiesVT, RuntimeRootProperties::WrapperType::client, RuntimeRootProperties::EnableCallbacks::yes);
    runtimeRootProperties.onSystemRequestedQuit = [this] ()
    {
        runtimeRootProperties.setPreferredQuitState (RuntimeRootProperties::QuitState::idle, false);
        overwritePresetOrCancel ([this] ()
        {
            juce::MessageManager::callAsync ([this] () { runtimeRootProperties.setQuitState (RuntimeRootProperties::QuitState::now, false); });
        }, [this] ()
        {
            // do nothing
        });
    };

    audioPlayerProperties.wrap (runtimeRootProperties.getValueTree (), AudioPlayerProperties::WrapperType::client, AudioPlayerProperties::EnableCallbacks::no);

    appProperties.wrap (persistentRootProperties.getValueTree (), AppProperties::WrapperType::client, AppProperties::EnableCallbacks::yes);
    appProperties.onMostRecentFileChange = [this] (juce::String fileName)
    {
        //DebugLog ("Assimil8orEditorComponent", "Assimil8orEditorComponent::init/appProperties.onMostRecentFileChange: " + fileName);
        //dumpStacktrace (-1, [this] (juce::String logLine) { DebugLog ("Assimil8orEditorComponent", logLine); });
        audioPlayerProperties.setPlayState (AudioPlayerProperties::PlayState::stop, false);
        channelTabs.setCurrentTabIndex (0);
    };

    guiControlProperties.wrap (runtimeRootProperties.getValueTree (), GuiControlProperties::WrapperType::client, GuiControlProperties::EnableCallbacks::no);

    PresetManagerProperties presetManagerProperties (runtimeRootProperties.getValueTree (), PresetManagerProperties::WrapperType::client, PresetManagerProperties::EnableCallbacks::no);
    unEditedPresetProperties.wrap (presetManagerProperties.getPreset ("unedited"), PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::yes);
    presetProperties.wrap (presetManagerProperties.getPreset ("edit"), PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::yes);

    SystemServices systemServices { runtimeRootProperties.getValueTree (), SystemServices::WrapperType::client, SystemServices::EnableCallbacks::yes };
    editManager = systemServices.getEditManager ();
    setupPresetPropertiesCallbacks ();
    copyBufferZoneProperties.wrap ({}, ZoneProperties::WrapperType::owner, ZoneProperties::EnableCallbacks::no);
    presetProperties.forEachChannel ([this, rootPropertiesVT] (juce::ValueTree channelPropertiesVT, int channelIndex)
    {
        channelEditors [channelIndex].init (channelPropertiesVT, unEditedPresetProperties.getChannelVT (channelIndex), rootPropertiesVT, copyBufferZoneProperties.getValueTree (), &copyBufferHasData);
        channelProperties [channelIndex].wrap (channelPropertiesVT, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::yes);
        channelProperties [channelIndex].onChannelModeChange = [this] (int)
        {
            updateAllChannelTabNames ();
            synchronizeAllStereoZones ();
        };
        channelEditors [channelIndex].onSelectedZoneChanged = [this, channelIndex] (int zoneIndex)
        {
            synchronizeStereoZones (channelIndex, zoneIndex);
        };
        channelEditors [channelIndex].displayToolsMenu = [this] (int channelIndex)
        {
            displayChannelToolsMenu (channelIndex);
        };
        channelEditors[channelIndex].createSampleFileActions = [this] (int channel, int zone) { return createSampleFileMenu (channel, zone); };
        return true;
    });
    channelEditorsInitialized = true;
    channelActionSession.init (rootPropertiesVT);
    synchronizeAllStereoZones ();

    idDataChanged (presetProperties.getId ());
    midiSetupDataChanged (presetProperties.getMidiSetup ());
    nameDataChanged (presetProperties.getName ());
    data2AsCvDataChanged (presetProperties.getData2AsCV ());
    xfadeCvDataChanged (0, presetProperties.getXfadeACV ());
    xfadeCvDataChanged (1, presetProperties.getXfadeBCV ());
    xfadeCvDataChanged (2, presetProperties.getXfadeCCV ());
    xfadeCvDataChanged (3, presetProperties.getXfadeDCV ());
    xfadeWidthDataChanged (0, presetProperties.getXfadeAWidth ());
    xfadeWidthDataChanged (1, presetProperties.getXfadeBWidth ());
    xfadeWidthDataChanged (2, presetProperties.getXfadeCWidth ());
    xfadeWidthDataChanged (3, presetProperties.getXfadeDWidth ());
}

void Assimil8orEditorComponent::synchronizeStereoZones (int sourceChannel, int zoneIndex)
{
    if (! channelEditorsInitialized || sourceChannel < 0 || sourceChannel >= 8 || zoneIndex < 0 || zoneIndex >= 8) return;
    const auto partner { StereoChannelTools::partner (channelProperties[sourceChannel].getValueTree ()) };
    if (! partner.isValid ()) return;
    ChannelProperties pairedChannel (partner, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
    channelEditors[pairedChannel.getId () - 1].setSelectedZoneFromPartner (zoneIndex);
}

void Assimil8orEditorComponent::synchronizeAllStereoZones ()
{
    if (! channelEditorsInitialized) return;
    for (int channel { 0 }; channel < 8; ++channel)
    {
        if (channelProperties[channel].getChannelMode () == ChannelProperties::ChannelMode::stereoRight) continue;
        const auto partner { StereoChannelTools::partner (channelProperties[channel].getValueTree ()) };
        if (! partner.isValid ()) continue;
        ChannelProperties pairedChannel (partner, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        const auto source { channelTabs.getCurrentTabIndex () == pairedChannel.getId () - 1 ? pairedChannel.getId () - 1 : channel };
        synchronizeStereoZones (source, channelEditors[source].getSelectedZoneIndex ());
    }
}

void Assimil8orEditorComponent::addChannelDefaultMenuItem (juce::PopupMenu& menu, int channelIndex)
{
    const auto channelTree { channelProperties[channelIndex].getValueTree () };
    const auto partner { StereoChannelTools::partner (channelTree) };
    const auto before { channelTree.createCopy () };
    const auto partnerBefore { partner.createCopy () };
    menu.addItem (partner.isValid () ? "Default (both channels)" : "Default", true, false,
                  [safe = juce::Component::SafePointer<Assimil8orEditorComponent> (this), channelIndex, channelTree, partner, before, partnerBefore] ()
    {
        // A popup can outlive its preset or a stereo-mode change. Never apply
        // its action to a newly selected/replaced channel or an altered pair.
        if (safe == nullptr || safe->channelProperties[channelIndex].getValueTree () != channelTree ||
            ! channelTree.isEquivalentTo (before) || StereoChannelTools::partner (channelTree) != partner ||
            (partner.isValid () && ! partner.isEquivalentTo (partnerBefore))) return;
        safe->audioPlayerProperties.setPlayState (AudioPlayerProperties::PlayState::stop, false);
        StereoChannelTools::resetSettings (channelTree, safe->defaultChannelProperties.getValueTree ());
    });
}

void Assimil8orEditorComponent::displayChannelToolsMenu (int channelIndex)
{
    if (! channelEditorsInitialized) return;
    auto popupMenuLnF { std::make_shared<ModernLookAndFeel> () };
    auto toolsMenu { createChannelToolsMenu (channelIndex) };
    toolsMenu.setLookAndFeel (popupMenuLnF.get ());
    toolsMenu.showMenuAsync ({}, [popupMenuLnF] (int) {});
}

void Assimil8orEditorComponent::addChannelPurgeMenuItem (juce::PopupMenu& menu, int channelIndex)
{
    const auto source { channelActionSession.snapshot () };
    const auto channelTree { channelProperties[channelIndex].getValueTree () };
    const auto partner { StereoChannelTools::partner (channelTree) };
    juce::Component::SafePointer<Assimil8orEditorComponent> safe (this);
    menu.addItem ("Purge this channel...", source.has_value (), false, [safe, source, channelIndex, channelTree, partner] ()
    {
        if (safe == nullptr || ! source) return;
        const auto current { safe->channelActionSession.snapshot () };
        if (! current || current->revision != source->revision || current->folder != source->folder ||
            ! current->preset.isEquivalentTo (source->preset) || safe->channelProperties[channelIndex].getValueTree () != channelTree) return;
        auto channels { "CH " + juce::String (channelIndex + 1) };
        if (partner.isValid ()) channels += " and CH " + partner.getProperty (ChannelProperties::IdPropertyId).toString ();
        const auto request { ++safe->purgeConfirmation };
        safe->confirmChannelPurge ("Purge " + channels + "?",
            "Clear all eight zones and reset the channel settings for " + channels + "?" +
            juce::String (partner.isValid () ? " Both stereo channels will be cleared and become independent Master channels." : "") +
            "\n\nWAV files and waveform recipes are NOT deleted. Other channels are unchanged. Click Save afterward to write the preset.",
            [safe, source, channelIndex, channelTree, partner, request] (bool accepted)
        {
            if (safe == nullptr || safe->purgeConfirmation != request) return;
            ++safe->purgeConfirmation;
            if (! accepted) return;
            const auto approved { safe->channelActionSession.snapshot () };
            if (! approved || approved->revision != source->revision || approved->folder != source->folder ||
                ! approved->preset.isEquivalentTo (source->preset) || safe->channelProperties[channelIndex].getValueTree () != channelTree ||
                StereoChannelTools::partner (channelTree) != partner) return;
            safe->audioPlayerProperties.setPlayState (AudioPlayerProperties::PlayState::stop, false);
            if (! StereoChannelTools::purge (channelTree, safe->defaultChannelProperties.getValueTree ()))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Cannot purge channel",
                    "The channel, its zones or its stereo pairing are invalid. Nothing was cleared; correct the preset structure and try again.");
                return;
            }
            safe->channelEditors[channelIndex].setSelectedZoneFromPartner (0);
            if (partner.isValid ())
                safe->channelEditors[static_cast<int> (partner.getProperty (ChannelProperties::IdPropertyId)) - 1].setSelectedZoneFromPartner (0);
            safe->updateAllChannelTabNames ();
        });
    });
}

void Assimil8orEditorComponent::addStereoCollapseMenu (juce::PopupMenu& menu, int channelIndex)
{
    const auto source { channelActionSession.snapshot () };
    const auto enabled { source && ! channelFileOperationBusy ()
        && StereoCollapse::pairLeftIndex (source->preset, channelIndex) >= 0 };
    auto safe = juce::Component::SafePointer<Assimil8orEditorComponent> (this);
    juce::PopupMenu choices;
    const std::array<std::pair<const char*, StereoCollapse::Mode>, 3> modes {{
        { "Merge L/R...", StereoCollapse::Mode::merge },
        { "Keep left...", StereoCollapse::Mode::keepLeft },
        { "Keep right...", StereoCollapse::Mode::keepRight } }};
    for (const auto& [label, mode] : modes)
        choices.addItem (label, enabled, false, [safe, source, channelIndex, mode]
        {
            if (safe != nullptr && source) safe->requestStereoCollapse (*source, channelIndex, mode);
        });
    menu.addSubMenu ("Collapse stereo to mono", choices, enabled);
}

void Assimil8orEditorComponent::requestStereoCollapse (const PresetEditSession::Snapshot& source, int channelIndex, StereoCollapse::Mode mode)
{
    if (channelFileOperationBusy () || ! channelActionSession.matches (source)) return;
    const auto left { StereoCollapse::pairLeftIndex (source.preset, channelIndex) };
    if (left < 0) return;
    auto zones { 0 };
    for (const auto& zone : source.preset.getChild (left))
        if (zone.getProperty (ZoneProperties::SamplePropertyId).toString ().isNotEmpty ()) ++zones;
    const auto channels { "CH " + juce::String (left + 1) + "-L and CH " + juce::String (left + 2) + "-R" };
    const auto action { mode == StereoCollapse::Mode::merge ? "Merge the left and right audio as (L + R) / 2. Opposite-phase material can cancel."
        : mode == StereoCollapse::Mode::keepLeft ? "Keep only the left audio; the right audio is not included in the new mono files."
                                               : "Keep only the right audio; the left audio is not included in the new mono files." };
    juce::String groupWarning;
    if (left + 2 < kNumChannels)
    {
        const auto followingMode { static_cast<int> (source.preset.getChild (left + 2).getProperty (ChannelProperties::ChannelModePropertyId)) };
        if (followingMode == ChannelProperties::link || followingMode == ChannelProperties::cycle)
            groupWarning = "\n\nThe following channel uses " + juce::String (followingMode == ChannelProperties::link ? "Link" : "Cycle")
                + " mode. Freeing CH " + juce::String (left + 2) + " may change its group relationship; review linked/cycled channels afterward.";
    }
    stereoCollapseConfirming = true;
    const auto request { ++stereoCollapseConfirmation };
    auto safe = juce::Component::SafePointer<Assimil8orEditorComponent> (this);
    confirmStereoCollapse ("Collapse " + channels + " to mono?",
        juce::String (action) + "\n\nThis applies to all " + juce::String (zones) + " populated zones of this pair, not just the selected zone. "
        "New mono WAVs will be created in the current folder. Original WAVs are never overwritten or deleted.\n\n"
        "CH " + juce::String (left + 1) + " keeps the left/master channel settings and zone markers. CH " + juce::String (left + 2)
        + " is cleared and becomes an independent channel. Other channel settings are unchanged. Click SAVE afterward to write the preset." + groupWarning,
        [safe, source, channelIndex, mode, request] (bool accepted)
        {
            if (safe == nullptr || safe->stereoCollapseConfirmation != request) return;
            ++safe->stereoCollapseConfirmation;
            safe->stereoCollapseConfirming = false;
            if (! accepted) return;
            if (! safe->channelActionSession.matches (source))
            {
                safe->notifyStereoCollapse (true, "Stereo conversion cancelled", "The preset or folder changed while confirming. Nothing was changed; reopen Channel tools and try again.");
                return;
            }
            safe->startStereoCollapse (source, channelIndex, mode);
        });
}

void Assimil8orEditorComponent::stopStereoCollapseAudition ()
{
    audioPlayerProperties.setPlayState (AudioPlayerProperties::PlayState::stop, false);
    SystemServices services (runtimeRootProperties.getValueTree (), SystemServices::WrapperType::client, SystemServices::EnableCallbacks::no);
    if (auto* player { services.getAudioPlayer () }) player->stopWaveformAudition ();
}

void Assimil8orEditorComponent::startStereoCollapse (const PresetEditSession::Snapshot& source, int channelIndex, StereoCollapse::Mode mode)
{
    if (channelFileOperationBusy () || ! channelActionSession.matches (source)) return;
    const auto left { StereoCollapse::pairLeftIndex (source.preset, channelIndex) };
    if (left < 0) return;
    if (stereoCollapseThread.joinable ()) stereoCollapseThread.join ();
    stopStereoCollapseAudition ();
    auto job { std::make_shared<StereoCollapseJob> () };
    job->source = source;
    job->selectedZone = channelEditors[left].getSelectedZoneIndex ();
    stereoCollapseJob = job;
    auto safe = juce::Component::SafePointer<Assimil8orEditorComponent> (this);
    try
    {
        stereoCollapseThread = std::thread ([safe, job, channelIndex, mode, prepare = prepareStereoCollapse, dispatch = dispatchStereoCollapse]
        {
            try { job->outcome = prepare (job->source.folder, job->source.preset, channelIndex, mode, job->prepared); }
            catch (...) { job->outcome = juce::Result::fail ("Unexpected failure while preparing the mono files."); }
            bool delivered { false };
            try
            {
                delivered = dispatch ([safe, job]
                {
                    if (safe != nullptr) safe->finishStereoCollapse (job);
                });
            }
            catch (...) {}
            if (! delivered)
            {
                const auto removed { StereoCollapse::cleanup (job->prepared) };
                job->outcome = juce::Result::fail ("Could not deliver the stereo conversion result; the preset was not changed. " + removed.getErrorMessage ());
                // The message-thread timer can report a rejected delivery; if
                // shutdown has begun, the owned job/destructor still cleans up.
                job->deliveryFailed.store (true);
            }
        });
    }
    catch (...)
    {
        job->outcome = juce::Result::fail ("Could not start the mono conversion. Please try again.");
        finishStereoCollapse (job);
    }
}

void Assimil8orEditorComponent::finishStereoCollapse (std::shared_ptr<StereoCollapseJob> job)
{
    if (stereoCollapseJob != job) return;
    auto result { job->outcome };
    if (result.wasOk () && ! channelActionSession.matches (job->source))
        result = juce::Result::fail ("The preset or folder changed while creating the mono files. Your current edits were not replaced; reopen Channel tools and try again.");
    const auto left { job->prepared.leftChannel };
    if (result.wasOk () && (left < 0 || left >= 7 || StereoCollapse::pairLeftIndex (job->source.preset, left) != left
        || ! job->prepared.editedPreset.hasType (PresetProperties::PresetTypeId) || job->prepared.editedPreset.getNumChildren () != 8
        || job->prepared.editedPreset.getProperty (PresetProperties::IdPropertyId) != job->source.preset.getProperty (PresetProperties::IdPropertyId)))
        result = juce::Result::fail ("The prepared mono preset did not match the requested stereo pair. Nothing was changed.");
    if (result.failed ())
    {
        const auto removed { StereoCollapse::cleanup (job->prepared) };
        auto message { result.getErrorMessage () };
        if (removed.failed ()) message += "\n\n" + removed.getErrorMessage ();
        stereoCollapseJob.reset ();
        notifyStereoCollapse (true, "Cannot collapse stereo to mono", message);
        return;
    }

    stopStereoCollapseAudition ();
    // The guard above is the last stale check. Unpair R first, then copy the
    // prepared snapshot without replacing any live ValueTree identities. Calling
    // session.apply after unpairing would correctly reject our own first edit.
    channelProperties[left + 1].setChannelMode (ChannelProperties::ChannelMode::master, false);
    // Remove the old right-side CV references before resetting its settings;
    // otherwise CV safety callbacks would force the newly empty channel muted.
    for (int zone { 0 }; zone < kNumZones; ++zone)
        ZoneProperties (channelProperties[left + 1].getZoneVT (zone), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no)
            .copyFrom (job->prepared.editedPreset.getChild (left + 1).getChild (zone), false);
    PresetProperties::copyTreeProperties (job->prepared.editedPreset, presetProperties.getValueTree ());
    job->committed = true;
    channelTabs.setCurrentTabIndex (left);
    channelEditors[left].setSelectedZoneFromPartner (juce::jlimit (0, 7, job->selectedZone));
    channelEditors[left + 1].setSelectedZoneFromPartner (0);
    updateAllChannelTabNames ();
    updateSaveIndicator ();
    stereoCollapseJob.reset ();
    notifyStereoCollapse (false, "Stereo pair converted to mono",
        "CH " + juce::String (left + 1) + " now uses mono WAVs; CH " + juce::String (left + 2) + " is free.\n\nCreated "
        + juce::String (job->prepared.createdFiles.size ()) + " mono file(s) in:\n" + job->source.folder.getFullPathName ()
        + "\n\nOriginal WAVs are unchanged. SAVE IS PENDING: click SAVE to write these preset changes.");
}

bool Assimil8orEditorComponent::channelFileOperationBusy () const
{
    return stereoCollapseConfirming || stereoCollapseJob || sampleRenamePrompting || sampleRenameJob;
}

juce::PopupMenu Assimil8orEditorComponent::createSampleFileMenu (int channelIndex, int zoneIndex)
{
    juce::PopupMenu menu;
    const auto source { channelActionSession.snapshot () };
    if (! source || channelIndex < 0 || channelIndex >= kNumChannels || zoneIndex < 0 || zoneIndex >= kNumZones) return menu;
    const auto filename { source->preset.getChild (channelIndex).getChild (zoneIndex).getProperty (ZoneProperties::SamplePropertyId).toString () };
    if (filename.isEmpty ()) return menu;
    auto safe = juce::Component::SafePointer<Assimil8orEditorComponent> (this);
    menu.addItem ("Rename sample copy...", ! channelFileOperationBusy (), false, [safe, source, filename]
    {
        if (safe != nullptr && safe->channelActionSession.matches (*source))
            safe->requestSampleRename (*source, filename, source->folder.getChildFile (filename).getFileNameWithoutExtension ());
    });
    return menu;
}

std::unique_ptr<juce::AlertWindow> Assimil8orEditorComponent::createSampleRenamePrompt (const juce::String& title,
                                                                                     const juce::String& message,
                                                                                     const juce::String& initialName)
{
    return std::make_unique<SampleRenamePrompt> (title, message, initialName);
}

void Assimil8orEditorComponent::requestSampleRename (const PresetEditSession::Snapshot& source, const juce::String& filename,
                                                    const juce::String& proposedName, const juce::String& error)
{
    if (channelFileOperationBusy () || ! channelActionSession.matches (source)) return;
    auto references { 0 };
    for (const auto& channel : source.preset)
        for (const auto& zone : channel)
            if (zone.getProperty (ZoneProperties::SamplePropertyId).toString ().equalsIgnoreCase (filename)) ++references;
    if (references == 0) return;
    sampleRenamePrompting = true;
    const auto request { ++sampleRenameConfirmation };
    auto safe = juce::Component::SafePointer<Assimil8orEditorComponent> (this);
    promptSampleRename ("Rename sample copy",
        (error.isEmpty () ? juce::String () : "Name not accepted: " + error + "\n\n")
        + "Create a copy of '" + filename + "' with a new name in this folder. All " + juce::String (references)
        + " matching sample references in the current preset will follow the copy, including stereo partners and other zones.\n\n"
        "The original WAV and other saved presets stay unchanged. The .wav extension is kept; the full name can contain up to 47 characters. Click SAVE afterward to write the updated preset.",
        proposedName, [safe, source, filename, request] (std::optional<juce::String> entered)
        {
            if (safe == nullptr || safe->sampleRenameConfirmation != request) return;
            ++safe->sampleRenameConfirmation;
            safe->sampleRenamePrompting = false;
            if (! entered) return;
            if (! safe->channelActionSession.matches (source))
            {
                safe->notifySampleRename (true, "Sample copy cancelled", "The preset or folder changed while entering the name. Nothing was changed; reopen the FILE menu and try again.");
                return;
            }
            juce::String finalName;
            auto requestedName { entered->trim () };
            // The prompt edits the stem. Keep the original extension's case so
            // accepting an unchanged Foo.WAV is a no-op on every platform.
            if (! requestedName.endsWithIgnoreCase (".wav") && filename.endsWithIgnoreCase (".wav"))
                requestedName += filename.getLastCharacters (4);
            auto valid { SampleRename::validateName (requestedName, finalName) };
            if (valid.wasOk () && finalName != filename)
            {
                const auto target { source.folder.getChildFile (finalName) };
                if (target.exists () || target.isSymbolicLink ()) valid = juce::Result::fail ("That filename is already in use. Choose another name; existing files are never overwritten.");
            }
            if (valid.failed ())
            {
                safe->requestSampleRename (source, filename, *entered, valid.getErrorMessage ());
                return;
            }
            safe->startSampleRename (source, filename, finalName);
        });
}

void Assimil8orEditorComponent::startSampleRename (const PresetEditSession::Snapshot& source, const juce::String& filename, const juce::String& requestedName)
{
    if (channelFileOperationBusy () || ! channelActionSession.matches (source)) return;
    if (sampleRenameThread.joinable ()) sampleRenameThread.join ();
    stopStereoCollapseAudition ();
    auto job { std::make_shared<SampleRenameJob> () };
    job->source = source;
    sampleRenameJob = job;
    auto safe = juce::Component::SafePointer<Assimil8orEditorComponent> (this);
    try
    {
        sampleRenameThread = std::thread ([safe, job, filename, requestedName, prepare = prepareSampleRename, dispatch = dispatchSampleRename]
        {
            try { job->outcome = prepare (job->source.folder, job->source.preset, filename, requestedName, job->prepared); }
            catch (...) { job->outcome = juce::Result::fail ("Unexpected failure while creating the named sample copy."); }
            bool delivered { false };
            try
            {
                delivered = dispatch ([safe, job]
                {
                    if (safe != nullptr) safe->finishSampleRename (job);
                });
            }
            catch (...) {}
            if (! delivered)
            {
                const auto removed { SampleRename::cleanup (job->prepared) };
                job->outcome = juce::Result::fail ("Could not deliver the sample copy result; the preset was not changed. " + removed.getErrorMessage ());
                job->deliveryFailed.store (true);
            }
        });
    }
    catch (...)
    {
        job->outcome = juce::Result::fail ("Could not start the sample copy. Please try again.");
        finishSampleRename (job);
    }
}

void Assimil8orEditorComponent::finishSampleRename (std::shared_ptr<SampleRenameJob> job)
{
    if (sampleRenameJob != job) return;
    auto result { job->outcome };
    if (result.wasOk () && ! channelActionSession.matches (job->source))
        result = juce::Result::fail ("The preset or folder changed while copying the sample. Your current edits were not replaced; reopen the FILE menu and try again.");
    if (result.wasOk () && job->prepared.references > 0)
    {
        stopStereoCollapseAudition ();
        // Sample filenames are applied before saved markers within each zone.
        // The existing copy helper preserves tree identities and stereo pairing.
        result = channelActionSession.apply (job->source, job->prepared.editedPreset);
    }
    if (result.failed ())
    {
        const auto removed { SampleRename::cleanup (job->prepared) };
        auto message { result.getErrorMessage () };
        if (removed.failed ()) message += "\n\n" + removed.getErrorMessage ();
        sampleRenameJob.reset ();
        notifySampleRename (true, "Cannot rename sample copy", message);
        return;
    }

    job->committed = true;
    updateAllChannelTabNames ();
    updateSaveIndicator ();
    sampleRenameJob.reset ();
    if (job->prepared.references == 0)
        notifySampleRename (false, "Sample name unchanged", "The name is unchanged. No files or preset references were changed.");
    else
        notifySampleRename (false, "Named sample copy created", "Created '" + job->prepared.filename + "' in:\n" + job->source.folder.getFullPathName ()
            + "\n\nUpdated " + juce::String (job->prepared.references) + " sample reference(s) in this preset. Original WAVs and other saved presets are unchanged.\n\nSAVE IS PENDING: click SAVE to write the preset.");
}

juce::PopupMenu Assimil8orEditorComponent::createChannelToolsMenu (int channelIndex)
{
    juce::PopupMenu toolsMenu;
    if (channelIndex < 0 || channelIndex >= 8 || ! channelProperties[channelIndex].isValid ()) return toolsMenu;
    toolsMenu.addSectionHeader ("Channel " + juce::String (channelProperties[channelIndex].getId ()));
    toolsMenu.addSeparator ();
    if (channelProperties[channelIndex].getChannelMode () == ChannelProperties::ChannelMode::stereoRight)
    {
        // Opening Tools on R must not expose independent zone/clone operations.
        addChannelDefaultMenuItem (toolsMenu, channelIndex);
        addStereoCollapseMenu (toolsMenu, channelIndex);
        addChannelPurgeMenuItem (toolsMenu, channelIndex);
        return toolsMenu;
    }
    {
        juce::PopupMenu cloneMenu;
        cloneMenu.addSubMenu ("Channel Settings", createChannelCloneMenu (channelIndex, [this, channelIndex] (ChannelProperties& destChannelProperties)
        {
            destChannelProperties.copyFrom (channelProperties[channelIndex].getValueTree ());
        }));
        cloneMenu.addSubMenu ("Zones", createChannelCloneMenu (channelIndex, [this, channelIndex] (ChannelProperties& destChannelProperties)
        {
            channelProperties[channelIndex].forEachZone ([&destChannelProperties] (juce::ValueTree zonePropertiesVT, int zoneIndex)
            {
                ZoneProperties destZoneProperties (destChannelProperties.getZoneVT (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                destZoneProperties.copyFrom (zonePropertiesVT, false);
                return true;
            });
        }));
        cloneMenu.addSubMenu ("Settings and Zones", createChannelCloneMenu (channelIndex, [this, channelIndex] (ChannelProperties& destChannelProperties)
        {
            destChannelProperties.copyFrom (channelProperties[channelIndex].getValueTree ());
            channelProperties[channelIndex].forEachZone ([&destChannelProperties] (juce::ValueTree zonePropertiesVT, int zoneIndex)
            {
                ZoneProperties destZoneProperties (destChannelProperties.getZoneVT (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                destZoneProperties.copyFrom (zonePropertiesVT, false);
                return true;
            });
        }));
        cloneMenu.addSubMenu ("MinVoltages", createChannelCloneMenu (channelIndex, [this, channelIndex] (ChannelProperties& destChannelProperties)
        {
            if (const auto destNumUsedZones { editManager->getNumUsedZones (destChannelProperties.getId () - 1) }; destNumUsedZones > 1)
                channelProperties[channelIndex].forEachZone ([&destChannelProperties, destNumUsedZones] (juce::ValueTree zonePropertiesVT, int curZoneIndex)
                {
                    if (curZoneIndex == destNumUsedZones - 1) return false;
                    ZoneProperties srcZone (zonePropertiesVT, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                    ZoneProperties destZone (destChannelProperties.getZoneVT (curZoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                    destZone.setMinVoltage (srcZone.getMinVoltage (), false);
                    return true;
                });
        }));
        toolsMenu.addSubMenu ("Clone", cloneMenu);
    }
    {
        juce::PopupMenu editMenu;
        editMenu.addItem ("Copy", true, false, [this, channelIndex] ()
        {
            copyBufferChannelProperties.copyFrom (channelProperties[channelIndex].getValueTree ());
            copyBufferHasData = true;
        });
        editMenu.addItem ("Paste", copyBufferHasData, false, [this, channelIndex] ()
        {
            channelProperties[channelIndex].copyFrom (copyBufferChannelProperties.getValueTree ());
        });
        toolsMenu.addSubMenu ("Edit", editMenu, true);
    }
    {
        juce::PopupMenu explodeMenu;
        for (auto explodeCount { 2 }; explodeCount < 9 - channelIndex; ++explodeCount)
            explodeMenu.addItem (juce::String (explodeCount) + " channels", true, false, [this, channelIndex, explodeCount] ()
            {
                explodeChannel (channelIndex, explodeCount);
            });
        toolsMenu.addSubMenu ("Explode", explodeMenu, channelIndex < 7);
    }
    addChannelDefaultMenuItem (toolsMenu, channelIndex);
    toolsMenu.addItem ("Revert", true, false, [this, channelIndex] ()
    {
        channelProperties[channelIndex].copyFrom (unEditedPresetProperties.getChannelVT (channelIndex));
    });
    toolsMenu.addSeparator ();
    addStereoCollapseMenu (toolsMenu, channelIndex);
    addChannelPurgeMenuItem (toolsMenu, channelIndex);
    return toolsMenu;
}

void Assimil8orEditorComponent::setupPresetPropertiesCallbacks ()
{
    presetProperties.onIdChange = [this] (int id) { idDataChanged (id); };
    presetProperties.onMidiSetupChange = [this] (int midiSetupId) { midiSetupDataChanged (midiSetupId); };
    presetProperties.onNameChange = [this] (juce::String name) { nameDataChanged (name); };
    presetProperties.onData2AsCVChange = [this] (juce::String cvInput) { data2AsCvDataChanged (cvInput); };
    // Xfade_CV
    presetProperties.onXfadeACVChange = [this] (juce::String dataAndCv) { xfadeCvDataChanged (0, dataAndCv); };
    presetProperties.onXfadeBCVChange = [this] (juce::String dataAndCv) { xfadeCvDataChanged (1, dataAndCv); };
    presetProperties.onXfadeCCVChange = [this] (juce::String dataAndCv) { xfadeCvDataChanged (2, dataAndCv); };
    presetProperties.onXfadeDCVChange = [this] (juce::String dataAndCv) { xfadeCvDataChanged (3, dataAndCv); };
    // Xfade_Width
    presetProperties.onXfadeAWidthChange = [this] (double width) { xfadeWidthDataChanged (0, width); };
    presetProperties.onXfadeBWidthChange = [this] (double width) { xfadeWidthDataChanged (1, width); };
    presetProperties.onXfadeCWidthChange = [this] (double width) { xfadeWidthDataChanged (2, width); };
    presetProperties.onXfadeDWidthChange = [this] (double width) { xfadeWidthDataChanged (3, width); };
}

void Assimil8orEditorComponent::receiveSampleLoadRequest (juce::File sampleFile)
{
    auto channelIndex { channelTabs.getCurrentTabIndex () };
    auto curChannelEditor { dynamic_cast<ChannelEditor*> (channelTabs.getTabContentComponent (channelIndex)) };
    curChannelEditor->receiveSampleLoadRequest (sampleFile);
}

void Assimil8orEditorComponent::overwritePresetOrCancel (std::function<void ()> overwriteFunction, std::function<void ()> cancelFunction)
{
    jassert (overwriteFunction != nullptr);
    jassert (cancelFunction != nullptr);

    PresetFileOperations::guardedChange (presetProperties.getValueTree (), unEditedPresetProperties.getValueTree (),
        [safe = juce::Component::SafePointer<Assimil8orEditorComponent> (this)] (std::function<void ()> proceed, std::function<void ()> cancel)
        {
            juce::AlertWindow::showOkCancelBox (juce::AlertWindow::WarningIcon, "Overwriting Edited Preset",
                "You have not saved a preset that you have edited.\n  Select Continue to lose your changes.\n  Select Cancel to go back and save.", "Continue (lose changes)", "Cancel", nullptr,
                juce::ModalCallbackFunction::create ([safe, proceed, cancel] (int option)
                {
                    juce::MessageManager::callAsync ([safe, option, proceed, cancel] ()
                    {
                        if (safe == nullptr) return;
                        if (option == 1) // Continue
                            proceed ();
                        else // cancel
                            cancel ();
                    });
                }));
        }, std::move (overwriteFunction), std::move (cancelFunction));
}

juce::Result Assimil8orEditorComponent::savePreset ()
{
    if (appProperties.getMRUList ().isEmpty ()) return juce::Result::fail ("Select a preset folder before saving.");
    const auto presetFile { juce::File (appProperties.getMRUList () [0]) };
    const auto result { PresetFileOperations::save (presetFile, presetProperties.getValueTree (), unEditedPresetProperties.getValueTree ()) };
    updateSaveIndicator ();
    if (result.failed ())
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Preset save failed", result.getErrorMessage () + " Your edits remain unsaved; you can retry or export them.");
    return result;
}

void Assimil8orEditorComponent::paint ([[maybe_unused]] juce::Graphics& g)
{
    g.fillAll (Theme::background);
}

void Assimil8orEditorComponent::explodeChannel (int channelIndex, int explodeCount)
{
    // clone channelIndex into explodeCount-1 subsequent channels
    for (auto destinationChannelIndex { channelIndex + 1 }; destinationChannelIndex < channelIndex + explodeCount; ++destinationChannelIndex)
    {
        auto& destChannelProperties { channelProperties [destinationChannelIndex] };
        destChannelProperties.copyFrom (channelProperties [channelIndex].getValueTree ());

        channelProperties [channelIndex].forEachZone ([this, &destChannelProperties] (juce::ValueTree zonePropertiesVT, int zoneIndex)
        {
            ZoneProperties destZoneProperties (destChannelProperties.getZoneVT (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            destZoneProperties.copyFrom (zonePropertiesVT, false);
            return true;
        });
    }
    // set sample/loop points for channelIndex and explodeCount-1 subsequent channels to sequential slice size pieces
    SampleManagerProperties sampleManagerProperties (runtimeRootProperties.getValueTree (), SampleManagerProperties::WrapperType::client, SampleManagerProperties::EnableCallbacks::no);
    channelProperties [channelIndex].forEachZone ([this, channelIndex, explodeCount, &sampleManagerProperties] (juce::ValueTree zonePropertiesVT, int zoneIndex)
    {
        ZoneProperties zoneProperties { zonePropertiesVT, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no };
        SampleProperties sampleProperties (sampleManagerProperties.getSamplePropertiesVT (channelIndex, zoneIndex), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::yes);
        juce::int64 sampleSize { sampleProperties.getLengthInSamples () };
        const auto sliceSize { sampleSize / explodeCount };
        auto setSamplePoints = [this, sliceSize] (ZoneProperties& zpToUpdate, int index)
        {
            const auto sampleStart { index * sliceSize };
            const auto sampleEnd { sampleStart + sliceSize };
            zpToUpdate.setSampleStart (sampleStart, true);
            zpToUpdate.setSampleEnd (sampleEnd, true);
            zpToUpdate.setLoopStart (sampleStart, true);
            zpToUpdate.setLoopLength (static_cast<double> (sliceSize), true);
        };
        setSamplePoints (zoneProperties, 0);
        for (auto channelCount { 0 }; channelCount < explodeCount - 1; ++channelCount)
        {
            auto& destChannelProperties { channelProperties [channelIndex + channelCount + 1] };
            ZoneProperties destZoneProperties { destChannelProperties.getZoneVT (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no };
            setSamplePoints (destZoneProperties, channelCount + 1);
        }
        return true;
    });
}

void Assimil8orEditorComponent::updateAllChannelTabNames ()
{
    for (auto channelIndex { 0 }; channelIndex < channelTabs.getNumTabs (); ++channelIndex)
        updateChannelTabName (channelIndex);
}

bool Assimil8orEditorComponent::isChannelActive (int channelIndex)
{
    bool active { false };
    channelProperties [channelIndex].forEachZone ([&] (juce::ValueTree zoneTree, int)
    {
        ZoneProperties zone (zoneTree, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        active = zone.getSample ().isNotEmpty ();
        return ! active;
    });
    return active;
}

void Assimil8orEditorComponent::updateChannelTabName (int channelIndex)
{
    auto channelTabTitle { juce::String ("CH ") + juce::String::charToString ('1' + channelIndex) };

    if (channelIndex != 7 && channelProperties [channelIndex].getChannelMode () != ChannelProperties::ChannelMode::stereoRight && isChannelActive (channelIndex) &&
        channelProperties [channelIndex + 1].getChannelMode () == ChannelProperties::ChannelMode::stereoRight)
    {
        channelTabTitle += "-L";
    }
    else if (channelProperties [channelIndex].getChannelMode () == ChannelProperties::ChannelMode::stereoRight)
    {
        channelTabTitle += "-R";
    }

    channelTabs.setTabName (channelIndex, channelTabTitle);
}

void Assimil8orEditorComponent::setPresetToDefaults ()
{
    PresetProperties::copyTreeProperties (defaultPresetProperties.getValueTree (), presetProperties.getValueTree ());
    presetProperties.setId (unEditedPresetProperties.getId (), true);
}

void Assimil8orEditorComponent::revertPreset ()
{
    PresetProperties::copyTreeProperties (unEditedPresetProperties.getValueTree (), presetProperties.getValueTree ());
}

bool Assimil8orEditorComponent::canRecallSelectedWaveform ()
{
    const auto channel { channelTabs.getCurrentTabIndex () };
    if (! channelEditorsInitialized || ! onRecallWaveform || channel < 0 || channel >= 8) return false;
    const auto zone { channelEditors[channel].getSelectedZoneIndex () };
    if (zone < 0 || zone >= 8 || appProperties.getMostRecentFolder ().isEmpty ()) return false;
    const auto sample { channelProperties[channel].getZoneVT (zone).getProperty (ZoneProperties::SamplePropertyId).toString () };
    if (sample.isEmpty () || juce::File::isAbsolutePath (sample) || sample.containsAnyOf ("/\\")) return false;
    WaveformDesignRecall::RecalledDesign recalled;
    return WaveformDesignRecall::recallWave (juce::File (appProperties.getMostRecentFolder ()).getChildFile (sample), recalled).wasOk ();
}

void Assimil8orEditorComponent::recallSelectedWaveform ()
{
    if (! canRecallSelectedWaveform ()) return;
    const auto channel { channelTabs.getCurrentTabIndex () };
    if (! channelEditorsInitialized || ! onRecallWaveform || channel < 0 || channel >= 8) return;
    const auto zone { channelEditors[channel].getSelectedZoneIndex () };
    if (zone >= 0 && zone < 8) onRecallWaveform (channel, zone);
}

void Assimil8orEditorComponent::displayToolsMenu ()
{
    auto popupMenuLnF { std::make_shared<ModernLookAndFeel> () };
    auto toolsMenu { createPresetToolsMenu () };
    toolsMenu.setLookAndFeel (popupMenuLnF.get ());
    toolsMenu.showMenuAsync ({}, [popupMenuLnF] (int) {});
}

juce::PopupMenu Assimil8orEditorComponent::createPresetToolsMenu ()
{
    juce::PopupMenu toolsMenu;
    toolsMenu.addSectionHeader ("Preset");
    toolsMenu.addSeparator ();

    {
        juce::PopupMenu importMenu;
        importMenu.addItem ("Settings and Samples", true, false, [this] () { importPresetSettingsAndSamples (); });
        importMenu.addItem ("Settings Only", true, false, [this] () { importPresetSettings (); });
        toolsMenu.addSubMenu ("Import", importMenu, true);
    }
    {
        juce::PopupMenu exportMenu;
        exportMenu.addItem ("Settings and Samples", true, false, [this] () { exportPresetSettingsAndSamples (); });
        exportMenu.addItem ("Settings Only", true, false, [this] () { exportPresetSettings (); });
        toolsMenu.addSubMenu ("Export", exportMenu, true);
    }
    toolsMenu.addItem ("Default", true, false, [this] () { setPresetToDefaults (); });
    toolsMenu.addItem ("Revert", true, false, [this] () { revertPreset (); });
    toolsMenu.addItem ("Midi Setups", true, false, [this] () { guiControlProperties.showMidiConfigWindow (true); });
    toolsMenu.addSeparator ();
    juce::Component::SafePointer<Assimil8orEditorComponent> safe (this);
    toolsMenu.addItem ("Edit selected waveform in designer...", canRecallSelectedWaveform (), false,
        [safe] () { if (safe != nullptr) safe->recallSelectedWaveform (); });

    return toolsMenu;
}

void Assimil8orEditorComponent::exportPresetSettings ()
{
    fileChooser.reset (new juce::FileChooser ("Please select the file to export to...", appProperties.getImportExportMruFolder (), ""));
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc) mutable
    {
        if (fc.getURLResults ().size () == 1 && fc.getURLResults () [0].isLocalFile ())
        {
            auto exportPresetFile { fc.getURLResults () [0].getLocalFile () };
            appProperties.setImportExportMruFolder (exportPresetFile.getParentDirectory ().getFullPathName ());
            Assimil8orPreset assimil8orPreset;
            const auto result { assimil8orPreset.write (exportPresetFile, presetProperties.getValueTree ()) };
            if (result.failed ()) juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Export failed", result.getErrorMessage ());
        }
    }, nullptr);
}

void Assimil8orEditorComponent::exportPresetSettingsAndSamples ()
{
    // query user for folder/file name to export to. This will be a zip file containing the preset settings and samples
    fileChooser.reset (new juce::FileChooser ("Please select the zip file to export to...", appProperties.getImportExportMruFolder (), "*.zip"));
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc) mutable
    {
        if (fc.getURLResults ().size () == 1 && fc.getURLResults () [0].isLocalFile ())
        {
            auto exportContainerFile { fc.getURLResults () [0].getLocalFile () };
            if (exportContainerFile.getFileExtension ().isEmpty ())
                exportContainerFile = exportContainerFile.withFileExtension (".zip");
            appProperties.setImportExportMruFolder (exportContainerFile.getParentDirectory ().getFullPathName ());

            auto sampleFolder { juce::File (appProperties.getMostRecentFolder ()) };
            // we use a std::set because a sample may be used by more than one channel/zone, but we only need to export/copy it once
            std::set<juce::File> sampleFiles;
            SampleManagerProperties sampleManagerProperties (runtimeRootProperties.getValueTree (), SampleManagerProperties::WrapperType::client, SampleManagerProperties::EnableCallbacks::no);
            for (auto channelIndex { 0 }; channelIndex < kNumChannels; ++channelIndex)
                for (auto zoneIndex { 0 }; zoneIndex < kNumZones; ++zoneIndex)
                {
                    SampleProperties sampleProperties (sampleManagerProperties.getSamplePropertiesVT (channelIndex,zoneIndex), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
                     if (sampleProperties.getStatus () == SampleStatus::exists)
                         sampleFiles.emplace (sampleFolder.getChildFile (sampleProperties.getName ()));
                }
            // create temp folder to hold temp preset file
            const auto tempFolder { exportContainerFile.getParentDirectory ().getNonexistentChildFile (".a8-preset-export", "", false) };
            if (auto result { tempFolder.createDirectory () }; result.failed ())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Export failed", result.getErrorMessage ());
                return;
            }
            struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { tempFolder };

            // write out temp preset file
            Assimil8orPreset assimil8orPreset;
            auto presetFile { tempFolder.getChildFile (exportContainerFile.getFileNameWithoutExtension () + (".yml")) };
            if (auto result { assimil8orPreset.write (presetFile, presetProperties.getValueTree ()) }; result.failed ())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Export failed", result.getErrorMessage ());
                return;
            }

            juce::ZipFile::Builder exportContainerBuilder;
            // add preset file to zip file
            exportContainerBuilder.addFile (presetFile, 9);
            // add samples to zip file
            for (auto& sampleFile : sampleFiles)
                exportContainerBuilder.addFile (sampleFile, 9);

            // write out zip file
            juce::TemporaryFile stagedArchive (exportContainerFile);
            auto outputStream { stagedArchive.getFile ().createOutputStream () };
            if (outputStream == nullptr || outputStream->failedToOpen () || ! exportContainerBuilder.writeToStream (*outputStream, nullptr))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Export failed", "Unable to write the archive. The previous destination was not replaced.");
                return;
            }
            outputStream->flush ();
            const auto result { outputStream->getStatus () };
            outputStream.reset ();
            if (result.failed () || ! stagedArchive.overwriteTargetFileWithTemporary ())
                juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Export failed", "Unable to finish saving the archive. Check the destination and available space.");
        }
    }, nullptr);
}

void Assimil8orEditorComponent::importPresetSettings ()
{
    overwritePresetOrCancel ([this] ()
    {
        fileChooser.reset (new juce::FileChooser ("Please select the file to import from...", appProperties.getImportExportMruFolder (), ""));
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) mutable
        {
            if (fc.getURLResults ().size () == 1 && fc.getURLResults () [0].isLocalFile ())
            {
                auto importPresetFile { fc.getURLResults () [0].getLocalFile () };

                appProperties.setImportExportMruFolder (importPresetFile.getParentDirectory ().getFullPathName ());
                juce::ValueTree imported;
                const auto result { PresetFileOperations::read (importPresetFile, imported) };
                if (result.failed ())
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Import failed", result.getErrorMessage ());
                    return;
                }

                // change the imported Preset Id to the current Preset Id
                PresetProperties importedPresetProperties (imported, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
                importedPresetProperties.setId (presetProperties.getId (), false);

                // copy imported preset to current preset
                PresetProperties::copyTreeProperties (importedPresetProperties.getValueTree (), presetProperties.getValueTree ());
            }
        }, nullptr);
    },
    [] () {});
}

void Assimil8orEditorComponent::importPresetSettingsAndSamples ()
{
    const auto safe { juce::Component::SafePointer<Assimil8orEditorComponent> (this) };
    overwritePresetOrCancel ([safe] ()
    {
        if (safe == nullptr) return;
        const auto folder { safe->appProperties.getMostRecentFolder () };
        const auto before { safe->presetProperties.getValueTree ().createCopy () };
        safe->fileChooser = std::make_unique<juce::FileChooser> ("Import preset and samples (existing samples will not be overwritten)", safe->appProperties.getImportExportMruFolder (), "*.zip");
        safe->fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe, folder, before] (const juce::FileChooser& chooser)
            {
                if (safe == nullptr || safe->appProperties.getMostRecentFolder () != folder ||
                    ! safe->presetProperties.getValueTree ().isEquivalentTo (before)) return;
                const auto results { chooser.getURLResults () };
                if (results.size () != 1 || ! results[0].isLocalFile ()) return;
                const auto archive { results[0].getLocalFile () };
                juce::ValueTree imported;
                const auto result { PresetArchive::importPreset (archive, juce::File (folder), imported) };
                if (result.failed ())
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Import failed", result.getErrorMessage ());
                    return;
                }
                imported.setProperty (PresetProperties::IdPropertyId, safe->presetProperties.getId (), nullptr);
                PresetProperties::copyTreeProperties (imported, safe->presetProperties.getValueTree ());
                safe->appProperties.setImportExportMruFolder (archive.getParentDirectory ().getFullPathName ());
            }, nullptr);
    }, [] () {});
}

void Assimil8orEditorComponent::resized ()
{
    auto localBounds { getLocalBounds () };

    //midiConfigWindow.setBounds (localBounds);

    auto topRow { localBounds.removeFromTop (25) };
    topRow.removeFromTop (3);
    topRow.removeFromLeft (5);
    titleLabel.setBounds (topRow.removeFromLeft (57));
    topRow.removeFromLeft (3);
    // Name
    nameEditor.setBounds (topRow.removeFromLeft (150));
    topRow.removeFromLeft (10);
    // Midi Setup
    midiSetupLabel.setBounds (topRow.removeFromLeft (75));
    topRow.removeFromLeft (3);
    midiSetupComboBox.setBounds (topRow.removeFromLeft (50));
    // Preset tools belongs with the preset controls, not the save-status group.
    topRow.removeFromLeft (8);
    toolsButton.setBounds (topRow.removeFromLeft (100));

    topRow.removeFromRight (3);
    // Save Button
    saveButton.setBounds (topRow.removeFromRight (75));
    topRow.removeFromRight (6);
    savePendingLabel.setBounds (topRow.removeFromRight (176));

    // Channel Tabs
    const auto channelSectionY { titleLabel.getBottom () + 3 };
    channelTabs.setBounds (3, channelSectionY, juce::jmax (0, getWidth () - 6), juce::jmax (0, getHeight () - channelSectionY - 32));
    const auto bottomRowY (getLocalBounds ().getBottom () - 26);
    // this is used to overlay the 'right channel' to indicate it is inactive
    windowDecorator.setBounds (getLocalBounds ().removeFromBottom (26));

    // Data2 as CV
    data2AsCvLabel.setBounds (6, bottomRowY + 3, 55, 20);
    data2AsCvComboBox.setBounds (data2AsCvLabel.getRight () + 3, bottomRowY + 3, 28, 20);

    // Cross fade groups
    xfadeGroupsLabel.setBounds (data2AsCvComboBox.getRight () + 5, bottomRowY + 3, 50, 20);
    auto startX { xfadeGroupsLabel.getRight () + 2 };
    for (auto xfadeGroupIndex { 0 }; xfadeGroupIndex < XfadeGroupIndex::numberOfGroups; ++xfadeGroupIndex)
    {
        auto& xfadeGroup { xfadeGroups [xfadeGroupIndex] };
        xfadeGroup.xfadeGroupLabel.setBounds (startX + (xfadeGroupIndex * 155), bottomRowY + 3, 17, 20);

        xfadeGroup.xfadeCvLabel.setBounds (xfadeGroup.xfadeGroupLabel.getRight (), bottomRowY + 3, 20, 20);
        xfadeGroup.xfadeCvComboBox.setBounds (xfadeGroup.xfadeCvLabel.getRight () + 1, bottomRowY + 3, 28, 20);

        xfadeGroup.xfadeWidthLabel.setBounds (xfadeGroup.xfadeCvComboBox.getRight () + 3, bottomRowY + 3, 35, 20);
        xfadeGroup.xfadeWidthEditor.setBounds (xfadeGroup.xfadeWidthLabel.getRight () + 1, bottomRowY + 3, 40, 20);
    }
}

void Assimil8orEditorComponent::idDataChanged (int id)
{
    titleLabel.setText ("Preset " + juce::String (id), juce::NotificationType::dontSendNotification);
}

void Assimil8orEditorComponent::midiSetupDataChanged (int midiSetupId)
{
    midiSetupComboBox.setSelectedItemIndex (midiSetupId);
}

void Assimil8orEditorComponent::midiSetupUiChanged (int midiSetupId)
{
    presetProperties.setMidiSetup (midiSetupId, false);
}

void Assimil8orEditorComponent::nameDataChanged (juce::String name)
{
    nameEditor.setText (name, false);
}

void Assimil8orEditorComponent::nameUiChanged (juce::String name)
{
    presetProperties.setName (name, false);
}

void Assimil8orEditorComponent::data2AsCvDataChanged (juce::String data2AsCvString)
{
    data2AsCvComboBox.setSelectedItemText (data2AsCvString);
}

void Assimil8orEditorComponent::data2AsCvUiChanged (juce::String data2AsCvString)
{
    presetProperties.setData2AsCV (data2AsCvString, false);
}

void Assimil8orEditorComponent::xfadeCvDataChanged (int group, juce::String data2AsCvString)
{
    jassert (group >= 0 && group < 4);
    xfadeGroups [group].xfadeCvComboBox.setSelectedItemText (data2AsCvString);
}

void Assimil8orEditorComponent::xfadeCvUiChanged (int group, juce::String data2AsCvString)
{
    switch (group)
    {
        case XfadeGroupIndex::groupA : presetProperties.setXfadeACV (data2AsCvString, false); break;
        case XfadeGroupIndex::groupB : presetProperties.setXfadeBCV (data2AsCvString, false); break;
        case XfadeGroupIndex::groupC : presetProperties.setXfadeCCV (data2AsCvString, false); break;
        case XfadeGroupIndex::groupD : presetProperties.setXfadeDCV (data2AsCvString, false); break;
        default: jassertfalse; break;
    }
}

void Assimil8orEditorComponent::xfadeWidthDataChanged (int group, double width)
{
    jassert (group >= 0 && group < 4);
    xfadeGroups [group].xfadeWidthEditor.setText (formatXfadeWidthString (width));
}

void Assimil8orEditorComponent::xfadeWidthUiChanged (int group, double width)
{
    switch (group)
    {
        case XfadeGroupIndex::groupA: presetProperties.setXfadeAWidth (width, false); break;
        case XfadeGroupIndex::groupB: presetProperties.setXfadeBWidth (width, false); break;
        case XfadeGroupIndex::groupC: presetProperties.setXfadeCWidth (width, false); break;
        case XfadeGroupIndex::groupD: presetProperties.setXfadeDWidth (width, false); break;
        default: jassertfalse; break;
    }
}

void Assimil8orEditorComponent::timerCallback ()
{
    if (stereoCollapseJob && stereoCollapseJob->deliveryFailed.load ())
        finishStereoCollapse (stereoCollapseJob);
    if (sampleRenameJob && sampleRenameJob->deliveryFailed.load ())
        finishSampleRename (sampleRenameJob);
    updateSaveIndicator ();
}

void Assimil8orEditorComponent::updateSaveIndicator ()
{
    const auto dirty { presetProperties.isValid () && unEditedPresetProperties.isValid ()
        && ! PresetHelpers::areEntirePresetsEqual (unEditedPresetProperties.getValueTree (), presetProperties.getValueTree ()) };
    saveButton.setEnabled (dirty);
    savePendingLabel.setVisible (dirty);
}
