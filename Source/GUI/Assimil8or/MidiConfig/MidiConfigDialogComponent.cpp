#include "MidiConfigDialogComponent.h"
#include "../../../Assimil8or/MidiSetup/MidiSetupFile.h"
#include "oolib/Properties/PersistentRootProperties.h"
#include "oolib/Properties/RuntimeRootProperties.h"

MidiConfigDialogComponent::MidiConfigDialogComponent ()
{
    setOpaque (true);
    for (auto curMidiSetupIndex { 0 }; curMidiSetupIndex < 9; ++curMidiSetupIndex)
        midiSetupTabs.addTab (juce::String::charToString ('1' + curMidiSetupIndex), Theme::panel, &midiSetupEditorComponents [curMidiSetupIndex], false);
    addAndMakeVisible (midiSetupTabs);

    saveButton.setButtonText ("SAVE");
    saveButton.setEnabled (false);
    addAndMakeVisible (saveButton);
    saveButton.onClick = [this] () { saveClicked (); };
    cancelButton.setButtonText ("CANCEL");
    addAndMakeVisible (cancelButton);
    cancelButton.onClick = [this] () { cancelClicked (); };
}

void MidiConfigDialogComponent::init (juce::ValueTree rootPropertiesVT)
{
    PersistentRootProperties persistentRootProperties (rootPropertiesVT, PersistentRootProperties::WrapperType::client, PersistentRootProperties::EnableCallbacks::no);
    RuntimeRootProperties runtimeRootProperties (rootPropertiesVT, RuntimeRootProperties::WrapperType::client, RuntimeRootProperties::EnableCallbacks::no);
    guiControlProperties.wrap (runtimeRootProperties.getValueTree (), GuiControlProperties::WrapperType::client, GuiControlProperties::EnableCallbacks::no);
    appProperties.wrap (persistentRootProperties.getValueTree (), AppProperties::WrapperType::client, AppProperties::EnableCallbacks::no);

    for (auto curMidiSetupIndex { 0 }; curMidiSetupIndex < 9; ++curMidiSetupIndex)
    {
        MidiSetupProperties midiSetupProperties { {}, MidiSetupProperties::WrapperType::owner, MidiSetupProperties::EnableCallbacks::no };
        MidiSetupProperties uneditedMidiSetupProperties { {}, MidiSetupProperties::WrapperType::owner, MidiSetupProperties::EnableCallbacks::no };
        midiSetupPropertiesListVT.addChild (midiSetupProperties.getValueTree (), -1, nullptr);
        uneditedMidiSetupPropertiesListVT.addChild (midiSetupProperties.getValueTree ().createCopy (), -1, nullptr);

        midiSetupEditorComponents [curMidiSetupIndex].init (curMidiSetupIndex, midiSetupPropertiesListVT, uneditedMidiSetupPropertiesListVT);
    }
}

void MidiConfigDialogComponent::handleShowChange (bool show)
{
    if (show)
    {
        const auto result { loadMidiSetups () };
        if (result.failed ())
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Some MIDI setups cannot be edited", result.getErrorMessage ());
        startTimer (250);
    }
    else
    {
        stopTimer ();
    }
}

juce::Result MidiConfigDialogComponent::loadMidiSetups ()
{
    // Keep the save destination bound to the folder that was actually loaded.
    loadedFolder = juce::File (appProperties.getMostRecentFolder ());
    juce::StringArray errors;
    // iterate over possible midi setup files
    for (auto curMidiSetupIndex { 0 }; curMidiSetupIndex < 9; ++curMidiSetupIndex)
    {
        MidiSetupProperties midiSetupProperties { midiSetupPropertiesListVT.getChild (curMidiSetupIndex), MidiSetupProperties::WrapperType::owner, MidiSetupProperties::EnableCallbacks::no };
        MidiSetupProperties uneditedMidiSetupProperties { uneditedMidiSetupPropertiesListVT.getChild (curMidiSetupIndex), MidiSetupProperties::WrapperType::owner, MidiSetupProperties::EnableCallbacks::no };
        const auto midiSetupRawFile { loadedFolder.getChildFile ("midi" + juce::String (curMidiSetupIndex + 1)).withFileExtension ("yml") };
        auto& document { midiSetupFiles [static_cast<size_t> (curMidiSetupIndex)] };
        const auto result { document.read (midiSetupRawFile) };
        if (result.wasOk ())
            midiSetupProperties.copyFrom (document.getMidiSetupPropertiesVT ());
        else
        {
            midiSetupProperties.copyFrom (MidiSetupProperties ({}, MidiSetupProperties::WrapperType::owner, MidiSetupProperties::EnableCallbacks::no).getValueTree ());
            errors.add (midiSetupRawFile.getFileName () + ": " + result.getErrorMessage ());
        }
        midiSetupProperties.getValueTree ().setProperty (MidiSetupProperties::FileReadOnlyPropertyId, result.failed (), nullptr);
        midiSetupEditorComponents [static_cast<size_t> (curMidiSetupIndex)].setEnabled (result.wasOk ());
        uneditedMidiSetupProperties.copyFrom (midiSetupProperties.getValueTree ());
    }
    timerCallback ();
    return errors.isEmpty () ? juce::Result::ok () : juce::Result::fail (errors.joinIntoString ("\n\n") +
        "\n\nThe affected tabs are disabled and their files will be left untouched. Other setups can still be edited.");
}

void MidiConfigDialogComponent::cancelClicked ()
{
    if (anyMidiSetupsEdited)
    {
        juce::AlertWindow::showOkCancelBox (juce::AlertWindow::WarningIcon, "Midi Setups Have Been Edited",
            "You have not saved your edited Midi Setups.\n  Select Continue to lose your changes.\n  Select Cancel to go back and save.", "Continue (lose changes)", "Cancel", nullptr,
            juce::ModalCallbackFunction::create ([this] (int option)
            {
                juce::MessageManager::callAsync ([this, option] ()
                {
                    if (option == 1) // Continue
                        closeDialog ();
                });
            }));
    }
    else
    {
        closeDialog ();
    }
}

void MidiConfigDialogComponent::closeDialog ()
{
    guiControlProperties.showMidiConfigWindow (false);
}

void MidiConfigDialogComponent::saveClicked ()
{
    const auto result { saveMidiSetups () };
    if (result.failed ())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "MIDI save failed", result.getErrorMessage ());
        return;
    }
    closeDialog ();
}

bool MidiConfigDialogComponent::isSetupEdited (int index) const
{
    return ! MidiSetupFile::settingsEqual (uneditedMidiSetupPropertiesListVT.getChild (index), midiSetupPropertiesListVT.getChild (index));
}

juce::Result MidiConfigDialogComponent::saveMidiSetups ()
{
    // Preflight every edited slot before writing any of them. Unedited and unreadable
    // setups are not rewritten just because a different tab was edited.
    for (auto curMidiSetupIndex { 0 }; curMidiSetupIndex < 9; ++curMidiSetupIndex)
    {
        if (! isSetupEdited (curMidiSetupIndex))
            continue;
        const auto result { midiSetupFiles [static_cast<size_t> (curMidiSetupIndex)].checkForExternalChanges () };
        if (result.failed ())
            return result;
    }
    juce::StringArray saved;
    for (auto curMidiSetupIndex { 0 }; curMidiSetupIndex < 9; ++curMidiSetupIndex)
    {
        if (! isSetupEdited (curMidiSetupIndex))
            continue;
        const auto file { loadedFolder.getChildFile ("midi" + juce::String (curMidiSetupIndex + 1) + ".yml") };
        const auto result { midiSetupFiles [static_cast<size_t> (curMidiSetupIndex)].write (file, midiSetupPropertiesListVT.getChild (curMidiSetupIndex)) };
        if (result.failed ())
        {
            timerCallback ();
            return juce::Result::fail (result.getErrorMessage () + (saved.isEmpty () ? juce::String {} :
                "\n\nAlready saved: " + saved.joinIntoString (", ") + ". Remaining edits are still open; you can retry."));
        }
        MidiSetupProperties baseline (uneditedMidiSetupPropertiesListVT.getChild (curMidiSetupIndex), MidiSetupProperties::WrapperType::client, MidiSetupProperties::EnableCallbacks::no);
        baseline.copyFrom (midiSetupPropertiesListVT.getChild (curMidiSetupIndex));
        saved.add (file.getFileName ());
    }
    timerCallback ();
    return juce::Result::ok ();
}

void MidiConfigDialogComponent::timerCallback ()
{
    anyMidiSetupsEdited = false;
    for (auto curMidiSetupIndex { 0 }; curMidiSetupIndex < 9; ++curMidiSetupIndex)
    {
        const auto midiSetupEdited { isSetupEdited (curMidiSetupIndex) };
        const auto unreadable { midiSetupFiles [static_cast<size_t> (curMidiSetupIndex)].getParseResult ().failed () };
        midiSetupTabs.setTabName (curMidiSetupIndex, juce::String::charToString ('1' + curMidiSetupIndex) + (unreadable ? "!" : midiSetupEdited ? "*" : ""));
        anyMidiSetupsEdited |= midiSetupEdited;
    }
    saveButton.setEnabled (anyMidiSetupsEdited);
}

void MidiConfigDialogComponent::resized ()
{
    constexpr auto kButtonHeight { 25 };
    constexpr auto kButtonBorder { 5 };
    constexpr auto kButtonWidth { 60 };
    constexpr auto kBetweenButtons { 5 };
    auto localBounds { getLocalBounds () };
    localBounds.reduce (5, 5);
    const auto tabArea { localBounds.removeFromTop (getHeight () - kButtonHeight - (kButtonBorder * 2)) };
    midiSetupTabs.setBounds (tabArea);
    localBounds.removeFromTop (kButtonBorder);
    saveButton.setBounds (localBounds.removeFromRight (kButtonWidth));
    localBounds.removeFromRight (kBetweenButtons);
    cancelButton.setBounds (localBounds.removeFromRight (kButtonWidth));
}

void MidiConfigDialogComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::border);
    g.drawRect (getLocalBounds (), 1);
}
