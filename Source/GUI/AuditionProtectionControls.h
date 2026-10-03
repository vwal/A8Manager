#pragma once

#include "GuiProperties.h"
#include "ModernTheme.h"
#include "../Assimil8or/Audio/AudioPlayerProperties.h"
#include "../Assimil8or/Audio/AuditionSignalCheck.h"
#include "oolib/Properties/PersistentRootProperties.h"
#include "oolib/Properties/RuntimeRootProperties.h"

// A single, always-visible preference/status for both computer audition routes.
// The audio player owns detection and gain; this component never edits a preset.
class AuditionProtectionControls : public juce::Component
{
public:
    AuditionProtectionControls ()
    {
        automaticReduction.setComponentID ("autoReduceAudition");
        automaticReduction.setButtonText ("Auto-reduce audition");
        automaticReduction.setTooltip ("Reduce computer audition by an additional 24 dB when sustained DC or strong sub-audio is detected. "
            "Turn this off to keep the normal monitoring level. Neither setting changes WAVs or presets. "
            "Detection and reduced volume cannot guarantee speaker/headphone safety; known CV files remain blocked.");
        automaticReduction.onClick = [this] ()
        {
            player.setAutoReduceAudition (automaticReduction.getToggleState (), true);
        };
        addAndMakeVisible (automaticReduction);
        signalStatus.setComponentID ("auditionSignalStatus");
        signalStatus.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        signalStatus.setMinimumHorizontalScale (1.0f);
        signalStatus.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (signalStatus);
    }

    void init (juce::ValueTree root)
    {
        PersistentRootProperties persistent (root, PersistentRootProperties::WrapperType::client, PersistentRootProperties::EnableCallbacks::no);
        RuntimeRootProperties runtime (root, RuntimeRootProperties::WrapperType::client, RuntimeRootProperties::EnableCallbacks::no);
        preferences.wrap (persistent.getValueTree (), GuiProperties::WrapperType::owner, GuiProperties::EnableCallbacks::no);
        player.wrap (runtime.getValueTree (), AudioPlayerProperties::WrapperType::owner, AudioPlayerProperties::EnableCallbacks::yes);
        player.onAutoReduceAuditionChange = [this] (bool enabled)
        {
            preferences.setAutoReduceAudition (enabled);
            automaticReduction.setToggleState (enabled, juce::dontSendNotification);
            updateStatus ();
        };
        player.onAuditionSignalWarningChange = [this] (bool) { updateStatus (); };
        player.onAuditionAttenuatedChange = [this] (bool) { updateStatus (); };
        player.setAutoReduceAudition (preferences.getAutoReduceAudition (), true);
        automaticReduction.setToggleState (player.getAutoReduceAudition (), juce::dontSendNotification);
        updateStatus ();
    }

private:
    GuiProperties preferences;
    AudioPlayerProperties player;
    juce::ToggleButton automaticReduction;
    juce::Label signalStatus;

    void updateStatus ()
    {
        const auto detected { player.getAuditionSignalWarning () };
        const auto reduced { player.getAuditionAttenuated () };
        signalStatus.setText (juce::String::fromUTF8 (detected ? (reduced ? "DC / sub-audio detected — level −24 dB"
                                                           : "DC / sub-audio detected — reduction off")
                                      : (reduced ? "Checking signal — audition reduced" : "")), juce::dontSendNotification);
        Theme::bindColour (signalStatus, juce::Label::textColourId, [detected] { return detected ? Theme::error : Theme::muted; });
        signalStatus.setTooltip (detected ? "Sustained DC or strong sub-audio was detected in the audition signal. "
            "Auto-reduce audition applies an additional 24 dB reduction without muting playback. "
            "It does not alter the Monitor level control, WAV files or presets. This is not a guarantee of safe listening."
            : reduced ? "The current audition is temporarily reduced while the changed signal is checked. Playback continues." : "");
    }

    void resized () override
    {
        auto row { getLocalBounds () };
        automaticReduction.setBounds (row.removeFromLeft (216));
        row.removeFromLeft (10);
        signalStatus.setBounds (row);
    }
};
