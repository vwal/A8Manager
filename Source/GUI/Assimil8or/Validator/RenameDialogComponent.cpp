#include "RenameDialogComponent.h"
#include "../../../Assimil8or/SafeRename.h"

RenameDialogContent::RenameDialogContent (juce::File oldFile, int maxNameLength, std::function<void (bool)> theDoneCallback)
{
    doneCallback = theDoneCallback;

    oldNameLabel.setColour (juce::Label::ColourIds::textColourId, juce::Colours::black);
    oldNameLabel.setText ("Current Name: " + oldFile.getFileName (), juce::NotificationType::dontSendNotification);
    addAndMakeVisible (oldNameLabel);
    newNamePromptLabel.setColour (juce::Label::ColourIds::textColourId, juce::Colours::black);
    newNamePromptLabel.setText ("New Name:", juce::NotificationType::dontSendNotification);
    addAndMakeVisible (newNamePromptLabel);

    newNameEditor.setIndents (2, 0);
    newNameEditor.setInputRestrictions (maxNameLength, {});
    newNameEditor.setJustification (juce::Justification::centredLeft);
    newNameEditor.onReturnKey = [this, oldFile] () { doRename (oldFile); };
    newNameEditor.onTextChange = [this, oldFile] () { checkNameAvailable (oldFile); };
    addAndMakeVisible (newNameEditor);

    okButton.setButtonText ("OK");
    okButton.setEnabled (false);
    addAndMakeVisible (okButton);
    cancelButton.setButtonText ("Cancel");
    addAndMakeVisible (cancelButton);

    cancelButton.onClick = [this] () { closeDialog (false); };
    okButton.onClick = [this, oldFile] () { doRename (oldFile); };

    newNameEditor.setWantsKeyboardFocus (true);
}

void RenameDialogContent::doRename (juce::File oldFile)
{
    const auto result { SafeRename::apply (oldFile, newNameEditor.getText ()) };
    if (result.wasOk ()) closeDialog (true);
    else juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Rename failed", result.getErrorMessage ());
}

void RenameDialogContent::checkNameAvailable (juce::File oldFile)
{
    juce::File target;
    const auto result { SafeRename::destination (oldFile, newNameEditor.getText (), target) };
    okButton.setEnabled (result.wasOk () && target != oldFile);
    newNameEditor.setTooltip (result.failed () ? result.getErrorMessage () : juce::String {});
}

void RenameDialogContent::closeDialog (bool renamed)
{
    if (doneCallback != nullptr)
        doneCallback (renamed);
    if (juce::DialogWindow * dw { findParentComponentOfClass<juce::DialogWindow> () })
        dw->exitModalState (0);
    delete this;
}

void RenameDialogContent::paint (juce::Graphics& g)
{
    if (isVisible () && neverVisible)
    {
        neverVisible = true;
        newNameEditor.grabKeyboardFocus ();
    }
    g.fillAll (juce::Colours::lightgrey);
}

void RenameDialogContent::resized ()
{
    auto localBounds { getLocalBounds () };
    auto bottomRow { localBounds.removeFromBottom (28).withTrimmedBottom (5) };
    okButton.setColour (juce::TextButton::ColourIds::textColourOnId, juce::Colours::white);
    okButton.setColour (juce::TextButton::ColourIds::textColourOffId, juce::Colours::white);
    okButton.setBounds (bottomRow.removeFromLeft (65).withTrimmedLeft (5));
    cancelButton.setColour (juce::TextButton::ColourIds::textColourOnId, juce::Colours::white);
    cancelButton.setColour (juce::TextButton::ColourIds::textColourOffId, juce::Colours::white);
    cancelButton.setBounds (bottomRow.removeFromLeft (65).withTrimmedLeft (5));

    oldNameLabel.setBounds (localBounds.removeFromTop (35).withTrimmedTop (5));

    //localBounds.removeFromTop (5);
    auto newNameRow { localBounds.removeFromTop (35).withTrimmedTop (5) };
    newNamePromptLabel.setBounds (newNameRow.removeFromLeft (80).withTrimmedLeft (5));
    newNameEditor.setBounds (newNameRow.reduced (5, 2));
}
