#include "BottomStatusWindow.h"
#include "ModernTheme.h"
#include "oolib/Properties/RuntimeRootProperties.h"

BottomStatusWindow::BottomStatusWindow ()
{
    addAndMakeVisible (progressUpdateLabel);
}

void BottomStatusWindow::init (juce::ValueTree rootPropertiesVT)
{
    RuntimeRootProperties runtimeRootProperties (rootPropertiesVT, RuntimeRootProperties::WrapperType::client, RuntimeRootProperties::EnableCallbacks::no);
    validatorProperties.wrap (runtimeRootProperties.getValueTree (), ValidatorProperties::WrapperType::client, ValidatorProperties::EnableCallbacks::yes);
    validatorProperties.onProgressUpdateChanged = [this] (juce::String progressUpdate)
    {
        juce::MessageManager::callAsync ([this, progressUpdate] ()
        {
            updateProgress (progressUpdate);
        });
    };
}

void BottomStatusWindow::updateProgress (juce::String progressUpdate)
{
    progressUpdateLabel.setText (progressUpdate, juce::NotificationType::dontSendNotification);
}

void BottomStatusWindow::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
}

void BottomStatusWindow::resized ()
{
    auto localBounds { getLocalBounds () };
    localBounds.reduce (5, 3);

    progressUpdateLabel.setBounds (localBounds);
}
