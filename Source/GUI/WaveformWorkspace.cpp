#include "WaveformWorkspace.h"
#include "ModernTheme.h"
#include "HardwareTestOutputComponent.h"
#include "../Assimil8or/Audio/WaveformDesignExport.h"
#include "../Assimil8or/Audio/WaveformDesignRecall.h"
#include "../Assimil8or/Preset/PresetProperties.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace
{
    using namespace WaveformDesign;

    juce::String durationText (double seconds)
    {
        const auto minutes { static_cast<int> (seconds / 60.0) };
        const auto precision { seconds < 1.0 ? 6 : 3 };
        return juce::String (minutes) + ":" + juce::String (seconds - minutes * 60.0, precision).paddedLeft ('0', precision + 3);
    }

    void styleLabel (juce::Label& label, const juce::String& text, float size = 13.0f, bool bold = false)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
        Theme::bindColour (label, juce::Label::textColourId, [] { return Theme::text; });
        label.setBorderSize ({ 0, 0, 0, 0 });
    }

    struct Control : juce::Component
    {
        juce::Label label;
        juce::Slider slider;
        Control (const juce::String& title, const juce::String& name, double low, double high, double step, const juce::String& suffix)
        {
            setName (name);
            styleLabel (label, title, 12.0f);
            slider.setName (name + "-value");
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 25);
            slider.setRange (low, high, step);
            slider.setTextValueSuffix (suffix);
            slider.setScrollWheelEnabled (false);
            addAndMakeVisible (label);
            addAndMakeVisible (slider);
        }
        void resized () override
        {
            auto bounds { getLocalBounds ().reduced (0, 3) };
            label.setBounds (bounds.removeFromLeft (getWidth () > 300 ? 108 : 88));
            slider.setBounds (bounds);
        }
    };

    struct Field : juce::Component
    {
        juce::Label label;
        juce::ComboBox box;
        Field (const juce::String& title, const juce::String& name)
        {
            styleLabel (label, title, 12.0f);
            box.setName (name);
            addAndMakeVisible (label);
            addAndMakeVisible (box);
        }
        void resized () override
        {
            auto bounds { getLocalBounds ().reduced (0, 4) };
            label.setBounds (bounds.removeFromLeft (108));
            box.setBounds (bounds);
        }
    };

    struct Card : juce::Component
    {
        juce::Label title, hint;
        std::vector<std::pair<juce::Component*, int>> rows;
        Card (const juce::String& name, const juce::String& help)
        {
            styleLabel (title, name, 15.0f, true);
            Theme::bindColour (title, juce::Label::textColourId, [] { return Theme::accent; });
            styleLabel (hint, help, 11.5f);
            Theme::bindColour (hint, juce::Label::textColourId, [] { return Theme::muted; });
            hint.setMinimumHorizontalScale (1.0f);
            addAndMakeVisible (title);
            addAndMakeVisible (hint);
        }
        void addRow (juce::Component& component, int height = 35)
        {
            rows.emplace_back (&component, height);
            addAndMakeVisible (component);
        }
        int preferredHeight () const
        {
            int height { 69 };
            for (const auto& row : rows) if (row.first->isVisible ()) height += row.second;
            return height;
        }
        void resized () override
        {
            auto bounds { getLocalBounds ().reduced (14, 10) };
            title.setBounds (bounds.removeFromTop (22));
            hint.setBounds (bounds.removeFromTop (29));
            bounds.removeFromTop (3);
            for (const auto& row : rows)
                if (row.first->isVisible ()) row.first->setBounds (bounds.removeFromTop (row.second));
        }
        void paint (juce::Graphics& g) override
        {
            g.setColour (Theme::panel);
            g.fillRoundedRectangle (getLocalBounds ().toFloat (), 7.0f);
            g.setColour (Theme::border);
            g.drawRoundedRectangle (getLocalBounds ().toFloat ().reduced (0.5f), 7.0f, 1.0f);
        }
    };

    struct PreviewData
    {
        struct Band { float minimum, maximum; };
        Settings settings;
        std::vector<std::vector<Band>> waves;
        double peak { 0 }, dc { 0 }, boundary { 0 }, rate { 48000 };
        juce::int64 frames { 0 }, clipped { 0 };
        juce::StringArray warnings;
    };

    struct Preview : juce::Component
    {
        std::shared_ptr<PreviewData> data;
        juce::String message { "Preparing visual preview..." };
        bool compact { false };
        void paint (juce::Graphics& g) override
        {
            const auto bounds { getLocalBounds ().toFloat () };
            g.setColour (Theme::field);
            g.fillRoundedRectangle (bounds, 6.0f);
            auto plot { bounds.reduced (45, 16).withTrimmedBottom (15) };
            g.setFont (juce::FontOptions (10.5f));
            for (int line { 0 }; line <= 4; ++line)
            {
                const float y { plot.getY () + plot.getHeight () * line / 4.0f };
                g.setColour (Theme::border.withAlpha (line == 2 ? 1.0f : 0.55f));
                g.drawHorizontalLine (juce::roundToInt (y), plot.getX (), plot.getRight ());
                g.setColour (Theme::muted);
                g.drawText (juce::String (100 - line * 50) + "%", 2, juce::roundToInt (y) - 7, 37, 14, juce::Justification::centredRight);
            }
            for (int line { 0 }; line <= 8; ++line)
            {
                g.setColour (Theme::border.withAlpha (0.35f));
                const auto x { plot.getX () + plot.getWidth () * line / 8.0f };
                g.drawVerticalLine (juce::roundToInt (x), plot.getY (), plot.getBottom ());
            }
            if (data)
                for (size_t voice { 0 }; voice < data->waves.size (); ++voice)
                {
                    const auto& wave { data->waves[voice] };
                    if (wave.empty ()) continue;
                    juce::Path outline;
                    for (size_t point { 0 }; point < wave.size (); ++point)
                    {
                        const auto x { plot.getX () + plot.getWidth () * static_cast<float> (point) / juce::jmax (1.0f, static_cast<float> (wave.size () - 1)) };
                        const auto y { plot.getCentreY () - wave[point].maximum * plot.getHeight () * 0.5f };
                        if (point == 0) outline.startNewSubPath (x, y); else outline.lineTo (x, y);
                        g.setColour (Theme::accent.withRotatedHue (static_cast<float> (voice) * 0.085f).withAlpha (0.45f));
                        g.drawVerticalLine (juce::roundToInt (x), y, plot.getCentreY () - wave[point].minimum * plot.getHeight () * 0.5f + 0.5f);
                    }
                    g.setColour (Theme::accent.withRotatedHue (static_cast<float> (voice) * 0.085f).withAlpha (0.85f));
                    g.strokePath (outline, juce::PathStrokeType (1.5f));
                }
            g.setColour (Theme::muted);
            g.setFont (juce::FontOptions (11.5f));
            g.drawText (compact ? "Source waveform - expand for a closer view" : message,
                        bounds.withTrimmedTop (bounds.getHeight () - 24).reduced (12, 0), juce::Justification::centredLeft);
        }
    };

    struct ExpandedPreview : juce::DocumentWindow
    {
        Preview waveform;
        explicit ExpandedPreview (bool nativePeer)
            : juce::DocumentWindow ("Source waveform - live design view", Theme::background, juce::DocumentWindow::closeButton, nativePeer)
        {
            setComponentID ("design-expanded-window");
            waveform.setName ("design-expanded-preview");
            setUsingNativeTitleBar (nativePeer);
            setResizable (true, true);
            setResizeLimits (560, 280, 2400, 1500);
            setContentNonOwned (&waveform, false);
            setSize (1040, 460);
        }
        void closeButtonPressed () override { setVisible (false); }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key.getKeyCode () != juce::KeyPress::escapeKey) return false;
            closeButtonPressed ();
            return true;
        }
    };

    struct CurveEditor : juce::Component
    {
        Settings* settings { nullptr };
        std::function<void ()> onChange;
        int previousIndex { -1 };
        double previousValue { 0 };
        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::field);
            if (settings == nullptr) return;
            const auto bounds { getLocalBounds ().toFloat ().reduced (8, 10) };
            g.setColour (Theme::border);
            g.drawHorizontalLine (juce::roundToInt (bounds.getCentreY ()), bounds.getX (), bounds.getRight ());
            const auto stepped { settings->shape == Shape::steps };
            const int count { stepped ? settings->stepCount : 33 };
            juce::Path path;
            for (int i { 0 }; i < count; ++i)
            {
                const auto value { stepped ? settings->steps[static_cast<size_t> (i)] : settings->drawn[static_cast<size_t> (i)] };
                const auto x { bounds.getX () + bounds.getWidth () * i / (stepped ? count : count - 1) };
                const auto y { bounds.getCentreY () - static_cast<float> (value) * bounds.getHeight () * 0.5f };
                if (stepped)
                {
                    g.setColour (Theme::accent.withAlpha (0.3f));
                    g.fillRect (juce::Rectangle<float> (x + 1, juce::jmin (y, bounds.getCentreY ()), bounds.getWidth () / count - 2, std::abs (y - bounds.getCentreY ()) + 1));
                    g.setColour (Theme::accent);
                    g.drawLine (x, y, x + bounds.getWidth () / count, y, 2.0f);
                }
                else
                {
                    if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
                    g.setColour (Theme::accent);
                    g.fillEllipse (x - 2, y - 2, 4, 4);
                }
            }
            g.setColour (Theme::accent);
            g.strokePath (path, juce::PathStrokeType (2.0f));
        }
        void edit (juce::Point<float> point)
        {
            if (settings == nullptr || ! isEnabled ()) return;
            const auto bounds { getLocalBounds ().toFloat ().reduced (8, 10) };
            const auto stepped { settings->shape == Shape::steps };
            const int count { stepped ? settings->stepCount : 33 };
            const auto index { juce::jlimit (0, count - 1, juce::roundToInt ((point.x - bounds.getX ()) / bounds.getWidth () * (stepped ? count : count - 1) - (stepped ? 0.5f : 0.0f))) };
            const auto value { juce::jlimit (-1.0, 1.0, static_cast<double> ((bounds.getCentreY () - point.y) / (bounds.getHeight () * 0.5f))) };
            auto assign = [&] (int position, double level)
            {
                if (stepped) settings->steps[static_cast<size_t> (position)] = level;
                else settings->drawn[static_cast<size_t> (position)] = level;
            };
            if (previousIndex >= 0 && previousIndex != index)
            {
                const int direction { index > previousIndex ? 1 : -1 };
                for (int position { previousIndex }; position != index; position += direction)
                {
                    const auto fraction { static_cast<double> (position - previousIndex) / (index - previousIndex) };
                    assign (position, previousValue + (value - previousValue) * fraction);
                }
            }
            assign (index, value);
            previousIndex = index; previousValue = value;
            repaint ();
            if (onChange) onChange ();
        }
        void mouseDown (const juce::MouseEvent& event) override { previousIndex = -1; edit (event.position); }
        void mouseDrag (const juce::MouseEvent& event) override { edit (event.position); }
        void mouseUp (const juce::MouseEvent&) override { previousIndex = -1; }
    };

    struct AsyncState
    {
        struct RenderRequest { Settings settings; unsigned generation, epoch; };
        struct ExportRequest { Settings settings; juce::File folder; juce::String name; int presetNumber; };
        struct AssignmentRequest { Settings settings; WaveformWorkspace::AssignmentContext context; juce::String name; int channel, zone; };
        std::mutex mutex;
        std::condition_variable ready;
        bool stopping { false }, rendering { false }, exporting { false };
        std::optional<RenderRequest> renderRequest;
        std::optional<ExportRequest> exportRequest;
        std::optional<AssignmentRequest> assignmentRequest, completedAssignmentRequest;
        AssignmentResult assignmentOutput;
        bool assignmentFinished { false }, assignmentFailed { false };
        juce::String assignmentMessage;
        unsigned completedGeneration { 0 }, completedEpoch { 0 };
        std::shared_ptr<PreviewData> preview;
        WaveformAudition::PayloadPtr audition;
        juce::String auditionError;
        juce::String renderError, exportMessage;
        juce::File exportedFolder;
        bool exportFinished { false }, exportFailed { false };
    };

    void runWorker (std::shared_ptr<AsyncState> state)
    {
        for (;;)
        {
            std::optional<AsyncState::RenderRequest> renderRequest;
            std::optional<AsyncState::ExportRequest> exportRequest;
            std::optional<AsyncState::AssignmentRequest> assignmentRequest;
            AssignmentResult assignmentOutput;
            {
                std::unique_lock<std::mutex> lock (state->mutex);
                state->ready.wait (lock, [&] { return state->stopping || state->exportRequest || state->assignmentRequest || state->renderRequest; });
                if (state->stopping) return;
                if (state->assignmentRequest)
                {
                    assignmentRequest = std::move (state->assignmentRequest);
                    state->assignmentRequest.reset ();
                }
                else if (state->exportRequest)
                {
                    exportRequest = std::move (state->exportRequest);
                    state->exportRequest.reset ();
                    state->exporting = true;
                }
                else
                {
                    renderRequest = std::move (state->renderRequest);
                    state->renderRequest.reset ();
                    state->rendering = true;
                }
            }
            try
            {
            if (assignmentRequest)
            {
                const auto result { prepareAssignment (assignmentRequest->settings, assignmentRequest->context.folder,
                    assignmentRequest->name, assignmentRequest->context.preset, assignmentRequest->channel, assignmentRequest->zone, assignmentOutput) };
                std::lock_guard<std::mutex> lock (state->mutex);
                state->assignmentFinished = true;
                state->assignmentFailed = result.failed ();
                state->assignmentMessage = result.getErrorMessage ();
                state->completedAssignmentRequest = std::move (assignmentRequest);
                state->assignmentOutput = std::move (assignmentOutput);
            }
            else if (exportRequest)
            {
                ExportResult output;
                const auto result { exportDesign (exportRequest->settings, exportRequest->folder, exportRequest->name, output, exportRequest->presetNumber) };
                std::lock_guard<std::mutex> lock (state->mutex);
                state->exporting = false;
                state->exportFinished = true;
                state->exportFailed = result.failed ();
                state->exportedFolder = result.wasOk () ? output.folder : juce::File {};
                state->exportMessage = result.wasOk () ? "Separate package created; current preset unchanged. " + juce::String (output.waves.size ()) + " WAV file(s), preset and recipe in " + output.folder.getFullPathName () : result.getErrorMessage ();
            }
            else if (renderRequest)
            {
                Render renderData;
                const auto result { render (renderRequest->settings, renderData) };
                WaveformAudition::PayloadPtr audition;
                juce::String auditionError;
                auto preview { std::make_shared<PreviewData> () };
                if (result.wasOk ())
                {
                    preview->settings = renderRequest->settings;
                    preview->frames = renderData.frames;
                    preview->rate = renderData.sampleRate;
                    preview->peak = renderData.peak;
                    preview->dc = renderData.dc;
                    preview->boundary = renderData.boundaryJump;
                    preview->clipped = renderData.clippedSamples;
                    preview->warnings = renderData.warnings;
                    for (const auto& voice : renderData.voices)
                    {
                        std::vector<PreviewData::Band> bands;
                        const auto points { juce::jmin (1024, voice.getNumSamples ()) };
                        for (int point { 0 }; point < points; ++point)
                        {
                            const auto start { static_cast<int> (static_cast<juce::int64> (point) * voice.getNumSamples () / points) };
                            const auto end { static_cast<int> (static_cast<juce::int64> (point + 1) * voice.getNumSamples () / points) };
                            const auto range { voice.findMinMax (0, start, juce::jmax (1, end - start)) };
                            bands.push_back ({ range.getStart (), range.getEnd () });
                        }
                        preview->waves.push_back (std::move (bands));
                    }
                    if (renderRequest->settings.mode != Mode::modulation)
                    {
                        const auto prepared { WaveformAudition::preparePayload (renderRequest->settings, renderData, audition) };
                        if (prepared.failed ()) auditionError = prepared.getErrorMessage ();
                    }
                }
                std::lock_guard<std::mutex> lock (state->mutex);
                state->rendering = false;
                state->completedGeneration = renderRequest->generation;
                state->completedEpoch = renderRequest->epoch;
                state->preview = result.wasOk () ? std::move (preview) : nullptr;
                state->renderError = result.getErrorMessage ();
                state->audition = std::move (audition);
                state->auditionError = auditionError;
            }
            }
            catch (const std::exception& exception)
            {
                std::lock_guard<std::mutex> lock (state->mutex);
                const auto message { "Waveform processing failed: " + juce::String (exception.what ()) };
                if (assignmentRequest)
                {
                    state->assignmentFinished = true; state->assignmentFailed = true; state->assignmentMessage = message;
                    state->completedAssignmentRequest = std::move (assignmentRequest);
                    state->assignmentOutput = std::move (assignmentOutput);
                }
                else if (exportRequest)
                {
                    state->exporting = false; state->exportFinished = true; state->exportFailed = true; state->exportMessage = message;
                }
                else if (renderRequest)
                {
                    state->rendering = false; state->completedGeneration = renderRequest->generation;
                    state->completedEpoch = renderRequest->epoch;
                    state->preview.reset (); state->audition.reset (); state->renderError = message;
                }
            }
            catch (...)
            {
                std::lock_guard<std::mutex> lock (state->mutex);
                if (assignmentRequest)
                {
                    state->assignmentFinished = true; state->assignmentFailed = true; state->assignmentMessage = "Unexpected assignment failure.";
                    state->completedAssignmentRequest = std::move (assignmentRequest);
                    state->assignmentOutput = std::move (assignmentOutput);
                }
                else if (exportRequest)
                {
                    state->exporting = false; state->exportFinished = true; state->exportFailed = true; state->exportMessage = "Unexpected export failure.";
                }
                else if (renderRequest)
                {
                    state->rendering = false; state->completedGeneration = renderRequest->generation;
                    state->completedEpoch = renderRequest->epoch;
                    state->preview.reset (); state->audition.reset (); state->renderError = "Unexpected render failure.";
                }
            }
        }
    }
}

struct WaveformWorkspace::Impl
{
    WaveformWorkspace& owner;
    Settings settings { startingPoint (Mode::oscillator, Shape::sine) };
    std::array<std::optional<Settings>, 3> modeDesigns;
    ModernLookAndFeel look;
    juce::Label title, subtitle, summary, status, nameLabel, renderStats, auditionTitle, auditionHint, assignmentHeading, packageHeading;
    juce::TextButton close { "Back to preset" }, load { "Load recipe / WAV..." }, create { "Export new package..." }, openExport { "Open in Sample workspace" }, assign { "Generate & Assign..." }, recall { "Recall assigned..." };
    juce::TextButton testOutput { "Test output..." };
    juce::TextEditor fileName;
    Field mode { "Workspace", "design-mode" }, shape { "Shape", "design-shape" }, preset { "Starting point", "design-preset" };
    Field targetChannel { "Target channel", "design-target-channel" }, targetZone { "Target zone", "design-target-zone" }, exportSlot { "Package preset", "design-export-slot" };
    std::optional<WaveformWorkspace::AssignmentContext> assignmentContext;
    bool assignmentBusy { false }, assignmentConfirming { false };
    bool recallConfirming { false };
    unsigned recallConfirmation { 0 };
    unsigned assignmentConfirmation { 0 };
    int targetVoiceCount { 0 };
    Preview preview;
    juce::TextButton expand { "Expand waveform..." };
    std::unique_ptr<ExpandedPreview> expandedPreview;
    juce::TextButton auditionButton { "Start audition" };
    Control monitorLevel { "Monitor level", "design-monitor-level", -60, 0, 0.1, " dB" };
    Control monitorTranspose { "Transpose", "design-monitor-transpose", -48, 48, 0.01, " st" };
    WaveformAudition::PayloadPtr auditionPayload;
    juce::String auditionError;
    bool auditionSuspended { false };
    juce::Viewport viewport;
    juce::Component content;
    Card tone { "SHAPE & CHARACTER", "Phase is in degrees; tone controls are non-destructive." };
    Card output { "LEVEL & POLARITY", "Percent of digital full scale, not assumed output volts." };
    Card timing { "LENGTH & PLAYBACK", "Exported settings are separate from audition speed." };
    Card envelope { "ENVELOPE", "A + D + R share one cycle; the remainder is sustain." };
    Card drawing { "EDIT THE SHAPE", "Click or drag to set points. Preview above shows the result." };
    Card layers { "LAYER BANK", "Phase/gain shape each WAV; detune/pan are saved as independent channel settings." };
    Field rate { "Sample rate", "design-rate" }, frames { "Cycle frames", "design-frames" }, playback { "Playback", "design-playback" }, match { "Match selection", "design-match" };
    juce::TextButton matchButton { "Use selected length" }, tempoButton { "Use BPM / beats" }, spreadButton { "Apply voice spread" }, supersaw { "Supersaw (7 voices)" }, flat { "Flat" }, ramp { "Ramp" }, sine { "Sine" };
    juce::Component drawingActions;
    juce::ToggleButton unipolar { "Unipolar (positive-going)" }, invert { "Invert waveform" };
    CurveEditor curve;
    struct Binding { std::unique_ptr<Control> control; std::function<double ()> read; };
    std::vector<Binding> controls;
    std::array<juce::Component, 8> voiceRows;
    std::array<juce::Label, 8> voiceLabels;
    std::array<std::array<Control*, 4>, 8> voiceControls {};
    Control *duration {}, *cycles {}, *bpm {}, *beats {}, *attack {}, *decay {}, *release {}, *stepCount {}, *voiceCount {};
    Control *symmetry {}, *width {}, *harmonics {}, *brightness {}, *drive {}, *fold {}, *glide {}, *seed {};
    double tempo { 120 }, beatCount { 4 }, detuneSpread { 24 }, phaseSpread { 300 }, panSpread { 0.8 };
    std::shared_ptr<AsyncState> worker { std::make_shared<AsyncState> () };
    std::thread workerThread;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File initialFolder, exportedFolder;
    unsigned generation { 1 }, displayedGeneration { 0 }, auditionEpoch { 1 };
    double renderAt { 0 }, lastRenderSubmittedAt { 0 };
    bool pendingRender { true }, exportBusy { false }, applying { false };

    Impl (WaveformWorkspace& workspaceComponent) : owner (workspaceComponent)
    {
        owner.setLookAndFeel (&look);
        owner.setName ("waveform-workspace");
        styleLabel (title, "WAVEFORM WORKSPACE", 22.0f, true);
        Theme::bindColour (title, juce::Label::textColourId, [] { return Theme::accent; });
        styleLabel (subtitle, "Design audio cycles, CV curves and layers; assign to this preset or export a separate package.", 12.0f);
        Theme::bindColour (subtitle, juce::Label::textColourId, [] { return Theme::muted; });
        styleLabel (summary, "", 12.0f);
        summary.setName ("design-summary");
        styleLabel (renderStats, "Preparing waveform statistics...", 12.0f);
        renderStats.setName ("design-render-stats");
        Theme::bindColour (renderStats, juce::Label::textColourId, [] { return Theme::muted; });
        renderStats.setJustificationType (juce::Justification::topLeft);
        styleLabel (auditionTitle, "LIVE AUDITION", 14.0f, true);
        Theme::bindColour (auditionTitle, juce::Label::textColourId, [] { return Theme::accent; });
        styleLabel (auditionHint, "Monitor only: loops continuously. Level and transpose do not affect export.", 12.0f);
        auditionHint.setName ("design-audition-hint");
        auditionHint.setJustificationType (juce::Justification::topLeft);
        monitorLevel.slider.setValue (-18, juce::dontSendNotification);
        monitorTranspose.slider.setValue (0, juce::dontSendNotification);
        monitorLevel.slider.setTooltip ("Speaker/headphone monitor gain only: -60 to 0 dB, initially -18 dB. Keep your hardware output level low until you have checked it. Does not affect export.");
        monitorTranspose.slider.setTooltip ("Monitor-only transposition, -48 to +48 semitones. Listen at a useful note without changing the generated cycle, recipe, or exported preset.");
        auditionButton.setName ("design-audition");
        auditionButton.setEnabled (false);
        styleLabel (status, "Audition starts only when requested. CV mode is always visual-only.", 12.0f);
        status.setName ("design-status");
        Theme::bindColour (status, juce::Label::textColourId, [] { return Theme::muted; });
        styleLabel (nameLabel, "Design name", 12.0f);
        styleLabel (assignmentHeading, "CURRENT PRESET - assign to the channel / zone below, then Save", 12.0f, true);
        assignmentHeading.setName ("design-assignment-heading");
        Theme::bindColour (assignmentHeading, juce::Label::textColourId, [] { return Theme::accent; });
        styleLabel (packageHeading, "SEPARATE PACKAGE - new folder, voices start at CH 1; current preset unchanged", 12.0f, true);
        packageHeading.setName ("design-package-heading");
        fileName.setName ("design-name");
        fileName.setText ("New Waveform", false);
        fileName.setTooltip ("A name for generated WAVs and the repeatable recipe. Assignment creates unique files; package export creates a new folder.");
        for (auto channel { 1 }; channel <= 8; ++channel) targetChannel.box.addItem ("CH " + juce::String (channel), channel);
        for (auto zone { 1 }; zone <= 8; ++zone) targetZone.box.addItem (juce::String (zone), zone);
        for (auto slot { 1 }; slot <= 199; ++slot) exportSlot.box.addItem (juce::String (slot).paddedLeft ('0', 3), slot);
        targetChannel.box.setSelectedId (1, juce::dontSendNotification);
        targetZone.box.setSelectedId (1, juce::dontSendNotification);
        exportSlot.box.setSelectedId (1, juce::dontSendNotification);
        targetChannel.box.setTooltip ("Single designs use one channel. Banks use consecutive channels from this starting channel. Stereo pairs cannot be replaced here.");
        targetZone.box.setTooltip ("Replace this zone or append the next empty zone. Earlier zones must be filled. Channel-wide playback settings also affect other zones.");
        exportSlot.box.setTooltip ("Preset number 1-199 for a separate exported package. This does not change the currently selected preset.");
        mode.box.addItemList ({ "Audio Cycle", "CV Modulation", "Layer Bank" }, 1);
        mode.box.setTooltip ("Each mode remembers its current design for this session. Export a design to save its editable recipe permanently.");
        preset.box.setTooltip ("Replace the current design with a fresh shape preset. Export first to keep the current design as a recipe.");
        rate.box.addItemList ({ "48,000 Hz", "96,000 Hz" }, 1);
        for (int size { 64 }; size <= 8192; size *= 2) frames.box.addItem (juce::String (size), size);
        playback.box.addItemList ({ "One shot", "Loop", "Gated loop" }, 1);
        match.box.addItemList ({ "Whole source file", "Selected sample region", "Selected loop region" }, 1);
        match.box.setSelectedId (2, juce::dontSendNotification);
        unipolar.setName ("design-unipolar"); invert.setName ("design-invert");
        curve.settings = &settings;
        curve.setName ("design-drawing");
        preview.compact = true;
        preview.setName ("design-compact-preview");
        expand.setName ("design-expand-preview");
        expand.setTooltip ("Open a larger, resizable source-waveform view. It follows design edits and mode changes. Escape or X closes only the view, not audition.");
        for (auto* component : std::initializer_list<juce::Component*> { &title, &subtitle, &summary, &status, &nameLabel, &assignmentHeading, &packageHeading, &close, &load, &create, &assign, &recall, &targetChannel, &targetZone, &exportSlot, &fileName, &mode, &shape, &preset, &preview, &expand, &renderStats, &auditionTitle, &auditionHint, &auditionButton, &monitorLevel, &monitorTranspose, &viewport })
            owner.addAndMakeVisible (component);
        viewport.setViewedComponent (&content, false);
        viewport.setName ("design-controls");
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::never);
        for (auto* card : { &tone, &output, &timing, &envelope, &drawing, &layers }) content.addAndMakeVisible (card);
        create.setName ("design-export");
        testOutput.setName ("design-test-output");
        testOutput.setTooltip ("Advanced hardware verification: export audio/CV test signals and a separate timing-reference channel. No computer playback; the current preset stays unchanged.");
        owner.addAndMakeVisible (testOutput);
        create.setTooltip ("Create a separate folder and preset with voices starting at CH 1. Target channel/zone above are NOT used. To fill them in the current preset, use Generate & Assign, then Save.");
        assign.setName ("design-assign");
        Theme::bindColour (assign, juce::TextButton::buttonColourId, [] { return Theme::accent.darker (0.6f); });
        assign.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        assign.setEnabled (false);
        assign.setTooltip ("Create WAVs and a recipe in the current folder, then update the selected preset in memory. Click Save in the shared preset header to save it.");
        recall.setName ("design-recall");
        recall.setTooltip ("Reopen the saved design behind the WAV in Target channel / Target zone. A bank restores all its voices. Requires the original recipe beside the WAV; does not change the preset.");
        load.setTooltip ("Open a saved JSON recipe, or a generated WAV with its recipe still beside it. Ordinary audio cannot be reverse-engineered into design controls.");
        openExport.setName ("design-open-export");
        openExport.setTooltip ("Open the last successfully exported folder in the sample workspace. The usual unsaved-preset check runs before changing folders. No automatic playback.");
        owner.addChildComponent (openExport);

        add (tone, "Phase", "design-phase", -360, 360, 1, " deg", [this] { return settings.phaseDegrees; }, [this] (double v) { settings.phaseDegrees = v; });
        symmetry = percent (tone, "Symmetry", "design-symmetry", settings.symmetry, 1, 99);
        width = percent (tone, "Pulse width", "design-width", settings.pulseWidth, 1, 99);
        harmonics = add (tone, "Harmonics", "design-harmonics", 1, 1024, 1, "", [this] { return settings.harmonics; }, [this] (double v) { settings.harmonics = juce::roundToInt (v); });
        brightness = percent (tone, "Brightness", "design-brightness", settings.brightness);
        drive = percent (tone, "Drive", "design-drive", settings.drive);
        fold = percent (tone, "Fold", "design-fold", settings.fold);
        percent (output, "Amplitude", "design-amplitude", settings.amplitude);
        percent (output, "DC offset", "design-offset", settings.offset, -100, 100);
        output.addRow (unipolar); output.addRow (invert);
        auto* calibration { add (output, "Measured FS", "design-calibration", 0, 100, 0.001, " V", [this] { return settings.measuredFullScaleVolts; }, [this] (double v) { settings.measuredFullScaleVolts = v; }) };
        calibration->slider.setTextValueSuffix ({});
        calibration->slider.textFromValueFunction = [] (double v) { return v == 0 ? juce::String ("Unknown") : juce::String (v, 3) + " V"; };
        calibration->slider.updateText ();
        calibration->slider.setTooltip ("Optional: your measured positive full-scale module output voltage. Zero means unknown. Percent values remain valid without calibration; no exact hardware voltage is assumed.");
        timing.addRow (rate); timing.addRow (frames);
        duration = add (timing, "Duration", "design-duration", 0.001, 60, 0.001, " s", [this] { return settings.durationSeconds; }, [this] (double v) { settings.durationSeconds = v; });
        duration->slider.setSkewFactorFromMidPoint (5);
        cycles = add (timing, "Cycles", "design-cycles", 0.01, 1024, 0.01, "", [this] { return settings.cycles; }, [this] (double v) { settings.cycles = v; });
        cycles->slider.setSkewFactorFromMidPoint (8);
        cycles->slider.setTooltip ("Number of complete contours in the CV file. For Steps, one contour includes every step; for Envelope, it includes the entire envelope.");
        duration->slider.setTooltip ("Total file duration, not the duration of one step. Tempo/Beats and Match selection replace this only when their buttons are pressed.");
        symmetry->slider.setTooltip ("Position of the half-cycle boundary. 50% is symmetric; changing it stretches the two halves while preserving the full cycle length.");
        width->slider.setTooltip ("Pulse duty cycle, or the flat-top width of a trapezoid. Values near the extremes create narrow features.");
        harmonics->slider.setTooltip ("Maximum harmonic retained in audio modes, also limited by this cycle's Nyquist limit. Larger values can produce a brighter waveform.");
        brightness->slider.setTooltip ("Spectral brightness in audio modes. Reducing this lowers the higher harmonics.");
        drive->slider.setTooltip ("Nonlinear saturation before harmonic filtering. It can add harmonics; the waveform is normalized before final amplitude/offset.");
        fold->slider.setTooltip ("Wavefolding before harmonic filtering. It reshapes the audio cycle, adding richer harmonics.");
        bpm = add (timing, "Tempo", "design-bpm", 1, 400, 0.1, " BPM", [this] { return tempo; }, [this] (double v) { tempo = v; });
        beats = add (timing, "Beats", "design-beats", 0.01, 64, 0.01, "", [this] { return beatCount; }, [this] (double v) { beatCount = v; });
        timing.addRow (tempoButton); timing.addRow (match); timing.addRow (matchButton); timing.addRow (playback);
        attack = percent (envelope, "Attack time", "design-attack", settings.attack);
        decay = percent (envelope, "Decay time", "design-decay", settings.decay);
        percent (envelope, "Sustain level", "design-sustain", settings.sustain);
        release = percent (envelope, "Release time", "design-release", settings.release);
        percent (envelope, "Curve", "design-curve", settings.curve, -100, 100);
        glide = percent (drawing, "Step glide", "design-smoothing", settings.smoothing);
        glide->slider.setTooltip ("Smooth step/random transitions. The amount is a fraction of one step's duration, not a filter on all waveform shapes.");
        stepCount = add (drawing, "Step count", "design-steps", 1, 16, 1, "", [this] { return settings.stepCount; }, [this] (double v) { settings.stepCount = juce::roundToInt (v); });
        seed = add (drawing, "Random seed", "design-seed", 0, 4294967295.0, 1, "", [this] { return settings.seed; }, [this] (double v) { settings.seed = static_cast<unsigned int> (v); });
        drawing.addRow (curve, 150);
        for (auto* button : { &flat, &ramp, &sine }) drawingActions.addAndMakeVisible (button);
        drawing.addRow (drawingActions);
        voiceCount = add (layers, "Voice count", "design-voices", 1, 8, 1, "", [this] { return settings.voiceCount; }, [this] (double v) { settings.voiceCount = juce::roundToInt (v); });
        add (layers, "Detune spread", "design-detune-spread", 0, 2400, 0.1, " ct", [this] { return detuneSpread; }, [this] (double v) { detuneSpread = v; });
        add (layers, "Phase spread", "design-phase-spread", 0, 360, 1, " deg", [this] { return phaseSpread; }, [this] (double v) { phaseSpread = v; });
        add (layers, "Pan spread", "design-pan-spread", 0, 1, 0.01, "", [this] { return panSpread; }, [this] (double v) { panSpread = v; });
        layers.addRow (spreadButton); layers.addRow (supersaw);
        for (size_t i { 0 }; i < 8; ++i)
        {
            styleLabel (voiceLabels[i], "CH " + juce::String (static_cast<int> (i + 1)), 12.0f, true);
            Theme::bindColour (voiceLabels[i], juce::Label::textColourId, [i] { return Theme::accent.withRotatedHue (static_cast<float> (i) * 0.085f); });
            voiceRows[i].addAndMakeVisible (voiceLabels[i]);
            layers.addRow (voiceRows[i], 40);
            auto voiceControl = [&] (int column, const juce::String& label, double low, double high, double step, const juce::String& suffix, double Voice::* member)
            {
                auto control { std::make_unique<Control> (label, "design-voice-" + juce::String (static_cast<int> (i + 1)) + "-" + juce::String (column), low, high, step, suffix) };
                auto* pointer { control.get () };
                voiceControls[i][static_cast<size_t> (column)] = pointer;
                voiceRows[i].addAndMakeVisible (pointer);
                pointer->slider.onValueChange = [this, i, member, pointer] { if (! applying) { settings.voices[i].*member = pointer->slider.getValue (); changed (); } };
                controls.push_back ({ std::move (control), [this, i, member] { return settings.voices[i].*member; } });
            };
            voiceControl (0, "Detune", -2400, 2400, 0.1, " ct", &Voice::detuneCents);
            voiceControl (1, "Phase", -360, 360, 1, " deg", &Voice::phaseDegrees);
            voiceControl (2, "Pan", -1, 1, 0.01, "", &Voice::pan);
            voiceControl (3, "Gain", 0, 1, 0.01, "", &Voice::level);
        }
        connect ();
        sync ();
        workerThread = std::thread (runWorker, worker);
    }

    ~Impl ()
    {
        clearAudition ();
        expandedPreview.reset ();
        {
            std::lock_guard<std::mutex> lock (worker->mutex);
            worker->stopping = true;
            worker->renderRequest.reset ();
            worker->exportRequest.reset ();
            worker->assignmentRequest.reset ();
        }
        worker->ready.notify_one ();
        // Finish an already-started export before JUCE/static services are torn
        // down at app exit. Ordinary workspace navigation keeps this view alive.
        if (workerThread.joinable ()) workerThread.join ();
        cleanupAssignmentFiles (worker->assignmentOutput);
        owner.setLookAndFeel (nullptr);
    }

    Control* add (Card& card, const juce::String& label, const juce::String& name, double low, double high, double step, const juce::String& suffix,
                  std::function<double ()> read, std::function<void (double)> write)
    {
        auto control { std::make_unique<Control> (label, name, low, high, step, suffix) };
        auto* pointer { control.get () };
        pointer->slider.onValueChange = [this, pointer, write] { if (! applying) { write (pointer->slider.getValue ()); changed (); } };
        card.addRow (*pointer);
        controls.push_back ({ std::move (control), std::move (read) });
        return pointer;
    }

    Control* percent (Card& card, const juce::String& label, const juce::String& name, double& value, double low = 0, double high = 100)
    {
        auto* pointer { &value };
        return add (card, label, name, low, high, 0.1, "%", [pointer] { return *pointer * 100; }, [pointer] (double v) { *pointer = v / 100; });
    }

    void connect ()
    {
        close.onClick = [this]
        {
            if (! exportBusy && ! assignmentBusy && ! assignmentConfirming)
            {
                // Navigation stops the host but keeps this already-prepared
                // design ready for an explicit Start when the view reopens.
                clearAudition (false);
                if (expandedPreview) expandedPreview->setVisible (false);
                if (owner.onClose) owner.onClose ();
            }
        };
        expand.onClick = [this]
        {
            if (! expandedPreview)
            {
                const bool nativePeer { owner.getPeer () != nullptr };
                expandedPreview = std::make_unique<ExpandedPreview> (nativePeer);
                expandedPreview->setLookAndFeel (&look);
                // Headless component tests have no display/monitor geometry.
                if (nativePeer) expandedPreview->centreAroundComponent (&owner, 1040, 460);
            }
            updateExpandedPreview ();
            expandedPreview->setVisible (true);
            expandedPreview->toFront (true);
        };
        auditionButton.onClick = [this]
        {
            if (settings.mode == Mode::modulation || auditionSuspended) return;
            if (owner.isAuditionActive && owner.isAuditionActive ())
            {
                if (owner.onStopAudition) owner.onStopAudition ();
                auditionError.clear ();
            }
            else if (auditionPayload && displayedGeneration == generation && owner.onStartAudition)
            {
                if (owner.onAuditionPayload) owner.onAuditionPayload (auditionPayload);
                if (owner.onAuditionMonitorChange)
                {
                    const auto monitor { owner.onAuditionMonitorChange (monitorLevel.slider.getValue (), monitorTranspose.slider.getValue ()) };
                    if (monitor.failed ())
                    {
                        auditionError = monitor.getErrorMessage ();
                        if (owner.onStopAudition) owner.onStopAudition ();
                        updateAuditionControls ();
                        return;
                    }
                }
                const auto started { owner.onStartAudition () };
                auditionError = started.getErrorMessage ();
            }
            updateAuditionControls ();
        };
        auto monitorChanged = [this]
        {
            auditionError.clear ();
            if (settings.mode != Mode::modulation && auditionPayload && owner.onAuditionMonitorChange)
            {
                const auto result { owner.onAuditionMonitorChange (monitorLevel.slider.getValue (), monitorTranspose.slider.getValue ()) };
                auditionError = result.getErrorMessage ();
                if (result.failed () && owner.onStopAudition) owner.onStopAudition ();
            }
            updateAuditionControls ();
        };
        monitorLevel.slider.onValueChange = monitorChanged;
        monitorTranspose.slider.onValueChange = monitorChanged;
        mode.box.onChange = [this]
        {
            if (applying) return;
            clearAudition ();
            const auto next { static_cast<Mode> (mode.box.getSelectedId () - 1) };
            modeDesigns[static_cast<size_t> (settings.mode)] = settings;
            const auto& saved { modeDesigns[static_cast<size_t> (next)] };
            if (saved.has_value ()) settings = *saved;
            else
            {
                settings = startingPoint (next, next == Mode::modulation ? Shape::triangle : next == Mode::layers ? Shape::saw : Shape::sine);
                if (next == Mode::layers) spreadVoices (settings, 7, 24, 300, 0.8);
            }
            sync (); changed ();
        };
        shape.box.onChange = [this] { if (! applying) { settings.shape = static_cast<Shape> (shape.box.getSelectedId () - 1); sync (); changed (); } };
        preset.box.onChange = [this]
        {
            if (applying || preset.box.getSelectedId () == 0) return;
            const auto selected { static_cast<Shape> (preset.box.getSelectedId () - 1) };
            settings = startingPoint (settings.mode, selected);
            if (settings.mode == Mode::layers) spreadVoices (settings, 7, 24, 300, 0.8);
            sync (); changed ();
        };
        rate.box.onChange = [this] { if (! applying) { settings.sampleRate = rate.box.getSelectedId () == 1 ? 48000 : 96000; changed (); } };
        frames.box.onChange = [this] { if (! applying) { settings.cycleFrames = frames.box.getSelectedId (); changed (); } };
        playback.box.onChange = [this] { if (! applying) { settings.playback = static_cast<Playback> (playback.box.getSelectedId () - 1); changed (); } };
        unipolar.onClick = [this] { settings.unipolar = unipolar.getToggleState (); changed (); };
        invert.onClick = [this] { settings.invert = invert.getToggleState (); changed (); };
        tempoButton.onClick = [this]
        {
            const auto seconds { 60.0 * beatCount / tempo };
            if (seconds < 0.001 || seconds > 60) { notice ("That tempo/beat length is outside 0.001-60 seconds. Reduce beats or raise BPM.", true); return; }
            settings.durationSeconds = seconds; sync (); changed ();
        };
        matchButton.onClick = [this]
        {
            const auto length { owner.onMatchDuration ? owner.onMatchDuration (match.box.getSelectedId () - 1) : std::optional<double> {} };
            if (! length || ! std::isfinite (*length) || *length < 0.001 || *length > 60) { notice ("Select a loaded sample/loop in the preset first. Its length must be between 0.001 and 60 seconds.", true); return; }
            settings.durationSeconds = *length; sync (); changed ();
        };
        curve.onChange = [this] { changed (); };
        auto fill = [this] (int pattern)
        {
            for (size_t i { 0 }; i < settings.drawn.size (); ++i)
                settings.drawn[i] = pattern == 0 ? 0 : pattern == 1 ? -1.0 + 2.0 * i / 32.0 : std::sin (juce::MathConstants<double>::twoPi * i / 32.0);
            for (size_t i { 0 }; i < settings.steps.size (); ++i)
                settings.steps[i] = pattern == 0 ? 0 : pattern == 1 ? -1.0 + 2.0 * (i % static_cast<size_t> (settings.stepCount)) / juce::jmax (1, settings.stepCount - 1) : std::sin (juce::MathConstants<double>::twoPi * i / settings.stepCount);
            sync (); changed ();
        };
        flat.onClick = [fill] { fill (0); }; ramp.onClick = [fill] { fill (1); }; sine.onClick = [fill] { fill (2); };
        spreadButton.onClick = [this] { spreadVoices (settings, settings.voiceCount, detuneSpread, phaseSpread, panSpread); sync (); changed (); };
        supersaw.onClick = [this] { settings = startingPoint (Mode::layers, Shape::saw); spreadVoices (settings, 7, 24, 300, 0.8); sync (); changed (); };
        load.onClick = [this] { loadRecipe (); };
        recall.onClick = [this] { recallAssigned (targetChannel.box.getSelectedId () - 1, targetZone.box.getSelectedId () - 1); };
        create.onClick = [this] { exportFiles (); };
        testOutput.onClick = [this]
        {
            if (chooser || exportBusy || assignmentBusy || assignmentConfirming || recallConfirming) return;
            clearAudition (false);
            updateAuditionControls ();
            if (owner.launchTestOutput) owner.launchTestOutput (settings, initialFolder, fileName.getText (), exportSlot.box.getSelectedId ());
            else HardwareTestOutputComponent::show (settings, initialFolder, fileName.getText (), exportSlot.box.getSelectedId ());
        };
        assign.onClick = [this] { assignFiles (); };
        targetChannel.box.onChange = [this] { updateAssignmentControls (); };
        targetZone.box.onChange = [this] { updateAssignmentControls (); };
        openExport.onClick = [this]
        {
            if (exportedFolder.isDirectory () && owner.onOpenExportedFolder) owner.onOpenExportedFolder (exportedFolder);
            else notice ("The exported folder is no longer available.", true);
        };
    }

    void sync ()
    {
        const juce::ScopedValueSetter<bool> guard (applying, true);
        const bool cv { settings.mode == Mode::modulation }, bank { settings.mode == Mode::layers };
        mode.box.setSelectedId (static_cast<int> (settings.mode) + 1, juce::dontSendNotification);
        shape.box.clear (juce::dontSendNotification); preset.box.clear (juce::dontSendNotification);
        for (int i { 0 }; i <= (cv ? 8 : 4); ++i)
        {
            const auto label { shapeName (static_cast<Shape> (i)) };
            shape.box.addItem (label, i + 1);
            preset.box.addItem (label + " preset", i + 1);
        }
        shape.box.setSelectedId (static_cast<int> (settings.shape) + 1, juce::dontSendNotification);
        preset.box.setTextWhenNothingSelected ("Choose a fresh starting point");
        rate.box.setSelectedId (settings.sampleRate == 96000 ? 2 : 1, juce::dontSendNotification);
        frames.box.setSelectedId (settings.cycleFrames, juce::dontSendNotification);
        playback.box.setSelectedId (static_cast<int> (settings.playback) + 1, juce::dontSendNotification);
        unipolar.setToggleState (settings.unipolar, juce::dontSendNotification);
        invert.setToggleState (settings.invert, juce::dontSendNotification);
        attack->slider.setRange (0, juce::jmax (0.001, (1.0 - settings.decay - settings.release) * 100), 0.1);
        decay->slider.setRange (0, juce::jmax (0.001, (1.0 - settings.attack - settings.release) * 100), 0.1);
        release->slider.setRange (0, juce::jmax (0.001, (1.0 - settings.attack - settings.decay) * 100), 0.1);
        for (auto& binding : controls) binding.control->slider.setValue (binding.read (), juce::dontSendNotification);
        for (auto* component : std::initializer_list<juce::Component*> { duration, cycles, bpm, beats, &tempoButton, &match, &matchButton }) component->setVisible (cv);
        frames.setVisible (! cv);
        envelope.setVisible (cv && settings.shape == Shape::envelope);
        drawing.setVisible (cv && (settings.shape == Shape::steps || settings.shape == Shape::drawn || settings.shape == Shape::random));
        stepCount->setVisible (settings.shape == Shape::steps || settings.shape == Shape::random);
        symmetry->setVisible (settings.shape == Shape::sine || settings.shape == Shape::triangle || settings.shape == Shape::saw || settings.shape == Shape::trapezoid);
        width->setVisible (settings.shape == Shape::pulse || settings.shape == Shape::trapezoid);
        for (auto* control : { harmonics, brightness, drive, fold }) control->setVisible (! cv);
        glide->setVisible (settings.shape == Shape::steps || settings.shape == Shape::random);
        seed->setVisible (settings.shape == Shape::random);
        drawing.title.setText (settings.shape == Shape::random ? "RANDOM STEPS" : settings.shape == Shape::steps ? "STEP SEQUENCE" : "DRAWN CURVE", juce::dontSendNotification);
        drawing.hint.setText (settings.shape == Shape::random ? "The seed makes steps repeatable; glide softens their transitions."
                              : settings.shape == Shape::steps ? "Click or drag to set step levels. Preview above shows the final waveform."
                              : "Draw a 33-point contour; fast drags fill the points in between.", juce::dontSendNotification);
        curve.setVisible (settings.shape == Shape::steps || settings.shape == Shape::drawn);
        drawingActions.setVisible (curve.isVisible ());
        layers.setVisible (bank);
        for (size_t i { 0 }; i < 8; ++i) voiceRows[i].setVisible (static_cast<int> (i) < settings.voiceCount);
        curve.repaint ();
        if (targetVoiceCount != (bank ? settings.voiceCount : 1)) refreshTargets (false);
        layout ();
    }

    void changed ()
    {
        if (applying) return;
        ++generation;
        const auto now { juce::Time::getMillisecondCounterHiRes () };
        // Idle edits (especially long CV curves) are debounced. During live
        // shaping keep a fixed submission deadline, so repeated drag events
        // cannot postpone the next audible update indefinitely.
        if (canUpdateLive ())
        {
            if (! pendingRender) renderAt = juce::jmax (now, lastRenderSubmittedAt + 90.0);
        }
        else renderAt = now + 140.0;
        pendingRender = true;
        sync ();
        const auto valid { validate (settings) };
        create.setEnabled (valid.wasOk () && ! exportBusy && ! assignmentBusy && ! assignmentConfirming);
        updateAssignmentControls ();
        if (valid.failed ())
        {
            clearAudition ();
            preview.data.reset ();
            summary.setText ({}, juce::dontSendNotification);
            notice (valid.getErrorMessage (), true);
        }
        else if (! exportBusy && ! assignmentBusy && ! assignmentConfirming) notice ("Updating visual preview... No changes to the current preset.");
        preview.message = "Updating visual preview...";
        renderStats.setText (preview.message, juce::dontSendNotification);
        preview.repaint ();
        updateExpandedPreview ();
        if (settings.mode == Mode::modulation) clearAudition ();
        updateAuditionControls ();
    }

    void notice (const juce::String& message, bool error = false)
    {
        status.setText (message, juce::dontSendNotification);
        status.setTooltip (message);
        Theme::bindColour (status, juce::Label::textColourId, [error] { return error ? Theme::warning : Theme::muted; });
    }

    void updateAssignmentControls ()
    {
        const auto busy { exportBusy || assignmentBusy || assignmentConfirming || recallConfirming };
        const auto channel { targetChannel.box.getSelectedId () };
        assign.setEnabled (! busy && assignmentContext && owner.onGetAssignmentContext && owner.onApplyAssignment &&
            channel > 0 && targetChannel.box.isItemEnabled (channel) && targetZone.box.getSelectedId () > 0 && validate (settings).wasOk ());
        targetChannel.setEnabled (! busy && assignmentContext.has_value ());
        targetZone.setEnabled (! busy && assignmentContext.has_value ());
        const auto zone { targetZone.box.getSelectedId () - 1 };
        recall.setEnabled (! busy && assignmentContext && channel >= 1 && channel <= 8 && zone >= 0 && zone < 8 &&
            assignmentContext->preset.getChild (channel - 1).getChild (zone).getProperty (ZoneProperties::SamplePropertyId).toString ().isNotEmpty ());
        exportSlot.setEnabled (! busy);
        create.setEnabled (! busy && validate (settings).wasOk ());
        testOutput.setEnabled (! busy); // Built-in test signals remain available even if this design is invalid.
        close.setEnabled (! busy); load.setEnabled (! busy); openExport.setEnabled (! busy);
    }

    void refreshTargets (bool suggest)
    {
        targetVoiceCount = settings.mode == Mode::layers ? settings.voiceCount : 1;
        const auto previous { targetChannel.box.getSelectedId () };
        targetChannel.box.clear (juce::dontSendNotification);
        int suggested { 0 };
        std::array<bool, 8> enabled {}, empty {};
        if (assignmentContext && assignmentContext->preset.isValid ())
        {
            PresetProperties presetProperties (assignmentContext->preset, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
            std::array<bool, 8> paired {};
            for (auto channel { 0 }; channel < 8; ++channel)
            {
                ChannelProperties properties (presetProperties.getChannelVT (channel), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
                paired[static_cast<size_t> (channel)] = properties.getChannelMode () == ChannelProperties::stereoRight;
                if (channel > 0 && paired[static_cast<size_t> (channel)]) paired[static_cast<size_t> (channel - 1)] = true;
                empty[static_cast<size_t> (channel)] = properties.getChannelMode () == ChannelProperties::master;
                for (auto zone { 0 }; zone < 8; ++zone)
                {
                    ZoneProperties zoneProperties (properties.getZoneVT (zone), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                    if (zoneProperties.getSample ().isNotEmpty ()) empty[static_cast<size_t> (channel)] = false;
                }
            }
            for (auto channel { 0 }; channel < 8; ++channel)
            {
                auto available { channel + targetVoiceCount <= 8 };
                auto allEmpty { available };
                for (auto voice { channel }; available && voice < channel + targetVoiceCount; ++voice)
                {
                    available = ! paired[static_cast<size_t> (voice)];
                    allEmpty = allEmpty && empty[static_cast<size_t> (voice)];
                }
                enabled[static_cast<size_t> (channel)] = available;
                const auto end { juce::jmin (8, channel + targetVoiceCount) };
                auto label { "CH " + juce::String (channel + 1) + (targetVoiceCount > 1 ? "-" + juce::String (end) : juce::String ()) };
                label += channel + targetVoiceCount > 8 ? " (not enough channels)" : ! available ? " (stereo pair)" : allEmpty ? " (empty)" : " (existing settings)";
                targetChannel.box.addItem (label, channel + 1);
                targetChannel.box.setItemEnabled (channel + 1, available);
                if (available && allEmpty && suggested == 0) suggested = channel + 1;
            }
        }
        else for (auto channel { 1 }; channel <= 8; ++channel) targetChannel.box.addItem ("CH " + juce::String (channel), channel);
        auto selected { previous };
        if (suggest || selected < 1 || selected > 8 || ! enabled[static_cast<size_t> (selected - 1)])
        {
            selected = suggested;
            if (selected == 0)
                for (auto channel { 0 }; channel < 8; ++channel)
                    if (enabled[static_cast<size_t> (channel)]) { selected = channel + 1; break; }
        }
        targetChannel.box.setSelectedId (selected, juce::dontSendNotification);
        if (suggest) targetZone.box.setSelectedId (1, juce::dontSendNotification);
        updateAssignmentControls ();
    }

    void refreshContext ()
    {
        auto next { owner.onGetAssignmentContext ? owner.onGetAssignmentContext () : std::nullopt };
        const auto changedIdentity { ! assignmentContext || ! next || assignmentContext->folder != next->folder ||
            assignmentContext->preset.getProperty (PresetProperties::IdPropertyId) != next->preset.getProperty (PresetProperties::IdPropertyId) };
        assignmentContext = std::move (next);
        refreshTargets (changedIdentity);
    }

    void assignFiles ()
    {
        if (chooser || exportBusy || assignmentBusy || assignmentConfirming || recallConfirming || ! owner.onGetAssignmentContext || ! owner.onApplyAssignment) return;
        const auto valid { validate (settings) };
        if (valid.failed ()) { notice (valid.getErrorMessage (), true); return; }
        const auto name { juce::File::createLegalFileName (fileName.getText ().trim ()).trim () };
        if (name.isEmpty () || name == "." || name == "..") { notice ("Enter a usable design name before generating files.", true); return; }
        auto context { owner.onGetAssignmentContext () };
        if (! context || ! context->preset.isValid () || ! context->folder.isDirectory ()) { notice ("Select a folder and preset before assigning a design.", true); return; }
        const auto channel { targetChannel.box.getSelectedId () - 1 }, zone { targetZone.box.getSelectedId () - 1 };
        const auto count { settings.mode == Mode::layers ? settings.voiceCount : 1 };
        if (channel < 0 || channel + count > 8 || zone < 0 || zone > 7) { notice ("Choose enough consecutive channels and a valid zone.", true); return; }
        auto message { "Generate " + juce::String (count) + " WAV file(s) in " + context->folder.getFullPathName () + "?\n\n" +
            "Assign to channel " + juce::String (channel + 1) + (count > 1 ? "-" + juce::String (channel + count) : juce::String ()) +
            ", zone " + juce::String (zone + 1) + ". Existing samples in those target zones will be replaced; other zones and their markers remain. " +
            "Appending a zone splits the previous zone's CV range.\n\n" +
            "Pitch, pan, envelope, play/loop and mix settings are channel-wide and also apply to all other zones in these channels. " +
            (count > 1 ? "Bank voices become a Master/Link group. " : "") +
            (settings.mode == Mode::modulation ? "CV uses Mix Off and individual outputs only; audio and CV cannot share a channel. " : "Audio and CV cannot share a channel. ") +
            "\n\nThe preset is changed in memory only. Use Save afterward to write the preset." };
        const auto snapshot { settings };
        const auto request { ++assignmentConfirmation };
        assignmentConfirming = true;
        updateAssignmentControls ();
        auto safe = juce::Component::SafePointer<WaveformWorkspace> (&owner);
        owner.confirmAssignment (message, [safe, context = *context, snapshot, name, channel, zone, request] (bool accepted)
        {
            if (safe == nullptr || safe->impl->assignmentConfirmation != request) return;
            auto& self { *safe->impl };
            ++self.assignmentConfirmation;
            self.assignmentConfirming = false;
            if (! accepted) { self.updateAssignmentControls (); self.notice ("Assignment canceled. No files or preset values changed."); return; }
            const auto current { safe->onGetAssignmentContext ? safe->onGetAssignmentContext () : std::nullopt };
            if (! current || current->revision != context.revision || current->folder != context.folder || ! current->preset.isEquivalentTo (context.preset))
            {
                self.updateAssignmentControls ();
                self.notice ("The preset or folder changed while confirmation was open. Review the destination and try again.", true);
                return;
            }
            self.assignmentBusy = true;
            self.updateAssignmentControls ();
            self.notice ("Generating WAVs and a recipe for this preset... Existing files are not overwritten.");
            {
                std::lock_guard<std::mutex> lock (self.worker->mutex);
                self.worker->assignmentRequest = AsyncState::AssignmentRequest { snapshot, context, name, channel, zone };
            }
            self.worker->ready.notify_one ();
        });
    }

    void updateExpandedPreview ()
    {
        if (! expandedPreview) return;
        expandedPreview->waveform.data = preview.data;
        expandedPreview->waveform.message = preview.message;
        expandedPreview->waveform.getProperties ().set ("renderGeneration", static_cast<int> (displayedGeneration));
        expandedPreview->waveform.repaint ();
    }

    void clearAudition (bool forgetPayload = true)
    {
        // Mode, recipe, invalid-design and visibility transitions form hard
        // barriers. A stop ramp may still report active, but no older worker
        // result is permitted to cross this boundary and restore its payload.
        ++auditionEpoch;
        if (owner.onStopAudition) owner.onStopAudition ();
        if (owner.onAuditionPayload) owner.onAuditionPayload ({});
        if (forgetPayload) auditionPayload.reset ();
        else if (displayedGeneration != generation)
        {
            // Back/hide/recipe cancellation can interrupt an in-flight edit.
            // Retain an up-to-date cache, or queue the current design under
            // the new epoch so returning never requires an artificial edit.
            pendingRender = true;
            renderAt = juce::Time::getMillisecondCounterHiRes ();
        }
        auditionError.clear ();
    }

    bool canUpdateLive () const
    {
        return ! auditionSuspended && settings.mode != Mode::modulation && auditionPayload
               && owner.isAuditionActive && owner.isAuditionActive ();
    }

    void updateAuditionControls ()
    {
        const bool cv { settings.mode == Mode::modulation };
        const bool active { ! cv && owner.isAuditionActive && owner.isAuditionActive () };
        auditionButton.setButtonText (active ? "Stop audition" : "Start audition");
        auditionButton.setEnabled (! cv && ! auditionSuspended && (active || (auditionPayload && displayedGeneration == generation && owner.onStartAudition)));
        monitorLevel.setEnabled (! cv); monitorTranspose.setEnabled (! cv);
        auditionTitle.setText (cv ? "CV - VISUAL ONLY" : "LIVE AUDITION", juce::dontSendNotification);
        const auto warn { cv || auditionError.isNotEmpty () };
        Theme::bindColour (auditionHint, juce::Label::textColourId, [warn] { return warn ? Theme::warning : Theme::muted; });
        auditionHint.setText (cv ? "CV speaker audition is disabled. Check DC/slow CV with a suitable meter or scope, not speakers."
                              : auditionError.isNotEmpty () ? auditionError
                              : active ? "Monitor loops continuously; edits update live. Level and transpose do not affect export."
                              : "Start at low speaker/headphone volume. Monitor loops continuously; level/transpose do not affect export.", juce::dontSendNotification);
        auditionHint.setTooltip (auditionHint.getText ());
    }

    void tick ()
    {
        if (pendingRender && juce::Time::getMillisecondCounterHiRes () >= renderAt)
        {
            std::lock_guard<std::mutex> lock (worker->mutex);
            worker->renderRequest = AsyncState::RenderRequest { settings, generation, auditionEpoch };
            lastRenderSubmittedAt = juce::Time::getMillisecondCounterHiRes ();
            pendingRender = false;
            worker->ready.notify_one ();
        }
        std::shared_ptr<PreviewData> completed;
        WaveformAudition::PayloadPtr completedAudition;
        juce::String preparedError;
        juce::String error, exportMessage;
        bool exportFinished { false }, exportFailed { false };
        bool assignmentFinished { false }, assignmentFailed { false };
        juce::String assignmentMessage;
        AssignmentResult assigned;
        std::optional<AsyncState::AssignmentRequest> assignedRequest;
        juce::File finishedFolder;
        {
            std::lock_guard<std::mutex> lock (worker->mutex);
            if (worker->completedEpoch == auditionEpoch && worker->completedGeneration > displayedGeneration
                && (worker->completedGeneration == generation || (worker->preview && canUpdateLive ())))
            {
                displayedGeneration = worker->completedGeneration;
                completed = worker->preview;
                error = worker->renderError;
                completedAudition = worker->audition;
                preparedError = worker->auditionError;
            }
            if (worker->exportFinished)
            {
                exportFinished = true; exportFailed = worker->exportFailed; exportMessage = worker->exportMessage;
                finishedFolder = worker->exportedFolder;
                worker->exportFinished = false;
            }
            if (worker->assignmentFinished)
            {
                assignmentFinished = true; assignmentFailed = worker->assignmentFailed; assignmentMessage = worker->assignmentMessage;
                assigned = std::move (worker->assignmentOutput);
                worker->assignmentOutput = {};
                assignedRequest = std::move (worker->completedAssignmentRequest);
                worker->completedAssignmentRequest.reset ();
                worker->assignmentFinished = false;
            }
        }
        if (completed)
        {
            const bool wasAuditioning { owner.isAuditionActive && owner.isAuditionActive () };
            preview.data = completed;
            auditionPayload = std::move (completedAudition);
            auditionError = preparedError;
            if (! auditionSuspended && owner.onAuditionPayload)
            {
                owner.onAuditionPayload (settings.mode == Mode::modulation ? WaveformAudition::PayloadPtr {} : auditionPayload);
                // A valid design edit can put an actively monitored voice
                // outside the audible range. Surface the host's explanation
                // immediately, without starting anything or probing a device
                // for an idle/visual-only workspace.
                if (wasAuditioning && settings.mode != Mode::modulation && auditionPayload && owner.onAuditionMonitorChange)
                {
                    const auto monitor { owner.onAuditionMonitorChange (monitorLevel.slider.getValue (), monitorTranspose.slider.getValue ()) };
                    if (monitor.failed ())
                    {
                        auditionError = monitor.getErrorMessage ();
                        if (owner.onStopAudition) owner.onStopAudition ();
                    }
                }
            }
            preview.getProperties ().set ("renderGeneration", static_cast<int> (displayedGeneration));
            const auto seconds { completed->frames / completed->rate };
            const auto& renderedSettings { completed->settings };
            juce::String length { "Length " + durationText (seconds) + "  |  " + juce::String (completed->frames) + " frames" };
            if (renderedSettings.mode != Mode::modulation)
            {
                const auto hz { renderedSettings.sampleRate / renderedSettings.cycleFrames };
                const auto midi { 69.0 + 12.0 * std::log2 (hz / 440.0) };
                length += "  |  Base " + juce::MidiMessage::getMidiNoteName (juce::roundToInt (midi), true, true, 4) + " " + juce::String ((midi - std::round (midi)) * 100, 1) + " ct / " + juce::String (hz, 2) + " Hz";
            }
            else length += "  |  " + juce::String (renderedSettings.cycles, 2) + " cycle(s)";
            if (renderedSettings.mode == Mode::layers) length += "  |  " + juce::String (renderedSettings.voiceCount) + " voices";
            summary.setText (length, juce::dontSendNotification);
            preview.message = "Peak " + juce::String (completed->peak * 100, 1) + "%  |  DC " + juce::String (completed->dc * 100, 1)
                            + "%  |  Boundary jump " + juce::String (completed->boundary * 100, 2) + "%  |  "
                            + (completed->clipped > 0 ? juce::String (completed->clipped) + " clipped samples" : "No clipping");
            if (renderedSettings.measuredFullScaleVolts > 0)
                preview.message += "  |  Measured-FS peak estimate " + juce::String (completed->peak * renderedSettings.measuredFullScaleVolts, 3) + " V";
            renderStats.setText (preview.message, juce::dontSendNotification);
            preview.repaint ();
            updateExpandedPreview ();
            if (! exportBusy && ! exportFinished && ! assignmentBusy && ! assignmentConfirming && ! assignmentFinished)
            {
                if (displayedGeneration != generation) notice ("Updating visual preview and audition... No changes to the current preset.");
                else notice (completed->warnings.isEmpty () ? "Ready. Generate & Assign updates this preset; Export new package creates a separate A8 folder. Both save an editable recipe."
                                                            : completed->warnings.joinIntoString ("  |  "), ! completed->warnings.isEmpty ());
            }
        }
        else if (error.isNotEmpty ())
        {
            preview.data.reset (); summary.setText ({}, juce::dontSendNotification);
            clearAudition ();
            preview.message = "Preview unavailable: " + error; preview.repaint (); notice (error, true);
            renderStats.setText (preview.message, juce::dontSendNotification);
            updateExpandedPreview ();
        }
        if (exportFinished)
        {
            exportBusy = false; close.setEnabled (true); load.setEnabled (true); openExport.setEnabled (true);
            updateAssignmentControls ();
            if (! exportFailed)
            {
                exportedFolder = finishedFolder;
                openExport.setVisible (true);
                layout ();
            }
            notice (exportMessage, exportFailed);
        }
        if (assignmentFinished)
        {
            if (! assignmentFailed)
            {
                const auto result { assignedRequest && owner.onApplyAssignment ? owner.onApplyAssignment (assignedRequest->context, assigned)
                    : juce::Result::fail ("The selected preset is no longer available.") };
                assignmentFailed = result.failed ();
                assignmentMessage = result.getErrorMessage ();
            }
            if (assignmentFailed)
            {
                const auto cleanup { cleanupAssignmentFiles (assigned) };
                if (cleanup.failed ()) assignmentMessage += " " + cleanup.getErrorMessage ();
            }
            assignmentBusy = false;
            refreshContext ();
            auto destination { juce::String ("this preset") };
            if (assignedRequest)
            {
                const auto count { assignedRequest->settings.mode == Mode::layers ? assignedRequest->settings.voiceCount : 1 };
                destination = "preset " + assignedRequest->context.preset.getProperty (PresetProperties::IdPropertyId).toString () +
                    ", CH " + juce::String (assignedRequest->channel + 1) +
                    (count > 1 ? "-" + juce::String (assignedRequest->channel + count) : juce::String ()) +
                    ", zone " + juce::String (assignedRequest->zone + 1);
            }
            notice (assignmentFailed ? "Assignment was not applied: " + assignmentMessage
                : "Assigned to " + destination + ". Click Save to write the preset. Other unsaved edits are preserved.", assignmentFailed);
        }
        updateAuditionControls ();
    }

    void requestRecall (juce::File file, bool fromWave, std::optional<AssignmentContext> context = {}, int channel = -1, int zone = -1)
    {
        if (chooser || exportBusy || assignmentBusy || assignmentConfirming || recallConfirming) return;
        WaveformDesignRecall::RecalledDesign design;
        const auto result { fromWave ? WaveformDesignRecall::recallWave (file, design) : WaveformDesignRecall::loadRecipe (file, design) };
        if (result.failed ()) { notice ("Could not recall design: " + result.getErrorMessage (), true); return; }
        const auto approvedSettings { juce::JSON::toString (toJson (design.settings)) };
        const auto beforeGeneration { generation };
        const auto beforeName { fileName.getText () };
        const auto request { ++recallConfirmation };
        recallConfirming = true;
        updateAssignmentControls ();
        juce::Component::SafePointer<WaveformWorkspace> safe (&owner);
        owner.confirmRecall ("Recall the design from " + design.recipe.getFileName () + "?\n\n" +
            (design.settings.mode == Mode::layers ? "This restores the entire bank, including all voices.\n\n" : "") +
            "The current design in that workspace mode will be replaced. Cancel and generate/assign or export first if you want to keep it.\n\n" +
            "Preset values and WAV files are not changed. After editing, use Generate & Assign, then Save, to update the preset.",
            [safe, file, fromWave, context, channel, zone, approvedSettings, beforeGeneration, beforeName, request] (bool accepted)
        {
            if (safe == nullptr || safe->impl->recallConfirmation != request) return;
            auto& self { *safe->impl };
            ++self.recallConfirmation;
            self.recallConfirming = false;
            self.updateAssignmentControls ();
            if (! accepted) { self.notice ("Recall canceled. Your design and preset are unchanged."); return; }
            if (self.generation != beforeGeneration || self.fileName.getText () != beforeName)
            {
                self.notice ("The design changed while confirmation was open. Nothing was replaced; recall again when ready.", true);
                return;
            }
            if (context)
            {
                const auto current { safe->onGetAssignmentContext ? safe->onGetAssignmentContext () : std::nullopt };
                if (! current || current->revision != context->revision || current->folder != context->folder || ! current->preset.isEquivalentTo (context->preset))
                {
                    self.notice ("The preset or folder changed while confirmation was open. Nothing was recalled; select the source again.", true);
                    return;
                }
            }
            WaveformDesignRecall::RecalledDesign recalled;
            const auto loaded { fromWave ? WaveformDesignRecall::recallWave (file, recalled) : WaveformDesignRecall::loadRecipe (file, recalled) };
            if (loaded.failed () || juce::JSON::toString (toJson (recalled.settings)) != approvedSettings)
            {
                self.notice ("The saved design changed or became unavailable. Nothing was recalled; select it again.", true);
                return;
            }
            self.clearAudition ();
            self.modeDesigns[static_cast<size_t> (self.settings.mode)] = self.settings;
            self.settings = recalled.settings;
            self.fileName.setText (recalled.displayName, false);
            self.initialFolder = recalled.recipe.getParentDirectory ();
            self.sync (); self.changed ();
            self.refreshContext ();
            if (context)
            {
                auto first { channel };
                if (recalled.settings.mode == Mode::layers)
                {
                    first -= recalled.voiceIndex;
                    // Only infer a whole bank destination if all original
                    // voice assignments still occupy the expected channels.
                    if (first < 0 || first + recalled.settings.voiceCount > 8) first = -1;
                    for (auto voice { 0 }; first >= 0 && voice < recalled.settings.voiceCount; ++voice)
                    {
                        const auto expected { file.getFileNameWithoutExtension ().dropLastCharacters (2) + juce::String (voice + 1).paddedLeft ('0', 2) + ".wav" };
                        if (context->preset.getChild (first + voice).getChild (zone).getProperty (ZoneProperties::SamplePropertyId).toString () != expected) first = -1;
                    }
                }
                self.targetChannel.box.setSelectedId (first >= 0 && self.targetChannel.box.isItemEnabled (first + 1) ? first + 1 : 0, juce::dontSendNotification);
                self.targetZone.box.setSelectedId (zone + 1, juce::dontSendNotification);
                self.updateAssignmentControls ();
            }
            self.notice ("Recalled " + recalled.recipe.getFileName () + ". Preset unchanged; edit, Generate & Assign, then Save.");
        });
    }

    void recallAssigned (int channel, int zone)
    {
        const auto context { owner.onGetAssignmentContext ? owner.onGetAssignmentContext () : std::nullopt };
        if (! context || channel < 0 || channel >= 8 || zone < 0 || zone >= 8)
        { notice ("Select a preset and an assigned channel/zone to recall.", true); return; }
        const auto sample { context->preset.getChild (channel).getChild (zone).getProperty (ZoneProperties::SamplePropertyId).toString () };
        if (sample.isEmpty ()) { notice ("This zone is empty. Select a zone containing a generated waveform.", true); return; }
        if (juce::File::isAbsolutePath (sample) || sample.containsAnyOf ("/\\"))
        { notice ("The zone does not reference a WAV in the current preset folder.", true); return; }
        requestRecall (context->folder.getChildFile (sample), true, context, channel, zone);
    }

    void loadRecipe ()
    {
        if (chooser || exportBusy || assignmentBusy || assignmentConfirming || recallConfirming) return;
        // Cancelling (or rejecting) a recipe must not discard the current
        // design's readiness. A successful replacement clears it below.
        clearAudition (false);
        chooser = std::make_unique<juce::FileChooser> ("Load waveform recipe or generated WAV", initialFolder, "*.json;*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [safe = juce::Component::SafePointer<WaveformWorkspace> (&owner)] (const juce::FileChooser& dialog)
        {
            const auto file { dialog.getResult () };
            if (safe == nullptr) return;
            auto& self { *safe->impl };
            self.chooser.reset ();
            if (file.existsAsFile ()) self.requestRecall (file, file.hasFileExtension ("wav"));
        });
    }

    void exportFiles ()
    {
        if (chooser || exportBusy || assignmentBusy || assignmentConfirming || recallConfirming) return;
        const auto valid { validate (settings) };
        if (valid.failed ()) { notice (valid.getErrorMessage (), true); return; }
        const auto name { juce::File::createLegalFileName (fileName.getText ().trim ()).trim () };
        if (name.isEmpty () || name == "." || name == "..") { notice ("Enter a usable design name before creating files.", true); return; }
        fileName.setText (name, false);
        const auto snapshot { settings };
        const auto presetNumber { exportSlot.box.getSelectedId () };
        chooser = std::make_unique<juce::FileChooser> ("Export separate package - choose parent folder (current preset unchanged)", initialFolder, "");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                             [safe = juce::Component::SafePointer<WaveformWorkspace> (&owner), snapshot, name, presetNumber] (const juce::FileChooser& dialog)
        {
            const auto folder { dialog.getResult () };
            if (safe == nullptr) return;
            auto& self { *safe->impl };
            if (folder.isDirectory ())
            {
                self.initialFolder = folder;
                self.exportBusy = true;
                self.updateAssignmentControls ();
                self.notice ("Creating a new design folder... The current preset and its files are untouched.");
                {
                    std::lock_guard<std::mutex> lock (self.worker->mutex);
                    self.worker->exportRequest = AsyncState::ExportRequest { snapshot, folder, name, presetNumber };
                }
                self.worker->ready.notify_one ();
            }
            self.chooser.reset ();
        });
    }

    void layout ()
    {
        if (owner.getWidth () <= 0 || owner.getHeight () <= 0) return;
        auto bounds { owner.getLocalBounds ().reduced (18, 12) };
        auto heading { bounds.removeFromTop (54) };
        close.setBounds (heading.removeFromRight (126).withHeight (32));
        heading.removeFromRight (10);
        load.setBounds (heading.removeFromRight (160).withHeight (32));
        title.setBounds (heading.removeFromTop (28)); subtitle.setBounds (heading);
        auto toolbar { bounds.removeFromTop (40) };
        const auto third { toolbar.getWidth () / 3 };
        mode.setBounds (toolbar.removeFromLeft (third).withTrimmedRight (14));
        shape.setBounds (toolbar.removeFromLeft (third).withTrimmedRight (14)); preset.setBounds (toolbar);
        bounds.removeFromTop (6);
        auto previewRow { bounds.removeFromTop (juce::jlimit (165, 190, owner.getHeight () / 4)) };
        auto picture { previewRow.removeFromLeft (juce::jlimit (400, 480, juce::roundToInt (previewRow.getWidth () * 0.42))) };
        auto expandRow { picture.removeFromBottom (28) };
        expand.setBounds (expandRow.removeFromRight (156));
        preview.setBounds (picture.withTrimmedBottom (4));
        previewRow.removeFromLeft (20);
        auto auditionHeading { previewRow.removeFromTop (29) };
        auditionButton.setBounds (auditionHeading.removeFromRight (128));
        auditionTitle.setBounds (auditionHeading);
        auto monitorRow { previewRow.removeFromTop (38) };
        monitorTranspose.setBounds (monitorRow.removeFromLeft (monitorRow.getWidth () / 2).withTrimmedRight (10));
        monitorLevel.setBounds (monitorRow);
        auditionHint.setBounds (previewRow.removeFromTop (42));
        renderStats.setBounds (previewRow.withTrimmedTop (5));
        summary.setBounds (bounds.removeFromTop (27));
        auto footer { bounds.removeFromBottom (200) };
        status.setBounds (footer.removeFromBottom (31));
        auto nameRow { footer.removeFromTop (31) };
        nameLabel.setBounds (nameRow.removeFromLeft (90));
        fileName.setBounds (nameRow.withWidth (juce::jmin (440, nameRow.getWidth ())));
        footer.removeFromTop (5);
        assignmentHeading.setBounds (footer.removeFromTop (20));
        auto assignmentRow { footer.removeFromTop (38) };
        assign.setBounds (assignmentRow.removeFromRight (180).reduced (0, 3));
        assignmentRow.removeFromRight (12);
        recall.setBounds (assignmentRow.removeFromRight (155).reduced (0, 3));
        assignmentRow.removeFromRight (12);
        targetChannel.setBounds (assignmentRow.removeFromLeft (juce::jmin (340, assignmentRow.getWidth () - 180)).withTrimmedRight (12));
        targetZone.setBounds (assignmentRow.removeFromLeft (200));
        packageHeading.setBounds (footer.removeFromTop (20));
        auto exportRow { footer.removeFromTop (38) };
        testOutput.setBounds (exportRow.removeFromRight (142).reduced (0, 3));
        exportRow.removeFromRight (12);
        exportSlot.setBounds (exportRow.removeFromLeft (240).withTrimmedRight (12));
        create.setBounds (exportRow.removeFromLeft (180).reduced (0, 3)); exportRow.removeFromLeft (12);
        if (openExport.isVisible ()) openExport.setBounds (exportRow.removeFromLeft (190).reduced (0, 3));
        viewport.setBounds (bounds.withTrimmedBottom (8));
        const int contentWidth { juce::jmax (800, viewport.getWidth () - viewport.getScrollBarThickness ()) };
        const int gap { 12 }, column { (contentWidth - gap * 2) / 3 };
        tone.setBounds (0, 0, column, tone.preferredHeight ());
        output.setBounds (column + gap, 0, column, output.preferredHeight ());
        timing.setBounds ((column + gap) * 2, 0, column, timing.preferredHeight ());
        int y { juce::jmax (tone.getBottom (), output.getBottom ()) + gap };
        if (envelope.isVisible ())
        {
            envelope.setBounds (0, tone.getBottom () + gap, column, envelope.preferredHeight ());
            y = juce::jmax (y, envelope.getBottom () + gap);
        }
        if (drawing.isVisible ())
        {
            drawing.setBounds (0, y, column * 2 + gap, drawing.preferredHeight ());
            auto row { drawingActions.getLocalBounds () };
            const auto buttonWidth { row.getWidth () / 3 };
            for (auto* button : { &flat, &ramp, &sine }) button->setBounds (row.removeFromLeft (buttonWidth).reduced (3, 2));
            y += drawing.getHeight () + gap;
        }
        y = juce::jmax (y, timing.getBottom () + gap);
        if (layers.isVisible ())
        {
            layers.setBounds (0, y, contentWidth, layers.preferredHeight ());
            for (size_t i { 0 }; i < 8; ++i)
            {
                auto row { voiceRows[i].getLocalBounds () };
                voiceLabels[i].setBounds (row.removeFromLeft (52));
                const auto cell { row.getWidth () / 4 };
                for (auto* control : voiceControls[i]) control->setBounds (row.removeFromLeft (cell).reduced (5, 0));
            }
            y += layers.getHeight () + gap;
        }
        content.setSize (contentWidth, y);
    }
};

WaveformWorkspace::WaveformWorkspace () : impl (std::make_unique<Impl> (*this))
{
    confirmAssignment = [] (const juce::String& message, std::function<void (bool)> callback)
    {
        juce::AlertWindow::showOkCancelBox (juce::AlertWindow::WarningIcon, "Generate and assign waveform", message, "Generate & Assign", "Cancel", nullptr,
            juce::ModalCallbackFunction::create ([completion = std::move (callback)] (int response) { completion (response == 1); }));
    };
    confirmRecall = [] (const juce::String& message, std::function<void (bool)> callback)
    {
        juce::AlertWindow::showOkCancelBox (juce::AlertWindow::WarningIcon, "Recall waveform design", message, "Recall", "Cancel", nullptr,
            juce::ModalCallbackFunction::create ([completion = std::move (callback)] (int response) { completion (response == 1); }));
    };
    startTimerHz (30);
}
WaveformWorkspace::~WaveformWorkspace () { stopTimer (); impl.reset (); }
void WaveformWorkspace::setInitialFolder (juce::File folder) { impl->initialFolder = std::move (folder); }
void WaveformWorkspace::refreshAssignmentContext () { impl->refreshContext (); }
void WaveformWorkspace::recallAssigned (int channel, int zone) { impl->recallAssigned (channel, zone); }
WaveformDesign::Settings WaveformWorkspace::getSettings () const { return impl->settings; }
void WaveformWorkspace::timerCallback () { impl->tick (); }
void WaveformWorkspace::resized () { if (impl) impl->layout (); }
void WaveformWorkspace::paint (juce::Graphics& g) { g.fillAll (Theme::background); }
void WaveformWorkspace::visibilityChanged ()
{
    updateAuditionVisibility (isShowing ());
}
void WaveformWorkspace::updateAuditionVisibility (bool showing)
{
    if (! impl) return;
    impl->auditionSuspended = ! showing;
    if (impl->auditionSuspended)
    {
        impl->clearAudition (false);
        if (impl->expandedPreview) impl->expandedPreview->setVisible (false);
    }
    else if (impl->auditionPayload && impl->settings.mode != WaveformDesign::Mode::modulation && impl->displayedGeneration == impl->generation && onAuditionPayload)
        onAuditionPayload (impl->auditionPayload);
    impl->updateAuditionControls ();
}
juce::Component* WaveformWorkspace::getExpandedPreview () const { return impl->expandedPreview.get (); }
