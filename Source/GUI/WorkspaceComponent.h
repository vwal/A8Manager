#pragma once
#include "MainComponent.h"
#include "ModernTheme.h"
#include "oolib/Properties/PersistentRootProperties.h"

// Scale the complete editor, including its fonts and hit targets. A viewport
// keeps all controls reachable when the window is smaller than the layout.
class WorkspaceComponent : public juce::Component
{
public:
    explicit WorkspaceComponent (juce::ValueTree root) : editor (root)
    {
        PersistentRootProperties persistent (root, PersistentRootProperties::WrapperType::client, PersistentRootProperties::EnableCallbacks::no);
        preferences.wrap (persistent.getValueTree (), GuiProperties::WrapperType::client, GuiProperties::EnableCallbacks::no);
        scale = preferences.getUiScale ();
        canvas.addAndMakeVisible (editor);
        viewport.setViewedComponent (&canvas, false);
        viewport.setScrollBarsShown (true, true);
        viewport.setScrollOnDragEnabled (false);
        addAndMakeVisible (viewport);
        for (const auto percentage : { 100, 125, 150, 175, 200 })
            scaleSelector.addItem (juce::String (percentage) + "%", percentage);
        scaleSelector.setSelectedId (juce::roundToInt (scale * 100.0), juce::dontSendNotification);
        scaleSelector.setTooltip ("Scale the entire interface, including text. Smaller windows can scroll.");
        scaleSelector.onChange = [this] ()
        {
            scale = scaleSelector.getSelectedId () / 100.0;
            preferences.setUiScale (scale);
            resized ();
        };
        addAndMakeVisible (scaleSelector);
        help.setButtonText ("Quick help");
        help.onClick = [this] ()
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon, "Editing shortcuts",
                "Folders: Options > Select Root Folder jumps to another location without scanning intermediate folders. Confirm the destination to start scanning; Cancel keeps the current folder. On macOS, Command-Shift-G in the chooser lets you enter a path.\n\n"
                "Values: left-drag vertically to adjust; Shift-drag for slower, unaccelerated fine steps. Focusing a field selects its contents; type to replace. Horizontal dragging selects text. Command-wheel on macOS / Ctrl-wheel elsewhere adjusts values; add Shift to nudge one increment.\n\n"
                "Waveform: use + / -, Fit, Zone or Loop. Scroll to zoom; double-click to fit. Edit edges: drag handles to resize, or the waveform to pan. Move zone / Move loop: drag to slide that pair without resizing; Shift for finer movement. White = sample, orange = loop. The yellow playhead shows the audition position.\n\n"
                "Audition speed: drag or type a multiplier (0.0625x to 4x); double-click the slider for 1x. Keep pitch preserves pitch while changing duration; turn it off for sampler-style varispeed. Zone PITCH OFFSET is heard in either mode. Extreme stretching can introduce artifacts. These preview controls do not change the preset.\n\n"
                "Loop join preview: END (left) meets START (right). Automatic visual gain makes quiet audio visible without changing its volume.\n\n"
                "Zones: Copy > next duplicates the current zone; Continue > next starts the next slice at the current end. Occupied zones require confirmation. Parenthesized voltages below the boundaries are read-only midpoint CV targets for external zone selection.\n\n"
                "UI size scales text and controls together. Changes are kept in separate preferences. Preset files are only changed when you save.");
        };
        addAndMakeVisible (help);
        setSize (1400, 880);
    }

private:
    GuiProperties preferences;
    MainComponent editor;
    juce::Component canvas;
    juce::Viewport viewport;
    juce::ComboBox scaleSelector;
    juce::TextButton help;
    double scale { 1.25 };

    void resized () override
    {
        auto bounds { getLocalBounds () };
        auto toolbar { bounds.removeFromTop (52).reduced (16, 10) };
        help.setBounds (toolbar.removeFromRight (104));
        toolbar.removeFromRight (12);
        scaleSelector.setBounds (toolbar.removeFromRight (92));
        viewport.setBounds (bounds);
        const auto width { juce::jmax (1160, static_cast<int> ((bounds.getWidth () - 16) / scale)) };
        const auto height { juce::jmax (800, static_cast<int> ((bounds.getHeight () - 16) / scale)) };
        editor.setTransform (juce::AffineTransform::scale (static_cast<float> (scale)));
        editor.setBounds (0, 0, width, height);
        canvas.setSize (juce::roundToInt (width * scale), juce::roundToInt (height * scale));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::background);
        g.setColour (Theme::accent);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText ("A8 MANAGER", 18, 5, 180, 25, juce::Justification::centredLeft);
        g.setColour (Theme::muted);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("SAMPLE & PRESET WORKSPACE", 19, 29, 340, 16, juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (16.0f));
        g.drawText ("UI size", scaleSelector.getX () - 70, 10, 62, 32, juce::Justification::centredRight);
    }
};
