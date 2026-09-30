#include "GUI/Assimil8or/Editor/Assimil8orEditorComponent.h"
#include "GUI/Assimil8or/Editor/SampleManager/SampleManager.h"
#include "Assimil8or/Audio/SampleRename.h"
#include "Assimil8or/Audio/WaveformDesignAssignment.h"
#include "Assimil8or/Audio/WaveformDesignExport.h"
#include "Assimil8or/Audio/WaveformDesignRecall.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "Assimil8or/PresetFileOperations.h"
#include "Assimil8or/PresetManagerProperties.h"
#include "SystemServices.h"
#include "oolib/Properties/PersistentRootProperties.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace
{
    void check (bool ok, const char* message) { if (! ok) throw std::runtime_error (message); }
    struct Action { bool present { false }, enabled { false }; std::function<void ()> invoke; };
    Action findAction (const juce::PopupMenu& menu, const juce::String& text)
    {
        for (juce::PopupMenu::MenuItemIterator items (menu); items.next ();)
        {
            const auto& item { items.getItem () };
            if (item.text == text) return { true, item.isEnabled, item.action };
            if (item.subMenu)
            {
                auto found { findAction (*item.subMenu, text) };
                if (found.present) return found;
            }
        }
        return {};
    }
    juce::StringArray fileNames (const juce::File& folder)
    {
        juce::StringArray names;
        for (const auto& file : folder.findChildFiles (juce::File::findFiles, false)) names.add (file.getFileName ());
        names.sort (false);
        return names;
    }
    juce::Component* findNamed (juce::Component& component, const juce::String& name)
    {
        if (component.getName () == name || component.getComponentID () == name) return &component;
        for (int index { 0 }; index < component.getNumChildComponents (); ++index)
            if (auto* found = findNamed (*component.getChildComponent (index), name)) return found;
        return nullptr;
    }
    void snapshot (juce::Component& component, const juce::String& name)
    {
        const auto artifacts { juce::SystemStats::getEnvironmentVariable ("A8MANAGER_TEST_ARTIFACTS", {}) };
        if (artifacts.isEmpty ()) return;
        const juce::File directory { artifacts };
        check (directory.createDirectory ().wasOk (), "Create rename UI screenshot directory");
        auto stream { directory.getChildFile (name + ".png").createOutputStream () };
        check (stream != nullptr && stream->setPosition (0)
               && juce::PNGImageFormat ().writeImageToStream (component.createComponentSnapshot (component.getLocalBounds ()), *stream)
               && stream->truncate ().wasOk (), "Render the actual rename UI without a native peer");
    }
}

struct SampleRenameUiTestAccess
{
    static void rawCycleMenus ()
    {
        const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-raw-cycle-menus", "", false) };
        check (folder.createDirectory ().wasOk (), "Create raw-cycle Samples menu fixtures");
        struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { folder };
        auto writeWave = [&] (const juce::String& name, int frames, bool cv)
        {
            juce::AudioBuffer<float> data (1, frames);
            for (int frame { 0 }; frame < frames; ++frame)
                data.setSample (0, frame, 0.25f * std::sin (juce::MathConstants<double>::twoPi * frame / frames));
            check (WaveformDesign::ExportSupport::writeWave (folder.getChildFile (name), data, 48000, cv).wasOk (), "Write raw menu WAV fixture");
        };
        writeWave ("ordinary.wav", 128, false);
        writeWave ("other.wav", 128, false);
        writeWave ("cv-short.wav", 128, true);
        writeWave ("long.wav", 8193, false);
        check (folder.getChildFile ("invalid.wav").replaceWithText ("Not a WAV"), "Create invalid raw menu fixture");
        juce::ValueTree root { "Root" };
        PersistentRootProperties persistent (root, PersistentRootProperties::WrapperType::owner, PersistentRootProperties::EnableCallbacks::no);
        RuntimeRootProperties runtime (root, RuntimeRootProperties::WrapperType::owner, RuntimeRootProperties::EnableCallbacks::no);
        AppProperties preferences;
        preferences.wrap (persistent.getValueTree (), AppProperties::WrapperType::owner, AppProperties::EnableCallbacks::no);
        preferences.setMostRecentFolder (folder.getFullPathName ());
        preferences.addRecentlyUsedFile (folder.getChildFile ("prst001.yml").getFullPathName ());
        AudioPlayerProperties audition (runtime.getValueTree (), AudioPlayerProperties::WrapperType::owner, AudioPlayerProperties::EnableCallbacks::no);
        GuiControlProperties gui (runtime.getValueTree (), GuiControlProperties::WrapperType::owner, GuiControlProperties::EnableCallbacks::no);
        DirectoryDataProperties directory (runtime.getValueTree (), DirectoryDataProperties::WrapperType::owner, DirectoryDataProperties::EnableCallbacks::no);
        PresetManagerProperties presets (runtime.getValueTree (), PresetManagerProperties::WrapperType::owner, PresetManagerProperties::EnableCallbacks::no);
        const auto defaults { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType) };
        presets.addPreset ("edit", defaults.createCopy ()); presets.addPreset ("unedited", defaults.createCopy ());
        const auto tree { presets.getPreset ("edit") };
        AudioManager audio;
        SystemServices services (runtime.getValueTree (), SystemServices::WrapperType::owner, SystemServices::EnableCallbacks::no);
        services.setAudioManager (&audio); services.setAudioPlayer (nullptr);
        SampleManager samples; samples.init (root);
        EditManager edits; services.setEditManager (&edits); edits.init (root, tree);
        ZoneProperties first (tree.getChild (0).getChild (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        ZoneProperties second (tree.getChild (2).getChild (0), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        first.setSample ("ordinary.wav", false); second.setSample ("other.wav", false);
        ModernLookAndFeel look;
        auto editor { std::make_unique<Assimil8orEditorComponent> () };
        editor->setLookAndFeel (&look); editor->init (root); editor->setSize (1140, 720);
        int requests { 0 }, recalledChannel { -1 }, recalledZone { -1 };
        editor->onRecallWaveform = [&] (int channel, int zone) { ++requests; recalledChannel = channel; recalledZone = zone; };
        auto fileMenu = [&] { return editor->channelEditors[0].zoneEditors[0].createSampleFileMenu (); };
        auto rawFile = [&] { return findAction (fileMenu (), "Import WAV as cycle in designer..."); };
        auto rawPreset = [&] { return findAction (editor->createPresetToolsMenu (), "Import selected WAV as cycle..."); };
        const auto original { tree.createCopy () };
        const auto fromFile { rawFile () }, fromPreset { rawPreset () };
        check (fromFile.present && fromFile.enabled && fromFile.invoke && fromPreset.present && fromPreset.enabled && fromPreset.invoke,
               "Ordinary short WAVs expose raw import through the actual FILE context and Preset tools menus");
        check (! findAction (editor->createPresetToolsMenu (), "Edit selected waveform in designer...").enabled
               && ! editor->canRecallSelectedWaveform () && editor->canImportSelectedCycle (),
               "Raw samples do not falsely acquire generated-recipe Edit availability");
        fromFile.invoke ();
        check (requests == 1 && recalledChannel == 0 && recalledZone == 0, "FILE raw import routes its exact channel and zone to designer recall/import");
        fromPreset.invoke ();
        check (requests == 2 && recalledChannel == 0 && recalledZone == 0 && tree.isEquivalentTo (original),
               "Preset tools raw import routes selection without directly mutating the preset");
        const auto staleFile { rawFile () }, stalePreset { rawPreset () };
        first.setSample ("other.wav", false);
        staleFile.invoke (); stalePreset.invoke ();
        check (requests == 2, "Changing the preset invalidates both pending FILE and Preset-tools raw-import actions");
        first.setSample ("ordinary.wav", false);
        const auto changedSelection { rawPreset () };
        editor->channelTabs.setCurrentTabIndex (2);
        changedSelection.invoke ();
        check (requests == 2, "A menu opened for another selected channel cannot import the new selection instead");
        editor->channelTabs.setCurrentTabIndex (0);

        for (const auto* name : { "cv-short.wav", "invalid.wav", "long.wav", "missing.wav" })
        {
            first.setSample (name, false);
            const auto fileAction { rawFile () }, presetAction { rawPreset () };
            check (fileAction.present && ! fileAction.enabled && presetAction.present && ! presetAction.enabled
                   && ! editor->canImportSelectedCycle (), "CV, malformed, long and missing WAVs do not offer raw-cycle import");
            if (fileAction.invoke) fileAction.invoke ();
            if (presetAction.invoke) presetAction.invoke ();
            check (requests == 2, "Disabled menu callbacks revalidate rather than importing ineligible data");
        }
        first.setSample ("ordinary.wav", false);
        ChannelProperties firstChannel (tree.getChild (0), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        firstChannel.setMixLevel (0, false);
        auto settings { WaveformDesign::startingPoint (WaveformDesign::Mode::oscillator, WaveformDesign::Shape::sine) };
        settings.cycleFrames = 128;
        WaveformDesign::AssignmentResult generated;
        check (WaveformDesign::prepareAssignment (settings, folder, "generated", tree, 0, 0, generated).wasOk (), "Create verified generated waveform menu fixture");
        first.setSample (generated.waves[0].getFileName (), false);
        const auto generatedEdit { findAction (editor->createPresetToolsMenu (), "Edit selected waveform in designer...") };
        check (generatedEdit.present && generatedEdit.enabled && generatedEdit.invoke && editor->canRecallSelectedWaveform ()
               && ! rawFile ().enabled && ! rawPreset ().enabled,
               "Verified generated waves retain Edit recipe but cannot bypass provenance through raw import");
        generatedEdit.invoke ();
        check (requests == 3 && recalledChannel == 0 && recalledZone == 0, "Generated Edit continues routing through the established recipe recall callback");
        first.setSample ("ordinary.wav", false);
        const auto disposedFile { rawFile () }, disposedPreset { rawPreset () };
        editor->setLookAndFeel (nullptr); editor.reset ();
        disposedFile.invoke (); disposedPreset.invoke ();
        check (requests == 3, "Late raw-import callbacks cannot act after the Samples editor is destroyed");
        std::cout << "PASS: Samples raw-cycle import menus (eligibility, FILE/Preset routes, generated recall, stale selection/document and lifetime guards)\n";
    }

    static void promptControls ()
    {
        // JUCE's native AlertWindow requires a display even before it is shown.
        // Fail clearly in a headless environment rather than dereferencing a
        // missing primary display inside JUCE (use a desktop or Xvfb for CI).
        check (juce::Desktop::getInstance ().getDisplays ().getPrimaryDisplay () != nullptr,
               "Rename dialog UI tests require a desktop display; use desktop access or Xvfb on headless CI");
        ModernLookAndFeel look;
        const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-rename-prompt", "", false) };
        check (folder.createDirectory ().wasOk (), "Create owned generated-rename prompt fixture");
        struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { folder };
        const juce::String friendly { "Warm-saw" }, suffix { "-abcdef012345-01" };
        const auto stem { friendly + suffix };
        auto settings { WaveformDesign::startingPoint (WaveformDesign::Mode::oscillator, WaveformDesign::Shape::sine) };
        settings.cycleFrames = 128;
        const auto defaults { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ()
            .getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType).createCopy () };
        WaveformDesign::AssignmentResult generated;
        check (WaveformDesign::prepareAssignment (settings, folder, friendly, defaults, 0, 0, generated, juce::String ("abcdef012345")).wasOk ()
               && Assimil8orEditorComponent::verifiedSampleRenameSuffix (generated.waves[0]) == suffix,
               "Suffix protection requires an actual generated WAV and a matching verified recipe");
        auto prompt { Assimil8orEditorComponent::createSampleRenamePrompt ("Rename sample copy",
            "Create a copy with a new name. All matching references in the current preset will follow the copy; the original WAV and other saved presets stay unchanged.", friendly, suffix) };
        prompt->removeFromDesktop ();
        prompt->setLookAndFeel (&look);
        auto* input { prompt->getTextEditor ("sample-rename-name") };
        auto* feedback { dynamic_cast<juce::Label*> (findNamed (*prompt, "sample-rename-character-count")) };
        check (prompt->getPeer () == nullptr && ! prompt->isCurrentlyModal () && input != nullptr && feedback != nullptr,
               "The actual rename dialog can be inspected offscreen without a native peer or modal event loop");
        check (prompt->getName () == "Rename sample copy" && feedback->getName ().isEmpty (),
               "Internal test component IDs do not replace the dialog title or become visible custom-component captions");
        check (input->getText () == friendly && input->getHighlightedRegion () == juce::Range<int> (0, friendly.length ()),
               "Verified generated filenames expose only the friendly prefix for editing, not their automatic suffix");
        check (feedback->getText ().contains (juce::String (stem.length () + 4) + " / 47")
               && feedback->getText ().contains ("Valid WAV filename") && input->getText () == friendly
               && prompt->getButton ("CREATE COPY")->isEnabled (),
               "Valid filename and character count retain the complete generated stem and count its .wav extension");
        check (feedback->getText ().contains ("A8 Select: Warm-s") && feedback->getText ().contains ("A8 Channels: Warm-saw...01")
               && feedback->getText ().contains ("first 6-10 characters") && feedback->getTooltip ().contains ("not editable")
               && feedback->getTooltip ().contains ("voice number") && feedback->getTooltip ().contains ("user-controlled name portion")
               && feedback->getTooltip ().contains ("hardware display can reveal"), "Generated rename previews the friendly name portion and explains the protected suffix omitted from it");
        snapshot (*prompt, "sample-rename-dialog-generated-prefix");
        input->insertTextAtCaret ("Bright saw");
        input->onTextChange ();
        check (input->getText () == "Bright saw" && feedback->getText ().contains ("A8 Channels: Bright saw...01"),
               "Typing replaces the friendly prefix while the name preview retains its automatic voice number");
        input->setHighlightedRegion ({ input->getText ().length () - 2, input->getText ().length () });
        input->insertTextAtCaret ("03");
        input->onTextChange ();
        check (input->getText () == "Bright s03" && feedback->getText ().contains ("...01"),
               "Editing the field's final characters cannot change the separately protected voice number");
        input->setText ("test", false); input->onTextChange ();
        check (feedback->getText ().contains ("A8 Select: test    A8 Channels: test...01"),
               "Short generated rename previews have no automatic separator or identifier characters");
        input->setText ("test-", false); input->onTextChange ();
        check (feedback->getText ().contains ("A8 Select: test-    A8 Channels: test-...01"),
               "Generated rename retains a hyphen that is part of the user-controlled prefix");
        input->setText (juce::String::repeatedString ("A", 27), false);
        input->onTextChange ();
        check (feedback->getText ().contains ("47 / 47") && prompt->getButton ("CREATE COPY")->isEnabled (),
               "The name input accepts the complete 47-character WAV filename limit");
        input->setText (juce::String::repeatedString ("A", 28), false);
        input->onTextChange ();
        check (feedback->getText ().contains ("48 / 47") && ! prompt->getButton ("CREATE COPY")->isEnabled (),
               "The dialog visibly rejects one character beyond the limit instead of truncating the name");
        snapshot (*prompt, "sample-rename-dialog-too-long");
        input->setText ({}, false); input->onTextChange ();
        check (feedback->getText ().contains ("A8 Select: --    A8 Channels: --") && ! prompt->getButton ("CREATE COPY")->isEnabled (),
               "An empty friendly name shows -- for both hardware previews and cannot generate a suffix-only filename");
        prompt->setLookAndFeel (nullptr);
        for (const auto& ordinary : { juce::String ("Bass sequence"), juce::String ("Lookalike-abcdef012345-01"), juce::String ("Warm saw-abcdef012345-09"), juce::String ("Warm saw-nothex012345-01") })
        {
            const auto ordinaryFile { folder.getChildFile (ordinary + ".wav") };
            check (generated.waves[0].copyFileTo (ordinaryFile) && Assimil8orEditorComponent::verifiedSampleRenameSuffix (ordinaryFile).isEmpty (),
                   "A real WAV with a generated-looking name but no matching recipe does not gain suffix protection");
            auto ordinaryPrompt { Assimil8orEditorComponent::createSampleRenamePrompt ("Rename sample copy", "Create a copy; originals remain unchanged.", ordinary) };
            ordinaryPrompt->removeFromDesktop ();
            auto* ordinaryInput { ordinaryPrompt->getTextEditor ("sample-rename-name") };
            check (ordinaryInput != nullptr && ordinaryInput->getText () == ordinary
                   && ordinaryInput->getHighlightedRegion () == juce::Range<int> (0, ordinary.length ()),
                   "Ordinary names and nonmatching suffix patterns select the complete stem for replacement");
            check (! ordinaryInput->getTooltip ().contains ("not editable") && ordinaryInput->getTooltip ().contains ("remain editable"),
                   "Ordinary names make no false claim about automatic or protected trailing characters");
            auto* ordinaryFeedback { dynamic_cast<juce::Label*> (findNamed (*ordinaryPrompt, "sample-rename-character-count")) };
            ordinaryInput->setText ("test-a", false); ordinaryInput->onTextChange ();
            check (ordinaryFeedback && ordinaryFeedback->getText ().contains ("A8 Select: test-a    A8 Channels: test-a"),
                   "Ordinary rename still previews the exact basename, including its user-entered hyphen and no automatic voice suffix");
            ordinaryInput->setText ({}, false); ordinaryInput->onTextChange ();
            check (ordinaryFeedback && ordinaryFeedback->getText ().contains ("A8 Select: --    A8 Channels: --"),
                   "An empty ordinary filename also keeps both -- placeholders");
        }
    }

    static void run ()
    {
        rawCycleMenus ();
        promptControls ();
        const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-sample-rename-ui", "", false) };
        check (folder.createDirectory ().wasOk (), "Create owned sample-rename UI fixture");
        struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { folder };
        const auto source { folder.getChildFile ("stereo.wav") }, other { folder.getChildFile ("other.wav") };
        {
            std::unique_ptr<juce::OutputStream> output { source.createOutputStream () };
            auto writer { juce::WavAudioFormat ().createWriterFor (output, juce::AudioFormatWriterOptions {}.withSampleRate (48000).withNumChannels (2).withBitsPerSample (24)) };
            juce::AudioBuffer<float> data (2, 256);
            for (int frame { 0 }; frame < 256; ++frame) { data.setSample (0, frame, 0.25f); data.setSample (1, frame, -0.5f); }
            check (writer && writer->writeFromAudioSampleBuffer (data, 0, 256) && writer->flush (), "Write real stereo source for rename-copy UI");
        }
        check (source.copyFileTo (other), "Create a distinct unchanged sample reference");
        juce::MemoryBlock originalAudio;
        check (source.loadFileAsData (originalAudio), "Read original WAV bytes");
        juce::ValueTree root { "Root" };
        PersistentRootProperties persistent (root, PersistentRootProperties::WrapperType::owner, PersistentRootProperties::EnableCallbacks::no);
        RuntimeRootProperties runtime (root, RuntimeRootProperties::WrapperType::owner, RuntimeRootProperties::EnableCallbacks::no);
        AppProperties preferences;
        preferences.wrap (persistent.getValueTree (), AppProperties::WrapperType::owner, AppProperties::EnableCallbacks::no);
        preferences.setMostRecentFolder (folder.getFullPathName ());
        const auto presetFile { folder.getChildFile ("prst023.yml") }, otherPreset { folder.getChildFile ("prst024.yml") };
        preferences.addRecentlyUsedFile (presetFile.getFullPathName ());
        AudioPlayerProperties audition (runtime.getValueTree (), AudioPlayerProperties::WrapperType::owner, AudioPlayerProperties::EnableCallbacks::no);
        GuiControlProperties gui (runtime.getValueTree (), GuiControlProperties::WrapperType::owner, GuiControlProperties::EnableCallbacks::no);
        DirectoryDataProperties directory (runtime.getValueTree (), DirectoryDataProperties::WrapperType::owner, DirectoryDataProperties::EnableCallbacks::no);
        PresetManagerProperties presets (runtime.getValueTree (), PresetManagerProperties::WrapperType::owner, PresetManagerProperties::EnableCallbacks::no);
        const auto defaults { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType) };
        presets.addPreset ("edit", defaults.createCopy ()); presets.addPreset ("unedited", defaults.createCopy ());
        const auto tree { presets.getPreset ("edit") }, baseline { presets.getPreset ("unedited") };
        PresetProperties preset (tree, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        preset.setId (23, false);
        AudioManager audio;
        SystemServices services (runtime.getValueTree (), SystemServices::WrapperType::owner, SystemServices::EnableCallbacks::no);
        services.setAudioManager (&audio); services.setAudioPlayer (nullptr);
        SampleManager samples; samples.init (root);
        EditManager edits; services.setEditManager (&edits); edits.init (root, tree);
        ChannelProperties (tree.getChild (1), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no).setChannelMode (ChannelProperties::stereoRight, false);
        const std::array<std::pair<int, int>, 5> references {{ { 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 }, { 3, 4 } }};
        for (const auto& [channel, index] : references)
        {
            ZoneProperties zone (tree.getChild (channel).getChild (index), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            zone.setSample (source.getFileName (), false); zone.setSide (channel == 1 || channel == 3 ? 1 : 0, false);
            zone.setSampleStart (8 + index, false); zone.setSampleEnd (240 - index, false);
            zone.setLoopStart (32 + index, false); zone.setLoopLength (128.0, false);
            zone.setPitchOffset (2.5, false);
        }
        ZoneProperties (tree.getChild (5).getChild (2), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no).setSample (other.getFileName (), false);
        check (PresetFileOperations::save (presetFile, tree, baseline).wasOk () && Assimil8orPreset ().write (otherPreset, tree).wasOk (), "Save two presets sharing the original sample");
        const auto original { tree.createCopy () }, clean { baseline.createCopy () };
        const auto savedText { presetFile.loadFileAsString () }, otherText { otherPreset.loadFileAsString () };
        const auto originalFiles { fileNames (folder) };
        std::mutex mutex;
        std::vector<std::function<void ()>> completions;
        std::atomic<int> preparations { 0 };
        std::atomic<bool> failPreparation { false };
        int prompts { 0 }, notices { 0 };
        bool noticeError { false };
        juce::String promptMessage, initialName, protectedSuffix, lastNotice;
        std::function<void (std::optional<juce::String>)> answer;
        std::function<void (bool)> collapseAnswer;
        ModernLookAndFeel look;
        auto editor { std::make_unique<Assimil8orEditorComponent> () };
        const auto dispatch = [&] (std::function<void ()> callback)
        {
            const std::lock_guard<std::mutex> lock (mutex);
            completions.push_back (std::move (callback));
            return true;
        };
        auto configure = [&]
        {
            editor->setLookAndFeel (&look); editor->init (root); editor->setSize (1140, 720);
            editor->promptSampleRename = [&] (const juce::String&, const juce::String& message, const juce::String& name, const juce::String& suffix,
                                              std::function<void (std::optional<juce::String>)> callback)
            {
                ++prompts; promptMessage = message; initialName = name; protectedSuffix = suffix; answer = std::move (callback);
            };
            editor->notifySampleRename = [&] (bool error, const juce::String&, const juce::String& message)
            { ++notices; noticeError = error; lastNotice = message; };
            editor->dispatchSampleRename = dispatch;
            editor->prepareSampleRename = [&] (const juce::File& location, const juce::ValueTree& captured, const juce::String& oldName,
                                               const juce::String& newName, SampleRename::Result& result)
            {
                ++preparations;
                if (failPreparation.load ()) return juce::Result::fail ("Injected rename preparation failure");
                return SampleRename::prepare (location, captured, oldName, newName, result);
            };
            editor->confirmStereoCollapse = [&] (const juce::String&, const juce::String&, std::function<void (bool)> callback) { collapseAnswer = std::move (callback); };
        };
        configure ();
        auto menu = [&] (int channel, int zone) { return editor->channelEditors[channel].zoneEditors[zone].createSampleFileMenu (); };
        auto action = [&] (int channel = 0, int zone = 0)
        {
            auto item { findAction (menu (channel, zone), "Rename sample copy...") };
            check (item.present && item.enabled && item.invoke != nullptr, "Actual FILE context menu offers rename-copy for a loaded sample");
            return item.invoke;
        };
        auto nextCompletion = [&] () -> std::function<void ()>
        {
            for (int tick { 0 }; tick < 500; ++tick)
            {
                {
                    const std::lock_guard<std::mutex> lock (mutex);
                    if (! completions.empty ())
                    {
                        auto callback { std::move (completions.front ()) }; completions.erase (completions.begin ()); return callback;
                    }
                }
                std::this_thread::sleep_for (std::chrono::milliseconds (10));
            }
            throw std::runtime_error ("Sample-rename worker did not queue completion");
        };
        auto unchangedFiles = [&]
        {
            juce::MemoryBlock current;
            check (source.loadFileAsData (current) && current == originalAudio && presetFile.loadFileAsString () == savedText
                   && otherPreset.loadFileAsString () == otherText && baseline.isEquivalentTo (clean),
                   "Rename-copy preserves source bytes, both saved presets and the original clean baseline");
        };
        check (! findAction (menu (7, 7), "Rename sample copy...").enabled, "An empty FILE field cannot rename a nonexistent sample");
        check (editor->channelEditors[1].zoneEditors[0].sampleNameSelectLabel.isEnabled ()
               && editor->channelEditors[1].zoneEditors[0].sampleNameSelectLabel.onPopupMenuCallback != nullptr,
               "Stereo-right FILE retains its real context-menu entry point");
        audition.setPlayState (AudioPlayerProperties::PlayState::loop, false);
        const auto originalAction { action (1, 0) };
        originalAction ();
        check (initialName.contains ("stereo") && promptMessage.containsIgnoreCase ("current preset") && promptMessage.containsIgnoreCase ("original"),
               "Rename prompt identifies the current file and explains current-preset-only copying with originals preserved");
        {
            auto actualPrompt { Assimil8orEditorComponent::createSampleRenamePrompt ("Rename sample copy", promptMessage, initialName) };
            actualPrompt->removeFromDesktop ();
            actualPrompt->setLookAndFeel (&look);
            snapshot (*actualPrompt, "sample-rename-dialog-full-message");
            actualPrompt->setLookAndFeel (nullptr);
        }
        originalAction ();
        check (prompts == 1 && ! findAction (menu (0, 0), "Rename sample copy...").enabled
               && ! findAction (editor->createChannelToolsMenu (0), "Keep right...").enabled,
               "Rename prompt blocks duplicate rename and stereo-collapse actions");
        const auto cancelledAnswer { answer };
        cancelledAnswer ({}); cancelledAnswer (juce::String ("Ignored late reply"));
        check (preparations.load () == 0 && tree.isEquivalentTo (original) && fileNames (folder) == originalFiles
               && audition.getPlayState () == AudioPlayerProperties::PlayState::loop, "Cancel is single-use and does not alter preset, files or audition");
        const auto collapse { findAction (editor->createChannelToolsMenu (0), "Keep right...") };
        check (collapse.enabled && collapse.invoke != nullptr, "Stereo collapse is available after rename cancellation");
        collapse.invoke ();
        check (! findAction (menu (0, 0), "Rename sample copy...").enabled, "A pending stereo-collapse confirmation also blocks rename-copy");
        collapseAnswer (false);

        action () ();
        const auto beforeInvalidName { prompts };
        const auto invalidNameReply { answer };
        invalidNameReply (juce::String ("../outside"));
        check (prompts == beforeInvalidName + 1 && preparations.load () == 0 && initialName == "../outside", "Unsafe names remain in the prompt for correction without starting file work");
        answer ({});
        action () ();
        const auto beforeCollision { prompts };
        const auto collisionReply { answer };
        collisionReply (juce::String ("other.wav"));
        check (prompts == beforeCollision + 1 && preparations.load () == 0 && fileNames (folder) == originalFiles, "Existing sample names never overwrite a destination and can be corrected in place");
        answer ({});
        action () ();
        editor->channelProperties[3].setPitch (3.0, false);
        const auto changedAtPrompt { tree.createCopy () };
        answer (juce::String ("Stale prompt"));
        check (preparations.load () == 0 && tree.isEquivalentTo (changedAtPrompt) && fileNames (folder) == originalFiles, "Preset edits invalidate a pending filename response");
        PresetProperties::copyTreeProperties (original, tree);
        action () ();
        const auto alternate { folder.getChildFile ("alternate") };
        check (alternate.createDirectory ().wasOk (), "Create owned alternate-folder fixture");
        preferences.setMostRecentFolder (alternate.getFullPathName ());
        answer (juce::String ("Wrong folder"));
        check (preparations.load () == 0 && tree.isEquivalentTo (original), "Folder navigation invalidates the pending rename destination");
        preferences.setMostRecentFolder (folder.getFullPathName ()); preferences.addRecentlyUsedFile (presetFile.getFullPathName ());

        failPreparation = true;
        action () (); answer (juce::String ("Failed copy"));
        const auto failure { nextCompletion () };
        check (! findAction (menu (1, 0), "Rename sample copy...").enabled
               && ! findAction (editor->createChannelToolsMenu (0), "Keep right...").enabled,
               "Queued rename completion remains mutually busy with rename and collapse");
        failure ();
        check (noticeError && lastNotice.contains ("Injected rename preparation failure") && tree.isEquivalentTo (original)
               && fileNames (folder) == originalFiles, "Worker failure reports an error and preserves all preset and file state");
        failPreparation = false;
        action () (); answer (juce::String ("Stale worker"));
        const auto stale { nextCompletion () };
        check (folder.getChildFile ("Stale worker.wav").existsAsFile (), "Stale-result fixture has prepared a real new WAV");
        editor->channelProperties[3].setPitch (4.0, false);
        const auto changedDuringWorker { tree.createCopy () };
        stale ();
        check (tree.isEquivalentTo (changedDuringWorker) && fileNames (folder) == originalFiles, "Stale worker completion keeps newer edits and cleans only its unapplied copy");
        PresetProperties::copyTreeProperties (original, tree);
        editor->dispatchSampleRename = [] (std::function<void ()>) { return false; };
        action () (); answer (juce::String ("Undelivered copy"));
        check (editor->sampleRenameThread.joinable (), "Rejected-delivery fixture starts a real worker");
        editor->sampleRenameThread.join ();
        editor->timerCallback ();
        check (noticeError && ! editor->sampleRenameJob && tree.isEquivalentTo (original) && fileNames (folder) == originalFiles,
               "Rejected completion delivery cleans its copy and releases busy state through the timer");
        editor->dispatchSampleRename = dispatch;

        action (1, 0) (); answer (juce::String ("Renamed take")); nextCompletion () ();
        editor->timerCallback ();
        const auto renamed { folder.getChildFile ("Renamed take.wav") };
        juce::MemoryBlock copied;
        check (renamed.loadFileAsData (copied) && copied == originalAudio && ! noticeError, "Successful rename creates an exact new WAV under the requested name");
        auto expected { original.createCopy () };
        for (const auto& [channel, zone] : references)
        {
            expected.getChild (channel).getChild (zone).setProperty (ZoneProperties::SamplePropertyId, renamed.getFileName (), nullptr);
            SampleProperties loaded (samples.getSampleProperties (channel, zone), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
            check (loaded.getName () == renamed.getFileName () && loaded.getStatus () == SampleStatus::exists && loaded.getNumChannels () == 2
                   && loaded.getLengthInSamples () == 256 && loaded.getAudioBufferPtr () != nullptr
                   && std::abs (loaded.getAudioBufferPtr ()->getSample (1, 100) + 0.5f) < 1.0e-6f
                   && editor->channelEditors[channel].zoneEditors[zone].sampleNameSelectLabel.getText () == renamed.getFileName (),
                   "All matching zones, including stereo-right and independent channels, immediately reload and display the copied sample");
        }
        check (tree.isEquivalentTo (expected), "Rename updates only the matching filename properties, preserving sides, markers, modes and other samples");
        check (editor->channelActionSession.isDirty () && editor->savePendingLabel.isVisible () && editor->saveButton.isEnabled ()
               && audition.getPlayState () == AudioPlayerProperties::PlayState::stop, "Completed rename is visibly unsaved and stops audition before switching sources");
        unchangedFiles ();

        // A pair can refer to separately split mono files. Applying L's renamed
        // filename must not let the live stereo-copy callbacks replace R's file.
        const auto separateLeft { folder.getChildFile ("separate-left.wav") }, separateRight { folder.getChildFile ("separate-right.wav") };
        for (const auto& file : { separateLeft, separateRight })
        {
            std::unique_ptr<juce::OutputStream> output { file.createOutputStream () };
            auto writer { juce::WavAudioFormat ().createWriterFor (output, juce::AudioFormatWriterOptions {}.withSampleRate (48000).withNumChannels (1).withBitsPerSample (24)) };
            juce::AudioBuffer<float> data (1, 256);
            for (int frame { 0 }; frame < 256; ++frame) data.setSample (0, frame, file == separateLeft ? 0.15f : -0.35f);
            check (writer && writer->writeFromAudioSampleBuffer (data, 0, 256) && writer->flush (), "Write separate mono files for a stereo pair");
        }
        for (int side { 0 }; side < 2; ++side)
            for (int zoneIndex { 0 }; zoneIndex < 2; ++zoneIndex)
            {
                ZoneProperties zone (tree.getChild (side).getChild (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                zone.setSample ((side == 0 ? separateLeft : separateRight).getFileName (), false);
                zone.setSide (0, false);
            }
        const auto separatedPair { tree.createCopy () };
        juce::MemoryBlock separateLeftBytes, separateRightBytes;
        check (separateLeft.loadFileAsData (separateLeftBytes) && separateRight.loadFileAsData (separateRightBytes), "Read separate source bytes before renaming one side");
        action (0, 0) (); answer (juce::String ("Left renamed")); nextCompletion () ();
        auto expectedSeparate { separatedPair.createCopy () };
        for (int zoneIndex { 0 }; zoneIndex < 2; ++zoneIndex)
            expectedSeparate.getChild (0).getChild (zoneIndex).setProperty (ZoneProperties::SamplePropertyId, "Left renamed.wav", nullptr);
        juce::MemoryBlock currentLeft, currentRight;
        check (tree.isEquivalentTo (expectedSeparate) && separateLeft.loadFileAsData (currentLeft) && currentLeft == separateLeftBytes
               && separateRight.loadFileAsData (currentRight) && currentRight == separateRightBytes,
               "Renaming only L's split source preserves R's different filename, channel side, markers and original file bytes");
        for (int zoneIndex { 0 }; zoneIndex < 2; ++zoneIndex)
        {
            SampleProperties rightSample (samples.getSampleProperties (1, zoneIndex), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::no);
            check (rightSample.getName () == separateRight.getFileName () && rightSample.getNumChannels () == 1 && rightSample.getAudioBufferPtr () != nullptr
                   && std::abs (rightSample.getAudioBufferPtr ()->getSample (0, 100) + 0.35f) < 1.0e-6f,
                   "The untouched right-side cache still contains its own mono source after renaming the left file");
        }
        unchangedFiles ();
        snapshot (*editor, "sample-rename-copied-and-pending");

        const auto uppercaseSource { folder.getChildFile ("Uppercase source.WAV") };
        check (source.copyFileTo (uppercaseSource), "Create an uppercase-extension source for unchanged-name confirmation");
        ZoneProperties (tree.getChild (5).getChild (2), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no)
            .setSample (uppercaseSource.getFileName (), false);
        const auto beforeUppercaseNoOp { tree.createCopy () };
        const auto filesBeforeUppercaseNoOp { fileNames (folder) };
        action (5, 2) ();
        check (initialName == "Uppercase source", "Uppercase WAV names are presented as the full editable stem without extension");
        answer (initialName); nextCompletion () ();
        check (! noticeError && lastNotice.contains ("name is unchanged") && tree.isEquivalentTo (beforeUppercaseNoOp)
               && fileNames (folder) == filesBeforeUppercaseNoOp && editor->channelActionSession.isDirty ()
               && editor->savePendingLabel.isVisible (),
               "Accepting an unchanged uppercase WAV stem preserves extension case, creates no files and retains existing pending edits");
        unchangedFiles ();

        auto design { WaveformDesign::startingPoint (WaveformDesign::Mode::oscillator, WaveformDesign::Shape::sine) };
        design.cycleFrames = 128;
        WaveformDesign::AssignmentResult generated;
        check (WaveformDesign::prepareAssignment (design, folder, "Generated", defaults.createCopy (), 0, 0, generated, juce::String ("fedcba987654")).wasOk (),
               "Create an actual generated WAV and recipe for the real FILE-menu rename path");
        ZoneProperties generatedZone (tree.getChild (5).getChild (2), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        generatedZone.setSample (generated.waves[0].getFileName (), false);
        action (5, 2) ();
        check (initialName == "Generated" && protectedSuffix == "-fedcba987654-01",
               "FILE rename separates a verified generated prefix from its immutable ID and voice suffix");
        answer (juce::String ("Renamed wave")); nextCompletion () ();
        const auto renamedGenerated { folder.getChildFile ("Renamed wave-fedcba987654-01.wav") };
        WaveformDesignRecall::RecalledDesign recalled;
        check (! noticeError && generatedZone.getSample () == renamedGenerated.getFileName () && renamedGenerated.existsAsFile ()
               && generated.waves[0].existsAsFile () && WaveformDesignRecall::recallWave (renamedGenerated, recalled).wasOk (),
               "The rename worker receives the reconstructed full name, retains the original, and preserves the copied recipe");
        action (5, 2) ();
        check (initialName == "Renamed wave" && protectedSuffix == "-fedcba987654-01",
               "A renamed generated copy still protects the same verified automatic suffix");
        answer ({});
        const auto lookalike { folder.getChildFile ("Ordinary-fedcba987654-01.wav") };
        check (source.copyFileTo (lookalike), "Create an ordinary source whose filename resembles a generated assignment");
        generatedZone.setSample (lookalike.getFileName (), false);
        action (5, 2) ();
        check (initialName == "Ordinary-fedcba987654-01" && protectedSuffix.isEmpty (),
               "The real FILE menu leaves a lookalike ordinary filename completely editable");
        answer ({});
        unchangedFiles ();

        action () ();
        const auto disposedPrompt { answer };
        const auto beforePromptDestroy { tree.createCopy () };
        const auto preparationsBeforeDestroy { preparations.load () };
        editor->setLookAndFeel (nullptr); editor.reset ();
        disposedPrompt (juce::String ("Too late")); originalAction ();
        check (preparations.load () == preparationsBeforeDestroy && tree.isEquivalentTo (beforePromptDestroy), "Late FILE-menu and text-dialog callbacks safely ignore a destroyed editor");
        editor = std::make_unique<Assimil8orEditorComponent> (); configure ();
        const auto beforeWorkerDestroy { tree.createCopy () };
        const auto filesBeforeWorkerDestroy { fileNames (folder) };
        const auto noticesBeforeWorkerDestroy { notices };
        action () (); answer (juce::String ("Disposed worker"));
        const auto disposedCompletion { nextCompletion () };
        check (folder.getChildFile ("Disposed worker.wav").existsAsFile (), "Destroyed-worker fixture has an unapplied real WAV copy");
        editor->setLookAndFeel (nullptr); editor.reset ();
        check (fileNames (folder) == filesBeforeWorkerDestroy && tree.isEquivalentTo (beforeWorkerDestroy), "Editor destruction cleans an unapplied copy even while its queued callback retains the worker result");
        disposedCompletion ();
        check (notices == noticesBeforeWorkerDestroy && fileNames (folder) == filesBeforeWorkerDestroy && tree.isEquivalentTo (beforeWorkerDestroy), "Late worker delivery after destruction cannot apply, notify or recreate a sample");
        unchangedFiles ();
        std::cout << "PASS: actual FILE rename-copy menu, shared/stereo references, prompt validation, cancellation, busy/stale/failure/lifecycle guards, cache reload and pending Save\n";
    }
};

void testSampleRenameUi () { SampleRenameUiTestAccess::run (); }
