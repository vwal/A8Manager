#pragma once

#include <JuceHeader.h>
#include "../Assimil8or/Audio/HardwareTestOutput.h"
#include "ModernTheme.h"
#include <atomic>
#include <thread>

// Export-only, detached hardware-validation package. This component never
// opens an audio device, auditions a signal, or edits the working preset.
class HardwareTestOutputComponent : public juce::Component, private juce::Timer
{
public:
    HardwareTestOutputComponent (const WaveformDesign::Settings& design, juce::File initialFolder,
                                 const juce::String& name, int presetNumber);
    ~HardwareTestOutputComponent () override;
    static void show (const WaveformDesign::Settings& design, juce::File initialFolder,
                      const juce::String& name, int presetNumber);

private:
    friend struct HardwareTestOutputUiTestAccess;
    enum class Activity { idle, choosingFolder, exporting };
    struct ExportJob
    {
        std::atomic<bool> done { false };
        juce::Result result { juce::Result::ok () };
        HardwareTestOutput::ExportResult files;
    };
    struct TestLookAndFeel : ModernLookAndFeel
    {
        juce::Label* createSliderTextBox (juce::Slider& slider) override
        {
            auto* label { ModernLookAndFeel::createSliderTextBox (slider) };
            label->setFont (Theme::numericFont (16.0f));
            return label;
        }
    };

    const WaveformDesign::Settings designSnapshot;
    juce::File initialFolder;
    TestLookAndFeel look;
    juce::Label title, introduction, sourceLabel, durationLabel, levelLabel, presetLabel, nameLabel;
    juce::ComboBox source;
    juce::Slider duration, level, preset;
    juce::TextEditor outputName, summary, safety, status;
    juce::ToggleButton safetyAcknowledged { "I have checked the output routing and made the connections safe." };
    juce::TextButton exportButton { "Export test package..." }, closeButton { "Close" };
    Activity activity { Activity::idle };
    unsigned int chooserGeneration { 0 };
    bool statusIsError { false };
    std::unique_ptr<juce::FileChooser> chooser;
    std::thread worker;
    std::shared_ptr<ExportJob> job;

    // Test seams exercise the real snapshot/worker lifecycle without showing a
    // native chooser or writing packages. Production defaults are installed below.
    std::function<void (const juce::File&, std::function<void (juce::File)>)> chooseFolder;
    std::function<juce::Result (const HardwareTestOutput::Settings&, const juce::File&,
                               const juce::String&, HardwareTestOutput::ExportResult&)> writePackage;

    HardwareTestOutput::Settings settingsFromControls () const;
    juce::Result validateControls () const;
    bool usesCurrentDesign () const;
    void refreshControls (bool resetStatus = false);
    void updateSummary ();
    void beginExport ();
    void launchExport (HardwareTestOutput::Settings settings, juce::File folder, juce::String name);
    void setStatus (const juce::String& text, bool error = false);
    void timerCallback () override;
    void lookAndFeelChanged () override;
    void resized () override;
    void paint (juce::Graphics& graphics) override;
};
