#include "Assimil8or/Assimil8orValidator.h"
#include "Assimil8or/Audio/WaveformDesignAssignment.h"
#include "Assimil8or/Audio/WaveformDesignExport.h"
#include "Assimil8or/Audio/WaveformDesignSidecars.h"
#include "Assimil8or/Audio/HardwareTestOutput.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include "Assimil8or/Preset/PresetProperties.h"
#include "Assimil8or/PresetFolderCopy.h"
#include "Assimil8or/PresetBankExport.h"
#include "Assimil8or/Validator/ValidatorResultProperties.h"
#include <iostream>
#include <stdexcept>

struct GeneratedSidecarValidatorTestAccess
{
    static void run ()
    {
        auto check = [] (bool ok, const char* message) { if (! ok) throw std::runtime_error (message); };
        using namespace WaveformDesign;
        const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-sidecar-validation", "", false) };
        check (folder.createDirectory ().wasOk (), "Create owned sidecar-validation fixture directory");
        struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { folder };
        AudioManager audio;
        Assimil8orValidator validator;
        // Exercise the real file validator synchronously. No init/scan request,
        // directory walker, message-loop pumping or native window is needed.
        validator.audioManager = &audio;
        auto validateSidecar = [&] (const juce::File& file, bool recognized)
        {
            ValidatorResultProperties result;
            result.update (ValidatorResultProperties::ResultTypeInfo, "File: " + file.getFileName (), false);
            const auto [ram, preset] { validator.validateFile (file, result.getValueTree ()) };
            check (ram == 0 && ! preset && result.getNumFixerEntries () == 0,
                   "Sidecars and unrelated files do not count as sample RAM, presets or conversion fixes");
            check (result.getType () == (recognized ? ValidatorResultProperties::ResultTypeInfo : ValidatorResultProperties::ResultTypeWarning),
                   "Only recognized generated artifacts are informational; unrelated or invalid files remain warnings");
            check (result.getText ().contains (recognized ? "desktop-only" : "unknown file type"),
                   "Validation explains generated desktop sidecars without hiding unknown files");
        };

        juce::String genuineReadme;
        for (const auto mode : { Mode::oscillator, Mode::modulation, Mode::layers })
        {
            auto settings { startingPoint (mode, mode == Mode::modulation ? Shape::triangle : Shape::saw) };
            settings.cycleFrames = 64;
            settings.durationSeconds = 0.001;
            settings.voiceCount = 2;
            ExportResult output;
            check (exportDesign (settings, folder, "Mode-" + juce::String (static_cast<int> (mode)), output, 47).wasOk (), "Export actual audio, CV and bank package fixtures");
            validateSidecar (output.recipe, true);
            validateSidecar (output.folder.getChildFile ("README.txt"), true);
            genuineReadme = output.folder.getChildFile ("README.txt").loadFileAsString ().replace ("\r\n", "\n");

            ValidatorResultProperties wavResult, presetResult;
            const auto [waveRam, wavePreset] { validator.validateFile (output.waves[0], wavResult.getValueTree ()) };
            check (waveRam > 0 && ! wavePreset && wavResult.getType () == ValidatorResultProperties::ResultTypeInfo,
                   "Recognizing sidecars does not bypass actual generated WAV validation or memory accounting");
            const auto [presetRam, presetSamples] { validator.validateFile (output.folder.getChildFile ("prst047.yml"), presetResult.getValueTree ()) };
            check (presetRam == 0 && presetSamples && ! presetSamples->empty () && presetResult.getType () == ValidatorResultProperties::ResultTypeInfo,
                   "Generated hardware presets still validate and contribute their actual sample references");
        }

        auto settings { startingPoint (Mode::oscillator, Shape::sine) };
        settings.cycleFrames = 64;
        const auto defaults { ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ().getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType) };
        AssignmentResult assigned;
        check (prepareAssignment (settings, folder, "Assigned waveform", defaults.createCopy (), 0, 0, assigned).wasOk (), "Create a real uniquely named assigned design recipe");
        check (assigned.recipe.getFileName ().endsWith (".design.json"), "Assignment fixture uses the shared-preset recipe naming format");
        validateSidecar (assigned.recipe, true);

        auto copiedPreset { assigned.editedPreset.createCopy () };
        copiedPreset.setProperty (PresetProperties::IdPropertyId, 47, nullptr);
        copiedPreset.setProperty (PresetProperties::NamePropertyId, "Copy fixture", nullptr);
        juce::File copiedFolder;
        const auto copied { PresetFolderCopy::createOrUpdate (folder, copiedPreset, copiedFolder) };
        const auto copyMessage { "Create actual preset-copy inventory fixture: " + copied.getErrorMessage () };
        check (copied.wasOk (), copyMessage.toRawUTF8 ());
        const auto copyManifest { copiedFolder.getChildFile (".a8-preset-copy.json") };
        validateSidecar (copyManifest, true);
        const auto genuineManifest { copyManifest.loadFileAsString () };
        PresetBankExport::Report bank;
        check (PresetBankExport::exportBank ({ { copiedFolder.getChildFile ("prst047.yml"), 47 } },
                folder.getChildFile ("Validation bank"), bank).wasOk (), "Create real bank metadata for validator coverage");
        const auto bankManifest { bank.folder.getChildFile (".a8-preset-bank.json") };
        validateSidecar (bankManifest, true);
        const auto genuineBank { bankManifest.loadFileAsString () };
        check (bankManifest.replaceWithText ("{\"format\":\"A8ManagerPresetBank\",\"version\":999}"), "Create unsupported bank marker in owned fixture");
        ValidatorResultProperties unknownBank;
        unknownBank.update (ValidatorResultProperties::ResultTypeInfo, "File: " + bankManifest.getFileName (), false);
        validator.validateFile (bankManifest, unknownBank.getValueTree ());
        check (unknownBank.getType () == ValidatorResultProperties::ResultTypeWarning
               && unknownBank.getText ().contains ("(ignored)") && ! PresetBankExport::isBankFolder (bank.folder),
               "Malformed bank markers do not suppress warnings or enable in-place bank saves");
        check (bankManifest.replaceWithText (genuineBank), "Restore valid bank metadata");
        check (PresetBankExport::isBankFolder (bank.folder), "A valid exported bank is recognized for flat in-place workspace saves");
        auto validateMalformedManifest = [&] (const juce::String& contents)
        {
            check (copyManifest.replaceWithText (contents), "Write malformed manifest in the owned preset-copy fixture");
            ValidatorResultProperties result;
            result.update (ValidatorResultProperties::ResultTypeInfo, "File: " + copyManifest.getFileName (), false);
            const auto [ram, preset] { validator.validateFile (copyManifest, result.getValueTree ()) };
            check (ram == 0 && ! preset && result.getNumFixerEntries () == 0,
                   "Malformed hidden copy manifests consume no sample RAM and never acquire a conversion fix");
            check (result.getType () == ValidatorResultProperties::ResultTypeWarning && result.getText ().contains ("(ignored)")
                   && ! result.getText ().contains ("desktop-only"),
                   "An invalid copy inventory retains the generic hidden-file warning instead of being trusted as generated metadata");
        };
        validateMalformedManifest ("{invalid JSON");
        auto futureManifest { juce::JSON::parse (genuineManifest) };
        futureManifest.getDynamicObject ()->setProperty ("version", 999);
        validateMalformedManifest (juce::JSON::toString (futureManifest));
        check (copyManifest.replaceWithText (genuineManifest), "Restore the genuine owned preset-copy manifest");
        validateSidecar (copyManifest, true);

        auto write = [&] (const juce::String& name, const juce::String& contents)
        {
            const auto file { folder.getChildFile (name) };
            check (file.replaceWithText (contents), "Write owned sidecar validation fixture");
            return file;
        };
        const auto validJson { juce::JSON::toString (toJson (settings)) };
        juce::String validTestJson, testReadme;
        for (const auto signal : { HardwareTestOutput::Signal::currentDesign, HardwareTestOutput::Signal::audioTone,
                                  HardwareTestOutput::Signal::cvLevels, HardwareTestOutput::Signal::cvSine, HardwareTestOutput::Signal::cvRamp })
        {
            HardwareTestOutput::Settings testSettings;
            testSettings.signal = signal;
            testSettings.design = startingPoint (Mode::layers, Shape::saw);
            testSettings.design.voiceCount = 7;
            testSettings.design.cycleFrames = 64;
            testSettings.design.sampleRate = signal == HardwareTestOutput::Signal::currentDesign ? 96000.0 : 48000.0;
            testSettings.durationSeconds = 1.00001;
            testSettings.presetNumber = 47;
            HardwareTestOutput::ExportResult output;
            const auto exported { HardwareTestOutput::exportPackage (testSettings, folder, "Test-sidecar", output) };
            const auto exportMessage { "Export real hardware-test signal " + juce::String (static_cast<int> (signal)) + ": " + exported.getErrorMessage () };
            check (exported.wasOk (), exportMessage.toRawUTF8 ());
            validateSidecar (output.manifest, true);
            validateSidecar (output.folder.getChildFile ("README.txt"), true);
            check (WaveformDesignSidecars::identify (output.manifest) == WaveformDesignSidecars::Kind::testManifest,
                   "Test manifests are recognized as analysis records, never waveform recipes");
            check (WaveformDesignSidecars::identify (output.folder.getChildFile ("README.txt")) == WaveformDesignSidecars::Kind::testInstructions,
                   "Test loading instructions have their own narrowly recognized kind");
            validTestJson = output.manifest.loadFileAsString ();
            testReadme = output.folder.getChildFile ("README.txt").loadFileAsString ();
        }
        validateSidecar (write ("ordinary.json", validTestJson), false);
        validateSidecar (write ("design.json", validTestJson), false);
        validateSidecar (write ("test-manifest.json", validJson), false);
        validateSidecar (write ("test-manifest.json", "{\"type\":\"A8Manager.HardwareTestOutput\",\"schemaVersion\":1}"), false);
        validateSidecar (write ("test-manifest.json", validTestJson + "\ntrailing content"), false);
        validateSidecar (write ("README.txt", "A8Manager Hardware Test Output\n\nIncomplete instructions."), false);
        validateSidecar (write ("README.txt", "A8Manager Hardware Test Output\n\nROUTING AND SAFETY\n\nREFERENCE MARKERS\n\nFILES AND EXPECTED VALUES\n"), false);
        validateSidecar (write ("README.txt", testReadme.replace ("\r\n", "\n").replace ("\n", "\r\n")), true);
        for (const auto& key : { "schemaVersion", "signal", "sampleRate", "durationSeconds", "windowFrames", "level", "presetNumber", "referenceChannel", "channels", "referenceBursts" })
        {
            auto malformed { juce::JSON::parse (validTestJson) };
            malformed.getDynamicObject ()->removeProperty (key);
            validateSidecar (write ("test-manifest.json", juce::JSON::toString (malformed)), false);
        }
        auto malformedTest = [&] (auto change)
        {
            auto malformed { juce::JSON::parse (validTestJson) };
            change (*malformed.getDynamicObject ());
            validateSidecar (write ("test-manifest.json", juce::JSON::toString (malformed)), false);
        };
        malformedTest ([] (auto& object) { object.setProperty ("schemaVersion", 2); });
        malformedTest ([] (auto& object) { object.setProperty ("windowFrames", 48000.5); });
        malformedTest ([] (auto& object) { object.setProperty ("durationSeconds", 61.0); });
        malformedTest ([] (auto& object) { object.getProperty ("channels").getArray ()->getReference (0).getDynamicObject ()->setProperty ("file", "../outside.wav"); });
        malformedTest ([] (auto& object) { object.getProperty ("channels").getArray ()->getLast ().getDynamicObject ()->setProperty ("purpose", "CV"); });
        malformedTest ([] (auto& object) { object.getProperty ("referenceBursts").getArray ()->getReference (0).getDynamicObject ()->setProperty ("startFrame", -1); });
        malformedTest ([] (auto& object) { object.getProperty ("referenceBursts").getArray ()->getLast ().getDynamicObject ()->setProperty ("endFrameExclusive", 999999999); });
        validateSidecar (write ("ordinary.json", validJson), false);
        validateSidecar (write ("other.txt", genuineReadme), false);
        validateSidecar (write ("design.json", "{\"name\":\"unrelated application\"}"), false);
        validateSidecar (write ("other.design.json", "{\"type\":\"A8Manager.WaveformDesign\",\"version\":1}"), false);
        validateSidecar (write ("README.txt", "An ordinary readme for somebody else's files."), false);
        validateSidecar (write ("README.txt", "A8Manager Waveform Design\n\nAn unrelated or incomplete document."), false);
        validateSidecar (write ("README.txt", genuineReadme.replace ("\n", "\r\n")), true);
        validateSidecar (write ("design.json", "{invalid JSON"), false);
        validateSidecar (write ("design.json", validJson + "\nTrailing unrelated content"), false);
        auto future { toJson (settings) };
        future.getDynamicObject ()->setProperty ("version", 999);
        validateSidecar (write ("design.json", juce::JSON::toString (future)), false);
        auto invalidSettings { toJson (settings) };
        invalidSettings["settings"].getDynamicObject ()->setProperty ("amplitude", 2.0);
        validateSidecar (write ("design.json", juce::JSON::toString (invalidSettings)), false);
        const auto deep { validJson.trimEnd ().dropLastCharacters (1) + ",\"extra\":" + juce::String::repeatedString ("[", 64) +
            "0" + juce::String::repeatedString ("]", 64) + "}" };
        validateSidecar (write ("design.json", deep), false);
        validateSidecar (write ("design.json", validJson + juce::String::repeatedString (" ", 1024 * 1024)), false);
        const auto binary { folder.getChildFile ("design.json") };
        const char invalidUtf8[] { static_cast<char> (0xff), 'a' };
        check (binary.replaceWithData (invalidUtf8, sizeof (invalidUtf8)), "Write invalid UTF-8 fixture");
        validateSidecar (binary, false);
        const char embeddedNull[] { '{', '}', '\0', 'x' };
        check (binary.replaceWithData (embeddedNull, sizeof (embeddedNull)), "Write embedded-NUL fixture");
        validateSidecar (binary, false);
        check (WaveformDesignSidecars::identify (folder.getChildFile ("missing.design.json")) == WaveformDesignSidecars::Kind::unknown,
               "Missing candidate sidecars are not recognized");

        std::cout << "PASS: generated recipe/readme/test-manifest/preset-copy inventory validation, real package and assignment outputs, preserved unknown warnings and bounded schema checks\n";
    }
};

void testGeneratedSidecarValidator () { GeneratedSidecarValidatorTestAccess::run (); }
