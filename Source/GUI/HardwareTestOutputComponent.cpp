#include "HardwareTestOutputComponent.h"
#include <exception>

namespace
{
    void setupReadOnlyText (juce::TextEditor& editor, const juce::String& name)
    {
        editor.setName (name);
        editor.setMultiLine (true, true);
        editor.setReadOnly (true);
        editor.setScrollbarsShown (true);
        editor.setCaretVisible (false);
        editor.setFont (juce::FontOptions (16.0f));
        editor.setIndents (8, 6);
    }
}

HardwareTestOutputComponent::HardwareTestOutputComponent (const WaveformDesign::Settings& design,
    juce::File folder, const juce::String& name, int presetNumber)
    : designSnapshot (design), initialFolder (std::move (folder))
{
    setLookAndFeel (&look);
    title.setText ("Hardware test output", juce::dontSendNotification);
    title.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    introduction.setText ("Create a separate WAV + preset + measurement-reference package.\n"
                          "Nothing is played here, and your current design and preset are not changed.", juce::dontSendNotification);
    introduction.setFont (juce::FontOptions (16.0f));
    introduction.setMinimumHorizontalScale (1.0f);
    sourceLabel.setText ("Test signal", juce::dontSendNotification);
    levelLabel.setText ("Built-in peak (% FS)", juce::dontSendNotification);
    presetLabel.setText ("Package preset slot", juce::dontSendNotification);
    nameLabel.setText ("Package name", juce::dontSendNotification);
    for (auto* label : { &sourceLabel, &durationLabel, &levelLabel, &presetLabel, &nameLabel })
        label->setFont (juce::FontOptions (16.0f));
    for (auto* label : { &title, &introduction, &sourceLabel, &durationLabel, &levelLabel, &presetLabel, &nameLabel })
    {
        label->setBorderSize ({});
        addAndMakeVisible (*label);
    }
    source.addItem ("Current waveform design (snapshot)", 1);
    source.addItem ("Audio: 440 Hz sine", 2);
    source.addItem ("CV: held DC levels (0, +, 0, -, 0)", 3);
    source.addItem ("CV: sine", 4);
    source.addItem ("CV: positive ramp up/down", 5);
    source.setSelectedId (1, juce::dontSendNotification);
    source.setComponentID ("hardwareTestSource");
    source.onChange = [this] ()
    {
        safetyAcknowledged.setToggleState (false, juce::dontSendNotification);
        refreshControls (true);
    };
    addAndMakeVisible (source);

    auto setupNumber = [this] (juce::Slider& slider, double minimum, double maximum, double step, double value, const char* id)
    {
        slider.setSliderStyle (juce::Slider::IncDecButtons);
        slider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 114, 28);
        slider.setRange (minimum, maximum, step);
        slider.setValue (value, juce::dontSendNotification);
        slider.setScrollWheelEnabled (false);
        slider.setComponentID (id);
        slider.onValueChange = [this] () { refreshControls (true); };
        addAndMakeVisible (slider);
    };
    setupNumber (duration, 1.0, 60.0, 0.1, 10.0, "hardwareTestDuration");
    duration.setTextValueSuffix (" s");
    duration.setTooltip ("For built-in signals, the test duration. For the current design, the reference observation window only; its original waveform and playback mode are preserved.");
    setupNumber (level, 1.0, 25.0, 0.1, 10.0, "hardwareTestLevel");
    level.setTextValueSuffix (" %");
    level.setTooltip ("Built-in signal peak as a fraction of digital full scale, not output volts. Current-design exports retain the original levels, offsets and tuning.");
    setupNumber (preset, 1.0, 199.0, 1.0, juce::jlimit (1, 199, presetNumber), "hardwareTestPreset");
    preset.setTooltip ("Preset filename/slot inside the new package only. The current working preset is not replaced or assigned.");
    outputName.setText (name.trim ().isEmpty () ? "Hardware test" : name.trim () + " test", false);
    outputName.setComponentID ("hardwareTestName");
    outputName.setFont (juce::FontOptions (16.0f));
    outputName.onTextChange = [this] () { refreshControls (true); };
    addAndMakeVisible (outputName);

    setupReadOnlyText (summary, "Output routing and reference");
    setupReadOnlyText (safety, "Hardware connection safety");
    setupReadOnlyText (status, "Export status and output path");
    summary.setWantsKeyboardFocus (false);
    safety.setWantsKeyboardFocus (false);
    status.setComponentID ("hardwareTestStatus");
    status.setTooltip ("Select text to copy the package path or export result.");
    safety.setText ("Before connecting the test outputs:\n"
                    "On Assimil8or, disconnect speakers/headphones from all tested individual outputs\n"
                    "and both stereo mix outputs.\n"
                    "For CV, use a DC-coupled oscilloscope. % FS is digital full scale, not volts.\n"
                    "Test channels export with Mix and Mix Mod OFF; Auto Trigger is OFF.\n"
                    "Other players and later hardware setting changes can bypass these protections.", false);
    for (auto* editor : { &summary, &safety, &status }) addAndMakeVisible (*editor);
    safetyAcknowledged.setComponentID ("hardwareTestSafetyAcknowledgement");
    safetyAcknowledged.setToggleState (false, juce::dontSendNotification);
    safetyAcknowledged.onClick = [this] () { refreshControls (true); };
    addAndMakeVisible (safetyAcknowledged);
    exportButton.setComponentID ("hardwareTestExport");
    exportButton.onClick = [this] () { beginExport (); };
    closeButton.onClick = [this] ()
    {
        if (activity == Activity::idle)
            if (auto* dialog { findParentComponentOfClass<juce::DialogWindow> () }) dialog->exitModalState (0);
    };
    addAndMakeVisible (exportButton);
    addAndMakeVisible (closeButton);

    chooseFolder = [this] (const juce::File& start, std::function<void (juce::File)> callback)
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose parent folder for a separate hardware test package", start, "");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safe = juce::Component::SafePointer<HardwareTestOutputComponent> (this), callback = std::move (callback)] (const juce::FileChooser& dialog)
            {
                const auto selected { dialog.getResult () };
                if (safe == nullptr) return;
                safe->chooser.reset ();
                callback (selected);
            });
    };
    writePackage = [] (const HardwareTestOutput::Settings& settings, const juce::File& parent,
                       const juce::String& packageName, HardwareTestOutput::ExportResult& result)
    {
        return HardwareTestOutput::exportPackage (settings, parent, packageName, result);
    };
    setSize (840, 720);
    lookAndFeelChanged ();
    refreshControls ();
}

HardwareTestOutputComponent::~HardwareTestOutputComponent ()
{
    stopTimer ();
    chooser.reset ();
    // The worker owns only its detached request/result, never this component.
    // Finish a package safely even if the window/application is closed mid-export.
    if (worker.joinable ()) worker.join ();
    setLookAndFeel (nullptr);
}

void HardwareTestOutputComponent::show (const WaveformDesign::Settings& design, juce::File folder,
                                       const juce::String& name, int presetNumber)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new HardwareTestOutputComponent (design, std::move (folder), name, presetNumber));
    options.dialogTitle = "Hardware test output";
    options.dialogBackgroundColour = Theme::background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync ();
}

bool HardwareTestOutputComponent::usesCurrentDesign () const { return source.getSelectedId () == 1; }

HardwareTestOutput::Settings HardwareTestOutputComponent::settingsFromControls () const
{
    HardwareTestOutput::Settings settings;
    settings.signal = static_cast<HardwareTestOutput::Signal> (source.getSelectedId () - 1);
    settings.design = designSnapshot;
    settings.durationSeconds = duration.getValue ();
    settings.level = level.getValue () / 100.0;
    settings.presetNumber = juce::roundToInt (preset.getValue ());
    return settings;
}

juce::Result HardwareTestOutputComponent::validateControls () const
{
    if (source.getSelectedId () < 1 || source.getSelectedId () > 5)
        return juce::Result::fail ("Select a test signal.");
    const auto name { outputName.getText ().trim () };
    if (name.isEmpty () || name == "." || name == ".." || juce::File::createLegalFileName (name) != name)
        return juce::Result::fail ("Enter a non-empty package name without path separators or reserved filename characters.");
    return HardwareTestOutput::validate (settingsFromControls ());
}

void HardwareTestOutputComponent::refreshControls (bool resetStatus)
{
    const auto idle { activity == Activity::idle };
    source.setEnabled (idle);
    duration.setEnabled (idle);
    level.setEnabled (idle && ! usesCurrentDesign ());
    preset.setEnabled (idle);
    outputName.setEnabled (idle);
    safetyAcknowledged.setEnabled (idle);
    closeButton.setEnabled (idle);
    const auto valid { validateControls () };
    exportButton.setEnabled (idle && valid.wasOk () && safetyAcknowledged.getToggleState ());
    updateSummary ();
    if (idle)
    {
        if (valid.failed ()) setStatus (valid.getErrorMessage (), true);
        else if (status.getText ().isEmpty () || resetStatus)
            setStatus ("Check the safety acknowledgment, then choose a parent folder. A new package will be created; existing presets are not overwritten.");
    }
}

void HardwareTestOutputComponent::updateSummary ()
{
    const auto current { usesCurrentDesign () };
    const auto cv { current ? designSnapshot.mode == WaveformDesign::Mode::modulation : source.getSelectedId () >= 3 };
    const auto voices { current && designSnapshot.mode == WaveformDesign::Mode::layers ? designSnapshot.voiceCount : 1 };
    durationLabel.setText (current ? "Reference window" : "Signal duration", juce::dontSendNotification);
    auto text { juce::String (cv ? "CV" : "AUDIO") + " test: " + (voices == 1 ? "CH 1" : "CH 1-" + juce::String (voices)) +
        ". Separate -30 dBFS sync reference: " + (voices >= 8 ? juce::String ("no channel available") : "CH " + juce::String (voices + 1)) + ".\n" +
        "Record individual outputs separately. Reference timing and expected values are in the exported manifest.\n" };
    if (current)
    {
        text += "Current design keeps original levels, DC offsets, tuning and playback. It may reach full scale: the built-in 25% cap does NOT apply.\n";
        text += designSnapshot.playback == WaveformDesign::Playback::oneShot
            ? "The reference window does not change its duration."
            : "Continuous loops do NOT stop at the END reference; stop them manually.";
    }
    else
        text += "Built-in signal: " + juce::String (level.getValue (), 1) + "% FS peak, " + juce::String (duration.getValue (), 1) +
                " seconds, One Shot. The reference level is fixed and independent.";
    summary.setText (text, false);
}

void HardwareTestOutputComponent::setStatus (const juce::String& text, bool error)
{
    statusIsError = error;
    status.setText (text, false);
    status.applyColourToAllText (error ? Theme::error : Theme::text, true);
}

void HardwareTestOutputComponent::beginExport ()
{
    if (activity != Activity::idle) return;
    const auto valid { validateControls () };
    if (valid.failed ()) { setStatus (valid.getErrorMessage (), true); return; }
    if (! safetyAcknowledged.getToggleState ())
    { setStatus ("Check the connection-safety acknowledgment before exporting.", true); return; }
    const auto settings { settingsFromControls () };
    const auto name { outputName.getText ().trim () };
    const auto generation { ++chooserGeneration };
    activity = Activity::choosingFolder;
    refreshControls ();
    setStatus ("Choose a parent folder. The design and test settings are now captured for this export.");
    auto safe { juce::Component::SafePointer<HardwareTestOutputComponent> (this) };
    try
    {
        chooseFolder (initialFolder, [safe, settings, name, generation] (juce::File folder)
        {
            if (safe == nullptr || safe->activity != Activity::choosingFolder || safe->chooserGeneration != generation) return;
            if (! folder.isDirectory ())
            {
                safe->activity = Activity::idle;
                safe->setStatus ("Export canceled. No package was created.");
                safe->refreshControls ();
                return;
            }
            safe->initialFolder = folder;
            safe->launchExport (settings, std::move (folder), name);
        });
    }
    catch (const std::exception& error)
    {
        ++chooserGeneration;
        chooser.reset ();
        activity = Activity::idle;
        setStatus (juce::String ("Could not open the folder chooser: ") + error.what (), true);
        refreshControls ();
    }
}

void HardwareTestOutputComponent::launchExport (HardwareTestOutput::Settings settings, juce::File folder, juce::String name)
{
    if (worker.joinable ()) worker.join ();
    job = std::make_shared<ExportJob> ();
    activity = Activity::exporting;
    refreshControls ();
    setStatus ("Creating and verifying the hardware test package...\nNothing is being played. Please wait before closing.");
    try
    {
        worker = std::thread ([state = job, settings = std::move (settings), folder = std::move (folder), name = std::move (name), writer = writePackage] ()
        {
            try { state->result = writer (settings, folder, name, state->files); }
            catch (const std::exception& error) { state->result = juce::Result::fail (juce::String ("Export failed: ") + error.what ()); }
            catch (...) { state->result = juce::Result::fail ("Export failed with an unexpected error."); }
            state->done.store (true, std::memory_order_release);
        });
        startTimerHz (20);
    }
    catch (const std::exception& error)
    {
        activity = Activity::idle;
        setStatus (juce::String ("Could not start the export worker: ") + error.what (), true);
        refreshControls ();
    }
}

void HardwareTestOutputComponent::timerCallback ()
{
    if (job == nullptr || ! job->done.load (std::memory_order_acquire)) return;
    stopTimer ();
    if (worker.joinable ()) worker.join ();
    activity = Activity::idle;
    if (job->result.wasOk ())
        setStatus ("Package ready. Current preset unchanged.\n" + job->files.folder.getFullPathName () +
                   "\nLoad the exported preset on Assimil8or only after checking the routing. Nothing has been opened or played automatically.");
    else
        setStatus (job->result.getErrorMessage (), true);
    job.reset ();
    refreshControls ();
}

void HardwareTestOutputComponent::lookAndFeelChanged ()
{
    title.setColour (juce::Label::textColourId, Theme::accent);
    for (auto* editor : { &summary, &safety, &status })
    {
        editor->setColour (juce::TextEditor::backgroundColourId, Theme::panel);
        editor->setColour (juce::TextEditor::outlineColourId, Theme::border);
    }
    safety.applyColourToAllText (Theme::warning, true);
    summary.applyColourToAllText (Theme::text, true);
    status.applyColourToAllText (statusIsError ? Theme::error : Theme::text, true);
    repaint ();
}

void HardwareTestOutputComponent::resized ()
{
    const auto width { getWidth () - 40 };
    title.setBounds (20, 16, width, 30);
    introduction.setBounds (20, 50, width, 40);
    sourceLabel.setBounds (20, 98, 120, 30);
    source.setBounds (146, 98, width - 126, 30);
    const auto column { (width - 32) / 3 };
    const auto second { 20 + column + 16 }, third { second + column + 16 };
    durationLabel.setBounds (20, 136, column, 22);
    levelLabel.setBounds (second, 136, column, 22);
    presetLabel.setBounds (third, 136, column, 22);
    duration.setBounds (20, 160, column, 32);
    level.setBounds (second, 160, column, 32);
    preset.setBounds (third, 160, column, 32);
    nameLabel.setBounds (20, 208, 120, 32);
    outputName.setBounds (146, 208, width - 126, 32);
    summary.setBounds (20, 254, width, 120);
    safety.setBounds (20, 386, width, 136);
    safetyAcknowledged.setBounds (20, 534, width, 34);
    status.setBounds (20, 580, width, getHeight () - 644);
    closeButton.setBounds (getWidth () - 110, getHeight () - 48, 90, 32);
    exportButton.setBounds (getWidth () - 342, getHeight () - 48, 220, 32);
}

void HardwareTestOutputComponent::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Theme::background);
}
