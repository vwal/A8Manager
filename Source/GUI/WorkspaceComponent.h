#pragma once
#include "MainComponent.h"
#include "ModernTheme.h"
#include "WorkspaceHeaderLayout.h"
#include "../Assimil8or/Audio/AudioPlayerProperties.h"
#include "oolib/Properties/PersistentRootProperties.h"
#include "oolib/Properties/RuntimeRootProperties.h"

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
        samples.setButtonText ("Samples");
        designer.setButtonText ("Waveform designer");
        samples.setToggleState (true, juce::dontSendNotification);
        samples.onClick = [this] () { editor.showWaveformWorkspace (false); };
        designer.onClick = [this] () { editor.showWaveformWorkspace (true); };
        editor.onWorkspaceChanged = [this] (bool show)
        {
            samples.setToggleState (! show, juce::dontSendNotification);
            designer.setToggleState (show, juce::dontSendNotification);
        };
        addAndMakeVisible (samples);
        addAndMakeVisible (designer);
        bankExport.setButtonText ("Save/Export Bank");
        bankExport.setComponentID ("workspaceBankExport");
        bankExport.setTooltip ("Combine saved presets into a new flat A8 folder. Original folders and files are kept untouched.");
        bankExport.onClick = [this] () { editor.showBankExport (); };
        addAndMakeVisible (bankExport);
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
        RuntimeRootProperties runtime (root, RuntimeRootProperties::WrapperType::client, RuntimeRootProperties::EnableCallbacks::no);
        audioPlayerProperties.wrap (runtime.getValueTree (), AudioPlayerProperties::WrapperType::owner, AudioPlayerProperties::EnableCallbacks::yes);
        outputDevice.setComponentID ("workspaceOutputDevice");
        outputDevice.setJustificationType (juce::Justification::centredLeft);
        outputDevice.setFont (juce::FontOptions (14.0f));
        audioPlayerProperties.onOutputDeviceNameChange = [this] (juce::String name) { updateOutputDevice (name); };
        updateOutputDevice (audioPlayerProperties.getOutputDeviceName ());
        addAndMakeVisible (outputDevice);
        appearanceSelector.setComponentID ("workspaceAppearance");
        appearanceSelector.addItem ("Dark", 1);
        appearanceSelector.addItem ("Light", 2);
        appearanceSelector.setSelectedId (preferences.getLightAppearance () ? 2 : 1, juce::dontSendNotification);
        appearanceSelector.setTooltip ("Choose a light or dark interface. This does not change audio or preset settings.");
        appearanceSelector.onChange = [this] ()
        {
            const auto light { appearanceSelector.getSelectedId () == 2 };
            preferences.setLightAppearance (light);
            Theme::setAppearance (light);
        };
        addAndMakeVisible (appearanceSelector);
        audioSettings.setButtonText ("Audio Settings");
        audioSettings.setComponentID ("workspaceAudioSettings");
        audioSettings.setTooltip ("Choose the computer audio output device used for auditioning. This does not change the preset.");
        audioSettings.onClick = [this] () { audioPlayerProperties.showConfigDialog (false); };
        addAndMakeVisible (audioSettings);
        help.setButtonText ("Quick help");
        help.onClick = [this] ()
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon, "Editing shortcuts",
                "Waveform designer: the folder, preset slots and unsaved preset are shared with Samples. Generate & Assign creates uniquely named WAVs and a recipe, then assigns to the chosen channel/zone; banks use consecutive Master/Link channels. Review channel-wide changes, then use the designer's Save to create/update a Pnn - name folder containing the complete preset and its samples, and save the working preset. Working/shared samples stay in place. Copy the complete named folder directly to the SD-card root, not its loose files. An already-open named folder for that slot saves in place. CV and audio cannot share a channel; known CV channels have Mix and Mix modulation locked Off. Export package creates a separate design-only folder with your chosen preset number. Audition plays audio cycles or the full bank while shaping; monitor level/transpose do not affect exports. CV speaker audition is disabled. Expand waveform opens a larger preview; Escape/X closes it. Leaving the designer stops its audio. Load recipe reopens generated designs. Simply designing does not change the preset.\n\n"
                "Folders: Options > Select Root Folder jumps to another location without scanning intermediate folders. Confirm the destination to start scanning; Cancel keeps the current folder. On macOS, Command-Shift-G in the chooser lets you enter a path.\n\n"
                "Save/Export Bank: combine saved presets from explicitly added folders into a new flat bank folder. Resolve duplicate preset slots before export. Originals are always kept. Open bank after export is optional; once a bank is open, both workspaces save in place there instead of creating per-preset folders. Copy the whole bank folder directly under the SD-card root.\n\n"
                "Values: left-drag vertically to adjust; Shift-drag for slower, unaccelerated fine steps. Focusing a field selects its contents; type to replace. Horizontal dragging selects text. Command-wheel on macOS / Ctrl-wheel elsewhere adjusts values; add Shift to nudge one increment.\n\n"
                "Waveform: + / - zoom; click the zoom percentage or double-click to reset both axes. The arrow expands the view; X or Escape closes it. The gear/right-click menu offers Zoom, Jump (keys 1-4 with waveform focus) and Zero Crossing Nudge; right-click also offers Set Marker Here.\n\n"
                "Markers: sample start = deep green, sample end = deep red, loop start = light green, loop end/length = pink, matching the zone-panel keys. By default loops stay inside SAMPLE, with stripes on its skipped tail. Allow loop outside sample (below XFADE GRP) permits independent, file-bounded loops for the whole channel and hides stripes; all loop edits/nudges retain a four-frame minimum. Imports preserve valid external loops and enable this option. Static simulation does not emulate external CV. Drag handles to edit; the side-panel selection follows. Option/Alt-drag INSIDE a region or on its handle moves that pair without resizing. In overlapping regions the selected pair wins; handles identify their own pair. Add Shift for finer movement. Plain drag pans; right-drag zooms.\n\n"
                "Times: marker timestamps are source-file positions. Lengths beside END markers and below the waveform include zone PITCH OFFSET, but exclude audition speed and Keep pitch stretching. The active region is undimmed.\n\n"
                "Audition speed: drag or type a multiplier (0.0625x to 4x); double-click the slider for 1x. Keep pitch preserves pitch while changing duration; turn it off for sampler-style varispeed. Zone PITCH OFFSET is heard in either mode. Extreme stretching can introduce artifacts. These preview controls do not change the preset.\n\n"
                "Loop join preview: END (left) meets START (right). Automatic visual gain makes quiet audio visible without changing its volume. Gear/right-click > Match Opposite Boundary > marker > Left << / Right >> stops at the nearest crossing of the opposite boundary's amplitude, even away from zero. It does not chase a distant, slightly better match; if the nearest crossing does not improve the join, nothing moves. The opposite marker stays fixed, including in Loop Length mode. Move START left or END right to retain material near the edge. Matching AND Zero Crossing Nudge require Yes/No confirmation for moves over 50 ms at the source sample rate. Unlike matching, nudging Loop Start in Length mode moves Loop End with it to preserve the length. Audition the result: matching amplitudes does not guarantee matching slopes or the other stereo side.\n\n"
                "Zones: Copy > next duplicates the current zone; Continue > next starts the next slice at the current end. Both select the next zone at full-file Fit and vertical 100%. Occupied zones require confirmation. Parenthesized voltages below the boundaries are read-only midpoint CV targets for external zone selection.\n\n"
                "Right-click END or START in the small join preview for that boundary's directional nudge/match commands. Direct typed positions do not need a distance confirmation; automated moves over 50 ms do. Stereo-right waveforms support read-only zoom, pan, marker jumps and expansion; edit their shared markers from the left channel.\n\n"
                "Audio Settings in the top bar selects the computer's audition output device. Output shows the live device or none. Appearance switches between Dark and Light; numeric parameters use monospaced text. Preset tools, Channel tools and Zone tools act on their named scopes. UI size scales text and controls together. Changes are kept in separate preferences. Preset files are only changed when you save.");
        };
        addAndMakeVisible (help);
        setSize (1400, 880);
    }

private:
    GuiProperties preferences;
    AudioPlayerProperties audioPlayerProperties;
    MainComponent editor;
    juce::Component canvas;
    juce::Viewport viewport;
    juce::ComboBox scaleSelector, appearanceSelector;
    juce::Label outputDevice;
    juce::TextButton help, audioSettings;
    juce::TextButton samples, designer, bankExport;
    double scale { 1.25 };

    void updateOutputDevice (const juce::String& name)
    {
        outputDevice.setText ("Output: " + (name.isEmpty () ? juce::String ("none") : name), juce::dontSendNotification);
        outputDevice.setTooltip (name.isEmpty () ? "No active computer audio output. Choose one in Audio Settings."
                                               : "Computer audition output: " + name + ". Change it in Audio Settings.");
    }

    void resized () override
    {
        auto bounds { getLocalBounds () };
        const auto header { WorkspaceHeaderLayout::forWidth (getWidth ()) };
        bounds.removeFromTop (header.height);
        help.setBounds (header.help);
        audioSettings.setBounds (header.audioSettings);
        scaleSelector.setBounds (header.scaleSelector);
        samples.setBounds (header.samples);
        designer.setBounds (header.designer);
        bankExport.setBounds (header.bankExport);
        outputDevice.setBounds (header.outputDevice);
        appearanceSelector.setBounds (header.appearanceSelector);
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
        g.drawText ("UI size", WorkspaceHeaderLayout::forWidth (getWidth ()).scaleLabel, juce::Justification::centredRight);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Appearance", WorkspaceHeaderLayout::forWidth (getWidth ()).appearanceLabel, juce::Justification::centredRight);
    }
};
